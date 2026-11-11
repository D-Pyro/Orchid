// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QBuffer>
#include <QDir>
#include <QImageReader>
#include <QSaveFile>

#include "common/fs/path_util.h"
#include "yuzu/modern/hero_art.h"

namespace HeroArt {
namespace {

QDir Directory() {
    return QDir(QString::fromStdString(
                    Common::FS::GetEdenPathString(Common::FS::EdenPath::EdenDir)))
        .filePath(QStringLiteral("hero_art"));
}

QString BaseName(u64 program_id) {
    return QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0')).toUpper();
}

/// Existing artwork files of the game, whatever their extension.
QStringList Files(u64 program_id) {
    if (program_id == 0) {
        return {};
    }
    const QDir dir = Directory();
    QStringList paths;
    for (const QString& name : dir.entryList({BaseName(program_id) + QStringLiteral(".*")},
                                             QDir::Files)) {
        paths << dir.filePath(name);
    }
    return paths;
}

/// The hero and the details header show the same game, so one entry is enough.
struct Cache {
    u64 id = 0;
    QPixmap pixmap;
};

Cache& Cached() {
    // Not a global: a QPixmap must not exist before the QApplication does.
    static Cache cache;
    return cache;
}

void Clear(u64 program_id) {
    for (const QString& path : Files(program_id)) {
        QFile::remove(path);
    }
    Cached() = {};
}

} // namespace

bool Exists(u64 program_id) {
    return !Files(program_id).isEmpty();
}

QPixmap Load(u64 program_id) {
    if (program_id != 0 && program_id == Cached().id) {
        return Cached().pixmap;
    }
    const QStringList files = Files(program_id);
    QPixmap pixmap;
    if (!files.isEmpty()) {
        QImageReader reader(files.first());
        // 4K artwork is more than any hero needs, and would cost 19 MB as a pixmap.
        if (reader.size().width() > 1920) {
            reader.setScaledSize(reader.size().scaled(1920, 1920, Qt::KeepAspectRatio));
        }
        pixmap = QPixmap::fromImageReader(&reader);
    }
    Cached() = {program_id, pixmap};
    return pixmap;
}

bool Save(u64 program_id, const QByteArray& image_data) {
    if (program_id == 0) {
        return false;
    }
    // The data comes from the network: only keep what decodes as an image.
    QBuffer buffer;
    buffer.setData(image_data);
    QImageReader reader(&buffer);
    const QByteArray format = reader.format();
    if (format.isEmpty() || reader.read().isNull()) {
        return false;
    }

    Clear(program_id);
    QDir dir = Directory();
    if (!dir.mkpath(QStringLiteral("."))) {
        return false;
    }
    QSaveFile file(dir.filePath(BaseName(program_id) + QLatin1Char('.') +
                                QString::fromLatin1(format == "jpeg" ? QByteArray("jpg") : format)));
    return file.open(QIODevice::WriteOnly) && file.write(image_data) == image_data.size() &&
           file.commit();
}

void Remove(u64 program_id) {
    Clear(program_id);
    // An empty marker file: the game counts as having artwork, so the automatic download does
    // not bring back what the user just removed, but there is nothing to show.
    QDir dir = Directory();
    if (program_id != 0 && dir.mkpath(QStringLiteral("."))) {
        QFile(dir.filePath(BaseName(program_id) + QStringLiteral(".none"))).open(QIODevice::WriteOnly);
    }
}

} // namespace HeroArt
