// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractButton>
#include <QFrame>
#include <QList>
#include <QString>
#include <QWidget>

class QAction;
class QLabel;
class QMainWindow;
class QMenu;
class QVBoxLayout;

/// One line of a list: text left, optional mono text right, optional check mark column.
/// Used for the sidebar entries, the menu panels and the action list of the game details.
class NavRow : public QAbstractButton {
    Q_OBJECT

public:
    enum class Tone { Text, Muted, Accent, Bad };

    explicit NavRow(const QString& text, QWidget* parent = nullptr);

    void SetRightText(const QString& text);
    void SetTone(Tone tone);
    /// Menu style: a checked row shows a check mark instead of the active background.
    void SetMenuStyle(int indent);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    QString right_text;
    Tone tone = Tone::Text;
    bool menu_style = false;
    int indent = 0;
};

/// Panel that shows one menu of the menu bar. It triggers the menu's own QActions, so
/// shortcuts, checked and enabled states stay those of the classic interface. Submenus are
/// listed inline under a header. The panel is an overlay inside the window, not a popup
/// window: rounded corners and the shadow then need no compositor.
class MenuFlyout : public QFrame {
    Q_OBJECT

public:
    /// `window` is the top-level window the panel floats in.
    explicit MenuFlyout(QWidget* window);

    /// Opens `menu`, or closes the panel if it already shows that menu.
    void Toggle(QMenu* menu, QWidget* opener, const QPoint& global_top_left);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void AddActions(QMenu* menu, int indent);

    QVBoxLayout* rows;
    QMenu* current = nullptr;
    /// The button that opened the panel; clicking it again closes it.
    QWidget* opener = nullptr;
};

class ModernSidebar : public QWidget {
    Q_OBJECT

public:
    enum class Scope { All, Favorites, Recent, Folder, None };

    struct Folder {
        QString label;
        QString tooltip;
        int count;
        /// Row of the directory in the game list model, for its context menu.
        int model_row;
    };

    explicit ModernSidebar(QMainWindow* main_window, QWidget* parent = nullptr);

    void SetCounts(int all, int favorites, int recent);
    void SetFolders(const QList<Folder>& folders);
    void SetActive(Scope scope, int folder = -1);
    void SetSettingsActive(bool active);
    void SetStatus(const QString& first_line, const QString& second_line);

signals:
    void ScopeChosen(ModernSidebar::Scope scope, int folder);
    void FolderMenuRequested(int model_row, const QPoint& global_pos);
    void AddDirectory();

private:
    void OpenMenu(QMenu* menu, QWidget* opener);

    QMainWindow* main_window;
    MenuFlyout* flyout;
    NavRow* scope_rows[3];
    NavRow* settings_row;
    QAction* configure;
    QVBoxLayout* folder_rows;
    QList<NavRow*> folder_buttons;
    QWidget* menus_label;
    QLabel* status_lines[2];
};
