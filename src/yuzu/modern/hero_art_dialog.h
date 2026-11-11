// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QList>

#include "common/common_types.h"
#include "yuzu/modern/steamgriddb.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

/// Browse the hero artwork SteamGridDB has for a game and pick one. Also where the user
/// enters their SteamGridDB API key and turns the automatic download on.
class HeroArtDialog : public QDialog {
    Q_OBJECT

public:
    explicit HeroArtDialog(u64 program_id, const QString& title, QWidget* parent = nullptr);

private:
    void RunSearch();
    void LoadHeroes();
    void LoadThumbnails();
    void UseSelected();
    void SetBusy(bool busy, const QString& status);
    std::string ApiKey() const;

    u64 program_id;
    QList<SteamGridDb::Game> games;
    QList<SteamGridDb::Hero> heroes;
    /// Counts the requests, so the answer to an outdated one is dropped.
    int request = 0;

    QLineEdit* key_edit;
    QCheckBox* automatic;
    QLineEdit* term_edit;
    QPushButton* search_button;
    QListWidget* game_list;
    QListWidget* hero_list;
    QLabel* status;
    QPushButton* use_button;
    QPushButton* remove_button;
};
