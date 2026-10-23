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
#include <QMainWindow>
#include <QDockWidget>
#include <QByteArray>
#include <QMenu>
#include <QPushButton>
#include <QAction>
#include <QList>
#include <QStackedWidget>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>
#include "themes/ThemeEngine.h"

class ConsolePanel;
class DatasetPanel;
class ViewportWidget;
class CaseTreePanel;
class PropertiesPanel;
class CakeMeshPanel;
class SystemConfigPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    enum class AppMode { Studio, Mesh };

    // Public so main() can open a .cake passed on the command line (New Project
    // relaunches the app with the new project's path).
    bool loadProject(const QString& path);

private slots:
    void onSwitchTheme();
    void onOpenCase();
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void applyWorkspaceDefault();
    void applyWorkspaceMesh();
    void applyWorkspacePostProcess();
    void applyWorkspaceWide();

private:
    void closeEvent(QCloseEvent* event) override;

    void setupUI();
    void setupMenuBar();
    void setupDockPanels();
    void setupToolBar();
    void saveWorkspace();
    void restoreWorkspace();
    void onDockTopLevelChanged(QDockWidget* dock, bool floating);
    void setMode(AppMode mode);
    void syncDocksToMode();   // hide docks that belong to the other mode after restoreState

    // Project (.cake) file management
    bool saveProject(const QString& path);
    void updateTitle();
    void markDirty();
    void addRecentProject(const QString& path);
    void rebuildRecentMenu();

    ConsolePanel*    m_console       = nullptr;
    DatasetPanel*    m_datasetPanel  = nullptr;
    ViewportWidget*  m_viewport      = nullptr;  // Studio viewport
    ViewportWidget*  m_meshViewport  = nullptr;  // Mesh viewport
    QStackedWidget*  m_viewportStack = nullptr;
    CaseTreePanel*   m_caseTree      = nullptr;
    PropertiesPanel* m_props         = nullptr;
    CakeMeshPanel*      m_meshPanel     = nullptr;
    SystemConfigPanel*  m_sysPanel      = nullptr;

    // Keep dock pointers so we can toggle, rearrange, and save state
    QDockWidget* m_caseDock    = nullptr;
    QDockWidget* m_propDock    = nullptr;
    QDockWidget* m_dataDock    = nullptr;
    QDockWidget* m_consoleDock = nullptr;
    QDockWidget* m_meshDock    = nullptr;

    // Mode toggle buttons in toolbar
    QPushButton*    m_btnStudio          = nullptr;
    QPushButton*    m_btnMesh            = nullptr;
    AppMode         m_mode               = AppMode::Studio;
    QList<QAction*> m_studioToolbarItems; // shown only in Studio mode

    // Factory-default layout captured at startup: restored by "Default" preset
    QByteArray m_defaultState;
    QByteArray m_defaultGeometry;

    void showToast(const QString& msg);
    void showStatusWarning(const QString& msg, int msec = 4000);

    // Project state
    QString m_projectPath;
    bool    m_projectDirty = false;
    QMenu*  m_recentMenu   = nullptr;

    // Last domain preview params: saved/restored with the project
    double  m_dpX0 = 0, m_dpY0 = 0, m_dpZ0 = 0;
    double  m_dpX1 = 0, m_dpY1 = 0, m_dpZ1 = 0;
    QString m_dpAxis;
    bool    m_hasDomainPreview = false;

    // Toast overlay
    QLabel* m_toast      = nullptr;
    QTimer* m_toastTimer = nullptr;

    // Autosave: fires every 3 minutes when a project path is set
    QTimer* m_autosaveTimer = nullptr;
    void    startAutosave();
    void    onAutosave();

    // Viewport focus mode: F key hides all docks
    bool       m_focusMode      = false;
    QByteArray m_preFocusState;
    void       toggleFocusMode();
};
