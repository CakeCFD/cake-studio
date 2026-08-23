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
#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QTextEdit>
#include <QPushButton>
#include <QGroupBox>
#include <memory>

namespace CFD { class CADGeometry; }

class MeshWizard : public QDialog {
    Q_OBJECT
public:
    explicit MeshWizard(QWidget* parent = nullptr, const QString& projectDir = {});

private slots:
    void onBrowseGeometry();
    void onLoadGeometry();
    void onBrowseOutputDir();
    void onGenerateCase();
    void onRunMesh();
    void onRunSolver();

private:
    void buildGeometryTab(QTabWidget*);
    void buildMeshTab(QTabWidget*);
    void buildPhysicsTab(QTabWidget*);
    void buildBottomBar();
    void log(const QString& msg);
    void runCommand(const QString& cmd);
    void setRunning(bool);

    // Geometry tab
    QLineEdit*   m_geoPath    = nullptr;
    QLabel*      m_geoStats   = nullptr;

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

    // Physics tab
    QComboBox*      m_solver      = nullptr;
    QComboBox*      m_turbulence  = nullptr;
    QDoubleSpinBox* m_velocity    = nullptr;
    QDoubleSpinBox* m_nu          = nullptr;
    QLineEdit*      m_caseName    = nullptr;
    QLineEdit*      m_outputDir   = nullptr;
    QSpinBox*       m_cores       = nullptr;

    // Bottom
    QPushButton* m_btnGenerate   = nullptr;
    QPushButton* m_btnRunMesh    = nullptr;
    QPushButton* m_btnRunSolver  = nullptr;
    QLabel*      m_status        = nullptr;
    QTextEdit*   m_log           = nullptr;

    // State
    std::shared_ptr<CFD::CADGeometry> m_geo;
    QString m_caseDir;
};
