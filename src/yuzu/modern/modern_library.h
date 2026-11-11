// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QPointer>
#include <QSet>
#include <QWidget>

#include "yuzu/modern/game_details.h"
#include "yuzu/modern/sidebar.h"

class GameList;
class KeyArt;
class QDialog;
class QLabel;
class QLineEdit;
class QListView;
class QMainWindow;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QStandardItemModel;
class QTimer;

namespace Core {
class System;
}

namespace PlayTime {
class PlayTimeManager;
}

/// The modern interface: sidebar, library (hero and shelf), game details and the settings
/// page. It lives inside GameList and shows that list's model, so scanning, favorites and
/// every game action stay the classic code.
class ModernLibrary : public QWidget {
    Q_OBJECT

public:
    explicit ModernLibrary(GameList* game_list, QMainWindow* main_window,
                           const PlayTime::PlayTimeManager& play_time_manager,
                           Core::System& system);

    /// Reads the games from the game list model; call when it finished populating.
    void Reload();
    void FocusSearch();
    /// Receives the key presses of controller navigation.
    QWidget* Shelf() const;

    /// Shows the settings dialog as a page and returns its result, like QDialog::exec.
    int ExecSettings(QDialog* dialog);
    bool SettingsOpen() const {
        return !settings_dialog.isNull();
    }

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    enum class Sort { Recent, Name, PlayTime };

    QWidget* BuildLibraryPage(QMainWindow* main_window);
    void RebuildShelf();
    void ApplyFilter();
    void ShowLibrary();
    void ShowDetails();
    void UpdateHero();
    /// Automatic hero art download for the selected game, if the user turned it on.
    void FetchHeroArt();
    void UpdateStatus();
    const ModernGame* Current() const;

    GameList* game_list;
    QMainWindow* main_window;
    const PlayTime::PlayTimeManager& play_time_manager;

    std::vector<ModernGame> games;
    QList<ModernSidebar::Folder> folders;
    ModernSidebar::Scope scope = ModernSidebar::Scope::All;
    int scope_folder = -1;
    Sort sort = Sort::Recent;

    ModernSidebar* sidebar;
    QStackedWidget* stack;
    QWidget* library_page;
    GameDetails* details;
    QWidget* settings_page;
    QScrollArea* settings_scroll;
    QPointer<QDialog> settings_dialog;

    /// Waits until the selection rests on a game before downloading artwork for it.
    QTimer* art_timer;
    bool art_download_running = false;
    /// Title IDs already asked for in this session, found or not.
    QSet<u64> art_tried;

    KeyArt* hero;
    QLineEdit* search;
    QLabel* hero_title;
    QLabel* hero_meta;
    QWidget* hero_buttons;
    QPushButton* favorite;
    QPushButton* add_directory;
    QLabel* shelf_title;
    QLabel* shelf_count;
    QPushButton* sort_button;
    QListView* shelf;
    QStandardItemModel* shelf_model;
};
