// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <utility>

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QStyleHints>

#include "common/fs/path_util.h"
#include "qt_common/config/uisettings.h"
#include "yuzu/modern/theme.h"

namespace ModernTheme {
namespace {

// The design gives these in oklch; converted to sRGB.
const Tokens DARK{
    .bg = QColor(0x14, 0x11, 0x16),
    .side = QColor(0x18, 0x14, 0x1b),
    .surf = QColor(0x20, 0x1b, 0x24),
    .surf2 = QColor(0x2a, 0x24, 0x30),
    .pop = QColor(0x23, 0x1e, 0x28),
    .line = QColor(255, 255, 255, 20),
    .hover = QColor(255, 255, 255, 15),
    .text = QColor(0xf3, 0xef, 0xf5),
    .muted = QColor(0xa6, 0x9d, 0xac),
    .dim = QColor(0x7d, 0x74, 0x83),
    .accent = QColor(0xde, 0x79, 0xd7),
    .accent_ink = QColor(0x1a, 0x0f, 0x18),
    .accent_soft = QColor(0xde, 0x79, 0xd7, 41),
    .ok = QColor(0x68, 0xca, 0x80),
    .warn = QColor(0xe9, 0xb4, 0x52),
    .bad = QColor(0xef, 0x66, 0x61),
};

const Tokens LIGHT{
    .bg = QColor(0xf5, 0xf3, 0xf6),
    .side = QColor(0xee, 0xeb, 0xf0),
    .surf = QColor(0xff, 0xff, 0xff),
    .surf2 = QColor(0xf1, 0xee, 0xf3),
    .pop = QColor(0xff, 0xff, 0xff),
    .line = QColor(30, 15, 35, 28),
    .hover = QColor(30, 15, 35, 13),
    .text = QColor(0x1c, 0x17, 0x20),
    .muted = QColor(0x5f, 0x57, 0x65),
    .dim = QColor(0x8a, 0x82, 0x8f),
    .accent = QColor(0xab, 0x3b, 0xa5),
    .accent_ink = QColor(0xff, 0xff, 0xff),
    .accent_soft = QColor(0xab, 0x3b, 0xa5, 31),
    .ok = QColor(0x2f, 0x8a, 0x55),
    .warn = QColor(0xa8, 0x77, 0x1f),
    .bad = QColor(0xb7, 0x38, 0x3d),
};

QString Css(const QColor& c) {
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(
        c.alpha());
}

/// The design fonts if they are installed, otherwise the system ones.
QFont Family(const QString& wanted, QFontDatabase::SystemFont fallback) {
    static const QStringList families = QFontDatabase::families();
    return families.contains(wanted) ? QFont(wanted) : QFontDatabase::systemFont(fallback);
}

/// A translucent colour as it looks on top of an opaque one.
QColor Over(const QColor& top, const QColor& below) {
    const double a = top.alphaF();
    return QColor::fromRgbF(top.redF() * a + below.redF() * (1 - a),
                            top.greenF() * a + below.greenF() * (1 - a),
                            top.blueF() * a + below.blueF() * (1 - a));
}

/// Stylesheets can only draw a switch knob or a chevron from an image, so render them with the
/// current colours. Returns the directory, with forward slashes for url().
QString WriteImages(const Tokens& t) {
    const QString dir = QDir(QString::fromStdString(Common::FS::GetEdenPathString(
                                 Common::FS::EdenPath::CacheDir)))
                            .filePath(IsDark() ? QStringLiteral("modern_theme/dark")
                                               : QStringLiteral("modern_theme/light"));
    QDir().mkpath(dir);

    const auto save = [&dir](const QString& name, QSize size, auto&& draw) {
        for (const int scale : {1, 2}) {
            QImage image(size * scale, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter p(&image);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(scale, scale);
            draw(p);
            p.end();
            image.save(QDir(dir).filePath(scale == 1 ? name + QStringLiteral(".png")
                                                     : name + QStringLiteral("@2x.png")));
        }
    };

    const auto draw_switch = [](QPainter& p, const QColor& track, bool on) {
        p.setPen(Qt::NoPen);
        p.setBrush(track);
        p.drawRoundedRect(QRectF(0, 0, 40, 23), 11.5, 11.5);
        p.setBrush(QColor(0, 0, 0, 60));
        p.drawEllipse(QRectF(on ? 20 : 3, 4, 17, 17));
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(on ? 20 : 3, 3, 17, 17));
    };
    save(QStringLiteral("switch_on"), {40, 23},
         [&](QPainter& p) { draw_switch(p, t.accent, true); });
    save(QStringLiteral("switch_off"), {40, 23},
         [&](QPainter& p) { draw_switch(p, Over(t.line, t.surf2).lighter(IsDark() ? 135 : 92), false); });

    const auto draw_chevron = [&t](QPainter& p, bool up) {
        p.setPen(QPen(t.muted, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF points[3] = {{2, up ? 7 : 3.5}, {5, up ? 4 : 6.5}, {8, up ? 7 : 3.5}};
        p.drawPolyline(points, 3);
    };
    save(QStringLiteral("chevron_down"), {10, 10},
         [&](QPainter& p) { draw_chevron(p, false); });
    save(QStringLiteral("chevron_up"), {10, 10}, [&](QPainter& p) { draw_chevron(p, true); });

    return dir;
}

} // namespace

bool IsDark() {
    const auto& theme = UISettings::values.theme;
    if (theme.empty() || theme == "default" || theme == "colorful") {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        // An unknown scheme gets the dark look the design was drawn in.
        return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
#else
        return true;
#endif
    }
    return UISettings::IsDarkTheme();
}

const Tokens& Current() {
    return IsDark() ? DARK : LIGHT;
}

QPalette Palette() {
    const Tokens& t = Current();
    QPalette p;
    p.setColor(QPalette::Window, t.bg);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.surf);
    p.setColor(QPalette::AlternateBase, t.surf2);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.surf2);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::BrightText, t.text);
    p.setColor(QPalette::ToolTipBase, t.pop);
    p.setColor(QPalette::ToolTipText, t.text);
    p.setColor(QPalette::PlaceholderText, t.dim);
    p.setColor(QPalette::Highlight, t.accent);
    p.setColor(QPalette::HighlightedText, t.accent_ink);
    p.setColor(QPalette::Link, t.accent);
    p.setColor(QPalette::LinkVisited, t.accent);
    p.setColor(QPalette::Light, t.surf2);
    p.setColor(QPalette::Midlight, t.surf2);
    p.setColor(QPalette::Mid, Over(t.line, t.bg));
    p.setColor(QPalette::Dark, Over(t.line, t.bg));
    p.setColor(QPalette::Shadow, Qt::black);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, t.dim);
    }
    return p;
}

QString StyleSheet() {
    QFile file(QStringLiteral(":/orchid_modern/style.qss"));
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return {};
    }
    QString sheet = QString::fromUtf8(file.readAll());

    const Tokens& t = Current();
    const std::array<std::pair<const char*, QString>, 18> values{{
        {"bg", Css(t.bg)},
        {"side", Css(t.side)},
        {"surf2", Css(t.surf2)},
        {"surf", Css(t.surf)},
        {"pop", Css(t.pop)},
        {"line", Css(t.line)},
        {"hover", Css(t.hover)},
        {"text", Css(t.text)},
        {"muted", Css(t.muted)},
        {"dim", Css(t.dim)},
        {"accentInk", Css(t.accent_ink)},
        {"accentSoft", Css(t.accent_soft)},
        {"accent", Css(t.accent)},
        {"ok", Css(t.ok)},
        {"warn", Css(t.warn)},
        {"bad", Css(t.bad)},
        {"mono", Mono(11).family()},
        {"img", WriteImages(t)},
    }};
    for (const auto& [name, value] : values) {
        sheet.replace(QLatin1Char('@') + QLatin1String(name) + QLatin1Char('@'), value);
    }
    return sheet;
}

QFont Ui(double px, int weight) {
    QFont font = Family(QStringLiteral("Schibsted Grotesk"), QFontDatabase::GeneralFont);
    // shortcut: assumes 96 logical dpi (true on Linux and Windows); macOS would need 72
    font.setPointSizeF(px * 0.75);
    font.setWeight(QFont::Weight(weight));
    if (px >= 20) {
        font.setLetterSpacing(QFont::PercentageSpacing, 98);
    }
    return font;
}

QFont Mono(double px, int weight) {
    QFont font = Family(QStringLiteral("JetBrains Mono"), QFontDatabase::FixedFont);
    font.setPointSizeF(px * 0.75);
    font.setWeight(QFont::Weight(weight));
    return font;
}

QFont Caps(double px) {
    QFont font = Mono(px);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::PercentageSpacing, 108);
    return font;
}

} // namespace ModernTheme
