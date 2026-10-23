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
#include <QFile>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>

namespace PlatformUtils {

inline bool isWSL()
{
    QFile f("/proc/version");
    if (!f.open(QIODevice::ReadOnly)) return false;
    return f.readAll().toLower().contains("microsoft");
}

inline void openDirectory(const QString& path)
{
    if (isWSL()) {
        QProcess wslpath;
        wslpath.start("wslpath", {"-w", path});
        wslpath.waitForFinished(3000);
        QString winPath = QString::fromUtf8(wslpath.readAllStandardOutput()).trimmed();
        if (!winPath.isEmpty())
            QProcess::startDetached("explorer.exe", {winPath});
    } else {
        QProcess::startDetached("xdg-open", {path});
    }
}

}
