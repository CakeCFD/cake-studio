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
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>

class ThermalPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThermalPropertiesDialog(const QString& caseDir, QWidget* parent = nullptr);

private slots:
    void onPresetChanged(const QString& preset);
    void onTransportChanged(const QString& transport);
    void onApply();

private:
    void buildFile();
    void loadExisting();   // populate widgets from constant/thermophysicalProperties

    QString m_caseDir;
    QString m_energy = "sensibleEnthalpy";   // kept from the existing file

    // janaf coefficients, kept verbatim from the existing file (air defaults otherwise)
    QString m_janafTlow, m_janafThigh, m_janafTcommon, m_janafHigh, m_janafLow;

    QComboBox*      m_preset;
    QComboBox*      m_thermoType;
    QComboBox*      m_eos;
    QComboBox*      m_transport;
    QComboBox*      m_thermo;

    QDoubleSpinBox* m_molWeight;
    QDoubleSpinBox* m_cp;
    QDoubleSpinBox* m_hf;
    QDoubleSpinBox* m_mu;
    QDoubleSpinBox* m_pr;

    // Sutherland extras
    QGroupBox*      m_suthBox;
    QDoubleSpinBox* m_as;
    QDoubleSpinBox* m_ts;

    // Equation-of-state extras (rho for rhoConst, pRef for incompressiblePerfectGas)
    QGroupBox*      m_eosBox;
    QDoubleSpinBox* m_rho;
    QDoubleSpinBox* m_pRef;

    QLabel*         m_statusLbl;
};
