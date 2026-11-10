// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPixmap>
#include <QWidget>

/// The icon with rounded corners, at `size` logical pixels.
QPixmap RoundedIcon(const QPixmap& icon, int size, int radius, qreal device_pixel_ratio);

/// Backdrop of the library hero and of the game details header. Games only ship a square icon,
/// so the backdrop is that icon blurred to fill the area, fading into the window background.
class KeyArt : public QWidget {
    Q_OBJECT

public:
    explicit KeyArt(QWidget* parent = nullptr);

    void SetIcon(const QPixmap& icon);
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
    QPixmap cache;
    double fade_left = 0.62;
    double fade_bottom = 0.42;
    bool show_icon = false;
};
