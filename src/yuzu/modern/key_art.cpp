// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include "yuzu/modern/key_art.h"
#include "yuzu/modern/theme.h"

QPixmap RoundedIcon(const QPixmap& icon, int size, int radius, qreal device_pixel_ratio) {
    QPixmap result(QSize(size, size) * device_pixel_ratio);
    result.setDevicePixelRatio(device_pixel_ratio);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
    p.setClipPath(clip);
    p.fillRect(QRect(0, 0, size, size), ModernTheme::Current().surf2);
    if (!icon.isNull()) {
        p.drawPixmap(QRect(0, 0, size, size), icon);
    }
    return result;
}

KeyArt::KeyArt(QWidget* parent) : QWidget(parent) {}

void KeyArt::SetIcon(const QPixmap& icon_) {
    icon = icon_;
    cache = {};
    update();
}

void KeyArt::SetWide(const QPixmap& wide_) {
    wide = wide_;
    cache = {};
    update();
}

void KeyArt::SetFade(double from_left, double from_bottom) {
    fade_left = from_left;
    fade_bottom = from_bottom;
}

void KeyArt::SetShowIcon(bool show) {
    show_icon = show;
}

void KeyArt::resizeEvent(QResizeEvent*) {
    cache = {};
}

void KeyArt::changeEvent(QEvent* event) {
    // The fades use the theme's background colour.
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        cache = {};
    }
    QWidget::changeEvent(event);
}

void KeyArt::paintEvent(QPaintEvent*) {
    if (cache.isNull()) {
        const auto& t = ModernTheme::Current();
        cache = QPixmap(size() * devicePixelRatioF());
        cache.setDevicePixelRatio(devicePixelRatioF());
        cache.fill(t.bg);

        QPainter p(&cache);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        if (!wide.isNull()) {
            // Fill the area; the left side is under the fade, so keep the right edge.
            const QSize scaled = wide.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
            p.drawPixmap(QRect(QPoint(width() - scaled.width(), (height() - scaled.height()) / 2),
                               scaled),
                         wide);
        } else if (!icon.isNull()) {
            // Shrinking to a few pixels and scaling back up is the blur.
            const QImage tiny = icon.toImage().scaled(12, 12, Qt::IgnoreAspectRatio,
                                                      Qt::SmoothTransformation);
            const int side = qMax(width(), height());
            p.setOpacity(0.75);
            p.drawImage(QRect((width() - side) / 2, (height() - side) / 2, side, side), tiny);
            p.setOpacity(1);

            if (show_icon) {
                const int icon_size = qBound(120, height() * 6 / 10, 340);
                const QRect target(width() - icon_size - qMax(48, width() / 12),
                                   (height() - icon_size) / 2 - 8, icon_size, icon_size);
                if (target.left() > width() / 2) {
                    p.drawPixmap(target.topLeft(), RoundedIcon(icon, icon_size, icon_size / 11,
                                                                devicePixelRatioF()));
                    p.setPen(QPen(t.line, 1));
                    p.setBrush(Qt::NoBrush);
                    p.drawRoundedRect(target, icon_size / 11, icon_size / 11);
                }
            }
        }

        QColor clear = t.bg;
        clear.setAlpha(0);
        QLinearGradient left(0, 0, width() * fade_left, 0);
        left.setColorAt(0, t.bg);
        left.setColorAt(1, clear);
        p.fillRect(rect(), left);
        QLinearGradient bottom(0, height(), 0, height() * (1 - fade_bottom));
        bottom.setColorAt(0, t.bg);
        bottom.setColorAt(1, clear);
        p.fillRect(rect(), bottom);
    }

    QPainter(this).drawPixmap(0, 0, cache);
}
