// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPixmap>
#include <QScrollArea>
#include <QString>

#include "common/common_types.h"

class ConfigurePerGameAddons;
class GameList;
class KeyArt;
class QLabel;
class QMainWindow;
class QPushButton;
class QVBoxLayout;

namespace Core {
class System;
}

/// One game of the library, copied out of the game list model.
struct ModernGame {
    QString path;
    QString title;
    QString file_type;
    QString size;
    /// Update, DLC and mod names, one per line, as the Add-ons column shows them.
    QString addons;
    u64 program_id{};
    u64 play_time{};
    QPixmap icon;
    /// Index into the sidebar's folder list.
    int folder = -1;
};

/// Replaces the game list's context menu: every action of that menu is a button or list entry
/// here, and they emit the same GameList signals.
class GameDetails : public QScrollArea {
    Q_OBJECT

public:
    explicit GameDetails(GameList* game_list, QMainWindow* main_window, Core::System& system,
                         QWidget* parent = nullptr);

    void SetGame(const ModernGame& game);
    const ModernGame& Game() const {
        return game;
    }
    /// True once if an add-on was switched since the last call; the library then rescans.
    bool TakeAddonsChanged();

signals:
    void Back();
    /// The user chose or removed hero artwork.
    void ArtChanged();

private:
    void RebuildActions();
    void RebuildAddons();

    GameList* game_list;
    Core::System& system;
    ModernGame game;
    bool addons_changed = false;

    KeyArt* header;
    QLabel* icon;
    QLabel* title;
    QLabel* file_meta;
    QLabel* play_time;
    QVBoxLayout* addons_layout;
    ConfigurePerGameAddons* addons = nullptr;
    QVBoxLayout* actions;
};
