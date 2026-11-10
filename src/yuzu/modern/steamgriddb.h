// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <QByteArray>
#include <QList>
#include <QString>

/// Client for the hero artwork of SteamGridDB (https://www.steamgriddb.com/api/v2). Every
/// request is authenticated with the API key of the user's own SteamGridDB account; Orchid
/// ships no key. The functions block on the network: call them from a worker thread. On
/// failure they return an empty result and set `error`.
namespace SteamGridDb {

struct Game {
    int id{};
    QString name;
    /// Empty if unknown
    QString year;
};

struct Hero {
    int id{};
    QString url;
    QString thumb;
    QString author;
    QString style;
};

QList<Game> Search(const std::string& api_key, const QString& term, QString& error);
QList<Hero> Heroes(const std::string& api_key, int game_id, QString& error);

/// Downloads an image the API pointed to. Refuses URLs outside steamgriddb.com.
QByteArray Download(const QString& url, QString& error);

/// The top voted hero of the best match for a game title, for the automatic download.
/// Empty without an error if there is no match.
QByteArray BestHero(const std::string& api_key, const QString& title, QString& error);

} // namespace SteamGridDb
