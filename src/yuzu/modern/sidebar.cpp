// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QAction>
#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>

#include "common/scm_rev.h"
#include "yuzu/modern/sidebar.h"
#include "yuzu/modern/theme.h"

namespace {

/// "&File" -> "File", "Save && Quit" -> "Save & Quit"
QString WithoutMnemonic(QString text) {
    return text.replace(QStringLiteral("&&"), QStringLiteral("\x01"))
        .remove(QLatin1Char('&'))
        .replace(QLatin1Char('\x01'), QLatin1Char('&'));
}

QLabel* SectionLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setFont(ModernTheme::Caps(10.5));
    label->setProperty("modern", QStringLiteral("dim"));
    label->setContentsMargins(10, 0, 10, 6);
    return label;
}

QFrame* Line(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setProperty("modern", QStringLiteral("line"));
    return line;
}

} // namespace

NavRow::NavRow(const QString& text, QWidget* parent) : QAbstractButton(parent) {
    setText(text);
    setFont(ModernTheme::Ui(13.5));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover);
}

void NavRow::SetRightText(const QString& text) {
    right_text = text;
    update();
}

void NavRow::SetTone(Tone tone_) {
    tone = tone_;
    update();
}

void NavRow::SetMenuStyle(int indent_) {
    menu_style = true;
    indent = indent_;
    setFont(ModernTheme::Ui(13));
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::StrongFocus);
}

QSize NavRow::sizeHint() const {
    return {120, fontMetrics().height() + (menu_style ? 14 : 16)};
}

bool NavRow::event(QEvent* event) {
    // Keyboard navigation in a menu panel highlights the focused row.
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        update();
    }
    return QAbstractButton::event(event);
}

void NavRow::paintEvent(QPaintEvent*) {
    const auto& t = ModernTheme::Current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) {
        p.setOpacity(0.4);
    }

    const bool active = isChecked() && !menu_style;
    if (active || (isEnabled() && (underMouse() || hasFocus()))) {
        p.setPen(Qt::NoPen);
        p.setBrush(active ? t.accent_soft : t.hover);
        const qreal radius = menu_style ? 7 : 8;
        p.drawRoundedRect(rect(), radius, radius);
    }

    QRect area = rect().adjusted(10 + indent, 0, -10, 0);
    if (menu_style) {
        if (isChecked()) {
            p.setPen(t.accent);
            p.setFont(ModernTheme::Ui(12));
            p.drawText(QRect(area.left(), 0, 12, height()), Qt::AlignCenter,
                       QStringLiteral("✓"));
        }
        area.setLeft(area.left() + 20);
    }

    if (!right_text.isEmpty()) {
        const QFont right_font = ModernTheme::Mono(menu_style ? 10.5 : 11);
        p.setFont(right_font);
        p.setPen(t.dim);
        p.drawText(area, Qt::AlignRight | Qt::AlignVCenter, right_text);
        area.setRight(area.right() - QFontMetrics(right_font).horizontalAdvance(right_text) - 10);
    }

    p.setFont(font());
    p.setPen(tone == Tone::Muted    ? t.muted
             : tone == Tone::Accent ? t.accent
             : tone == Tone::Bad    ? t.bad
                                    : t.text);
    p.drawText(area, Qt::AlignLeft | Qt::AlignVCenter,
               fontMetrics().elidedText(text(), Qt::ElideMiddle, area.width()));
}

MenuFlyout::MenuFlyout(QWidget* window) : QFrame(window) {
    hide();
    setProperty("modern", QStringLiteral("flyout"));
    setFocusPolicy(Qt::StrongFocus);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(50);
    shadow->setOffset(0, 18);
    shadow->setColor(QColor(0, 0, 0, 90));
    setGraphicsEffect(shadow);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    // Long menus (View) scroll instead of leaving the window.
    scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    layout->addWidget(scroll);

    auto* content = new QWidget(scroll);
    content->setAutoFillBackground(false);
    rows = new QVBoxLayout(content);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    scroll->setWidget(content);
}

void MenuFlyout::Toggle(QMenu* menu, QWidget* opener_, const QPoint& global_top_left) {
    const bool close_only = isVisible() && current == menu;
    // Also when switching to another menu: rows added to a visible panel would not count
    // towards its size yet.
    hide();
    if (close_only) {
        return;
    }
    current = menu;
    opener = opener_;

    while (QLayoutItem* item = rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    auto* title = new QLabel(WithoutMnemonic(menu->title()), this);
    title->setFont(ModernTheme::Ui(13, 600));
    title->setContentsMargins(10, 8, 10, 6);
    rows->addWidget(title);

    // Menus that fill themselves in when opened do so on this signal.
    emit menu->aboutToShow();
    AddActions(menu, 0);

    // Level with the anchor, moved up if the menu would leave the window, and scrolling if it
    // is taller than the window.
    const QWidget* window = parentWidget();
    const QPoint wanted = window->mapFromGlobal(global_top_left);
    const int height = qMin(rows->sizeHint().height() + 12, window->height() - 16);
    setGeometry(wanted.x(), qBound(8, wanted.y(), window->height() - height - 8), 300, height);
    show();
    raise();
    setFocus();
    qApp->installEventFilter(this);
}

void MenuFlyout::hideEvent(QHideEvent* event) {
    qApp->removeEventFilter(this);
    QFrame::hideEvent(event);
}

bool MenuFlyout::eventFilter(QObject* watched, QEvent* event) {
    // Events come by twice, for the window and for the widget in it.
    if (!watched->isWidgetType()) {
        return false;
    }
    switch (event->type()) {
    case QEvent::ShortcutOverride:
        // Escape belongs to the open panel, not to the main window's "Exit Fullscreen" hotkey.
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
        return false;
    case QEvent::KeyPress:
        return HandleKey(static_cast<QKeyEvent*>(event)->key());
    case QEvent::MouseButtonPress: {
        // A click anywhere else closes the panel, like a menu.
        const auto* target = static_cast<const QWidget*>(watched);
        if (target != this && target != opener && !isAncestorOf(target)) {
            hide();
        }
        return false;
    }
    default:
        return false;
    }
}

bool MenuFlyout::HandleKey(int key) {
    QList<NavRow*> choices = findChildren<NavRow*>();
    choices.removeIf([](const NavRow* row) { return !row->isEnabled(); });
    const auto count = choices.size();
    const auto index = choices.indexOf(qobject_cast<NavRow*>(QApplication::focusWidget()));

    switch (key) {
    case Qt::Key_Escape:
        hide();
        return true;
    case Qt::Key_Down:
    case Qt::Key_Up:
        if (count > 0) {
            NavRow* next = choices[key == Qt::Key_Down ? (index + 1) % count
                                                       : (index <= 0 ? count : index) - 1];
            next->setFocus();
            scroll->ensureWidgetVisible(next);
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (index >= 0) {
            choices[index]->click();
        }
        return true;
    default:
        return false;
    }
}

void MenuFlyout::AddActions(QMenu* menu, int indent) {
    for (QAction* action : menu->actions()) {
        if (!action->isVisible()) {
            continue;
        }
        if (action->isSeparator()) {
            auto* line = Line(this);
            auto* holder = new QWidget(this);
            auto* holder_layout = new QVBoxLayout(holder);
            holder_layout->setContentsMargins(6, 5, 6, 5);
            holder_layout->addWidget(line);
            rows->addWidget(holder);
            continue;
        }
        if (QMenu* submenu = action->menu()) {
            auto* header = new QLabel(WithoutMnemonic(action->text()), this);
            header->setFont(ModernTheme::Caps(10));
            header->setProperty("modern", QStringLiteral("dim"));
            header->setContentsMargins(10 + indent, 8, 10, 3);
            header->setEnabled(action->isEnabled());
            rows->addWidget(header);
            emit submenu->aboutToShow();
            AddActions(submenu, indent + 14);
            continue;
        }

        auto* row = new NavRow(WithoutMnemonic(action->text()), this);
        row->SetMenuStyle(indent);
        row->SetRightText(action->shortcut().toString(QKeySequence::NativeText));
        row->setEnabled(action->isEnabled());
        row->setCheckable(action->isCheckable());
        row->setChecked(action->isChecked());
        row->setToolTip(action->toolTip() == WithoutMnemonic(action->text()) ? QString()
                                                                              : action->toolTip());
        // Close first: the action may open a dialog.
        connect(row, &NavRow::clicked, this, [this, action] {
            hide();
            action->trigger();
        });
        rows->addWidget(row);
    }
}

ModernSidebar::ModernSidebar(QMainWindow* main_window_, QWidget* parent)
    : QWidget(parent), main_window{main_window_} {
    setObjectName(QStringLiteral("ModernSidebar"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedWidth(240);

    flyout = new MenuFlyout(main_window);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 18, 12, 14);
    layout->setSpacing(22);

    // Header
    auto* header = new QHBoxLayout;
    header->setContentsMargins(8, 0, 8, 0);
    header->setSpacing(10);
    auto* icon = new QLabel(this);
    icon->setPixmap(QPixmap(QStringLiteral(":/icons/default/256x256/orchid.png"))
                        .scaled(QSize(28, 28) * devicePixelRatioF(), Qt::KeepAspectRatio,
                                Qt::SmoothTransformation));
    icon->setFixedSize(28, 28);
    icon->setScaledContents(true);
    auto* name = new QLabel(QStringLiteral("Orchid"), this);
    name->setFont(ModernTheme::Ui(16, 650));
    auto* version = new QLabel(this);
    version->setFont(ModernTheme::Mono(10.5));
    // Development builds have a long "<commit>-<branch>" version.
    version->setText(version->fontMetrics().elidedText(QString::fromUtf8(Common::g_build_version),
                                                       Qt::ElideRight, 96));
    version->setToolTip(QString::fromUtf8(Common::g_build_version));
    version->setProperty("modern", QStringLiteral("dim"));
    header->addWidget(icon);
    header->addWidget(name);
    header->addStretch();
    header->addWidget(version);
    layout->addLayout(header);

    const auto section = [this, layout](const QString& title) {
        auto* box = new QVBoxLayout;
        box->setSpacing(2);
        box->addWidget(SectionLabel(title, this));
        layout->addLayout(box);
        return box;
    };

    // Library
    auto* library = section(tr("Library"));
    const std::pair<QString, Scope> scopes[3] = {{tr("All games"), Scope::All},
                                                 {tr("Favorites"), Scope::Favorites},
                                                 {tr("Recently played"), Scope::Recent}};
    for (int i = 0; i < 3; i++) {
        scope_rows[i] = new NavRow(scopes[i].first, this);
        scope_rows[i]->setCheckable(true);
        connect(scope_rows[i], &NavRow::clicked, this,
                [this, scope = scopes[i].second] { emit ScopeChosen(scope, -1); });
        library->addWidget(scope_rows[i]);
    }

    // Folders
    auto* folders = section(tr("Folders"));
    folder_rows = new QVBoxLayout;
    folder_rows->setSpacing(2);
    folders->addLayout(folder_rows);
    auto* add_directory = new NavRow(tr("+ Add game directory"), this);
    add_directory->setFont(ModernTheme::Ui(13));
    add_directory->SetTone(NavRow::Tone::Accent);
    connect(add_directory, &NavRow::clicked, this, &ModernSidebar::AddDirectory);
    folders->addWidget(add_directory);

    // Menus: one entry per menu of the (hidden) menu bar
    auto* menus = section(tr("Menus"));
    menus_label = menus->itemAt(0)->widget();
    for (QAction* action : main_window->menuBar()->actions()) {
        QMenu* menu = action->menu();
        if (menu == nullptr) {
            continue;
        }
        auto* row = new NavRow(WithoutMnemonic(menu->title()), this);
        row->SetRightText(QStringLiteral("›"));
        connect(row, &NavRow::clicked, this, [this, menu, row] { OpenMenu(menu, row); });
        menus->addWidget(row);

        // Alt+F and friends keep working without the menu bar.
        const QKeySequence mnemonic = QKeySequence::mnemonic(menu->title());
        if (!mnemonic.isEmpty()) {
            auto* shortcut = new QShortcut(mnemonic, this);
            shortcut->setContext(Qt::WindowShortcut);
            connect(shortcut, &QShortcut::activated, row, &NavRow::click);
        }
    }

    layout->addStretch();

    // Settings and status
    auto* bottom = new QVBoxLayout;
    bottom->setSpacing(10);
    settings_row = new NavRow(tr("Settings"), this);
    settings_row->setCheckable(true);
    // The classic "Configure..." action, with its hotkey (Ctrl+, by default).
    configure = main_window->findChild<QAction*>(QStringLiteral("action_Configure"));
    if (configure != nullptr) {
        connect(settings_row, &NavRow::clicked, configure, &QAction::trigger);
    }
    SetSettingsActive(false);
    bottom->addWidget(settings_row);
    bottom->addWidget(Line(this));

    auto* status = new QVBoxLayout;
    status->setContentsMargins(10, 0, 10, 0);
    status->setSpacing(3);
    for (auto*& line : status_lines) {
        line = new QLabel(this);
        line->setFont(ModernTheme::Mono(10.5));
        line->setProperty("modern", QStringLiteral("dim"));
        status->addWidget(line);
    }
    bottom->addLayout(status);
    layout->addLayout(bottom);
}

void ModernSidebar::SetCounts(int all, int favorites, int recent) {
    const int counts[3] = {all, favorites, recent};
    for (int i = 0; i < 3; i++) {
        scope_rows[i]->SetRightText(QString::number(counts[i]));
    }
}

void ModernSidebar::SetFolders(const QList<Folder>& folders) {
    qDeleteAll(folder_buttons);
    folder_buttons.clear();

    for (int i = 0; i < folders.size(); i++) {
        const Folder& folder = folders[i];
        auto* row = new NavRow(folder.label, this);
        row->setFont(ModernTheme::Ui(13));
        row->SetTone(NavRow::Tone::Muted);
        row->SetRightText(QString::number(folder.count));
        row->setToolTip(folder.tooltip);
        row->setCheckable(true);
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(row, &NavRow::clicked, this, [this, i] { emit ScopeChosen(Scope::Folder, i); });
        connect(row, &NavRow::customContextMenuRequested, this,
                [this, row, model_row = folder.model_row](const QPoint& pos) {
                    emit FolderMenuRequested(model_row, row->mapToGlobal(pos));
                });
        folder_rows->addWidget(row);
        folder_buttons.append(row);
    }
}

void ModernSidebar::SetActive(Scope scope, int folder) {
    scope_rows[0]->setChecked(scope == Scope::All);
    scope_rows[1]->setChecked(scope == Scope::Favorites);
    scope_rows[2]->setChecked(scope == Scope::Recent);
    for (int i = 0; i < folder_buttons.size(); i++) {
        folder_buttons[i]->setChecked(scope == Scope::Folder && i == folder);
    }
}

void ModernSidebar::SetSettingsActive(bool active) {
    settings_row->setChecked(active);
    // Also picks up a hotkey the user just changed in the settings.
    if (configure != nullptr) {
        settings_row->SetRightText(configure->shortcut().toString(QKeySequence::NativeText));
    }
}

void ModernSidebar::SetStatus(const QString& first_line, const QString& second_line) {
    status_lines[0]->setText(first_line);
    status_lines[1]->setText(second_line);
}

void ModernSidebar::OpenMenu(QMenu* menu, QWidget* opener) {
    // 14px right of the sidebar, level with the "Menus" section
    flyout->Toggle(menu, opener, QPoint(mapToGlobal(QPoint(width() + 14, 0)).x(),
                              menus_label->mapToGlobal(QPoint(0, 0)).y()));
}
