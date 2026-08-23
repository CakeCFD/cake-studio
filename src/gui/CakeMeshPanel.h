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
#include <QWidget>
#include <QJsonObject>
#include <QTabWidget>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QProcess>
#include <functional>
#include <memory>

namespace CFD { class CADGeometry; }
class ViewportWidget;

class CakeMeshPanel : public QWidget {
    Q_OBJECT
public:
    explicit CakeMeshPanel(QWidget* parent = nullptr);
    void setViewport(ViewportWidget* vp);
    void setProjectDir(const QString& dir);
    void onThemeChanged();

    QJsonObject getState() const;
    void        setState(const QJsonObject& obj);
    void        reset();   // clear all fields: call before wiring a new project

signals:
    // Emitted only after mesh is complete: ready for Studio to pick up
    void exportRequested(const QString& caseDir);

private slots:
    void onBrowseGeometry();
    void onLoadGeometry();
    void onBrowseOutputDir();
    void onGenerateCase();
    void onRunMesh();
    void onExportToStudio();
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void buildGeometryTab(QTabWidget*);
    void buildMeshTab(QTabWidget*);
    void buildPhysicsTab(QTabWidget*);
    void buildBCTable();                 // populates m_bcTable with domain + geometry rows
    void log(const QString& msg);
    void runQueue(QStringList cmds, std::function<void(bool)> onDone);
    void runNextInQueue();
    void setRunning(bool);
    QString ofEnvPrefix() const;

    // Geometry tab
    QLineEdit*   m_geoPath  = nullptr;
    QLabel*      m_geoStats = nullptr;

    // Mesh tab
    QDoubleSpinBox* m_padding         = nullptr;
    QSpinBox*       m_bgCells         = nullptr;
    QSpinBox*       m_maxGlobal       = nullptr;
    QSpinBox*       m_maxLocal        = nullptr;
    QSpinBox*       m_refMin          = nullptr;
    QSpinBox*       m_refMax          = nullptr;
    QSpinBox*       m_featureAngle    = nullptr;
    QSpinBox*       m_nLayers         = nullptr;
    QDoubleSpinBox* m_layerExpansion  = nullptr;
    QDoubleSpinBox* m_firstLayerThick = nullptr;
    QDoubleSpinBox* m_minThickness    = nullptr;

    // Physics / BCs tab
    QComboBox*      m_turbulence = nullptr;
    QDoubleSpinBox* m_velocity   = nullptr;  // default inlet velocity (seeds BC table)
    QDoubleSpinBox* m_aoa        = nullptr;  // angle of attack, degrees
    QDoubleSpinBox* m_nu         = nullptr;
    QTableWidget*   m_bcTable    = nullptr;  // per-patch BC table (ANSYS-style)
    QLineEdit*      m_caseName   = nullptr;
    QLineEdit*      m_outputDir  = nullptr;
    QSpinBox*       m_cores      = nullptr;

    // Action bar
    QPushButton* m_btnGenerate = nullptr;
    QPushButton* m_btnRunMesh  = nullptr;
    QPushButton* m_btnExport   = nullptr;  // enabled only after mesh is done
    QLabel*      m_status      = nullptr;
    QTextEdit*   m_log         = nullptr;

    // State
    std::shared_ptr<CFD::CADGeometry> m_geo;
    QString         m_caseDir;
    ViewportWidget* m_viewport = nullptr;

    // Async command queue
    QProcess*                  m_process     = nullptr;
    QStringList                m_cmdQueue;
    bool                       m_queueFailed = false;
    std::function<void(bool)>  m_onQueueDone;
};
