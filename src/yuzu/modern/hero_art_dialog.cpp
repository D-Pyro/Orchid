// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <type_traits>
#include <utility>

#include <QCheckBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "qt_common/config/uisettings.h"
#include "yuzu/modern/hero_art.h"
#include "yuzu/modern/hero_art_dialog.h"
#include "yuzu/modern/theme.h"

namespace {

/// What a worker hands back: the result, or why there is none.
template <typename T>
struct Answer {
    T value;
    QString error;
};

/// Runs `work` on a worker thread and `done` with its answer back on the dialog's thread.
/// `done` is skipped if the dialog is gone by then.
template <typename Work, typename Done>
void RunAsync(QObject* owner, Work&& work, Done&& done) {
    using Result = std::invoke_result_t<Work>;
    auto* watcher = new QFutureWatcher<Result>(owner);
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, owner,
                     [watcher, done = std::forward<Done>(done)]() mutable {
                         done(watcher->result());
                         watcher->deleteLater();
                     });
    watcher->setFuture(QtConcurrent::run(std::forward<Work>(work)));
}

/// A few connections at a time, like a browser, instead of one per CPU core.
QThreadPool* ThumbnailPool() {
    static QThreadPool pool;
    pool.setMaxThreadCount(4);
    return &pool;
}

constexpr QSize THUMBNAIL_SIZE{310, 100};

} // namespace

HeroArtDialog::HeroArtDialog(u64 program_id_, const QString& title, QWidget* parent)
    : QDialog(parent), program_id{program_id_} {
    setWindowTitle(tr("Hero Art"));
    resize(1060, 680);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // API key
    auto* key_row = new QHBoxLayout;
    key_row->addWidget(new QLabel(tr("SteamGridDB API key"), this));
    key_edit = new QLineEdit(this);
    key_edit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    key_edit->setPlaceholderText(tr("Paste the key of your SteamGridDB account"));
    key_edit->setText(QString::fromStdString(UISettings::values.steamgriddb_api_key.GetValue()));
    key_row->addWidget(key_edit, 1);
    auto* key_link = new QLabel(
        QStringLiteral("<a href=\"https://www.steamgriddb.com/profile/preferences/api\">%1</a>")
            .arg(tr("Get a free key")),
        this);
    key_link->setOpenExternalLinks(true);
    key_row->addWidget(key_link);
    layout->addLayout(key_row);

    automatic = new QCheckBox(
        tr("Download hero art automatically for the selected game if it has none"), this);
    automatic->setToolTip(tr("Takes the top voted artwork of the best match for the game's "
                             "title. Choose another one here if it picked the wrong game."));
    automatic->setChecked(UISettings::values.hero_art_auto_download.GetValue());
    layout->addWidget(automatic);

    // Search
    auto* search_row = new QHBoxLayout;
    term_edit = new QLineEdit(title, this);
    term_edit->setPlaceholderText(tr("Game title"));
    search_button = new QPushButton(tr("Search"), this);
    search_row->addWidget(term_edit, 1);
    search_row->addWidget(search_button);
    layout->addLayout(search_row);

    // Matching games left, their artwork right
    auto* lists = new QHBoxLayout;
    game_list = new QListWidget(this);
    game_list->setFixedWidth(280);
    hero_list = new QListWidget(this);
    hero_list->setViewMode(QListView::IconMode);
    hero_list->setResizeMode(QListView::Adjust);
    hero_list->setMovement(QListView::Static);
    hero_list->setIconSize(THUMBNAIL_SIZE);
    hero_list->setGridSize(THUMBNAIL_SIZE + QSize(18, 44));
    hero_list->setWordWrap(true);
    lists->addWidget(game_list);
    lists->addWidget(hero_list, 1);
    layout->addLayout(lists, 1);

    // Status and buttons
    auto* bottom = new QHBoxLayout;
    status = new QLabel(this);
    status->setProperty("modern", QStringLiteral("muted"));
    status->setWordWrap(true);
    bottom->addWidget(status, 1);
    remove_button = new QPushButton(tr("Remove Current Art"), this);
    remove_button->setEnabled(!HeroArt::Load(program_id).isNull());
    use_button = new QPushButton(tr("Use This Art"), this);
    use_button->setDefault(true);
    use_button->setEnabled(false);
    auto* close_button = new QPushButton(tr("Close"), this);
    bottom->addWidget(remove_button);
    bottom->addWidget(use_button);
    bottom->addWidget(close_button);
    layout->addLayout(bottom);

    connect(key_edit, &QLineEdit::editingFinished, this, [this] {
        UISettings::values.steamgriddb_api_key = ApiKey();
    });
    connect(automatic, &QCheckBox::toggled, this,
            [](bool checked) { UISettings::values.hero_art_auto_download = checked; });
    connect(search_button, &QPushButton::clicked, this, &HeroArtDialog::RunSearch);
    connect(term_edit, &QLineEdit::returnPressed, this, &HeroArtDialog::RunSearch);
    connect(key_edit, &QLineEdit::returnPressed, this, &HeroArtDialog::RunSearch);
    connect(game_list, &QListWidget::currentRowChanged, this, &HeroArtDialog::LoadHeroes);
    connect(hero_list, &QListWidget::itemSelectionChanged, this, [this] {
        use_button->setEnabled(!hero_list->selectedItems().isEmpty());
    });
    connect(hero_list, &QListWidget::itemDoubleClicked, this, &HeroArtDialog::UseSelected);
    connect(use_button, &QPushButton::clicked, this, &HeroArtDialog::UseSelected);
    connect(remove_button, &QPushButton::clicked, this, [this] {
        HeroArt::Remove(program_id);
        accept();
    });
    connect(close_button, &QPushButton::clicked, this, &QDialog::reject);

    if (ApiKey().empty()) {
        status->setText(tr("Artwork comes from SteamGridDB. It needs the API key of a free "
                           "SteamGridDB account: create one, then paste the key above."));
        key_edit->setFocus();
    } else {
        RunSearch();
    }
}

std::string HeroArtDialog::ApiKey() const {
    return key_edit->text().trimmed().toStdString();
}

void HeroArtDialog::SetBusy(bool busy, const QString& text) {
    search_button->setEnabled(!busy);
    status->setText(text);
}

void HeroArtDialog::RunSearch() {
    UISettings::values.steamgriddb_api_key = ApiKey();
    if (ApiKey().empty()) {
        status->setText(tr("Enter your SteamGridDB API key first."));
        return;
    }

    const int this_request = ++request;
    game_list->clear();
    hero_list->clear();
    SetBusy(true, tr("Searching..."));

    RunAsync(
        this,
        [key = ApiKey(), term = term_edit->text()] {
            Answer<QList<SteamGridDb::Game>> answer;
            answer.value = SteamGridDb::Search(key, term, answer.error);
            return answer;
        },
        [this, this_request](const Answer<QList<SteamGridDb::Game>>& answer) {
            if (this_request != request) {
                return;
            }
            games = answer.value;
            SetBusy(false, !answer.error.isEmpty() ? answer.error
                           : games.isEmpty()
                               ? tr("SteamGridDB has no game with that name. Try a shorter title.")
                               : QString());
            for (const SteamGridDb::Game& game : std::as_const(games)) {
                game_list->addItem(game.year.isEmpty()
                                       ? game.name
                                       : QStringLiteral("%1 (%2)").arg(game.name, game.year));
            }
            // Selecting the best match loads its artwork.
            game_list->setCurrentRow(0);
        });
}

void HeroArtDialog::LoadHeroes() {
    const int row = game_list->currentRow();
    if (row < 0 || row >= games.size()) {
        return;
    }

    const int this_request = ++request;
    hero_list->clear();
    SetBusy(true, tr("Loading artwork..."));

    RunAsync(
        this,
        [key = ApiKey(), game_id = games[row].id] {
            Answer<QList<SteamGridDb::Hero>> answer;
            answer.value = SteamGridDb::Heroes(key, game_id, answer.error);
            return answer;
        },
        [this, this_request](const Answer<QList<SteamGridDb::Hero>>& answer) {
            if (this_request != request) {
                return;
            }
            heroes = answer.value;
            SetBusy(false, !answer.error.isEmpty() ? answer.error
                           : heroes.isEmpty() ? tr("No hero artwork for this game yet.")
                                              : tr("Artwork by the SteamGridDB community."));
            QPixmap placeholder(THUMBNAIL_SIZE);
            placeholder.fill(ModernTheme::Current().surf2);
            for (const SteamGridDb::Hero& hero : std::as_const(heroes)) {
                hero_list->addItem(new QListWidgetItem(
                    QIcon(placeholder),
                    hero.style.isEmpty() ? hero.author
                                         : QStringLiteral("%1 · %2").arg(hero.author,
                                                                             hero.style)));
            }
            LoadThumbnails();
        });
}

void HeroArtDialog::LoadThumbnails() {
    const int this_request = request;
    for (int i = 0; i < heroes.size(); i++) {
        auto* watcher = new QFutureWatcher<QByteArray>(this);
        connect(watcher, &QFutureWatcher<QByteArray>::finished, this,
                [this, watcher, this_request, i] {
                    QPixmap thumbnail;
                    // A thumbnail Qt cannot read keeps the placeholder; the row still works.
                    if (this_request == request && thumbnail.loadFromData(watcher->result())) {
                        hero_list->item(i)->setIcon(
                            QIcon(thumbnail.scaled(THUMBNAIL_SIZE, Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation)));
                    }
                    watcher->deleteLater();
                });
        watcher->setFuture(QtConcurrent::run(ThumbnailPool(), [url = heroes[i].thumb] {
            QString error;
            return SteamGridDb::Download(url, error);
        }));
    }
}

void HeroArtDialog::UseSelected() {
    const int row = hero_list->currentRow();
    if (row < 0 || row >= heroes.size()) {
        return;
    }

    const int this_request = ++request;
    use_button->setEnabled(false);
    SetBusy(true, tr("Downloading..."));

    RunAsync(
        this,
        [url = heroes[row].url] {
            Answer<QByteArray> answer;
            answer.value = SteamGridDb::Download(url, answer.error);
            return answer;
        },
        [this, this_request](const Answer<QByteArray>& answer) {
            if (this_request != request) {
                return;
            }
            if (answer.error.isEmpty() && HeroArt::Save(program_id, answer.value)) {
                accept();
                return;
            }
            use_button->setEnabled(true);
            SetBusy(false, answer.error.isEmpty() ? tr("The download is not a usable image.")
                                                  : answer.error);
        });
}
