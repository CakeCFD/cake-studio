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
#include <QApplication>

enum class Theme { Dark, Blueprint };

class ThemeEngine {
public:
    static ThemeEngine& instance();
    void apply(Theme theme, QApplication& app);
    Theme current() const { return m_current; }

private:
    ThemeEngine() = default;
    Theme m_current = Theme::Dark;
    QString loadStylesheet(Theme theme);
};
