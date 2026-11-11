// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPixmap>
#include <QWidget>

/// The icon with rounded corners, at `size` logical pixels.
QPixmap RoundedIcon(const QPixmap& icon, int size, int radius, qreal device_pixel_ratio);

/// Backdrop of the library hero and of the game details header, fading into the window
/// background. It shows the game's wide artwork if there is one (see HeroArt). Games only ship
/// a square icon, so without artwork the backdrop is that icon blurred to fill the area.
class KeyArt : public QWidget {
    Q_OBJECT

public:
    explicit KeyArt(QWidget* parent = nullptr);

    void SetIcon(const QPixmap& icon);
    /// Wide artwork; a null pixmap goes back to the blurred icon.
    void SetWide(const QPixmap& wide);
    /// Where the fades from the left and from the bottom end, as fractions of the size.
    void SetFade(double from_left, double from_bottom);
    /// Also draw the sharp icon on the right (library hero).
    void SetShowIcon(bool show);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QPixmap icon;
    QPixmap wide;
    QPixmap cache;
    double fade_left = 0.62;
    double fade_bottom = 0.42;
    bool show_icon = false;
};
