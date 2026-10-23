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

#include "FileBrowserDialog.h"
#include "MainWindow.h"
#include "MeshWizard.h"
#include "CakeMeshPanel.h"
#include "ExportResultsDialog.h"
#include "ConsolePanel.h"
#include "DatasetPanel.h"
#include "ViewportWidget.h"
#include "CaseTreePanel.h"
#include "PhysicsPanel.h"
#include "PropertiesPanel.h"
#include "BCEditorDialog.h"
#include "OpenCaseDialog.h"
#include "MeshQualityDialog.h"
#include "MRFDialog.h"
#include "SystemConfigPanel.h"
#include <QCloseEvent>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDockWidget>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QFile>
#include <QApplication>
#include <QProcess>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QFileInfo>
#include <QKeySequence>
#include <QInputDialog>
#include <QShortcut>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QTextEdit>
#include <QPlainTextEdit>

// helpers

// Docked panels: closable only: no dragging, no floating on WSLg. objName is
// REQUIRED: QMainWindow::saveState()/restoreState() matches
// docks by objectName; without it Qt can't restore the layout on restart.
static QDockWidget* makeDock(const QString& title, const QString& objName,
                              QWidget* widget, Qt::DockWidgetAreas areas, MainWindow* win)
{
    auto* dock = new QDockWidget(title, win);
    dock->setObjectName(objName);
    dock->setAllowedAreas(areas);
    dock->setWidget(widget);
    dock->setFeatures(QDockWidget::DockWidgetClosable);
    dock->setStyleSheet(
        "QDockWidget::title { padding: 4px 6px; font-size: 11px; font-weight: bold; }"
        "QDockWidget::close-button { padding: 2px; }");
    return dock;
}

// construction

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Cake Studio");
    setDockOptions(QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks);
    resize(1400, 900);
    setupUI();

    // Toast overlay: sits above everything, hidden by default
    m_toast = new QLabel(this);
    m_toast->setAlignment(Qt::AlignCenter);
    m_toast->setStyleSheet(
        "background: rgba(40,42,54,220);"
        "color: #a6e3a1;"
        "border: 1px solid #a6e3a1;"
        "border-radius: 6px;"
        "padding: 4px 16px;"
        "font-size: 12px;");
    m_toast->hide();
    m_toast->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, m_toast, &QLabel::hide);

    // Restore saved theme
    QSettings s("CakeCFD", "CakeStudio");
    Theme saved = (s.value("theme", "dark").toString() == "blueprint") ? Theme::Blueprint : Theme::Dark;
    if (saved == Theme::Blueprint) {
        ThemeEngine::instance().apply(Theme::Blueprint, *qApp);
        if (m_viewport) m_viewport->setTheme(Theme::Blueprint);
        if (m_meshViewport) m_meshViewport->setTheme(Theme::Blueprint);
        if (m_console)  m_console->setTheme(Theme::Blueprint);
        if (m_props)    m_props->onThemeChanged();
        if (m_sysPanel) m_sysPanel->onThemeChanged();
    }

    restoreWorkspace();
}

void MainWindow::setupUI() {
    setupDockPanels();   // panels first: menu needs their toggle actions
    setupMenuBar();
    setupToolBar();
    updateTitle();
    statusBar()->showMessage("Ready");
    // Capture the factory-default layout before any saved state is applied.
    // applyWorkspaceDefault() restores this exact snapshot.
    m_defaultState    = saveState();
    m_defaultGeometry = saveGeometry();
}

// dock panel setup

void MainWindow::setupDockPanels() {
    // Two separate viewports: Studio for results, Mesh for geometry preview.
    // m_meshViewport is created lazily on first switch to Mesh mode to avoid
    // paying for two VTK OpenGL contexts at startup.
    m_viewport      = new ViewportWidget(this, true);   // Studio: full toolbar
    m_viewportStack = new QStackedWidget(this);
    m_viewportStack->addWidget(m_viewport);          // index 0: Studio
    m_viewportStack->addWidget(new QWidget(this));   // index 1: placeholder until Mesh mode
    setCentralWidget(m_viewportStack);

    // System config bar: pinned to the top
    m_sysPanel = new SystemConfigPanel(this);
    auto* sysDock = makeDock("System", "dockSystem", m_sysPanel, Qt::TopDockWidgetArea, this);
    sysDock->setFeatures(QDockWidget::NoDockWidgetFeatures);  // not movable/closable
    sysDock->setTitleBarWidget(new QWidget);                   // hide title bar
    addDockWidget(Qt::TopDockWidgetArea, sysDock);

    // All four panels: float / dock / tab freely. Any area allowed.
    m_caseTree  = new CaseTreePanel(this);
    m_caseDock  = makeDock("Case", "dockCase", m_caseTree, Qt::AllDockWidgetAreas, this);
    addDockWidget(Qt::LeftDockWidgetArea, m_caseDock);

    m_props    = new PropertiesPanel(this);
    m_propDock = makeDock("Properties", "dockProperties", m_props, Qt::AllDockWidgetAreas, this);
    addDockWidget(Qt::RightDockWidgetArea, m_propDock);

    m_datasetPanel = new DatasetPanel(this);
    m_dataDock     = makeDock("Dataset Manager", "dockDataset", m_datasetPanel,
                               Qt::AllDockWidgetAreas, this);
    addDockWidget(Qt::RightDockWidgetArea, m_dataDock);
    tabifyDockWidget(m_propDock, m_dataDock);
    m_propDock->raise();

    m_console     = new ConsolePanel(this);
    m_consoleDock = makeDock("Solver Console", "dockConsole", m_console,
                              Qt::AllDockWidgetAreas, this);
    addDockWidget(Qt::BottomDockWidgetArea, m_consoleDock);

    // Floors so a panel can never be dragged down to a clipped sliver
    m_caseDock->setMinimumWidth(160);
    m_propDock->setMinimumWidth(260);
    m_dataDock->setMinimumWidth(260);
    m_consoleDock->setMinimumHeight(80);

    // Style the tab bar Qt places on tabified dock groups so text elides with "…"
    // instead of hard-clipping, and tabs have breathing room.
    // min-width must cover the longest tab label ("Dataset Manager" ≈ 130 px at 11 px font).
    setStyleSheet(styleSheet() + R"(
        QTabBar::tab {
            padding: 4px 14px;
            min-width: 130px;
            min-height: 22px;
        }
        QTabBar::tab:only-one {
            min-width: 80px;
        }
        QDockWidget::title {
            padding: 4px 8px;
        }
    )");

    // Cake Mesh panel: hidden until the user switches to Mesh mode.
    // setViewport() is called lazily in setMode() when m_meshViewport is created.
    m_meshPanel = new CakeMeshPanel(this);
    m_meshDock  = makeDock("Cake Mesh", "dockMesh", m_meshPanel, Qt::AllDockWidgetAreas, this);
    addDockWidget(Qt::LeftDockWidgetArea, m_meshDock);
    m_meshDock->hide();
    m_meshDock->setMinimumWidth(280);

    // Wire up inter-panel signals
    m_caseTree->setViewport(m_viewport);
    connect(m_caseTree, &CaseTreePanel::itemSelected, m_props, &PropertiesPanel::showItem);
    connect(m_console,  &ConsolePanel::resultsReady,  m_viewport,     &ViewportWidget::loadOFResults);
    connect(m_console,  &ConsolePanel::resultsReady,  m_datasetPanel, &DatasetPanel::setCaseDir);
    connect(m_console,  &ConsolePanel::resultsReady,  m_caseTree,     &CaseTreePanel::setCaseDir);
    connect(m_sysPanel, &SystemConfigPanel::stepsChanged, m_viewport, &ViewportWidget::setSliderMax);
    connect(m_console,  &ConsolePanel::aboutToRun,        m_sysPanel, &SystemConfigPanel::onApply);
    connect(m_console,  &ConsolePanel::caseFilesChanged, this, [this](const QString& cd){
        m_props->showItem(TypeSection, cd, cd);
        m_sysPanel->refreshControlDict();
    });
    connect(m_viewport, &ViewportWidget::streamlinesDiverged, this, [this]() {
        statusBar()->showMessage(
            "Warning: velocity field diverged: streamlines suppressed. "
            "Check Physics settings and reduce relaxation factors before re-running.", 10000);
    });
    connect(m_viewport, &ViewportWidget::streamlinesAxisMismatch, this,
            [this](const QString& configured, const QString& actual) {
        statusBar()->showMessage(
            QString("Stale results: solved on %1 but Domain Setup is set to %2. "
                    "Re-run blockMesh / snappyHexMesh / the solver to see %2 streamlines.")
                .arg(actual, configured), 15000);
    });

    connect(m_props, &PropertiesPanel::loadDomainRequested, this,
            [this](const QString& caseDir, const QString& stlPath) {
        if (!caseDir.isEmpty()) {
            if (m_console)    m_console->setCaseDir(caseDir);
            if (m_caseTree)   m_caseTree->setCaseDir(caseDir);
            if (m_meshPanel)  m_meshPanel->setProjectDir(caseDir);
            if (m_sysPanel)     m_sysPanel->setCaseDir(caseDir);
            if (m_datasetPanel) m_datasetPanel->setCaseDir(caseDir);
        }
        if (m_viewport && !stlPath.isEmpty() && QFile::exists(stlPath))
            m_viewport->loadSTL(stlPath);
        statusBar()->showMessage("Domain loaded: " + QFileInfo(stlPath).fileName(), 5000);
        markDirty();
    });

    connect(m_props, &PropertiesPanel::domainPreviewChanged, this,
            [this](double x0, double y0, double z0, double x1, double y1, double z1,
                   const QString& axis) {
        m_dpX0 = x0; m_dpY0 = y0; m_dpZ0 = z0;
        m_dpX1 = x1; m_dpY1 = y1; m_dpZ1 = z1;
        m_dpAxis = axis;
        m_hasDomainPreview = true;
        markDirty();
        if (m_viewport)     m_viewport->updateDomainPreview(x0,y0,z0, x1,y1,z1, axis);
        if (m_meshViewport) m_meshViewport->updateDomainPreview(x0,y0,z0, x1,y1,z1, axis);
    });

    // Export to Studio: switch mode, load mesh STL into Studio viewport, set case dir
    connect(m_meshPanel, &CakeMeshPanel::exportRequested, this, [this](const QString& caseDir){
        if (m_console)       m_console->setCaseDir(caseDir);
        if (m_caseTree)      m_caseTree->setCaseDir(caseDir);
        if (m_meshPanel)     m_meshPanel->setProjectDir(caseDir);
        if (m_sysPanel)      m_sysPanel->setCaseDir(caseDir);
        if (m_datasetPanel)  m_datasetPanel->setCaseDir(caseDir);
        // Show the meshed surface in the Studio viewport
        QString caseName = QFileInfo(caseDir).fileName();
        QString stlPath  = caseDir + "/constant/triSurface/" + caseName + ".stl";
        if (QFile::exists(stlPath) && m_viewport)
            m_viewport->loadSTL(stlPath);
        markDirty();
        setMode(AppMode::Studio);
        statusBar()->showMessage("Exported to Cake Studio: ready to run solver.", 6000);
    });
}

// menu bar

void MainWindow::setupMenuBar() {
    // File
    auto* file = menuBar()->addMenu("File");

    auto* newProj = file->addAction("New Project");
    newProj->setShortcut(QKeySequence::New);
    connect(newProj, &QAction::triggered, this, &MainWindow::onNewProject);

    auto* openProj = file->addAction("Open Project...");
    openProj->setShortcut(QKeySequence::Open);
    connect(openProj, &QAction::triggered, this, &MainWindow::onOpenProject);

    m_recentMenu = file->addMenu("Recent Projects");
    rebuildRecentMenu();

    file->addSeparator();

    auto* saveProj = file->addAction("Save Project");
    saveProj->setShortcut(QKeySequence::Save);
    saveProj->setShortcutContext(Qt::ApplicationShortcut);
    connect(saveProj, &QAction::triggered, this, &MainWindow::onSaveProject);

    auto* saveAs = file->addAction("Save Project As...");
    saveAs->setShortcut(QKeySequence::SaveAs);
    saveAs->setShortcutContext(Qt::ApplicationShortcut);
    connect(saveAs, &QAction::triggered, this, &MainWindow::onSaveProjectAs);

    file->addSeparator();
    connect(file->addAction("Open Case..."), &QAction::triggered, this, &MainWindow::onOpenCase);
    file->addSeparator();
    file->addAction("Exit", qApp, &QApplication::quit);

    // Edit
    auto* editMenu = menuBar()->addMenu("Edit");

    auto* undoAct = editMenu->addAction("Undo");
    undoAct->setShortcut(QKeySequence::Undo);
    undoAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(undoAct, &QAction::triggered, this, []{
        if (auto* w = QApplication::focusWidget()) {
            QKeyEvent kp(QEvent::KeyPress,   Qt::Key_Z, Qt::ControlModifier);
            QKeyEvent kr(QEvent::KeyRelease, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(w, &kp);
            QApplication::sendEvent(w, &kr);
        }
    });

    auto* redoAct = editMenu->addAction("Redo");
    redoAct->setShortcut(QKeySequence::Redo);
    redoAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(redoAct, &QAction::triggered, this, []{
        if (auto* w = QApplication::focusWidget()) {
            QKeyEvent kp(QEvent::KeyPress,   Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
            QKeyEvent kr(QEvent::KeyRelease, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
            QApplication::sendEvent(w, &kp);
            QApplication::sendEvent(w, &kr);
        }
    });

    editMenu->addSeparator();

    auto* cutAct = editMenu->addAction("Cut");
    cutAct->setShortcut(QKeySequence::Cut);
    cutAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(cutAct, &QAction::triggered, this, []{
        if (auto* w = QApplication::focusWidget()) {
            QKeyEvent kp(QEvent::KeyPress,   Qt::Key_X, Qt::ControlModifier);
            QKeyEvent kr(QEvent::KeyRelease, Qt::Key_X, Qt::ControlModifier);
            QApplication::sendEvent(w, &kp);
            QApplication::sendEvent(w, &kr);
        }
    });

    auto* copyAct = editMenu->addAction("Copy");
    copyAct->setShortcut(QKeySequence::Copy);
    copyAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(copyAct, &QAction::triggered, this, []{
        if (auto* w = QApplication::focusWidget()) {
            QKeyEvent kp(QEvent::KeyPress,   Qt::Key_C, Qt::ControlModifier);
            QKeyEvent kr(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier);
            QApplication::sendEvent(w, &kp);
            QApplication::sendEvent(w, &kr);
        }
    });

    auto* pasteAct = editMenu->addAction("Paste");
    pasteAct->setShortcut(QKeySequence::Paste);
    pasteAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(pasteAct, &QAction::triggered, this, []{
        if (auto* w = QApplication::focusWidget()) {
            QKeyEvent kp(QEvent::KeyPress,   Qt::Key_V, Qt::ControlModifier);
            QKeyEvent kr(QEvent::KeyRelease, Qt::Key_V, Qt::ControlModifier);
            QApplication::sendEvent(w, &kp);
            QApplication::sendEvent(w, &kr);
        }
    });

    auto* selAllAct = editMenu->addAction("Select All");
    selAllAct->setShortcut(QKeySequence::SelectAll);
    selAllAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(selAllAct, &QAction::triggered, this, []{
        if (auto* w = QApplication::focusWidget()) {
            QKeyEvent kp(QEvent::KeyPress,   Qt::Key_A, Qt::ControlModifier);
            QKeyEvent kr(QEvent::KeyRelease, Qt::Key_A, Qt::ControlModifier);
            QApplication::sendEvent(w, &kp);
            QApplication::sendEvent(w, &kr);
        }
    });

    // View
    auto* view     = menuBar()->addMenu("View");
    auto* themeMenu = view->addMenu("Theme");
    connect(themeMenu->addAction("Dark"), &QAction::triggered, this, [this](){
        ThemeEngine::instance().apply(Theme::Dark, *qApp);
        if (m_viewport) m_viewport->setTheme(Theme::Dark);
        if (m_meshViewport) m_meshViewport->setTheme(Theme::Dark);
        if (m_console)  m_console->setTheme(Theme::Dark);
        if (m_props)    m_props->onThemeChanged();
        if (m_sysPanel) m_sysPanel->onThemeChanged();
        QSettings("CakeCFD","CakeStudio").setValue("theme","dark");
        markDirty();
    });
    connect(themeMenu->addAction("Blueprint"), &QAction::triggered, this, [this](){
        ThemeEngine::instance().apply(Theme::Blueprint, *qApp);
        if (m_viewport) m_viewport->setTheme(Theme::Blueprint);
        if (m_meshViewport) m_meshViewport->setTheme(Theme::Blueprint);
        if (m_console)  m_console->setTheme(Theme::Blueprint);
        if (m_props)    m_props->onThemeChanged();
        if (m_sysPanel) m_sysPanel->onThemeChanged();
        QSettings("CakeCFD","CakeStudio").setValue("theme","blueprint");
        markDirty();
    });
    view->addSeparator();
    connect(view->addAction("Reset Camera"), &QAction::triggered,
            this, [this](){ if (m_viewport) m_viewport->resetCamera(); });

    // Mesh
    auto* meshMenu = menuBar()->addMenu("Mesh");
    connect(meshMenu->addAction("Mesh Setup Wizard..."), &QAction::triggered,
            this, [this](){
                QString cd = m_console ? m_console->caseDir() : QString();
                MeshWizard dlg(this, cd);
                dlg.exec();
            });
    meshMenu->addSeparator();
    connect(meshMenu->addAction("Mesh Quality (checkMesh)..."), &QAction::triggered,
            this, [this](){
                QString cd = m_console ? m_console->caseDir() : QString();
                if (cd.isEmpty()) { showStatusWarning("No case directory set."); return; }
                MeshQualityDialog dlg(cd, this);
                dlg.exec();
            });

    // Physics
    auto* physicsMenu = menuBar()->addMenu("Physics");
    connect(physicsMenu->addAction("Physics Settings..."), &QAction::triggered, this, [this](){
        QString cd = m_console ? m_console->caseDir() : QString();
        if (cd.isEmpty()) {
            QMessageBox::warning(this, "No Case Open", "Open a case directory before editing physics settings.");
            return;
        }
        PhysicsPanel dlg(cd, this);
        if (m_console)
            connect(&dlg, &PhysicsPanel::solverRecommended, m_console, &ConsolePanel::setSolver);
        dlg.exec();
        markDirty();
    });
    connect(physicsMenu->addAction("Boundary Conditions..."), &QAction::triggered, this, [this](){
        QString cd = m_console ? m_console->caseDir() : QString();
        if (cd.isEmpty()) {
            QMessageBox::warning(this, "No Case Open", "Open a case directory before editing boundary conditions.");
            return;
        }
        BCEditorDialog dlg(cd, this);
        dlg.exec();
    });
    physicsMenu->addSeparator();
    connect(physicsMenu->addAction("MRF / Rotating Zones..."), &QAction::triggered, this, [this](){
        QString cd = m_console ? m_console->caseDir() : QString();
        if (cd.isEmpty()) {
            QMessageBox::warning(this, "No Case Open", "Open a case directory before configuring MRF zones.");
            return;
        }
        MRFDialog dlg(cd, this);
        dlg.exec();
    });

    // Solve
    auto* solveMenu = menuBar()->addMenu("Solve");
    auto* runAct = solveMenu->addAction("Run Solver");
    runAct->setShortcut(Qt::Key_F5);
    runAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(runAct, &QAction::triggered, this, [this](){ if (m_console) m_console->onRunClicked(); });

    auto* stopAct = solveMenu->addAction("Stop Solver");
    stopAct->setShortcut(Qt::Key_F6);
    stopAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(stopAct, &QAction::triggered, this, [this](){ if (m_console) m_console->onStopClicked(); });

    connect(solveMenu->addAction("Reset && Run"), &QAction::triggered, this, [this](){
        if (!m_console) return;
        QString cd = m_console->caseDir();
        if (cd.isEmpty()) { showStatusWarning("No case directory set."); return; }
        // Never delete time dirs out from under a live solver
        if (m_console->runner()->isRunning()) { showStatusWarning("A solver is already running: stop it first."); return; }
        for (const QString& e : QDir(cd).entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
            bool ok; int n = e.toInt(&ok);
            if (ok && n > 0) QDir(QDir(cd).filePath(e)).removeRecursively();
        }
        m_console->onRunClicked();
    });

    // Results
    auto* resultsMenu = menuBar()->addMenu("Results");
    auto* loadResAct = resultsMenu->addAction("Load Results");
    loadResAct->setShortcut(Qt::Key_F7);
    loadResAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(loadResAct, &QAction::triggered, this, [this](){
        if (!m_console || !m_viewport) return;
        QString cd = m_console->caseDir();
        if (cd.isEmpty()) {
            QMessageBox::warning(this, "No Case Open", "Open a case directory before loading results.");
            return;
        }
        m_viewport->loadOFResults(cd);
    });
    auto* expResAct = resultsMenu->addAction("Export Results…");
    expResAct->setShortcut(QKeySequence("Ctrl+Shift+E"));
    expResAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(expResAct, &QAction::triggered, this, [this](){
        QString cd = m_console ? m_console->caseDir() : QString();
        if (cd.isEmpty()) {
            QMessageBox::warning(this, "No Case Open", "Open a case directory before exporting results.");
            return;
        }
        ExportResultsDialog dlg(cd, this);
        dlg.exec();
    });
    auto* resetViewAct = resultsMenu->addAction("Reset View");
    resetViewAct->setShortcut(Qt::Key_F4);
    resetViewAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(resetViewAct, &QAction::triggered,
            this, [this](){ if (m_viewport) m_viewport->resetCamera(); });

    // Window menu: panel toggles + workspace presets
    auto* winMenu = menuBar()->addMenu("Window");

    winMenu->addSection("Panels");
    winMenu->addAction(m_caseDock->toggleViewAction());
    winMenu->addAction(m_propDock->toggleViewAction());
    winMenu->addAction(m_dataDock->toggleViewAction());
    winMenu->addAction(m_consoleDock->toggleViewAction());

    winMenu->addSeparator();
    winMenu->addSection("Mode");
    auto* studioModeAct = winMenu->addAction("Cake Studio");
    studioModeAct->setShortcut(QKeySequence("Ctrl+1"));
    studioModeAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(studioModeAct, &QAction::triggered, this, [this]{ setMode(AppMode::Studio); });

    auto* meshModeAct = winMenu->addAction("Cake Mesh");
    meshModeAct->setShortcut(QKeySequence("Ctrl+2"));
    meshModeAct->setShortcutContext(Qt::ApplicationShortcut);
    connect(meshModeAct, &QAction::triggered, this, [this]{ setMode(AppMode::Mesh); });

    winMenu->addSeparator();
    winMenu->addSection("Workspace");
    auto* wsDefault = winMenu->addAction("Default");
    wsDefault->setShortcut(QKeySequence("Ctrl+Shift+1"));
    wsDefault->setShortcutContext(Qt::ApplicationShortcut);
    connect(wsDefault, &QAction::triggered, this, &MainWindow::applyWorkspaceDefault);

    connect(winMenu->addAction("Mesh Setup"),     &QAction::triggered, this, &MainWindow::applyWorkspaceMesh);
    connect(winMenu->addAction("Post-Process"),   &QAction::triggered, this, &MainWindow::applyWorkspacePostProcess);
    connect(winMenu->addAction("Wide Viewport"),  &QAction::triggered, this, &MainWindow::applyWorkspaceWide);
    winMenu->addSeparator();
    connect(winMenu->addAction("Save Layout"),    &QAction::triggered, this, &MainWindow::saveWorkspace);
    connect(winMenu->addAction("Restore Layout"), &QAction::triggered, this, &MainWindow::restoreWorkspace);

    winMenu->addSeparator();
    auto* focusAct = winMenu->addAction("Viewport Focus\tF");
    focusAct->setToolTip("Hide all panels: press F again to restore");
    connect(focusAct, &QAction::triggered, this, &MainWindow::toggleFocusMode);

    // QShortcut lives on the window directly: stays active even when menubar is hidden.
    // Window context only (not dialogs), and never while a text/choice widget has focus.
    auto* focusShortcut = new QShortcut(Qt::Key_F, this);
    focusShortcut->setContext(Qt::WindowShortcut);
    connect(focusShortcut, &QShortcut::activated, this, [this]{
        QWidget* fw = QApplication::focusWidget();
        if (qobject_cast<QLineEdit*>(fw) || qobject_cast<QAbstractSpinBox*>(fw)
            || qobject_cast<QComboBox*>(fw) || qobject_cast<QTextEdit*>(fw))
            return;
        if (auto* pte = qobject_cast<QPlainTextEdit*>(fw); pte && !pte->isReadOnly())
            return;
        toggleFocusMode();
    });

    // Help
    auto* helpMenu = menuBar()->addMenu("Help");
    connect(helpMenu->addAction("About Cake Studio"), &QAction::triggered, this, [this](){
        QMessageBox::about(this, "About Cake Studio",
            "<b>Cake Studio</b><br>"
            "OpenFOAM based computational fluid dynamics<br><br>"
            "Engine: CakeCFD (OpenFOAM 2412)<br>"
            "Schemes: TENO5, kOmegaSST");
    });
}

// toolbar

void MainWindow::setupToolBar() {
    auto* tb = addToolBar("Main");
    tb->setObjectName("MainToolBar");   // needed for saveState
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb->setFixedHeight(28);
    tb->setStyleSheet(
        "QToolBar { padding: 0; spacing: 1px; }"
        "QToolButton { padding: 3px 10px; margin: 1px; border-radius: 3px; font-size: 12px; }"
        "QToolBar::separator { width: 1px; background: #45475a; margin: 4px 2px; }");

    // Mode switcher (always visible)
    auto* modeWidget = new QWidget;
    auto* modeLayout = new QHBoxLayout(modeWidget);
    modeLayout->setContentsMargins(2, 2, 2, 2);
    modeLayout->setSpacing(0);
    m_btnStudio = new QPushButton("Studio");
    m_btnMesh   = new QPushButton("Mesh");
    m_btnStudio->setCheckable(true);
    m_btnMesh->setCheckable(true);
    m_btnStudio->setChecked(true);
    m_btnStudio->setStyleSheet(
        "QPushButton { padding:3px 14px; border-radius:0; border-top-left-radius:4px; "
        "border-bottom-left-radius:4px; font-size:12px; font-weight:bold; }"
        "QPushButton:checked { background:#89b4fa; color:#1e1e2e; }");
    m_btnMesh->setStyleSheet(
        "QPushButton { padding:3px 14px; border-radius:0; border-top-right-radius:4px; "
        "border-bottom-right-radius:4px; font-size:12px; font-weight:bold; }"
        "QPushButton:checked { background:#a6e3a1; color:#1e1e2e; }");
    modeLayout->addWidget(m_btnStudio);
    modeLayout->addWidget(m_btnMesh);
    tb->addWidget(modeWidget);

    connect(m_btnStudio, &QPushButton::clicked, this, [this]{ setMode(AppMode::Studio); });
    connect(m_btnMesh,   &QPushButton::clicked, this, [this]{ setMode(AppMode::Mesh); });

    // Studio-only toolbar actions (hidden in Mesh mode)
    auto addStudio = [&](QAction* a){ m_studioToolbarItems << a; return a; };

    addStudio(tb->addSeparator());
    addStudio(tb->addAction("Open Case", this, &MainWindow::onOpenCase));
    addStudio(tb->addSeparator());
    addStudio(tb->addAction("Run Solver",  this, [this](){ if (m_console) m_console->onRunClicked(); }));
    addStudio(tb->addAction("Stop Solver", this, [this](){ if (m_console) m_console->onStopClicked(); }));
    addStudio(tb->addSeparator());
    addStudio(tb->addAction("Reset View",  this, [this](){ if (m_viewport) m_viewport->resetCamera(); }));
    addStudio(tb->addSeparator());
    addStudio(tb->addAction("Load Results", this, [this](){
        QString start = (m_console && !m_console->caseDir().isEmpty())
                        ? m_console->caseDir() : QDir::homePath();
        QString cd = FileBrowserDialog::getExistingDirectory(
            this, "Load Results: Select Case Directory", start);
        if (cd.isEmpty()) return;
        if (m_console)      m_console->setCaseDir(cd);
        if (m_caseTree)     m_caseTree->setCaseDir(cd);
        if (m_meshPanel)    m_meshPanel->setProjectDir(cd);
        if (m_sysPanel)     m_sysPanel->setCaseDir(cd);
        if (m_datasetPanel) m_datasetPanel->setCaseDir(cd);
        if (!m_viewport) return;
        QStringList ts;
        for (const QString& e : QDir(cd).entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
            bool ok; double v = e.toDouble(&ok);
            if (ok && v > 0) ts << e;
        }
        if (ts.isEmpty()) {
            showStatusWarning("No result time steps found in " + QFileInfo(cd).fileName(), 5000);
            return;
        }
        statusBar()->showMessage("Loading " + QString::number(ts.size()) + " steps from " + QFileInfo(cd).fileName() + "...");
        m_viewport->loadOFResults(cd);
        statusBar()->showMessage("Loaded " + QString::number(ts.size()) + " steps  |  " + QFileInfo(cd).fileName(), 8000);
    }));
    addStudio(tb->addSeparator());
    addStudio(tb->addAction("Export Results", this, [this](){
        QString cd = m_console ? m_console->caseDir() : QString();
        if (cd.isEmpty()) { showStatusWarning("No case directory set."); return; }
        ExportResultsDialog dlg(cd, this);
        dlg.exec();
    }));
}

// workspace presets

void MainWindow::applyWorkspaceDefault() {
    for (auto* d : {m_caseDock, m_propDock, m_dataDock, m_consoleDock})
        { d->setFloating(false); d->show(); }
    // Explicitly re-place: Case left, Properties+Dataset right, Console bottom
    addDockWidget(Qt::LeftDockWidgetArea,   m_caseDock);
    addDockWidget(Qt::RightDockWidgetArea,  m_propDock);
    addDockWidget(Qt::RightDockWidgetArea,  m_dataDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_consoleDock);
    tabifyDockWidget(m_propDock, m_dataDock);
    m_propDock->raise();
    resizeDocks({m_caseDock},    {280}, Qt::Horizontal);
    resizeDocks({m_propDock},    {300}, Qt::Horizontal);
    resizeDocks({m_consoleDock}, {180}, Qt::Vertical);
    statusBar()->showMessage("Workspace: Default", 3000);
}

void MainWindow::applyWorkspaceMesh() {
    for (auto* d : {m_caseDock, m_propDock, m_dataDock, m_consoleDock})
        { d->setFloating(false); d->show(); }
    addDockWidget(Qt::LeftDockWidgetArea,   m_caseDock);
    addDockWidget(Qt::LeftDockWidgetArea,   m_propDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_consoleDock);
    splitDockWidget(m_caseDock, m_propDock, Qt::Vertical);
    m_dataDock->hide();   // not needed while meshing
    resizeDocks({m_caseDock},    {260}, Qt::Horizontal);
    resizeDocks({m_consoleDock}, {260}, Qt::Vertical);
    statusBar()->showMessage("Workspace: Mesh Setup", 3000);
}

void MainWindow::applyWorkspacePostProcess() {
    for (auto* d : {m_caseDock, m_propDock, m_dataDock, m_consoleDock})
        { d->setFloating(false); d->show(); }
    addDockWidget(Qt::RightDockWidgetArea,  m_propDock);
    addDockWidget(Qt::RightDockWidgetArea,  m_dataDock);
    addDockWidget(Qt::RightDockWidgetArea,  m_caseDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_consoleDock);
    tabifyDockWidget(m_propDock, m_dataDock);
    tabifyDockWidget(m_dataDock, m_caseDock);
    m_dataDock->raise();
    resizeDocks({m_propDock},    {320}, Qt::Horizontal);
    resizeDocks({m_consoleDock}, {140}, Qt::Vertical);
    statusBar()->showMessage("Workspace: Post-Process", 3000);
}

void MainWindow::applyWorkspaceWide() {
    m_caseDock->hide();
    m_propDock->hide();
    m_dataDock->hide();
    m_consoleDock->show();
    m_consoleDock->setFloating(false);
    addDockWidget(Qt::BottomDockWidgetArea, m_consoleDock);
    resizeDocks({m_consoleDock}, {140}, Qt::Vertical);
    statusBar()->showMessage("Workspace: Wide Viewport", 3000);
}

// save / restore

// Bumped whenever the dock set or their objectNames change. State saved by an older
// build is discarded rather than restored: restoring a mismatched layout is what
// produced the broken/overlapping panels.
static constexpr int kLayoutVersion = 5;  // bumped: added dockDataset

void MainWindow::saveWorkspace() {
    QSettings s("CakeCFD", "CakeStudio");
    s.setValue("layoutVersion",  kLayoutVersion);
    s.setValue("windowGeometry", saveGeometry());
    s.setValue("windowState",    saveState(kLayoutVersion));
    statusBar()->showMessage("Layout saved", 3000);
}

void MainWindow::restoreWorkspace() {
    QSettings s("CakeCFD", "CakeStudio");
    if (s.value("layoutVersion", 0).toInt() != kLayoutVersion) {
        s.remove("windowGeometry");
        s.remove("windowState");
        return;  // fall back to the layout setupDockPanels() just built
    }
    if (s.contains("windowGeometry")) restoreGeometry(s.value("windowGeometry").toByteArray());
    if (s.contains("windowState"))    restoreState(s.value("windowState").toByteArray(), kLayoutVersion);
    // The saved state may have been captured in the other mode
    syncDocksToMode();
}

void MainWindow::syncDocksToMode()
{
    const bool studio = (m_mode == AppMode::Studio);
    if (studio) {
        if (m_meshDock) m_meshDock->hide();
    } else {
        for (QDockWidget* d : {m_caseDock, m_propDock, m_dataDock, m_consoleDock})
            if (d) d->hide();
    }
}

void MainWindow::onDockTopLevelChanged(QDockWidget* dock, bool floating) {
    Q_UNUSED(dock); Q_UNUSED(floating);
}

void MainWindow::setMode(AppMode mode)
{
    m_mode = mode;
    bool studio = (mode == AppMode::Studio);

    // Lazy-create the mesh viewport the first time the user switches to Mesh mode.
    if (!studio && !m_meshViewport) {
        m_meshViewport = new ViewportWidget(this, false);
        // Replace the placeholder at index 1
        auto* placeholder = m_viewportStack->widget(1);
        m_viewportStack->removeWidget(placeholder);
        delete placeholder;
        m_viewportStack->insertWidget(1, m_meshViewport);
        m_meshPanel->setViewport(m_meshViewport);
        m_meshViewport->setTheme(ThemeEngine::instance().current());
        // Replay any domain preview that was already configured
        if (m_hasDomainPreview)
            m_meshViewport->updateDomainPreview(
                m_dpX0, m_dpY0, m_dpZ0,
                m_dpX1, m_dpY1, m_dpZ1, m_dpAxis);
    }

    // Viewports
    m_viewportStack->setCurrentIndex(studio ? 0 : 1);

    // Docks
    m_caseDock->setVisible(studio);
    m_propDock->setVisible(studio);
    m_dataDock->setVisible(studio);
    m_consoleDock->setVisible(studio);
    m_meshDock->setVisible(!studio);

    // Toolbar
    for (QAction* a : m_studioToolbarItems) a->setVisible(studio);

    // Mode toggle state
    m_btnStudio->setChecked(studio);
    m_btnMesh->setChecked(!studio);

    statusBar()->showMessage(studio ? "Cake Studio" : "Cake Mesh", 3000);
}

// actions


void MainWindow::onOpenCase() {
    OpenCaseDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString dir = dlg.selectedDir();
    if (dir.isEmpty() || !QDir(dir).exists()) return;

    // Wire up all panels
    if (m_console)      m_console->setCaseDir(dir);
    if (m_caseTree)     m_caseTree->setCaseDir(dir);
    if (m_viewport)     m_viewport->setProjectDir(dir);
    if (m_meshPanel)    m_meshPanel->setProjectDir(dir);
    if (m_sysPanel)     m_sysPanel->setCaseDir(dir);
    if (m_datasetPanel) m_datasetPanel->setCaseDir(dir);

    // Auto-load results
    if (m_viewport) {
        m_viewport->loadOFResults(dir);

        // Auto-detect STL from constant/triSurface
        QDir triSurf(dir + "/constant/triSurface");
        const auto stls = triSurf.entryInfoList({"*.stl","*.STL"}, QDir::Files);
        if (!stls.isEmpty())
            m_viewport->loadSTL(stls.first().absoluteFilePath());
    }

    // Migrate any stale TENO libs placement and restore physics
    PhysicsPanel::migrateTenolibsToControlDict(dir);

    statusBar()->showMessage("Case: " + dir);
    markDirty();
}

void MainWindow::onSwitchTheme() {}

// project (.cake)

void MainWindow::updateTitle()
{
    QString name = m_projectPath.isEmpty()
                 ? "Untitled"
                 : QFileInfo(m_projectPath).completeBaseName();
    setWindowTitle(QString("Cake Studio: %1%2").arg(name, m_projectDirty ? " *" : ""));
}

void MainWindow::markDirty()
{
    if (!m_projectDirty) { m_projectDirty = true; updateTitle(); }
}

void MainWindow::addRecentProject(const QString& path)
{
    QSettings s("CakeCFD", "CakeStudio");
    QStringList recents = s.value("recentProjects").toStringList();
    recents.removeAll(path);
    recents.prepend(path);
    while (recents.size() > 8) recents.removeLast();
    s.setValue("recentProjects", recents);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    QStringList recents = QSettings("CakeCFD","CakeStudio").value("recentProjects").toStringList();
    if (recents.isEmpty()) {
        m_recentMenu->addAction("(no recent projects)")->setEnabled(false);
        return;
    }
    for (const QString& p : recents) {
        QString label = QFileInfo(p).fileName() + ": " + QFileInfo(p).absolutePath();
        auto* act = m_recentMenu->addAction(label);
        connect(act, &QAction::triggered, this, [this, p]{ loadProject(p); });
    }
    m_recentMenu->addSeparator();
    m_recentMenu->addAction("Clear Recent", this, [this]{
        QSettings("CakeCFD","CakeStudio").remove("recentProjects");
        rebuildRecentMenu();
    });
}

bool MainWindow::saveProject(const QString& path)
{
    QString caseDir = m_console ? m_console->caseDir() : QString();
    Theme   theme   = ThemeEngine::instance().current();

    QJsonObject root;
    root["version"]  = 1;
    root["app"]      = "CakeStudio";
    root["saved"]    = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["case_dir"] = caseDir;
    root["theme"]    = (theme == Theme::Blueprint) ? "blueprint" : "dark";

    if (m_viewport) {
        QJsonObject vp;
        vp["active_field"]       = m_viewport->activeField();
        vp["active_time_step"]   = m_viewport->activeTimeStep();
        vp["camera"]             = m_viewport->getCameraState();
        vp["stl_path"]           = m_viewport->stlPath();
        vp["mesh_hidden"]        = m_viewport->meshHidden();
        vp["stream_proximity"]   = m_viewport->streamProximity();
        vp["stream_density"]     = m_viewport->streamDensity();
        vp["stream_tube_scale"]  = m_viewport->streamTubeScale();
        root["viewport"] = vp;
    }

    if (m_hasDomainPreview) {
        QJsonObject dp;
        dp["x0"] = m_dpX0; dp["y0"] = m_dpY0; dp["z0"] = m_dpZ0;
        dp["x1"] = m_dpX1; dp["y1"] = m_dpY1; dp["z1"] = m_dpZ1;
        dp["axis"] = m_dpAxis;
        root["domain_preview"] = dp;
    }

    if (m_meshPanel)
        root["mesh_panel"] = m_meshPanel->getState();

    // Snapshot physics settings (solver, scheme, inlet, fluid props) so they
    // survive even if the case files are regenerated.
    if (!caseDir.isEmpty())
        root["physics"] = PhysicsPanel::snapshotFromCase(caseDir);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Save Failed", "Could not write:\n" + path);
        return false;
    }
    f.write(QJsonDocument(root).toJson());

    m_projectPath  = path;
    m_projectDirty = false;
    updateTitle();
    addRecentProject(path);
    statusBar()->showMessage("Saved: " + QFileInfo(path).fileName());
    showToast("Saved");
    startAutosave();
    return true;
}

void MainWindow::startAutosave()
{
    if (!m_autosaveTimer) {
        m_autosaveTimer = new QTimer(this);
        m_autosaveTimer->setInterval(3 * 60 * 1000); // 3 minutes
        connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::onAutosave);
    }
    if (!m_projectPath.isEmpty())
        m_autosaveTimer->start();
}

void MainWindow::onAutosave()
{
    if (m_projectPath.isEmpty()) { m_autosaveTimer->stop(); return; }
    QFile f(m_projectPath);
    if (!f.open(QIODevice::WriteOnly)) return;
    // Build the same JSON as saveProject but without the dirty-flag side effects
    QString caseDir = m_console ? m_console->caseDir() : QString();
    Theme   theme   = ThemeEngine::instance().current();
    QJsonObject root;
    root["version"]  = 1;
    root["app"]      = "CakeStudio";
    root["saved"]    = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["case_dir"] = caseDir;
    root["theme"]    = (theme == Theme::Blueprint) ? "blueprint" : "dark";
    if (m_viewport) {
        QJsonObject vp;
        vp["active_field"]      = m_viewport->activeField();
        vp["active_time_step"]  = m_viewport->activeTimeStep();
        vp["camera"]            = m_viewport->getCameraState();
        vp["stl_path"]          = m_viewport->stlPath();
        vp["mesh_hidden"]       = m_viewport->meshHidden();
        vp["stream_proximity"]  = m_viewport->streamProximity();
        vp["stream_density"]    = m_viewport->streamDensity();
        vp["stream_tube_scale"] = m_viewport->streamTubeScale();
        root["viewport"] = vp;
    }
    if (m_hasDomainPreview) {
        QJsonObject dp;
        dp["x0"] = m_dpX0; dp["y0"] = m_dpY0; dp["z0"] = m_dpZ0;
        dp["x1"] = m_dpX1; dp["y1"] = m_dpY1; dp["z1"] = m_dpZ1;
        dp["axis"] = m_dpAxis;
        root["domain_preview"] = dp;
    }
    if (m_meshPanel)
        root["mesh_panel"] = m_meshPanel->getState();
    if (!caseDir.isEmpty())
        root["physics"] = PhysicsPanel::snapshotFromCase(caseDir);
    f.write(QJsonDocument(root).toJson());
    showToast("Autosaved");
}

void MainWindow::toggleFocusMode()
{
    m_focusMode = !m_focusMode;
    if (m_focusMode) {
        m_preFocusState = saveState();
        for (QDockWidget* d : {m_caseDock, m_propDock, m_dataDock, m_consoleDock, m_meshDock})
            if (d) d->hide();
        menuBar()->hide();
        statusBar()->hide();
        showToast("Focus mode: press F to exit");
    } else {
        menuBar()->show();
        statusBar()->show();
        restoreState(m_preFocusState);
        // The mode may have changed (Ctrl+1/Ctrl+2) while focus mode was on
        syncDocksToMode();
    }
}

void MainWindow::showToast(const QString& msg)
{
    if (!m_toast) return;
    m_toast->setText(msg);
    m_toast->adjustSize();
    // Centre it horizontally near the top of the window
    int x = (width() - m_toast->width()) / 2;
    m_toast->move(x, 8);
    m_toast->show();
    m_toast->raise();
    m_toastTimer->start(2000);
}

void MainWindow::showStatusWarning(const QString& msg, int msec)
{
    statusBar()->setStyleSheet("color: #ffffff;");
    statusBar()->showMessage(msg, msec);
    QTimer::singleShot(msec, this, [this](){
        statusBar()->setStyleSheet("");
    });
}

bool MainWindow::loadProject(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Open Failed", "Could not read:\n" + path);
        return false;
    }

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, "Invalid File",
            "Not a valid .cake project file:\n" + err.errorString());
        return false;
    }

    QJsonObject root = doc.object();
    if (root["app"].toString() != "CakeStudio") {
        QMessageBox::warning(this, "Wrong Format",
            "This file was not created by Cake Studio.");
        return false;
    }

    // Restore theme first so panels rebuild with correct colors
    QString themeStr = root["theme"].toString();
    Theme theme = (themeStr == "blueprint") ? Theme::Blueprint : Theme::Dark;
    ThemeEngine::instance().apply(theme, *qApp);
    if (m_viewport) m_viewport->setTheme(theme);
        if (m_meshViewport) m_meshViewport->setTheme(theme);
    if (m_console)  m_console->setTheme(theme);
    if (m_props)    m_props->onThemeChanged();
    if (m_sysPanel) m_sysPanel->onThemeChanged();
    QSettings("CakeCFD","CakeStudio").setValue("theme", themeStr);

    // Restore case
    QString caseDir = root["case_dir"].toString();
    if (!caseDir.isEmpty() && QDir(caseDir).exists()) {
        if (m_console)      m_console->setCaseDir(caseDir);
        if (m_caseTree)     m_caseTree->setCaseDir(caseDir);
        if (m_viewport)     m_viewport->setProjectDir(caseDir);
        if (m_meshPanel)    m_meshPanel->setProjectDir(caseDir);
        if (m_sysPanel)     m_sysPanel->setCaseDir(caseDir);
        if (m_datasetPanel) m_datasetPanel->setCaseDir(caseDir);

        // Re-apply saved physics snapshot so settings survive case file regeneration
        if (root.contains("physics"))
            PhysicsPanel::restoreToCase(caseDir, root["physics"].toObject());

        // Migrate stale libs placement (older projects put libtenoScheme.so in fvSchemes)
        PhysicsPanel::migrateTenolibsToControlDict(caseDir);
    }

    // Restore viewport: load results + geometry regardless of whether vp key exists
    if (m_viewport && !caseDir.isEmpty() && QDir(caseDir).exists()) {
        m_viewport->loadOFResults(caseDir);

        // Restore STL geometry: prefer saved path, fall back to constant/triSurface auto-detect
        QJsonObject vp = root["viewport"].toObject();
        QString stlPath = vp["stl_path"].toString();
        if (stlPath.isEmpty() || !QFile::exists(stlPath)) {
            QDir triSurf(caseDir + "/constant/triSurface");
            const auto stls = triSurf.entryInfoList({"*.stl"}, QDir::Files);
            if (!stls.isEmpty()) stlPath = stls.first().absoluteFilePath();
        }
        if (!stlPath.isEmpty() && QFile::exists(stlPath))
            m_viewport->loadSTL(stlPath);

        QString field = vp["active_field"].toString();
        if (!field.isEmpty()) m_viewport->setActiveField(field);
        m_viewport->setActiveTimeStep(vp["active_time_step"].toInt(0));
        if (vp.contains("camera"))
            m_viewport->setCameraState(vp["camera"].toObject());
        if (vp.contains("mesh_hidden"))
            m_viewport->setMeshHidden(vp["mesh_hidden"].toBool());
        if (vp.contains("stream_proximity"))
            m_viewport->setStreamProximity(vp["stream_proximity"].toDouble());
        if (vp.contains("stream_density"))
            m_viewport->setStreamDensity(vp["stream_density"].toInt());
        if (vp.contains("stream_tube_scale"))
            m_viewport->setStreamTubeScale(vp["stream_tube_scale"].toDouble());
    }

    // Restore domain preview box if it was saved
    if (root.contains("domain_preview")) {
        QJsonObject dp = root["domain_preview"].toObject();
        m_dpX0 = dp["x0"].toDouble(); m_dpY0 = dp["y0"].toDouble(); m_dpZ0 = dp["z0"].toDouble();
        m_dpX1 = dp["x1"].toDouble(); m_dpY1 = dp["y1"].toDouble(); m_dpZ1 = dp["z1"].toDouble();
        m_dpAxis = dp["axis"].toString();
        // Older .cake files may not have the axis field; fall back to QSettings
        // (written by DomainSetupDialog on Generate Files) so seeds land on the right face.
        if (m_dpAxis.isEmpty())
            m_dpAxis = QSettings().value("domainSetup/flowAxis").toString();
        m_hasDomainPreview = true;
        if (!m_dpAxis.isEmpty())
            QSettings().setValue("domainSetup/flowAxis", m_dpAxis);
        if (m_viewport)
            m_viewport->updateDomainPreview(m_dpX0,m_dpY0,m_dpZ0, m_dpX1,m_dpY1,m_dpZ1, m_dpAxis);
        if (m_meshViewport)
            m_meshViewport->updateDomainPreview(m_dpX0,m_dpY0,m_dpZ0, m_dpX1,m_dpY1,m_dpZ1, m_dpAxis);
    }

    // Restore mesh panel state: geometry reload triggers viewport update
    if (m_meshPanel && root.contains("mesh_panel"))
        m_meshPanel->setState(root["mesh_panel"].toObject());

    m_projectPath  = path;
    m_projectDirty = false;
    updateTitle();
    addRecentProject(path);
    statusBar()->showMessage("Opened: " + QFileInfo(path).fileName());
    startAutosave();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_projectDirty) {
        auto btn = QMessageBox::question(this, "Unsaved Changes",
            "You have unsaved changes.\nSave before closing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel) { event->ignore(); return; }
        if (btn == QMessageBox::Save)   onSaveProject();
    }
    event->accept();
}

void MainWindow::onNewProject()
{
    if (m_projectDirty) {
        auto btn = QMessageBox::question(this, "Unsaved Changes",
            "Save the current project before creating a new one?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel) return;
        if (btn == QMessageBox::Save)   onSaveProject();
    }

    // Ask for project name
    bool ok;
    QString name = QInputDialog::getText(this, "New Project", "Project name:",
                                          QLineEdit::Normal, "NewProject", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed().replace(' ', '_');

    // Ask where to put it
    QString location = FileBrowserDialog::getExistingDirectory(
        this, "Select project location",
        QSettings("CakeCFD","CakeStudio").value("lastProjectDir", QDir::homePath()).toString());
    if (location.isEmpty()) return;

    // Create project folder: <location>/<name>/
    QString projectDir = location + "/" + name;
    if (QDir(projectDir).exists()) {
        auto btn = QMessageBox::question(this, "Folder Exists",
            "Project folder already exists:\n" + projectDir + "\n\nUse it anyway?",
            QMessageBox::Yes | QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel) return;
    }
    if (!QDir().mkpath(projectDir)) {
        QMessageBox::warning(this, "Error", "Could not create project directory:\n" + projectDir);
        return;
    }

    // Clear existing scene state
    if (m_viewport)     m_viewport->clearScene();
    if (m_meshViewport) m_meshViewport->clearScene();
    if (m_meshPanel)    m_meshPanel->reset();
    m_hasDomainPreview = false;

    // Wire up the new project dir as the case dir
    if (m_console)      m_console->setCaseDir(projectDir);
    if (m_caseTree)     m_caseTree->setCaseDir(projectDir);
    if (m_viewport)     m_viewport->setProjectDir(projectDir);
    if (m_meshPanel)    m_meshPanel->setProjectDir(projectDir);
    if (m_sysPanel)     m_sysPanel->setCaseDir(projectDir);
    if (m_datasetPanel) m_datasetPanel->setCaseDir(projectDir);

    // Save the .cake file inside the project folder
    const QString cakePath = projectDir + "/" + name + ".cake";
    m_projectPath.clear();
    saveProject(cakePath);

    QSettings("CakeCFD","CakeStudio").setValue("lastProjectDir", location);

    // Relaunch into the new project rather than trying to scrub every panel, actor and
    // cached dataset in place. Too much state (VTK actors, cached meshes, panel widgets)
    // survives an in-place reset and leaks the previous project's geometry into the new
    // one. A fresh process is the only reliable guarantee of a clean slate.
    m_projectDirty = false;
    if (QProcess::startDetached(QApplication::applicationFilePath(), {cakePath})) {
        qApp->quit();
        return;
    }
    // Relaunch failed: carry on in-process rather than losing the new project.
    statusBar()->showMessage("Project created: " + projectDir);
}

void MainWindow::onOpenProject()
{
    if (m_projectDirty) {
        auto btn = QMessageBox::question(this, "Unsaved Changes",
            "Save the current project before opening another?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel) return;
        if (btn == QMessageBox::Save)   onSaveProject();
    }

    QString path = FileBrowserDialog::getOpenFileName(
        this, "Open Cake Project",
        QSettings("CakeCFD","CakeStudio").value("lastProjectDir", QDir::homePath()).toString(),
        "Cake Project Files (*.cake);;All Files (*)");
    if (path.isEmpty()) return;

    QSettings("CakeCFD","CakeStudio").setValue("lastProjectDir", QFileInfo(path).absolutePath());
    loadProject(path);
}

void MainWindow::onSaveProject()
{
    if (m_projectPath.isEmpty()) { onSaveProjectAs(); return; }
    saveProject(m_projectPath);
}

void MainWindow::onSaveProjectAs()
{
    // Default to case directory with case name as filename, fall back to last used dir
    QString caseDir = m_console ? m_console->caseDir() : QString();
    QString defaultPath;
    if (!caseDir.isEmpty()) {
        QString caseName = QFileInfo(caseDir).fileName();
        defaultPath = caseDir + "/" + caseName + ".cake";
    } else {
        defaultPath = QSettings("CakeCFD","CakeStudio").value("lastProjectDir", QDir::homePath()).toString()
                    + "/untitled.cake";
    }

    QString path = FileBrowserDialog::getSaveFileName(
        this, "Save Cake Project As", defaultPath,
        "Cake Project Files (*.cake);;All Files (*)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".cake", Qt::CaseInsensitive)) path += ".cake";
    QSettings("CakeCFD","CakeStudio").setValue("lastProjectDir", QFileInfo(path).absolutePath());
    saveProject(path);
}
