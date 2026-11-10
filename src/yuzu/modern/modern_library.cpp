// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "frontend_common/content_manager.h"
#include "frontend_common/play_time_manager.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/common.h"
#include "yuzu/game/game_list.h"
#include "yuzu/modern/key_art.h"
#include "yuzu/modern/modern_library.h"
#include "yuzu/modern/theme.h"
#include "yuzu/multiplayer/state.h"
#include "yuzu/util/clickable_label.h"

namespace {

/// Index into ModernLibrary::games
constexpr int GameIndexRole = Qt::UserRole + 100;
constexpr int PlayTimeRole = Qt::UserRole + 101;

constexpr int ICON_SIZE = 164;
/// Room around the icon for the selection ring
constexpr int RING = 6;

QString ReadablePlayTime(u64 seconds) {
    return QString::fromStdString(PlayTime::PlayTimeManager::GetReadablePlayTime(seconds));
}

/// A game on the shelf: icon, title and one muted line.
class ShelfDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {ICON_SIZE + 18, RING + ICON_SIZE + 9 + 20 + 18 + RING};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const auto& t = ModernTheme::Current();
        p->save();
        p->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

        const QRect icon(option.rect.left() + RING, option.rect.top() + RING, ICON_SIZE, ICON_SIZE);
        QPainterPath clip;
        clip.addRoundedRect(icon, 14, 14);
        p->save();
        p->setClipPath(clip);
        p->fillRect(icon, t.surf2);
        const auto pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
        if (!pixmap.isNull()) {
            p->drawPixmap(icon, pixmap);
        }
        p->restore();

        p->setBrush(Qt::NoBrush);
        if (option.state & QStyle::State_Selected) {
            p->setPen(QPen(t.accent, 2));
            p->drawRoundedRect(QRectF(icon).adjusted(-4, -4, 4, 4), 17, 17);
        } else if (option.state & QStyle::State_MouseOver) {
            p->setPen(QPen(t.dim, 1));
            p->drawRoundedRect(QRectF(icon).adjusted(-0.5, -0.5, 0.5, 0.5), 14, 14);
        }

        const QFont title_font = ModernTheme::Ui(13.5, 500);
        const QRect title(icon.left(), icon.bottom() + 9, ICON_SIZE, 20);
        p->setFont(title_font);
        p->setPen(t.text);
        p->drawText(title, Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(title_font).elidedText(index.data(Qt::DisplayRole).toString(),
                                                        Qt::ElideRight, ICON_SIZE));

        const u64 play_time = index.data(PlayTimeRole).toULongLong();
        p->setFont(ModernTheme::Ui(12));
        p->setPen(t.muted);
        p->drawText(QRect(icon.left(), title.bottom() + 2, ICON_SIZE, 18),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    play_time > 0 ? ReadablePlayTime(play_time)
                                  : index.data(GameListItemPath::FileTypeRole).toString());
        p->restore();
    }
};

QPushButton* Button(const QString& text, const char* style, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setProperty("modern", QString::fromLatin1(style));
    button->setFont(ModernTheme::Ui(14.5));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

} // namespace

ModernLibrary::ModernLibrary(GameList* game_list_, QMainWindow* main_window_,
                             const PlayTime::PlayTimeManager& play_time_manager_,
                             Core::System& system)
    : QWidget(game_list_), game_list{game_list_}, main_window{main_window_},
      play_time_manager{play_time_manager_} {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    sidebar = new ModernSidebar(main_window, this);
    layout->addWidget(sidebar);

    stack = new QStackedWidget(this);
    layout->addWidget(stack, 1);

    library_page = BuildLibraryPage(main_window);
    stack->addWidget(library_page);

    details = new GameDetails(game_list, main_window, system, this);
    stack->addWidget(details);

    settings_page = new QWidget(this);
    auto* settings_layout = new QVBoxLayout(settings_page);
    settings_layout->setContentsMargins(40, 28, 40, 16);
    auto* settings_title = new QLabel(tr("Settings"), settings_page);
    settings_title->setFont(ModernTheme::Ui(26, 650));
    settings_layout->addWidget(settings_title);
    // In a scroll area, so a wide page (Controls) cannot force the main window wider.
    settings_scroll = new QScrollArea(settings_page);
    settings_scroll->setWidgetResizable(true);
    settings_scroll->setFrameShape(QFrame::NoFrame);
    settings_layout->addWidget(settings_scroll, 1);
    stack->addWidget(settings_page);

    connect(sidebar, &ModernSidebar::ScopeChosen, this,
            [this](ModernSidebar::Scope new_scope, int folder) {
                scope = new_scope;
                scope_folder = folder;
                RebuildShelf();
                ShowLibrary();
            });
    connect(sidebar, &ModernSidebar::AddDirectory, game_list, &GameList::AddDirectory);
    connect(sidebar, &ModernSidebar::FolderMenuRequested, this,
            [this](int model_row, const QPoint& global_pos) {
                // The classic context menu of a game directory
                const QModelIndex index = game_list->GetModel()->index(model_row, 0);
                QMenu menu;
                game_list->AddPermDirPopup(menu, index);
                if (index.data(GameListItem::TypeRole).value<GameListItemType>() ==
                    GameListItemType::CustomDir) {
                    game_list->AddCustomDirPopup(menu, index);
                }
                menu.exec(global_pos);
                Reload();
            });
    connect(details, &GameDetails::Back, this, &ModernLibrary::ShowLibrary);
    // Favorites changed (hero button, details page)
    connect(game_list, &GameList::SaveConfig, this, &ModernLibrary::RebuildShelf);

    Reload();
}

QWidget* ModernLibrary::BuildLibraryPage(QMainWindow* window) {
    auto* page = new QWidget(this);
    auto* page_layout = new QVBoxLayout(page);
    page_layout->setContentsMargins(0, 0, 0, 0);
    page_layout->setSpacing(0);

    // Hero
    hero = new KeyArt(page);
    hero->SetShowIcon(true);
    hero->setMinimumHeight(330);
    auto* hero_layout = new QVBoxLayout(hero);
    hero_layout->setContentsMargins(40, 24, 40, 36);
    hero_layout->setSpacing(16);

    auto* top = new QHBoxLayout;
    search = new QLineEdit(hero);
    search->setObjectName(QStringLiteral("ModernSearch"));
    search->setPlaceholderText(tr("Search games or title ID"));
    search->setClearButtonEnabled(true);
    search->setFixedWidth(380);
    search->setFont(ModernTheme::Ui(14));
    connect(search, &QLineEdit::textChanged, this, &ModernLibrary::ApplyFilter);
    top->addWidget(search);
    top->addStretch();
    if (auto* install = window->findChild<QAction*>(QStringLiteral("action_Install_File_NAND"))) {
        auto* install_button = Button(tr("Install files"), "surface", hero);
        install_button->setFont(ModernTheme::Ui(14));
        install_button->setToolTip(install->text().remove(QLatin1Char('&')));
        connect(install_button, &QPushButton::clicked, install, &QAction::trigger);
        top->addWidget(install_button);
    }
    hero_layout->addLayout(top);
    hero_layout->addStretch();

    hero_title = new QLabel(hero);
    hero_title->setFont(ModernTheme::Ui(46, 650));
    hero_title->setWordWrap(true);
    hero_title->setMaximumWidth(640);
    hero_layout->addWidget(hero_title);

    hero_meta = new QLabel(hero);
    hero_meta->setFont(ModernTheme::Mono(12.5));
    hero_meta->setProperty("modern", QStringLiteral("muted"));
    hero_meta->setMaximumWidth(640);
    hero_layout->addWidget(hero_meta);

    auto* button_row = new QHBoxLayout;
    button_row->setSpacing(10);
    hero_buttons = new QWidget(hero);
    auto* game_buttons = new QHBoxLayout(hero_buttons);
    game_buttons->setContentsMargins(0, 6, 0, 0);
    game_buttons->setSpacing(10);

    auto* play = Button(tr("▶  Play"), "primary", hero_buttons);
    play->setFont(ModernTheme::Ui(15.5, 650));
    connect(play, &QPushButton::clicked, this, [this] {
        if (const auto* game = Current()) {
            game_list->LaunchGame(game->path, game->program_id);
        }
    });
    auto* details_button = Button(tr("Details"), "ghost", hero_buttons);
    connect(details_button, &QPushButton::clicked, this, &ModernLibrary::ShowDetails);
    auto* configure = Button(GameList::tr("Configure Game"), "ghost", hero_buttons);
    connect(configure, &QPushButton::clicked, this, [this] {
        if (const auto* game = Current()) {
            emit game_list->OpenPerGameGeneralRequested(game->path.toStdString());
        }
    });
    favorite = Button(QStringLiteral("★"), "ghost", hero_buttons);
    favorite->setFont(ModernTheme::Ui(17));
    favorite->setCheckable(true);
    favorite->setFixedWidth(46);
    favorite->setToolTip(GameList::tr("Favorite"));
    connect(favorite, &QPushButton::clicked, this, [this] {
        if (const auto* game = Current()) {
            game_list->ToggleFavorite(game->program_id);
        }
    });
    game_buttons->addWidget(play);
    game_buttons->addWidget(details_button);
    game_buttons->addWidget(configure);
    game_buttons->addWidget(favorite);
    button_row->addWidget(hero_buttons);

    // Only while the library is empty
    add_directory = Button(tr("+ Add game directory"), "primary", hero);
    connect(add_directory, &QPushButton::clicked, game_list, &GameList::AddDirectory);
    button_row->addWidget(add_directory);
    button_row->addStretch();
    hero_layout->addLayout(button_row);
    page_layout->addWidget(hero, 1);

    // Shelf header
    auto* shelf_header = new QHBoxLayout;
    shelf_header->setContentsMargins(40, 4, 40, 8);
    shelf_header->setSpacing(12);
    shelf_title = new QLabel(page);
    shelf_title->setFont(ModernTheme::Ui(17, 600));
    shelf_count = new QLabel(page);
    shelf_count->setFont(ModernTheme::Mono(12));
    shelf_count->setProperty("modern", QStringLiteral("dim"));
    shelf_header->addWidget(shelf_title, 0, Qt::AlignBaseline);
    shelf_header->addWidget(shelf_count, 0, Qt::AlignBaseline);
    shelf_header->addStretch();

    sort_button = Button({}, "mutedLink", page);
    sort_button->setFont(ModernTheme::Ui(13));
    auto* sort_menu = new QMenu(sort_button);
    auto* sort_group = new QActionGroup(sort_menu);
    const std::pair<QString, Sort> sorts[] = {{tr("Recently played"), Sort::Recent},
                                              {tr("Name"), Sort::Name},
                                              {tr("Play time"), Sort::PlayTime}};
    for (const auto& [name, value] : sorts) {
        QAction* action = sort_menu->addAction(name);
        action->setCheckable(true);
        action->setChecked(value == sort);
        sort_group->addAction(action);
        const auto choose = [this, name, value] {
            sort = value;
            sort_button->setText(tr("Sort: %1 ▾").arg(name));
            RebuildShelf();
        };
        connect(action, &QAction::triggered, this, choose);
        if (value == sort) {
            sort_button->setText(tr("Sort: %1 ▾").arg(name));
        }
    }
    sort_button->setMenu(sort_menu);
    shelf_header->addWidget(sort_button);
    page_layout->addLayout(shelf_header);

    // Shelf
    shelf = new QListView(page);
    shelf->setObjectName(QStringLiteral("ModernShelf"));
    shelf->setFlow(QListView::LeftToRight);
    shelf->setWrapping(false);
    shelf->setUniformItemSizes(true);
    shelf->setSelectionMode(QAbstractItemView::SingleSelection);
    shelf->setEditTriggers(QAbstractItemView::NoEditTriggers);
    shelf->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    shelf->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    shelf->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    shelf->setFrameShape(QFrame::NoFrame);
    shelf->setMouseTracking(true);
    shelf->viewport()->setAutoFillBackground(false);
    shelf->setItemDelegate(new ShelfDelegate(shelf));
    shelf->setFixedHeight(ShelfDelegate(nullptr).sizeHint({}, {}).height());
    shelf->installEventFilter(this);
    shelf_model = new QStandardItemModel(shelf);
    shelf->setModel(shelf_model);

    connect(shelf->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] {
        if (const auto* game = Current()) {
            UISettings::values.modern_selected_game = game->program_id;
        }
        UpdateHero();
    });
    connect(shelf, &QListView::doubleClicked, this, [this] {
        if (const auto* game = Current()) {
            game_list->LaunchGame(game->path, game->program_id);
        }
    });

    auto* shelf_row = new QHBoxLayout;
    shelf_row->setContentsMargins(40 - RING, 0, 0, 0);
    shelf_row->addWidget(shelf);
    page_layout->addLayout(shelf_row);
    return page;
}

void ModernLibrary::Reload() {
    const QString details_path = stack->currentWidget() == details ? details->Game().path
                                                                   : QString();

    games.clear();
    folders.clear();
    const QStandardItemModel* model = game_list->GetModel();
    for (int row = 0; row < model->rowCount(); row++) {
        const QStandardItem* dir = model->item(row, 0);
        const auto type = static_cast<GameListItemType>(dir->type());
        if (type != GameListItemType::CustomDir && type != GameListItemType::SdmcDir &&
            type != GameListItemType::UserNandDir && type != GameListItemType::SysNandDir) {
            continue;
        }

        const QString full = dir->data(Qt::DisplayRole).toString();
        QString label = full;
        if (type == GameListItemType::CustomDir && label.startsWith(QDir::homePath())) {
            label.replace(0, QDir::homePath().size(), QStringLiteral("~"));
        }
        folders.append({label, full, dir->rowCount(), row});

        for (int i = 0; i < dir->rowCount(); i++) {
            const QStandardItem* item = dir->child(i, 0);
            ModernGame game;
            game.path = item->data(GameListItemPath::FullPathRole).toString();
            game.title = item->data(GameListItemPath::TitleRole).toString();
            if (game.title.isEmpty()) {
                game.title = QFileInfo(game.path).completeBaseName();
            }
            game.file_type = item->data(GameListItemPath::FileTypeRole).toString();
            game.program_id = item->data(GameListItemPath::ProgramIdRole).toULongLong();
            game.play_time = play_time_manager.GetPlayTime(game.program_id);
            game.icon = item->data(Qt::DecorationRole).value<QPixmap>();
            game.size = dir->child(i, GameListModel::COLUMN_SIZE)->text();
            game.addons = dir->child(i, GameListModel::COLUMN_ADD_ONS)->text();
            game.folder = static_cast<int>(folders.size()) - 1;
            games.push_back(std::move(game));
        }
    }

    if (scope == ModernSidebar::Scope::Folder && scope_folder >= folders.size()) {
        scope = ModernSidebar::Scope::All;
    }
    sidebar->SetFolders(folders);
    UpdateStatus();
    RebuildShelf();

    // A game action on the details page rescanned the library: show the same game again.
    if (!details_path.isEmpty()) {
        const auto it = std::find_if(games.begin(), games.end(), [&](const ModernGame& game) {
            return game.path == details_path;
        });
        if (it != games.end()) {
            details->SetGame(*it);
        } else {
            ShowLibrary();
        }
    }
}

void ModernLibrary::RebuildShelf() {
    const auto& recent_files = UISettings::values.recent_files;
    const auto recent_rank = [&recent_files](const ModernGame& game) {
        const auto rank = recent_files.indexOf(game.path);
        return rank < 0 ? recent_files.size() : rank;
    };
    const auto is_favorite = [](const ModernGame& game) {
        return UISettings::values.favorited_ids.contains(game.program_id);
    };

    std::vector<int> order;
    int favorites = 0;
    int recent = 0;
    for (int i = 0; i < static_cast<int>(games.size()); i++) {
        const ModernGame& game = games[i];
        const bool game_is_recent = recent_files.contains(game.path);
        favorites += is_favorite(game);
        recent += game_is_recent;

        using Scope = ModernSidebar::Scope;
        if ((scope == Scope::Favorites && !is_favorite(game)) ||
            (scope == Scope::Recent && !game_is_recent) ||
            (scope == Scope::Folder && game.folder != scope_folder)) {
            continue;
        }
        order.push_back(i);
    }

    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const ModernGame& left = games[a];
        const ModernGame& right = games[b];
        if (sort == Sort::Recent && recent_rank(left) != recent_rank(right)) {
            return recent_rank(left) < recent_rank(right);
        }
        if (sort == Sort::PlayTime && left.play_time != right.play_time) {
            return left.play_time > right.play_time;
        }
        return left.title.localeAwareCompare(right.title) < 0;
    });

    shelf_model->clear();
    int selected_row = 0;
    for (const int index : order) {
        const ModernGame& game = games[index];
        auto* item = new QStandardItem(game.title);
        item->setData(game.icon, Qt::DecorationRole);
        item->setData(index, GameIndexRole);
        item->setData(qulonglong(game.play_time), PlayTimeRole);
        // The roles the classic filter reads
        item->setData(game.path, GameListItemPath::FullPathRole);
        item->setData(game.title, GameListItemPath::TitleRole);
        item->setData(qulonglong(game.program_id), GameListItemPath::ProgramIdRole);
        item->setData(game.file_type, GameListItemPath::FileTypeRole);
        if (game.program_id == UISettings::values.modern_selected_game.GetValue()) {
            selected_row = shelf_model->rowCount();
        }
        shelf_model->appendRow(item);
    }

    const QString names[] = {tr("All games"), tr("Favorites"), tr("Recently played")};
    shelf_title->setText(scope == ModernSidebar::Scope::Folder ? folders[scope_folder].label
                                                               : names[static_cast<int>(scope)]);
    sidebar->SetCounts(static_cast<int>(games.size()), favorites, recent);
    sidebar->SetActive(stack->currentWidget() == library_page ? scope
                                                              : ModernSidebar::Scope::None,
                       scope_folder);

    shelf->setCurrentIndex(shelf_model->index(selected_row, 0));
    ApplyFilter();
}

void ModernLibrary::ApplyFilter() {
    const QString filter = search->text().toLower();
    int visible = 0;
    int first_visible = -1;
    for (int row = 0; row < shelf_model->rowCount(); row++) {
        const bool matches = Yuzu::FilterMatches(filter, shelf_model->item(row));
        shelf->setRowHidden(row, !matches);
        if (matches && first_visible < 0) {
            first_visible = row;
        }
        visible += matches;
    }
    shelf_count->setText(QString::number(visible));

    const QModelIndex current = shelf->currentIndex();
    if (first_visible >= 0 && (!current.isValid() || shelf->isRowHidden(current.row()))) {
        shelf->setCurrentIndex(shelf_model->index(first_visible, 0));
    }
    UpdateHero();
}

const ModernGame* ModernLibrary::Current() const {
    const QModelIndex index = shelf->currentIndex();
    if (!index.isValid() || shelf->isRowHidden(index.row())) {
        return nullptr;
    }
    return &games[index.data(GameIndexRole).toInt()];
}

void ModernLibrary::UpdateHero() {
    const ModernGame* game = Current();
    hero_buttons->setVisible(game != nullptr);
    add_directory->setVisible(games.empty());

    if (game == nullptr) {
        hero->SetIcon({});
        hero_title->setText(games.empty() ? tr("Your library is empty") : tr("No games found"));
        hero_meta->setText(games.empty() ? tr("Add a folder with your games to get started.")
                                         : QString());
        return;
    }

    hero->SetIcon(game->icon);
    hero_title->setText(game->title);

    QStringList meta;
    if (game->play_time > 0) {
        meta << tr("%1 played").arg(ReadablePlayTime(game->play_time));
    }
    meta << game->addons.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    hero_meta->setText(hero_meta->fontMetrics().elidedText(meta.join(QStringLiteral(" · ")),
                                                           Qt::ElideRight, 640));

    const bool is_favorite = UISettings::values.favorited_ids.contains(game->program_id);
    favorite->setChecked(is_favorite);
    favorite->setVisible(game->program_id != 0);
}

void ModernLibrary::UpdateStatus() {
    // Both lines repeat what the status bar shows.
    QString firmware;
    if (const auto* label =
            main_window->statusBar()->findChild<QLabel*>(QStringLiteral("FirmwareLabel"))) {
        firmware = label->text();
    }
    QStringList first;
    if (!firmware.isEmpty()) {
        first << tr("Firmware %1").arg(firmware);
    }
    first << (ContentManager::AreKeysPresent() ? tr("Keys installed") : tr("Keys missing"));

    QString multiplayer;
    if (const auto* state = main_window->findChild<MultiplayerState*>()) {
        // Without the status bar's "Click here to find a room!"
        multiplayer = state->GetStatusText()->text().section(QLatin1Char('.'), 0, 0);
    }
    sidebar->SetStatus(first.join(QStringLiteral(" · ")),
                       tr("Multiplayer · %1").arg(multiplayer));
}

void ModernLibrary::ShowLibrary() {
    if (settings_dialog) {
        settings_dialog->reject();
    }
    const bool rescan = stack->currentWidget() == details && details->TakeAddonsChanged();
    stack->setCurrentWidget(library_page);
    sidebar->SetActive(scope, scope_folder);
    shelf->setFocus();
    if (rescan) {
        // The add-on names in the hero come from the scan.
        game_list->PopulateAsync(UISettings::values.game_dirs);
    }
}

void ModernLibrary::ShowDetails() {
    const ModernGame* game = Current();
    if (game == nullptr) {
        return;
    }
    details->SetGame(*game);
    stack->setCurrentWidget(details);
    sidebar->SetActive(ModernSidebar::Scope::None);
}

void ModernLibrary::FocusSearch() {
    ShowLibrary();
    search->setFocus();
    search->selectAll();
}

QWidget* ModernLibrary::Shelf() const {
    return shelf;
}

int ModernLibrary::ExecSettings(QDialog* dialog) {
    QWidget* const previous = stack->currentWidget();
    dialog->setWindowFlags(Qt::Widget);
    settings_scroll->setWidget(dialog);
    stack->setCurrentWidget(settings_page);
    sidebar->SetActive(ModernSidebar::Scope::None);
    sidebar->SetSettingsActive(true);
    dialog->show();
    dialog->setFocus();

    // Stands in for QDialog::exec, which only works for a dialog in its own window.
    settings_dialog = dialog;
    QEventLoop loop;
    connect(dialog, &QDialog::finished, &loop, &QEventLoop::quit);
    loop.exec();
    settings_dialog = nullptr;

    // The caller owns the dialog again.
    settings_scroll->takeWidget();
    sidebar->SetSettingsActive(false);
    // Unless the user already left through the sidebar
    if (stack->currentWidget() == settings_page) {
        stack->setCurrentWidget(previous);
        sidebar->SetActive(previous == library_page ? scope : ModernSidebar::Scope::None,
                           scope_folder);
    }
    return dialog->result();
}

bool ModernLibrary::eventFilter(QObject* object, QEvent* event) {
    if (object == shelf && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (const auto* game = Current()) {
                game_list->LaunchGame(game->path, game->program_id);
            }
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void ModernLibrary::hideEvent(QHideEvent* event) {
    // A game is starting: the settings page must not stay open behind it.
    if (settings_dialog) {
        settings_dialog->reject();
    }
    QWidget::hideEvent(event);
}

void ModernLibrary::changeEvent(QEvent* event) {
    // In separate-window mode a starting game disables the list instead of hiding it.
    if (event->type() == QEvent::EnabledChange && !isEnabled() && settings_dialog) {
        settings_dialog->reject();
    }
    QWidget::changeEvent(event);
}
