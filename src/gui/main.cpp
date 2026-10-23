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
    // WSLg ships both the d3d12 Mesa driver (real GPU through /dev/dxg) and
    // llvmpipe (CPU rasteriser). Auto-selection lands on llvmpipe, which reports
    // "Accelerated: no" and makes the entire interface stutter, not just the 3D
    // viewport. d3d12 is a Gallium driver, so it is chosen with GALLIUM_DRIVER.
    //
    // Mesa resolves the driver while libGL is loaded, which happens before main()
    // runs, so setting the variable here would be too late for this process. Set
    // it and re-exec, which gives the replacement process the variable from the
    // start. The guard on GALLIUM_DRIVER already being set makes this run once.
    if (qEnvironmentVariableIsEmpty("GALLIUM_DRIVER") && QFile::exists("/dev/dxg"))
    {
        qputenv("GALLIUM_DRIVER", "d3d12");
        execv("/proc/self/exe", argv);
        // Only reached if execv failed; carry on with software rendering.
    }
    // Must be set before QApplication so all GL surfaces inherit it.
    // No MSAA (samples=0) is the single biggest rendering speedup on WSLg :
    // default multisampling forces the GPU to render every frame 4-8x over.
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

    ThemeEngine::instance().apply(Theme::Dark, app);

    MainWindow window;
    window.show();

    // A .cake path on the command line is opened at startup. New Project relaunches
    // the app this way so the new project starts from a truly clean process.
    if (argc > 1) {
        const QString arg = QString::fromLocal8Bit(argv[1]);
        if (arg.endsWith(".cake", Qt::CaseInsensitive) && QFile::exists(arg))
            window.loadProject(arg);
    }

    return app.exec();
}
