// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "common/fs/path_util.h"
#include "core/core.h"
#include "core/file_sys/nup_writer.h"
#include "core/loader/loader.h"
#include "frontend_common/play_time_manager.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"
#include "qt_common/util/game.h"
#include "yuzu/configuration/configure_per_game_addons.h"
#include "yuzu/game/game_list.h"
#include "yuzu/modern/game_details.h"
#include "yuzu/modern/hero_art.h"
#include "yuzu/modern/hero_art_dialog.h"
#include "yuzu/modern/key_art.h"
#include "yuzu/modern/sidebar.h"
#include "yuzu/modern/theme.h"

namespace {

QPushButton* Button(const QString& text, const char* style, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setProperty("modern", QString::fromLatin1(style));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QFrame* Card(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setProperty("modern", QStringLiteral("card"));
    return card;
}

QLabel* Heading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setFont(ModernTheme::Ui(15, 600));
    return label;
}

} // namespace

GameDetails::GameDetails(GameList* game_list_, QMainWindow* main_window, Core::System& system_,
                         QWidget* parent)
    : QScrollArea(parent), game_list{game_list_}, system{system_} {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(this);
    auto* page = new QVBoxLayout(content);
    page->setContentsMargins(0, 0, 0, 0);
    page->setSpacing(0);

    // Header: backdrop, back button, icon, title, Play and Configure
    header = new KeyArt(content);
    header->SetFade(0.55, 0.7);
    header->setFixedHeight(330);
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(40, 24, 40, 28);

    auto* top_row = new QHBoxLayout;
    auto* back = Button(tr("← Library"), "surface", header);
    connect(back, &QPushButton::clicked, this, &GameDetails::Back);
    auto* hero_art = Button(tr("Hero Art..."), "surface", header);
    hero_art->setToolTip(tr("Choose wide artwork for this game from SteamGridDB"));
    connect(hero_art, &QPushButton::clicked, this, [this] {
        if (HeroArtDialog(game.program_id, game.title, this).exec() == QDialog::Accepted) {
            header->SetWide(HeroArt::Load(game.program_id));
            emit ArtChanged();
        }
    });
    top_row->addWidget(back);
    top_row->addStretch();
    top_row->addWidget(hero_art);
    header_layout->addLayout(top_row);
    header_layout->addStretch();

    auto* title_row = new QHBoxLayout;
    title_row->setSpacing(24);
    icon = new QLabel(header);
    icon->setFixedSize(132, 132);
    title_row->addWidget(icon, 0, Qt::AlignBottom);

    auto* title_column = new QVBoxLayout;
    title_column->setSpacing(10);
    title = new QLabel(header);
    title->setFont(ModernTheme::Ui(38, 650));
    title->setWordWrap(true);
    file_meta = new QLabel(header);
    file_meta->setFont(ModernTheme::Mono(12.5));
    file_meta->setProperty("modern", QStringLiteral("muted"));
    title_column->addStretch();
    title_column->addWidget(title);
    title_column->addWidget(file_meta);
    title_row->addLayout(title_column, 1);

    // Split button: Play, and a menu with the start without the per-game configuration
    auto* play_row = new QHBoxLayout;
    play_row->setSpacing(0);
    auto* play = Button(tr("▶  Play"), "primaryLeft", header);
    play->setFont(ModernTheme::Ui(15.5, 650));
    connect(play, &QPushButton::clicked, this,
            [this] { game_list->LaunchGame(game.path, game.program_id); });
    auto* play_more = Button(QStringLiteral("▾"), "primaryRight", header);
    play_more->setToolTip(GameList::tr("Start Game without Custom Configuration"));
    auto* play_menu = new QMenu(play_more);
    connect(play_menu->addAction(GameList::tr("Start Game without Custom Configuration")),
            &QAction::triggered, this,
            [this] { emit game_list->BootGame(game.path, StartGameType::Global); });
    play_more->setMenu(play_menu);
    play_row->addWidget(play);
    play_row->addWidget(play_more);

    auto* configure = Button(GameList::tr("Configure Game"), "ghost", header);
    connect(configure, &QPushButton::clicked, this,
            [this] { emit game_list->OpenPerGameGeneralRequested(game.path.toStdString()); });

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addLayout(play_row);
    buttons->addWidget(configure);
    title_row->addLayout(buttons);
    title_row->setAlignment(buttons, Qt::AlignBottom);
    header_layout->addLayout(title_row);
    page->addWidget(header);

    // Body: cards left, action list right
    auto* body = new QHBoxLayout;
    body->setContentsMargins(40, 8, 40, 40);
    body->setSpacing(20);
    auto* left = new QVBoxLayout;
    left->setSpacing(20);

    // There is no compatibility database to show a rating from, so this card only links to the
    // issue tracker.
    auto* compatibility = Card(content);
    auto* compatibility_layout = new QVBoxLayout(compatibility);
    compatibility_layout->setContentsMargins(22, 22, 22, 22);
    compatibility_layout->setSpacing(14);
    compatibility_layout->addWidget(Heading(tr("Compatibility"), compatibility));
    auto* compatibility_note = new QLabel(tr("No compatibility rating for this title yet."),
                                          compatibility);
    compatibility_note->setFont(ModernTheme::Ui(15));
    compatibility_note->setProperty("modern", QStringLiteral("muted"));
    compatibility_layout->addWidget(compatibility_note);
    if (auto* report = main_window->findChild<QAction*>(QStringLiteral("action_Report_Crash"))) {
        QString text = report->text();
        auto* link = Button(text.remove(QLatin1Char('&')), "link", compatibility);
        connect(link, &QPushButton::clicked, report, &QAction::trigger);
        compatibility_layout->addWidget(link, 0, Qt::AlignLeft);
    }
    left->addWidget(compatibility);

    auto* addons_card = Card(content);
    addons_layout = new QVBoxLayout(addons_card);
    addons_layout->setContentsMargins(22, 16, 22, 18);
    addons_layout->setSpacing(12);
    auto* addons_header = new QHBoxLayout;
    addons_header->addWidget(Heading(tr("Add-ons"), addons_card), 1);
    auto* open_mods = Button(GameList::tr("Open Mod Data Location"), "link", addons_card);
    connect(open_mods, &QPushButton::clicked, this, [this] {
        emit game_list->OpenFolderRequested(game.program_id, GameListOpenTarget::ModData,
                                            game.path.toStdString());
    });
    addons_header->addWidget(open_mods);
    addons_layout->addLayout(addons_header);
    left->addWidget(addons_card);

    auto* play_time_card = Card(content);
    auto* play_time_layout = new QHBoxLayout(play_time_card);
    play_time_layout->setContentsMargins(22, 22, 22, 22);
    play_time_layout->setSpacing(16);
    auto* play_time_column = new QVBoxLayout;
    play_time_column->setSpacing(4);
    play_time_column->addWidget(Heading(tr("Play time"), play_time_card));
    play_time = new QLabel(play_time_card);
    play_time->setFont(ModernTheme::Ui(22, 600));
    play_time_column->addWidget(play_time);
    play_time_layout->addLayout(play_time_column, 1);
    auto* edit_play_time = Button(GameList::tr("Edit Play Time Data"), "link", play_time_card);
    connect(edit_play_time, &QPushButton::clicked, this,
            [this] { emit game_list->SetPlayTimeRequested(game.program_id); });
    auto* remove_play_time =
        Button(GameList::tr("Remove Play Time Data"), "mutedLink", play_time_card);
    connect(remove_play_time, &QPushButton::clicked, this,
            [this] { emit game_list->RemovePlayTimeRequested(game.program_id); });
    play_time_layout->addWidget(edit_play_time);
    play_time_layout->addWidget(remove_play_time);
    left->addWidget(play_time_card);
    left->addStretch();
    body->addLayout(left, 1);

    auto* actions_card = Card(content);
    actions_card->setFixedWidth(400);
    actions = new QVBoxLayout(actions_card);
    actions->setContentsMargins(8, 10, 8, 10);
    actions->setSpacing(0);
    body->addWidget(actions_card, 0, Qt::AlignTop);

    page->addLayout(body, 1);
    setWidget(content);
}

void GameDetails::SetGame(const ModernGame& game_) {
    game = game_;

    header->SetIcon(game.icon);
    header->SetWide(HeroArt::Load(game.program_id));
    icon->setPixmap(RoundedIcon(game.icon, 132, 18, devicePixelRatioF()));
    title->setText(game.title);
    play_time->setText(
        game.play_time > 0
            ? QString::fromStdString(PlayTime::PlayTimeManager::GetReadablePlayTime(game.play_time))
            : QObject::tr("Never Played"));

    QString directory = QFileInfo(game.path).absolutePath();
    if (directory.startsWith(QDir::homePath())) {
        directory.replace(0, QDir::homePath().size(), QStringLiteral("~"));
    }
    QStringList meta{game.file_type, game.size, directory};
    meta.removeAll(QString());
    file_meta->setText(meta.join(QStringLiteral(" · ")));

    RebuildAddons();
    RebuildActions();
    ensureVisible(0, 0);
}

bool GameDetails::TakeAddonsChanged() {
    return std::exchange(addons_changed, false);
}

void GameDetails::RebuildAddons() {
    delete addons;
    addons = nullptr;

    // Same widget as the Add-Ons tab of "Configure Game", so the rules for updates, DLC and
    // mods live in one place. The tab applies on OK; here every switch applies at once.
    const auto file = Core::GetGameFileFromPath(QtCommon::vfs, game.path.toStdString());
    const auto loader = file ? Loader::GetLoader(system, file) : nullptr;
    u64 title_id{};
    if (loader == nullptr || loader->ReadProgramId(title_id) != Loader::ResultStatus::Success) {
        return;
    }

    addons = new ConfigurePerGameAddons(system, this);
    addons->setMinimumHeight(240);
    addons->SetTitleId(title_id);
    addons->LoadFromFile(file);
    addons_layout->addWidget(addons);

    if (const auto* tree = addons->findChild<QTreeView*>()) {
        connect(tree->model(), &QAbstractItemModel::dataChanged, this, [this] {
            addons->ApplyConfiguration();
            addons_changed = true;
            emit game_list->SaveConfig();
        });
    }
}

void GameDetails::RebuildActions() {
    while (QLayoutItem* item = actions->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const auto group = [this](const QString& name) {
        auto* label = new QLabel(name, this);
        label->setFont(ModernTheme::Caps(10.5));
        label->setProperty("modern", QStringLiteral("dim"));
        label->setContentsMargins(14, 14, 14, 6);
        actions->addWidget(label);
    };
    const auto add = [this](const QString& text, auto&& slot, bool shown = true,
                            NavRow::Tone tone = NavRow::Tone::Text) {
        if (!shown) {
            return static_cast<NavRow*>(nullptr);
        }
        auto* row = new NavRow(text, this);
        row->SetTone(tone);
        connect(row, &NavRow::clicked, this, slot);
        actions->addWidget(row);
        return row;
    };

    // The lambdas copy what they need: an action can rescan the library and replace `game`.
    const u64 id = game.program_id;
    const std::string path = game.path.toStdString();
    const bool has_id = id != 0;
    using QtCommon::Game::GameListRemoveTarget;
    using QtCommon::Game::InstalledEntryType;
    using QtCommon::Game::ShortcutTarget;

    group(tr("Start"));
    add(GameList::tr("Start Game without Custom Configuration"),
        [this, path] { emit game_list->BootGame(QString::fromStdString(path), StartGameType::Global); });
    add(UISettings::values.favorited_ids.contains(id) ? tr("Remove from Favorites")
                                                      : GameList::tr("Favorite"),
        [this, id] {
            game_list->ToggleFavorite(id);
            // Queued: rebuilding deletes the row whose click is being handled.
            QMetaObject::invokeMethod(this, &GameDetails::RebuildActions, Qt::QueuedConnection);
        },
        has_id);

    group(tr("Locations"));
    add(GameList::tr("Open Save Data Location"),
        [this, id, path] {
            emit game_list->OpenFolderRequested(id, GameListOpenTarget::SaveData, path);
        },
        has_id);
    add(GameList::tr("Open Mod Data Location"),
        [this, id, path] {
            emit game_list->OpenFolderRequested(id, GameListOpenTarget::ModData, path);
        },
        has_id);
    add(GameList::tr("Open Transferable Pipeline Cache"),
        [this, id] { emit game_list->OpenTransferableShaderCacheRequested(id); }, has_id);

    group(tr("Tools"));
    add(GameList::tr("Verify Integrity"),
        [this, path] { emit game_list->VerifyIntegrityRequested(path); });
    add(GameList::tr("Dump RomFS"),
        [this, id, path] { emit game_list->DumpRomFSRequested(id, path, DumpRomFSTarget::Normal); });
    add(GameList::tr("Dump RomFS to SDMC"),
        [this, id, path] { emit game_list->DumpRomFSRequested(id, path, DumpRomFSTarget::SDMC); });
    add(GameList::tr("Copy Title ID to Clipboard"),
        [this, id] { emit game_list->CopyTIDRequested(id); });
    const bool nup_idle = !game_list->nup_conversion_running;
    if (game.path.endsWith(QStringLiteral(".nup"), Qt::CaseInsensitive)) {
        add(GameList::tr("Extract NUP..."),
            [this, path] { emit game_list->ExtractNupRequested(path); })
            ->setEnabled(nup_idle);
        add(GameList::tr("Edit NUP Metadata..."),
            [this, path] { emit game_list->EditNupMetadataRequested(path); })
            ->setEnabled(nup_idle);
    } else if (FileSys::Nup::DumpRootFromMain(path)) {
        add(GameList::tr("Convert to NUP..."),
            [this, path] { emit game_list->ConvertToNupRequested(path); })
            ->setEnabled(nup_idle);
    }

#if !defined(__APPLE__)
    group(GameList::tr("Create Shortcut"));
    add(GameList::tr("Add to Desktop"),
        [this, id, path] { emit game_list->CreateShortcut(id, path, ShortcutTarget::Desktop); });
    add(GameList::tr("Add to Applications Menu"), [this, id, path] {
        emit game_list->CreateShortcut(id, path, ShortcutTarget::Applications);
    });
#endif

    group(GameList::tr("Remove"));
    const auto remove_entry = [&](const QString& text, InstalledEntryType type) {
        add(text, [this, id, type] { emit game_list->RemoveInstalledEntryRequested(id, type); },
            has_id, NavRow::Tone::Bad);
    };
    const auto remove_file = [&](const QString& text, GameListRemoveTarget target,
                                 bool shown = true) {
        add(text, [this, id, path, target] { emit game_list->RemoveFileRequested(id, target, path); },
            shown, NavRow::Tone::Bad);
    };
    remove_entry(GameList::tr("Remove Installed Update"), InstalledEntryType::Update);
    remove_entry(GameList::tr("Remove All Installed DLC"), InstalledEntryType::AddOnContent);
    remove_file(GameList::tr("Remove Custom Configuration"),
                GameListRemoveTarget::CustomConfiguration);
    remove_file(GameList::tr("Remove Cache Storage"), GameListRemoveTarget::CacheStorage);
    remove_file(GameList::tr("Remove OpenGL Pipeline Cache"), GameListRemoveTarget::GlShaderCache,
                has_id);
    remove_file(GameList::tr("Remove Vulkan Pipeline Cache"), GameListRemoveTarget::VkShaderCache,
                has_id);
    remove_file(GameList::tr("Remove All Pipeline Caches"), GameListRemoveTarget::AllShaderCache,
                has_id);
    remove_entry(GameList::tr("Remove All Installed Contents"), InstalledEntryType::Game);
}
