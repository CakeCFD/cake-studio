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
    void        reset();

signals:
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
    void buildBCTable();
    void log(const QString& msg);
    void runQueue(QStringList cmds, std::function<void(bool)> onDone);
    void runNextInQueue();
    void setRunning(bool);
    QString ofEnvPrefix() const;

    QLineEdit*   m_geoPath  = nullptr;
    QLabel*      m_geoStats = nullptr;

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

    QComboBox*      m_turbulence = nullptr;
    QDoubleSpinBox* m_velocity   = nullptr;
    QDoubleSpinBox* m_aoa        = nullptr;
    QDoubleSpinBox* m_nu         = nullptr;
    QTableWidget*   m_bcTable    = nullptr;
    QLineEdit*      m_caseName   = nullptr;
    QLineEdit*      m_outputDir  = nullptr;
    QSpinBox*       m_cores      = nullptr;

    QPushButton* m_btnGenerate = nullptr;
    QPushButton* m_btnRunMesh  = nullptr;
    QPushButton* m_btnExport   = nullptr;
    QLabel*      m_status      = nullptr;
    QTextEdit*   m_log         = nullptr;

    std::shared_ptr<CFD::CADGeometry> m_geo;
    QString         m_caseDir;
    ViewportWidget* m_viewport = nullptr;

    QProcess*                  m_process     = nullptr;
    QStringList                m_cmdQueue;
    bool                       m_queueFailed = false;
    std::function<void(bool)>  m_onQueueDone;
};
