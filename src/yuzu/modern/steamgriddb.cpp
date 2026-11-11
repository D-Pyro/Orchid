// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "common/httplib.h"
#include "common/logging.h"
#include "yuzu/modern/steamgriddb.h"

#ifdef YUZU_BUNDLED_OPENSSL
#include <openssl/cert.h>
#endif

namespace SteamGridDb {
namespace {

constexpr std::size_t TIMEOUT_SECONDS = 15;
/// Hero images are a few MB; anything far larger is not one.
constexpr std::size_t MAX_BODY_SIZE = 32 * 1024 * 1024;

QString Tr(const char* text) {
    return QCoreApplication::translate("SteamGridDb", text);
}

/// GET https://<host><path>. Returns the body, or sets `error`.
QByteArray Get(const std::string& host, const std::string& path, const std::string& api_key,
               QString& error) {
    httplib::Client client("https://" + host);
    client.set_connection_timeout(TIMEOUT_SECONDS);
    client.set_read_timeout(TIMEOUT_SECONDS);
    client.set_write_timeout(TIMEOUT_SECONDS);
    client.set_follow_location(false);
    httplib::Headers headers{{"User-Agent", "Orchid"}};
    if (!api_key.empty()) {
        headers.emplace("Authorization", "Bearer " + api_key);
    }
#ifdef YUZU_BUNDLED_OPENSSL
    client.load_ca_cert_store(kCert, sizeof(kCert));
#endif

    QByteArray body;
    const auto result =
        client.Get(path, headers, [&body](const char* data, std::size_t length) {
            if (static_cast<std::size_t>(body.size()) + length > MAX_BODY_SIZE) {
                return false;
            }
            body.append(data, static_cast<qsizetype>(length));
            return true;
        });

    if (!result) {
        LOG_ERROR(Frontend, "SteamGridDB: GET {}{} failed: {}", host, path,
                  httplib::to_string(result.error()));
        error = Tr("Could not reach SteamGridDB. Check your internet connection.");
        return {};
    }
    switch (result->status) {
    case 200:
        return body;
    case 401:
    case 403:
        error = Tr("SteamGridDB did not accept the API key.");
        break;
    case 404:
        // "No such game" and "no artwork" are answers, not failures.
        break;
    case 429:
        error = Tr("SteamGridDB is rate limiting requests. Try again in a moment.");
        break;
    default:
        LOG_ERROR(Frontend, "SteamGridDB: GET {}{} returned {}", host, path, result->status);
        error = Tr("SteamGridDB returned an error (%1).").arg(result->status);
    }
    return {};
}

/// The "data" array of an API response, empty if the response is not a successful one.
QJsonArray Data(const QByteArray& body) {
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    return root.value(QLatin1String("success")).toBool()
               ? root.value(QLatin1String("data")).toArray()
               : QJsonArray{};
}

const std::string API_HOST = "www.steamgriddb.com";

} // namespace

QList<Game> Search(const std::string& api_key, const QString& term, QString& error) {
    const QByteArray encoded = QUrl::toPercentEncoding(term.trimmed());
    if (encoded.isEmpty()) {
        return {};
    }
    QList<Game> games;
    for (const QJsonValue& value :
         Data(Get(API_HOST, "/api/v2/search/autocomplete/" + encoded.toStdString(), api_key,
                  error))) {
        const QJsonObject object = value.toObject();
        const qint64 released = object.value(QLatin1String("release_date")).toInteger();
        games.append({
            .id = object.value(QLatin1String("id")).toInt(),
            .name = object.value(QLatin1String("name")).toString(),
            .year = released > 0
                        ? QString::number(QDateTime::fromSecsSinceEpoch(released).date().year())
                        : QString(),
        });
    }
    return games;
}

QList<Hero> Heroes(const std::string& api_key, int game_id, QString& error) {
    // Still PNG and JPEG only: Qt reads those without extra plugins. Adult and joke artwork
    // is left out by the API's defaults.
    const std::string path = "/api/v2/heroes/game/" + std::to_string(game_id) +
                             "?types=static&mimes=image/png,image/jpeg&limit=24";
    QList<Hero> heroes;
    for (const QJsonValue& value : Data(Get(API_HOST, path, api_key, error))) {
        const QJsonObject object = value.toObject();
        heroes.append({
            .id = object.value(QLatin1String("id")).toInt(),
            .url = object.value(QLatin1String("url")).toString(),
            .thumb = object.value(QLatin1String("thumb")).toString(),
            .author = object.value(QLatin1String("author"))
                          .toObject()
                          .value(QLatin1String("name"))
                          .toString(),
            .style = object.value(QLatin1String("style")).toString(),
        });
    }
    return heroes;
}

QByteArray Download(const QString& url_text, QString& error) {
    // The URL comes out of an API response; only follow it to SteamGridDB's own hosts.
    const QUrl url(url_text, QUrl::StrictMode);
    const QString host = url.host().toLower();
    if (!url.isValid() || url.scheme() != QLatin1String("https") ||
        !(host == QLatin1String("steamgriddb.com") ||
          host.endsWith(QLatin1String(".steamgriddb.com")))) {
        LOG_ERROR(Frontend, "SteamGridDB: refusing image URL {}", url_text.toStdString());
        error = Tr("SteamGridDB returned an unexpected image address.");
        return {};
    }
    // No API key: the image host is a plain file server.
    return Get(host.toStdString(), url.path(QUrl::FullyEncoded).toStdString(), {}, error);
}

QByteArray BestHero(const std::string& api_key, const QString& title, QString& error) {
    const QList<Game> games = Search(api_key, title, error);
    if (games.isEmpty()) {
        return {};
    }
    const QList<Hero> heroes = Heroes(api_key, games.first().id, error);
    if (heroes.isEmpty()) {
        return {};
    }
    return Download(heroes.first().url, error);
}

} // namespace SteamGridDb
