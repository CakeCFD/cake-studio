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

#include "ThermalPropertiesDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QFile>
#include <QDir>
#include <QLabel>
#include <QGroupBox>

struct FluidPreset {
    double molWeight, cp, hf, mu, pr;
};

static const QMap<QString, FluidPreset> PRESETS = {
    { "Air",          { 28.9,  1005, 0, 1.8e-5,  0.713 } },
    { "Water vapour", { 18.0,  2080, 0, 1.26e-5, 0.9   } },
    { "CO2",          { 44.01, 846,  0, 1.48e-5, 0.766 } },
    { "Custom",       { 28.9,  1005, 0, 1.8e-5,  0.713 } },
};

static QDoubleSpinBox* makeSpin(double lo, double hi, double val, int dec, const QString& suffix = {})
{
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(dec);
    s->setValue(val);
    s->setSingleStep(0);
    if (!suffix.isEmpty()) s->setSuffix("  " + suffix);
    return s;
}

ThermalPropertiesDialog::ThermalPropertiesDialog(const QString& caseDir, QWidget* parent)
    : QDialog(parent), m_caseDir(caseDir)
{
    setWindowTitle("Thermal Properties: " + QDir(caseDir).dirName());
    setMinimumWidth(460);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

    // Solver type
    auto* typeBox = new QGroupBox("Solver / EOS");
    auto* typeForm = new QFormLayout(typeBox);

    m_preset = new QComboBox;
    m_preset->addItems(PRESETS.keys());
    typeForm->addRow("Fluid preset:", m_preset);

    m_thermoType = new QComboBox;
    m_thermoType->addItems({"hePsiThermo", "heRhoThermo"});
    m_thermoType->setToolTip(
        "hePsiThermo: compressible ideal gas (simpleFoam / buoyantSimpleFoam)\n"
        "heRhoThermo: density-based (rhoCentralFoam)");
    typeForm->addRow("Thermo type:", m_thermoType);

    m_eos = new QComboBox;
    m_eos->addItems({"perfectGas", "incompressiblePerfectGas", "rhoConst"});
    m_eos->setToolTip(
        "perfectGas: rho = p / (R T) : standard compressible\n"
        "incompressiblePerfectGas: rho from T only : buoyant flows\n"
        "rhoConst: fixed density : liquid approximation");
    typeForm->addRow("Equation of state:", m_eos);

    m_transport = new QComboBox;
    m_transport->addItems({"const", "sutherland"});
    m_transport->setToolTip(
        "const: fixed mu and Pr\n"
        "sutherland: mu(T) = As*sqrt(T)/(1+Ts/T) : accurate for gases");
    typeForm->addRow("Transport:", m_transport);

    m_thermo = new QComboBox;
    m_thermo->addItems({"hConst", "janaf"});
    m_thermo->setToolTip(
        "hConst: constant Cp: suitable for most engineering cases\n"
        "janaf: polynomial Cp(T): high-accuracy combustion / wide T range");
    typeForm->addRow("Thermodynamics:", m_thermo);

    root->addWidget(typeBox);

    // Mixture properties
    auto* mixBox = new QGroupBox("Mixture properties");
    auto* mixForm = new QFormLayout(mixBox);

    m_molWeight = makeSpin(1, 300, 28.9, 3, "g/mol");
    m_cp        = makeSpin(1, 1e5, 1005, 1, "J/kg/K");
    m_hf        = makeSpin(-1e8, 1e8, 0,  1, "J/kg");
    m_mu        = makeSpin(1e-9, 1,   1.8e-5, 8, "Pa·s");
    m_pr        = makeSpin(0.01, 100, 0.713,  4);

    mixForm->addRow("Molecular weight:", m_molWeight);
    mixForm->addRow("Cp:", m_cp);
    mixForm->addRow("Hf (heat of formation):", m_hf);
    mixForm->addRow("Dynamic viscosity (mu):", m_mu);
    mixForm->addRow("Prandtl number:", m_pr);

    root->addWidget(mixBox);

    // Sutherland extras (hidden by default)
    m_suthBox = new QGroupBox("Sutherland coefficients");
    auto* suthForm = new QFormLayout(m_suthBox);
    m_as = makeSpin(1e-8, 1, 1.458e-6, 10);
    m_ts = makeSpin(0, 5000, 110.4, 2, "K");
    suthForm->addRow("As:", m_as);
    suthForm->addRow("Ts:", m_ts);
    m_suthBox->hide();
    root->addWidget(m_suthBox);

    // Status + buttons
    m_statusLbl = new QLabel;
    m_statusLbl->setStyleSheet("font-size:11px;");
    root->addWidget(m_statusLbl);

    auto* btns = new QHBoxLayout;
    btns->addStretch();
    auto* btnCancel = new QPushButton("Cancel");
    auto* btnApply  = new QPushButton("Apply");
    btnApply->setDefault(true);
    btns->addWidget(btnCancel);
    btns->addWidget(btnApply);
    root->addLayout(btns);

    connect(m_preset,    &QComboBox::currentTextChanged, this, &ThermalPropertiesDialog::onPresetChanged);
    connect(m_transport, &QComboBox::currentTextChanged, this, &ThermalPropertiesDialog::onTransportChanged);
    connect(btnCancel,   &QPushButton::clicked,          this, &QDialog::reject);
    connect(btnApply,    &QPushButton::clicked,          this, &ThermalPropertiesDialog::onApply);

    onPresetChanged(m_preset->currentText());
}

void ThermalPropertiesDialog::onPresetChanged(const QString& preset)
{
    if (!PRESETS.contains(preset)) return;
    const FluidPreset& p = PRESETS[preset];
    m_molWeight->setValue(p.molWeight);
    m_cp->setValue(p.cp);
    m_hf->setValue(p.hf);
    m_mu->setValue(p.mu);
    m_pr->setValue(p.pr);

    bool editable = (preset == "Custom");
    m_molWeight->setEnabled(editable);
    m_cp->setEnabled(editable);
    m_hf->setEnabled(editable);
    m_mu->setEnabled(editable);
    m_pr->setEnabled(editable);
}

void ThermalPropertiesDialog::onTransportChanged(const QString& transport)
{
    m_suthBox->setVisible(transport == "sutherland");
    adjustSize();
}

void ThermalPropertiesDialog::onApply()
{
    QDir().mkpath(m_caseDir + "/constant");

    QString transport = m_transport->currentText();

    QString transportBlock;
    if (transport == "sutherland") {
        transportBlock = QString(
            "    transport\n    {\n"
            "        As          %1;\n"
            "        Ts          %2;\n"
            "    }\n")
            .arg(m_as->value(), 0, 'g', 8)
            .arg(m_ts->value(), 0, 'f', 2);
    } else {
        transportBlock = QString(
            "    transport\n    {\n"
            "        mu          %1;\n"
            "        Pr          %2;\n"
            "    }\n")
            .arg(m_mu->value(), 0, 'g', 8)
            .arg(m_pr->value(), 0, 'f', 6);
    }

    QString content =
        "FoamFile\n{\n"
        "    version     2.0;\n"
        "    format      ascii;\n"
        "    class       dictionary;\n"
        "    object      thermophysicalProperties;\n"
        "}\n\n"
        "thermoType\n{\n"
        + QString("    type            %1;\n").arg(m_thermoType->currentText())
        + "    mixture         pureMixture;\n"
        + QString("    transport       %1;\n").arg(transport)
        + QString("    thermo          %1;\n").arg(m_thermo->currentText())
        + QString("    equationOfState %1;\n").arg(m_eos->currentText())
        + "    specie          specie;\n"
        + "    energy          sensibleEnthalpy;\n"
        + "}\n\n"
        + "mixture\n{\n"
        + "    specie\n    {\n"
        + "        nMoles      1;\n"
        + QString("        molWeight   %1;\n").arg(m_molWeight->value(), 0, 'f', 3)
        + "    }\n"
        + "    thermodynamics\n    {\n"
        + QString("        Cp          %1;\n").arg(m_cp->value(), 0, 'f', 2)
        + QString("        Hf          %1;\n").arg(m_hf->value(), 0, 'f', 1)
        + "    }\n"
        + transportBlock
        + "}\n";

    QFile f(m_caseDir + "/constant/thermophysicalProperties");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_statusLbl->setText("Write failed.");
        return;
    }
    f.write(content.toUtf8());
    m_statusLbl->setText("Written: constant/thermophysicalProperties");
    accept();
}
