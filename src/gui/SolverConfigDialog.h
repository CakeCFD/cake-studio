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
#include <QVBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QStackedWidget>
#include <QTabWidget>

class SolverConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit SolverConfigDialog(const QString& solver,
                                const QString& caseDir,
                                QWidget* parent = nullptr);

private slots:
    void onPresetChanged(const QString& preset);
    void onTransportChanged(const QString& transport);
    void onApply();
    void updateMachDisplay();

private:
    struct PatchInfo { QString name; QString type; };

    void buildIncompressibleGroup(QVBoxLayout* vb);
    void buildCompressibleGroup(QVBoxLayout* vb);
    void buildSolverSettingsGroup(QVBoxLayout* vb);

    void writeAll();
    void writeTransportProperties();
    void writeThermophysicalProperties();
    void writeFvSchemes();
    void writeFvSolution();
    void writeZeroFields(const QList<PatchInfo>& patches);
    void writeGravity();
    void writeTurbulenceModel();
    void patchControlDictTransient();
    void patchControlDictLibs();
    void writeRadiationProperties();

    QList<PatchInfo> parseBoundary() const;
    template<typename BC>
    void writeBoundaryField(QTextStream& o, const QList<PatchInfo>& patches, BC patchBC);

    QString m_solver;
    QString m_caseDir;

    bool m_isCompressible;
    bool m_isBuoyant;
    bool m_isCentral;
    bool m_isTransient;
    bool m_isLaminar;  // icoFoam

    // Incompressible
    QDoubleSpinBox* m_nu            = nullptr;

    // Compressible fluid
    QComboBox*      m_preset        = nullptr;
    QComboBox*      m_thermoType    = nullptr;
    QComboBox*      m_eos           = nullptr;
    QComboBox*      m_transport     = nullptr;
    QComboBox*      m_thermo        = nullptr;
    QDoubleSpinBox* m_molWeight     = nullptr;
    QDoubleSpinBox* m_cp            = nullptr;
    QDoubleSpinBox* m_hf            = nullptr;
    QDoubleSpinBox* m_mu            = nullptr;
    QDoubleSpinBox* m_pr            = nullptr;
    QDoubleSpinBox* m_refPressure   = nullptr;
    QDoubleSpinBox* m_refTemp       = nullptr;
    QGroupBox*      m_suthBox       = nullptr;
    QDoubleSpinBox* m_as            = nullptr;
    QDoubleSpinBox* m_ts            = nullptr;

    // Buoyant
    QComboBox*      m_gravityDir    = nullptr;

    // Flow conditions
    QDoubleSpinBox* m_inletVel      = nullptr;
    QComboBox*      m_divScheme     = nullptr;
    QComboBox*      m_simType       = nullptr;  // RANS / LES
    QComboBox*      m_sgsModel      = nullptr;  // SGS model (LES only)

    // Solver settings
    QSpinBox*       m_nCorrectors   = nullptr;
    QSpinBox*       m_nNonOrth      = nullptr;
    QDoubleSpinBox* m_relaxP        = nullptr;
    QDoubleSpinBox* m_relaxU        = nullptr;

    QLabel*         m_statusLbl     = nullptr;
    QLabel*         m_machLbl       = nullptr;
};
