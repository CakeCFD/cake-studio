/*---------------------------------------------------------------------------*\
    Copyright (C) 2026 CakeCFD Contributors

    This file is part of CakeCFD.

    CakeCFD is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    CakeCFD is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
    details.

    You should have received a copy of the GNU General Public License along
    with CakeCFD. If not, see <https://www.gnu.org/licenses/>.
\*---------------------------------------------------------------------------*/

#pragma once
#include <QString>
#include <QColor>
#include <QList>
#include <QApplication>
#include <QWidget>
#include <QStyle>

enum class Theme { Graphite, Light, Navy };

struct ThemeColors {
    bool   light;
    QColor window, panel, base, raised, border, borderStrong;
    QColor text, textMuted, textDim;
    QColor accent, accentHover, onAccent, selection;
    QColor viewport, plotBg, plotGrid;
    QColor success, warning, danger, warningBg;
};

class ThemeEngine {
public:
    static ThemeEngine& instance();
    void apply(Theme theme, QApplication& app);
    Theme current() const { return m_current; }
    const ThemeColors& colors() const { return m_colors; }

    static ThemeColors colorsFor(Theme theme);
    static QList<Theme> all() { return { Theme::Graphite, Theme::Light, Theme::Navy }; }
    static QString key(Theme theme);
    static QString displayName(Theme theme);
    // Unknown and older keys ("dark", "blueprint") fall back to Graphite
    static Theme fromKey(const QString& key);
    // Tags a widget for a [cakeRole] rule in the theme stylesheet and restyles it now
    static void setRole(QWidget* w, const QString& role) {
        w->setProperty("cakeRole", role);
        w->style()->unpolish(w);
        w->style()->polish(w);
    }

private:
    ThemeEngine() : m_colors(colorsFor(Theme::Graphite)) {}
    Theme m_current = Theme::Graphite;
    ThemeColors m_colors;
    static QString stylesheet(const ThemeColors& c);
};
