// SPDX-FileCopyrightText: Copyright 2026 Orchid Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>

/// Design tokens of the modern interface. The stylesheet and the custom-painted widgets both
/// read them from here, so dark and light stay in step.
namespace ModernTheme {

struct Tokens {
    QColor bg, side, surf, surf2, pop, line, hover;
    QColor text, muted, dim;
    QColor accent, accent_ink, accent_soft;
    QColor ok, warn, bad;
};

/// Follows the Theme setting; "Default" follows the system colour scheme.
bool IsDark();
const Tokens& Current();
QPalette Palette();

/// dist/qt_themes/orchid_modern/style.qss with the current tokens filled in.
QString StyleSheet();

/// Sizes are the CSS pixel sizes of the design.
QFont Ui(double px, int weight = 400);
QFont Mono(double px, int weight = 400);
/// Mono, uppercase and letter-spaced: section and group headers.
QFont Caps(double px);

} // namespace ModernTheme
