// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QPixmap>

#include "common/common_types.h"

/// Wide artwork for the library hero and the game details header, one image per title in
/// <data>/hero_art/<title id>.<png|jpg>. The files come from the SteamGridDB browser, the
/// automatic download, or the user dropping in an image with that name.
namespace HeroArt {

/// Null if the game has no artwork.
QPixmap Load(u64 program_id);
/// Also true after Remove(): the automatic download then leaves the game alone.
bool Exists(u64 program_id);

/// Stores image data as the game's artwork. False if it is not an image Qt can read.
bool Save(u64 program_id, const QByteArray& image_data);
/// Removes the artwork and remembers that the user wants none for this game.
void Remove(u64 program_id);

} // namespace HeroArt
