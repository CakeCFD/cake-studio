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

#include <QApplication>
#include <QSurfaceFormat>
#include <QFile>
#include <unistd.h>
#include "MainWindow.h"
#include "themes/ThemeEngine.h"

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("GALLIUM_DRIVER") && QFile::exists("/dev/dxg"))
    {
        qputenv("GALLIUM_DRIVER", "d3d12");
        execv("/proc/self/exe", argv);
    }
    QSurfaceFormat fmt;
    fmt.setVersion(3, 2);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(0);
    fmt.setDepthBufferSize(16);
    fmt.setStencilBufferSize(0);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName("Cake Studio");
    app.setOrganizationName("CFDStudio");

    ThemeEngine::instance().apply(Theme::Graphite, app);

    MainWindow window;
    window.show();

    if (argc > 1) {
        const QString arg = QString::fromLocal8Bit(argv[1]);
        if (arg.endsWith(".cake", Qt::CaseInsensitive) && QFile::exists(arg))
            window.loadProject(arg);
    }

    return app.exec();
}
