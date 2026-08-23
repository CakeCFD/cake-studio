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

    QString m_caseDir;

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

    QLabel*         m_statusLbl;
};
