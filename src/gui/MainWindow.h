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
class QActionGroup;

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
    void syncDocksToMode();

    bool saveProject(const QString& path);
    void updateTitle();
    void markDirty();
    void applyTheme(Theme theme);
    QActionGroup* m_themeGroup = nullptr;
    void addRecentProject(const QString& path);
    void rebuildRecentMenu();

    ConsolePanel*    m_console       = nullptr;
    DatasetPanel*    m_datasetPanel  = nullptr;
    ViewportWidget*  m_viewport      = nullptr;
    ViewportWidget*  m_meshViewport  = nullptr;
    QStackedWidget*  m_viewportStack = nullptr;
    CaseTreePanel*   m_caseTree      = nullptr;
    PropertiesPanel* m_props         = nullptr;
    CakeMeshPanel*      m_meshPanel     = nullptr;
    SystemConfigPanel*  m_sysPanel      = nullptr;

    QDockWidget* m_caseDock    = nullptr;
    QDockWidget* m_propDock    = nullptr;
    QDockWidget* m_dataDock    = nullptr;
    QDockWidget* m_consoleDock = nullptr;
    QDockWidget* m_meshDock    = nullptr;

    QPushButton*    m_btnStudio          = nullptr;
    QPushButton*    m_btnMesh            = nullptr;
    AppMode         m_mode               = AppMode::Studio;
    QList<QAction*> m_studioToolbarItems;

    QByteArray m_defaultState;
    QByteArray m_defaultGeometry;

    void showToast(const QString& msg);
    void showStatusWarning(const QString& msg, int msec = 4000);

    QString m_projectPath;
    bool    m_projectDirty = false;
    QMenu*  m_recentMenu   = nullptr;

    double  m_dpX0 = 0, m_dpY0 = 0, m_dpZ0 = 0;
    double  m_dpX1 = 0, m_dpY1 = 0, m_dpZ1 = 0;
    QString m_dpAxis;
    bool    m_hasDomainPreview = false;

    QLabel* m_toast      = nullptr;
    QTimer* m_toastTimer = nullptr;

    QTimer* m_autosaveTimer = nullptr;
    void    startAutosave();
    void    onAutosave();

    bool       m_focusMode      = false;
    QByteArray m_preFocusState;
    void       toggleFocusMode();
};
