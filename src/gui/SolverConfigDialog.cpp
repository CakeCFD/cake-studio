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

#include "SolverConfigDialog.h"
#include "themes/ThemeEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <cmath>

struct FluidPreset { double molWeight, cp, hf, mu, pr; };
static const QMap<QString, FluidPreset> FLUID_PRESETS = {
    { "Air",          { 28.97, 1005, 0, 1.8e-5,  0.713 } },
    { "Water vapour", { 18.0,  2080, 0, 1.26e-5, 0.9   } },
    { "CO2",          { 44.01, 846,  0, 1.48e-5, 0.766 } },
    { "Custom",       { 28.97, 1005, 0, 1.8e-5,  0.713 } },
};

static QDoubleSpinBox* dblSpin(double lo, double hi, double val, int dec,
                                const QString& suf = {})
{
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi); s->setDecimals(dec); s->setValue(val);
    s->setSingleStep(0);
    if (!suf.isEmpty()) s->setSuffix("  " + suf);
    return s;
}

SolverConfigDialog::SolverConfigDialog(const QString& solver,
                                       const QString& caseDir,
                                       QWidget* parent)
    : QDialog(parent), m_solver(solver), m_caseDir(caseDir)
{
    m_isCompressible = (solver == "rhoSimpleFoam"  || solver == "rhoPimpleFoam"
                     || solver == "rhoCentralFoam"
                     || solver == "buoyantSimpleFoam" || solver == "buoyantPimpleFoam");
    m_isBuoyant  = solver.startsWith("buoyant");
    m_isCentral  = (solver == "rhoCentralFoam");
    m_isTransient = (solver == "pimpleFoam" || solver == "pisoFoam"
                  || solver == "rhoPimpleFoam" || solver == "buoyantPimpleFoam"
                  || solver == "icoFoam");
    m_isLaminar  = (solver == "icoFoam");

    setWindowTitle("Solver Config: " + solver);
    setMinimumWidth(480);

    auto* root   = new QVBoxLayout(this);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* inner  = new QWidget;
    auto* vbox   = new QVBoxLayout(inner);
    vbox->setSpacing(8);

    {
        auto* grp  = new QGroupBox("Flow conditions");
        auto* form = new QFormLayout(grp);

        m_inletVel = dblSpin(0, 1e5, 30.0, 2, "m/s");
        form->addRow("Inlet velocity:", m_inletVel);
        if (m_isCompressible) {
            m_machLbl = new QLabel("M ≈ -");
            m_machLbl->setStyleSheet("color: gray; font-style: italic;");
            form->addRow("", m_machLbl);
        }

        m_divScheme = new QComboBox;
        m_divScheme->addItem("linearUpwind",  QString("linearUpwind"));
        m_divScheme->addItem("TENO5",         QString("teno"));
        m_divScheme->addItem("TENO6",         QString("teno6"));
        if (m_isCentral) {
            m_divScheme->setEnabled(false);
            m_divScheme->setCurrentIndex(0);
            m_divScheme->setToolTip("rhoCentralFoam uses density-based flux reconstruction "
                                    "(reconstruct(U) vanLeerV), not div(phi,U).\n"
                                    "Div scheme selection has no effect for this solver.");
        } else if (m_isBuoyant) {
            m_divScheme->setEnabled(false);
            m_divScheme->setCurrentIndex(0);
            m_divScheme->setToolTip("buoyantSimpleFoam/buoyantPimpleFoam use linearUpwind "
                                    "hardcoded for stability with the energy equation.");
        } else {
            m_divScheme->setToolTip("Convection scheme for div(phi,U).\n"
                                    "TENO5/6 are CakeCFD adaptive-stencil schemes.\n"
                                    "Measured order is 2 on smooth advection;\n"
                                    "see validation case VC-001.");
        }
        form->addRow("Div scheme:", m_divScheme);

        const bool isSteadyBuoyant = (m_solver == "buoyantSimpleFoam");
        if (!m_isLaminar && !isSteadyBuoyant) {
            m_simType = new QComboBox;
            m_simType->addItem("RANS", QString("RAS"));
            m_simType->addItem("LES",  QString("LES"));
            form->addRow("Turbulence:", m_simType);

            m_sgsModel = new QComboBox;
            m_sgsModel->addItem("Smagorinsky",   QString("Smagorinsky"));
            m_sgsModel->addItem("WALE",           QString("WALE"));
            m_sgsModel->addItem("dynamicKEqn",    QString("dynamicKEqn"));
            m_sgsModel->addItem("kEqn",           QString("kEqn"));
            form->addRow("SGS model:", m_sgsModel);
            m_sgsModel->setVisible(false);
            if (auto* lbl = form->labelForField(m_sgsModel)) lbl->setVisible(false);

            m_rasModel = new QComboBox;
            m_rasModel->addItem("kOmegaSST",       QString("kOmegaSST"));
            m_rasModel->addItem("SpalartAllmaras", QString("SpalartAllmaras"));
            m_rasModel->addItem("kEpsilon",        QString("kEpsilon"));
            form->addRow("RAS model:", m_rasModel);

            connect(m_simType, &QComboBox::currentTextChanged, this, [this, form](const QString& t){
                const bool les = (t == "LES");
                m_sgsModel->setVisible(les);
                if (auto* lbl = form->labelForField(m_sgsModel)) lbl->setVisible(les);
                m_rasModel->setVisible(!les);
                if (auto* lbl = form->labelForField(m_rasModel)) lbl->setVisible(!les);
            });
        }

        if (m_simType) {
            for (const QString& name : { QString("turbulenceProperties"), QString("momentumTransport") }) {
                QFile tf(m_caseDir + "/constant/" + name);
                if (!tf.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
                QString txt = tf.readAll();
                QRegularExpression re(R"(\b(?:RAS|LES)?[Mm]odel\s+(\w+)\s*;)");
                auto mm = re.match(txt);
                const QString cur = mm.hasMatch() ? mm.captured(1) : QString();
                if (txt.contains(QRegularExpression(R"(simulationType\s+LES\s*;)"))) {
                    m_simType->setCurrentIndex(m_simType->findData(QString("LES")));
                    if (!cur.isEmpty()) {
                        int idx = m_sgsModel->findData(cur);
                        if (idx < 0) { m_sgsModel->addItem(cur, cur); idx = m_sgsModel->count() - 1; }
                        m_sgsModel->setCurrentIndex(idx);
                    }
                } else if (txt.contains(QRegularExpression(R"(simulationType\s+RAS\s*;)"))
                           && !cur.isEmpty()) {
                    int idx = m_rasModel->findData(cur);
                    if (idx < 0) { m_rasModel->addItem(cur, cur); idx = m_rasModel->count() - 1; }
                    m_rasModel->setCurrentIndex(idx);
                }
                break;
            }
        }

        {
            QFile fvf(m_caseDir + "/system/fvSchemes");
            if (fvf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString txt = fvf.readAll();
                QRegularExpression re(R"(div\(phi,U\)\s+Gauss\s+(\w+))");
                auto mm = re.match(txt);
                if (mm.hasMatch()) {
                    QString cur = mm.captured(1);
                    if (cur == "linearUpwindV") cur = "linearUpwind";
                    int idx = m_divScheme->findData(cur);
                    if (idx >= 0) m_divScheme->setCurrentIndex(idx);
                }
            }
        }

        vbox->addWidget(grp);
    }

    if (!m_isCompressible) {
        buildIncompressibleGroup(vbox);
    } else {
        buildCompressibleGroup(vbox);
    }

    buildSolverSettingsGroup(vbox);

    m_statusLbl = new QLabel;
    m_statusLbl->setStyleSheet("font-size:11px;");
    vbox->addWidget(m_statusLbl);
    vbox->addStretch(1);

    scroll->setWidget(inner);
    root->addWidget(scroll);

    auto* btns   = new QHBoxLayout;
    btns->addStretch();
    auto* btnCancel   = new QPushButton("Cancel");
    auto* btnApply    = new QPushButton("Write");
    auto* btnWriteOk  = new QPushButton("Write & Close");
    btnWriteOk->setDefault(true);
    btns->addWidget(btnCancel);
    btns->addWidget(btnApply);
    btns->addWidget(btnWriteOk);
    root->addLayout(btns);

    connect(btnCancel,  &QPushButton::clicked, this, &QDialog::reject);
    connect(btnApply,   &QPushButton::clicked, this, &SolverConfigDialog::onApply);
    connect(btnWriteOk, &QPushButton::clicked, this, [this](){
        onApply();
        accept();
    });

    if (m_preset) onPresetChanged(m_preset->currentText());

    if (m_machLbl) {
        connect(m_inletVel, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SolverConfigDialog::updateMachDisplay);
        if (m_refTemp)    connect(m_refTemp,    QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SolverConfigDialog::updateMachDisplay);
        if (m_cp)         connect(m_cp,         QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SolverConfigDialog::updateMachDisplay);
        if (m_molWeight)  connect(m_molWeight,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SolverConfigDialog::updateMachDisplay);
        updateMachDisplay();
    }
}

void SolverConfigDialog::buildIncompressibleGroup(QVBoxLayout* vb)
{
    auto* grp  = new QGroupBox("Fluid properties");
    auto* form = new QFormLayout(grp);
    m_nu = dblSpin(1e-9, 1, 1.5e-5, 8, "m²/s");
    form->addRow("Kinematic viscosity (nu):", m_nu);
    vb->addWidget(grp);
}

void SolverConfigDialog::buildCompressibleGroup(QVBoxLayout* vb)
{

    {
        auto* grp  = new QGroupBox("Fluid properties");
        auto* form = new QFormLayout(grp);

        m_preset = new QComboBox;
        m_preset->addItems(FLUID_PRESETS.keys());
        form->addRow("Fluid preset:", m_preset);

        m_thermoType = new QComboBox;
        m_thermoType->addItems({"hePsiThermo", "heRhoThermo"});
        m_thermoType->setCurrentText(m_isBuoyant ? "heRhoThermo" : "hePsiThermo");
        form->addRow("Thermo type:", m_thermoType);

        m_eos = new QComboBox;
        m_eos->addItems({"perfectGas", "incompressiblePerfectGas", "rhoConst"});
        m_eos->setCurrentText(m_isBuoyant ? "incompressiblePerfectGas" : "perfectGas");
        form->addRow("Equation of state:", m_eos);

        m_transport = new QComboBox;
        m_transport->addItems({"const", "sutherland"});
        form->addRow("Transport model:", m_transport);

        m_thermo = new QComboBox;
        m_thermo->addItems({"hConst", "janaf"});
        form->addRow("Thermodynamics:", m_thermo);

        m_molWeight = dblSpin(1,   300,  28.97, 3, "g/mol");
        m_cp        = dblSpin(1,   1e5,  1005,  1, "J/kg/K");
        m_hf        = dblSpin(-1e8,1e8,  0,     1, "J/kg");
        m_mu        = dblSpin(1e-9,1,    1.8e-5,8, "Pa * s");
        m_pr        = dblSpin(0.01,100,  0.713, 4);
        form->addRow("Molecular weight:", m_molWeight);
        form->addRow("Cp:", m_cp);
        form->addRow("Hf:", m_hf);
        form->addRow("Dynamic viscosity (mu):", m_mu);
        form->addRow("Prandtl number:", m_pr);

        m_refPressure = dblSpin(0, 1e7, 101325, 0, "Pa");
        m_refTemp     = dblSpin(0, 5000, 300,   1, "K");
        form->addRow("Reference pressure:", m_refPressure);
        form->addRow("Reference temperature:", m_refTemp);

        vb->addWidget(grp);

        connect(m_preset,    &QComboBox::currentTextChanged,
                this, &SolverConfigDialog::onPresetChanged);
        connect(m_transport, &QComboBox::currentTextChanged,
                this, &SolverConfigDialog::onTransportChanged);
    }

    {
        m_suthBox = new QGroupBox("Sutherland coefficients");
        auto* form = new QFormLayout(m_suthBox);
        m_as = dblSpin(1e-8, 1, 1.458e-6, 10);
        m_ts = dblSpin(0, 5000, 110.4, 2, "K");
        form->addRow("As:", m_as);
        form->addRow("Ts:", m_ts);
        m_suthBox->hide();
        vb->addWidget(m_suthBox);
    }

    if (m_isBuoyant) {
        auto* grp  = new QGroupBox("Buoyancy");
        auto* form = new QFormLayout(grp);
        m_gravityDir = new QComboBox;
        m_gravityDir->addItems({"(0 -9.81 0)", "(0 0 -9.81)",
                                "(0  9.81 0)", "(0 0  9.81)"});
        m_gravityDir->setToolTip("Gravity direction vector");
        form->addRow("Gravity g:", m_gravityDir);
        vb->addWidget(grp);
    }
}

void SolverConfigDialog::buildSolverSettingsGroup(QVBoxLayout* vb)
{

    auto* grp  = new QGroupBox("Solver settings");
    auto* form = new QFormLayout(grp);

    if (m_isTransient) {
        m_nCorrectors = new QSpinBox;
        m_nCorrectors->setRange(1, 10); m_nCorrectors->setValue(2);
        form->addRow("nCorrectors:", m_nCorrectors);
    }

    m_nNonOrth = new QSpinBox;
    m_nNonOrth->setRange(0, 10);
    m_nNonOrth->setValue(m_isTransient ? 1 : 3);
    form->addRow("nNonOrthogonalCorrectors:", m_nNonOrth);

    if (!m_isCentral) {
        m_relaxP = dblSpin(0.01, 1.0, m_isCompressible ? 0.3 : 0.3, 2);
        m_relaxU = dblSpin(0.01, 1.0, m_isTransient    ? 0.9 : 0.5, 2);
        form->addRow("Relaxation p:", m_relaxP);
        form->addRow("Relaxation U:", m_relaxU);
    }

    vb->addWidget(grp);
}

void SolverConfigDialog::onPresetChanged(const QString& preset)
{
    if (!FLUID_PRESETS.contains(preset) || !m_molWeight) return;
    const auto& p = FLUID_PRESETS[preset];
    m_molWeight->setValue(p.molWeight);
    m_cp->setValue(p.cp);
    m_hf->setValue(p.hf);
    m_mu->setValue(p.mu);
    m_pr->setValue(p.pr);
    bool editable = (preset == "Custom");
    m_molWeight->setEnabled(editable); m_cp->setEnabled(editable);
    m_hf->setEnabled(editable);        m_mu->setEnabled(editable);
    m_pr->setEnabled(editable);
}

void SolverConfigDialog::onTransportChanged(const QString& transport)
{
    if (m_suthBox) { m_suthBox->setVisible(transport == "sutherland"); adjustSize(); }
}

void SolverConfigDialog::updateMachDisplay()
{
    if (!m_machLbl || !m_inletVel) return;
    const double vel  = m_inletVel->value();
    const double T    = m_refTemp   ? m_refTemp->value()   : 300.0;
    const double cp   = m_cp        ? m_cp->value()        : 1005.0;
    const double molW = m_molWeight ? m_molWeight->value() : 28.97;
    const double R    = 8314.46 / molW;
    const double gamma = cp / std::max(cp - R, 1.0);
    const double a     = std::sqrt(std::max(0.0, gamma * R * std::max(T, 1.0)));
    const double M     = a > 0 ? vel / a : 0.0;
    m_machLbl->setText(QString("M ≈ %1  (a = %2 m/s)").arg(M, 0, 'f', 2).arg(a, 0, 'f', 0));
    const ThemeColors& tc = ThemeEngine::instance().colors();
    const QColor mc = M >= 1.0 ? tc.danger : (M >= 0.8 ? tc.warning : tc.textMuted);
    m_machLbl->setStyleSheet("color: " + mc.name() + "; font-style: italic;");
}

QList<SolverConfigDialog::PatchInfo> SolverConfigDialog::parseBoundary() const
{
    QList<PatchInfo> patches;
    QFile bf(m_caseDir + "/constant/polyMesh/boundary");
    if (!bf.open(QIODevice::ReadOnly | QIODevice::Text)) return patches;
    QString txt = bf.readAll();
    QRegularExpression re(R"((\w+)\s*\{[^}]*\btype\s+(\w+)\s*;[^}]*\})",
                          QRegularExpression::DotMatchesEverythingOption);
    auto it = re.globalMatch(txt);
    while (it.hasNext()) {
        auto m = it.next();
        if (m.captured(1) != "FoamFile") patches.append({m.captured(1), m.captured(2)});
    }
    return patches;
}

template<typename BC>
void SolverConfigDialog::writeBoundaryField(QTextStream& o,
                                             const QList<PatchInfo>& patches,
                                             BC patchBC)
{
    o << "boundaryField\n{\n";
    if (patches.isEmpty()) {
        o << "    inlet  { type zeroGradient; }\n"
          << "    outlet { type fixedValue; value uniform 0; }\n";
    } else {
        for (const auto& p : patches)
            o << "    " << p.name << " { " << patchBC(p.name, p.type) << " }\n";
    }
    o << "}\n";
}

void SolverConfigDialog::onApply()
{
    QDir().mkpath(m_caseDir + "/0");
    QDir().mkpath(m_caseDir + "/system");
    QDir().mkpath(m_caseDir + "/constant");

    writeAll();
    m_statusLbl->setText("Written: fvSchemes, fvSolution, 0/ fields"
                         + QString(m_isCompressible ? ", thermophysicalProperties" : "")
                         + QString(m_isBuoyant      ? ", g, p_rgh" : ""));
}

void SolverConfigDialog::patchControlDictTransient()
{
    if (!m_isTransient && !m_isCentral) {
        QString ctlPath = m_caseDir + "/system/controlDict";
        QFile rf(ctlPath);
        if (!rf.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QString txt = rf.readAll();
        rf.close();
        txt.replace(QRegularExpression(R"(adjustTimeStep\s+\w+\s*;)"), "adjustTimeStep  no;");
        txt.replace(QRegularExpression(R"(deltaT\s+[\d.eE+\-]+\s*;)"),       "deltaT          1;");

        {
            QRegularExpression etRe(R"(^([ \t]{0,4})endTime[ \t]+([\d.eE+\-]+)[ \t]*;)",
                                    QRegularExpression::MultilineOption);
            auto m = etRe.match(txt);
            bool ok = false;
            const double cur = m.hasMatch() ? m.captured(2).toDouble(&ok) : 0.0;
            if (m.hasMatch() && ok && cur < 10.0)
                txt.replace(etRe, "\\1endTime         200;");
        }
        {
            QRegularExpression wiRe(R"(^([ \t]{0,4})writeInterval[ \t]+([\d.eE+\-]+)[ \t]*;)",
                                    QRegularExpression::MultilineOption);
            auto m = wiRe.match(txt);
            bool ok = false;
            const double cur = m.hasMatch() ? m.captured(2).toDouble(&ok) : 0.0;
            if (m.hasMatch() && ok && cur < 1.0 && cur != std::floor(cur))
                txt.replace(wiRe, "\\1writeInterval   100;");
        }
        QFile wf(ctlPath);
        if (wf.open(QIODevice::WriteOnly | QIODevice::Text))
            wf.write(txt.toUtf8());
        return;
    }

    QString ctlPath = m_caseDir + "/system/controlDict";
    QFile rf(ctlPath);
    if (!rf.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString txt = rf.readAll();
    rf.close();

    const double vel = m_inletVel ? m_inletVel->value() : 30.0;

    double dtSafe;
    if (m_isCentral) {
        const double refT  = (m_refTemp ? m_refTemp->value() : 300.0);
        const double molW  = m_molWeight ? m_molWeight->value() : 28.97;
        const double R     = 8314.46 / molW;
        const double gamma = (m_cp ? m_cp->value() : 1005.0) / ((m_cp ? m_cp->value() : 1005.0) - R);
        const double a     = std::sqrt(gamma * R * std::max(refT, 1.0));
        dtSafe = 0.5 * 0.01 / (vel + a);
        dtSafe = std::max(dtSafe, 1e-7);
    } else {
        dtSafe = std::max(1e-5, 0.5 * 0.01 / std::max(vel, 1.0));
    }

    const QString dt = QString::number(dtSafe, 'g', 3);

    txt.replace(QRegularExpression(R"(adjustTimeStep\s+\w+\s*;)"), "adjustTimeStep  yes;");
    txt.replace(QRegularExpression(R"(deltaT\s+[\d.eE+\-]+\s*;)"),
                QString("deltaT          %1;").arg(dt));

    {
        QRegularExpression etRe(R"(^[ \t]{0,4}endTime[ \t]+([\d.eE+\-]+)[ \t]*;)",
                                QRegularExpression::MultilineOption);
        auto etM = etRe.match(txt);
        bool existingIsDefault = true;
        if (etM.hasMatch()) {
            bool ok; double ev = etM.captured(1).toDouble(&ok);
            if (ok && ev < 10.0) existingIsDefault = false;
        }
        if (existingIsDefault) {
            const double flowThrough = 2.0 / std::max(vel, 1.0);
            const double endTimeSec  = std::ceil(10.0 * flowThrough * 10.0) / 10.0;
            const QString et = QString::number(endTimeSec, 'g', 3);
            txt.replace(QRegularExpression(R"(^[ \t]{0,4}endTime[ \t]+[\d.eE+\-]+[ \t]*;)",
                        QRegularExpression::MultilineOption),
                        QString("endTime         %1;").arg(et));
        }
    }
    txt.replace(QRegularExpression(R"(^[ \t]{0,4}writeInterval[ \t]+[\d.eE+\-]+[ \t]*;)",
                QRegularExpression::MultilineOption),
                "writeInterval   100;");
    txt.replace(QRegularExpression(R"(^[ \t]{0,4}writeControl[ \t]+\w+[ \t]*;)",
                QRegularExpression::MultilineOption),
                "writeControl    timeStep;");
    txt.replace(QRegularExpression(R"(^([ \t]{5,}writeInterval)[ \t]+\d*\.\d+[ \t]*;)",
                QRegularExpression::MultilineOption),
                "\\1   1;");
    if (!txt.contains("maxCo"))
        txt.replace(QRegularExpression(R"(adjustTimeStep\s+yes\s*;)"),
                    QString("adjustTimeStep  yes;\nmaxCo           0.5;"));

    QFile wf(ctlPath);
    if (wf.open(QIODevice::WriteOnly | QIODevice::Text))
        wf.write(txt.toUtf8());
}

void SolverConfigDialog::patchControlDictLibs()
{
    const QString scheme = m_divScheme ? m_divScheme->currentData().toString() : "";
    const bool needsTeno = (scheme == "teno" || scheme == "teno6");

    QString ctlPath = m_caseDir + "/system/controlDict";
    QFile rf(ctlPath);
    if (!rf.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString txt = rf.readAll();
    rf.close();

    txt.replace(QRegularExpression(R"((\bapplication\s+)\w+(\s*;))"),
                "\\1" + m_solver + "\\2");

    const bool hasLib = txt.contains("libtenoScheme") || txt.contains("libTENOScheme");
    if (needsTeno && !hasLib)
        txt += "\nlibs (\"libtenoScheme.so\");\n";
    else if (!needsTeno && hasLib)
        txt.remove(QRegularExpression(R"(\n?libs\s*\([^)]*(?:libtenoScheme|libTENOScheme)[^)]*\)\s*;[^\n]*)"));

    QFile wf(ctlPath);
    if (wf.open(QIODevice::WriteOnly | QIODevice::Text))
        wf.write(txt.toUtf8());
}

void SolverConfigDialog::writeAll()
{
    auto patches = parseBoundary();

    const QStringList compressibleOnly = { "T", "alphat", "mut", "p_rgh", "rho" };
    const QStringList incompressibleOnly = { "nut" };
    const QStringList turbulenceFields = { "k", "omega", "epsilon", "nuTilda" };

    if (!m_isCompressible) {
        for (const auto& f : compressibleOnly)
            QFile::remove(m_caseDir + "/0/" + f);
        for (const auto& f : { QString("thermophysicalProperties"), QString("g"), QString("radiationProperties") })
            QFile::remove(m_caseDir + "/constant/" + f);
    }
    if (m_isLaminar) {
        for (const auto& f : turbulenceFields)
            QFile::remove(m_caseDir + "/0/" + f);
        for (const auto& f : incompressibleOnly)
            QFile::remove(m_caseDir + "/0/" + f);
    }

    writeFvSchemes();
    writeFvSolution();
    writeTurbulenceModel();
    patchControlDictLibs();
    patchControlDictTransient();

    if (m_isCompressible) {
        writeThermophysicalProperties();
        if (m_isBuoyant) { writeGravity(); writeRadiationProperties(); }
    } else {
        writeTransportProperties();
    }

    writeZeroFields(patches);
}

void SolverConfigDialog::writeTurbulenceModel()
{
    const QString hdr = "FoamFile\n{ version 2.0; format ascii; class dictionary; object turbulenceProperties; }\n\n";

    QString content;
    if (m_isLaminar) {
        content = hdr + "simulationType laminar;\n";
    } else if (m_simType && m_simType->currentData().toString() == "LES") {
        const QString sgs = m_sgsModel ? m_sgsModel->currentData().toString() : "Smagorinsky";
        content = hdr
            + "simulationType LES;\n\n"
            + "LES\n{\n"
            + "    model           " + sgs + ";\n"
            + "    turbulence      on;\n"
            + "    printCoeffs     on;\n"
            + "    delta           cubeRootVol;\n"
            + "    cubeRootVolCoeffs { deltaCoeff 1; }\n"
            + (sgs == "WALE" ?
               "    WALECoeffs { Cw 0.325; }\n" :
               sgs == "dynamicKEqn" ?
               "    dynamicKEqnCoeffs { filter simple; }\n" : "")
            + "}\n";
    } else {
        const QString ras = m_rasModel ? m_rasModel->currentData().toString() : "kOmegaSST";
        content = hdr
            + "simulationType RAS;\n\nRAS\n{\n"
            + "    model       " + ras + ";\n"
            + "    turbulence  on;\n"
            + "    printCoeffs on;\n"
            + "}\n";
    }

    for (const QString& name : { QString("turbulenceProperties"), QString("momentumTransport") }) {
        QFile f(m_caseDir + "/constant/" + name);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&f) << content;
    }
}

void SolverConfigDialog::writeTransportProperties()
{
    QFile f(m_caseDir + "/constant/transportProperties");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&f);
    o << "FoamFile\n{ version 2.0; format ascii; class dictionary;"
      << " object transportProperties; }\n\n"
      << "transportModel  Newtonian;\n\n"
      << "nu              " << m_nu->value() << ";\n";
}

void SolverConfigDialog::writeThermophysicalProperties()
{
    if (!m_thermoType) return;
    QString transport = m_transport->currentText();
    QString transportBlock;
    if (transport == "sutherland") {
        transportBlock = QString("    transport\n    {\n        As  %1;\n        Ts  %2;\n    }\n")
                         .arg(m_as->value(), 0, 'g', 8).arg(m_ts->value(), 0, 'f', 2);
    } else {
        transportBlock = QString("    transport\n    {\n        mu  %1;\n        Pr  %2;\n    }\n")
                         .arg(m_mu->value(), 0, 'g', 8).arg(m_pr->value(), 0, 'f', 6);
    }

    QString thermoBlock;
    if (m_thermo->currentText() == "janaf") {
        thermoBlock =
            "    thermodynamics\n    {\n"
            "        Tlow            200;\n"
            "        Thigh           3500;\n"
            "        Tcommon         1000;\n"
            "        highCpCoeffs    ( 3.57304 -7.24383e-04 1.67022e-06 -1.26501e-10"
            " -4.20580e-13 -1047.41 3.12431 );\n"
            "        lowCpCoeffs     ( 3.09589 1.22835e-03 -4.14267e-07 6.56910e-11"
            " -3.87021e-15 -983.191 5.34161 );\n"
            "    }\n";
    } else {
        thermoBlock = QString("    thermodynamics  { Cp %1; Hf %2; }\n")
                      .arg(m_cp->value()).arg(m_hf->value());
    }

    const QString energy = m_isCentral ? "sensibleInternalEnergy" : "sensibleEnthalpy";

    QFile f(m_caseDir + "/constant/thermophysicalProperties");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&f);
    o << "FoamFile\n{ version 2.0; format ascii; class dictionary;"
      << " object thermophysicalProperties; }\n\n"
      << "thermoType\n{\n"
      << "    type            " << m_thermoType->currentText() << ";\n"
      << "    mixture         pureMixture;\n"
      << "    transport       " << transport << ";\n"
      << "    thermo          " << m_thermo->currentText() << ";\n"
      << "    equationOfState " << m_eos->currentText() << ";\n"
      << "    specie          specie;\n"
      << "    energy          " << energy << ";\n"
      << "}\n\n"
      << "mixture\n{\n"
      << "    specie          { nMoles 1; molWeight " << m_molWeight->value() << "; }\n"
      << "    equationOfState { " << [&]() -> QString {
              const QString eos = m_eos->currentText();
              if (eos == "incompressiblePerfectGas")
                  return QString("pRef %1;").arg((int)m_refPressure->value());
              if (eos == "rhoConst")
                  return "rho 1.225;";
              return "";
         }() << " }\n"
      << thermoBlock
      << transportBlock
      << "}\n";
}

void SolverConfigDialog::writeFvSchemes()
{
    QString schPath = m_caseDir + "/system/fvSchemes";
    QFile::remove(schPath);
    QFile f(schPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&f);

    const QString divSch = m_divScheme->currentData().toString();
    const QString divUScheme = m_isLaminar        ? "Gauss linearUpwind grad(U)"
                             : (divSch == "teno")  ? "Gauss teno"
                             : (divSch == "teno6") ? "Gauss teno6"
                             : "Gauss linearUpwind grad(U)";

    if (m_isCentral) {
        o << "FoamFile\n{ version 2.0; format ascii; class dictionary; object fvSchemes; }\n\n"
          << "ddtSchemes      { default Euler; }\n\n"
          << "gradSchemes     { default Gauss linear; }\n\n"
          << "divSchemes\n{\n"
          << "    default          none;\n"
          << "    div(tauMC)       Gauss linear;\n"
          << "    div(phi,k)       bounded Gauss upwind;\n"
          << "    div(phi,omega)   bounded Gauss upwind;\n"
          << "    div(phi,epsilon) bounded Gauss upwind;\n"
          << "    div(phi,nuTilda) bounded Gauss upwind;\n"
          << "}\n\n"
          << "interpolationSchemes\n{\n"
          << "    default          linear;\n"
          << "    reconstruct(rho) vanLeer;\n"
          << "    reconstruct(U)   vanLeerV;\n"
          << "    reconstruct(T)   vanLeer;\n"
          << "}\n\n"
          << "laplacianSchemes { default Gauss linear corrected; }\n\n"
          << "snGradSchemes    { default corrected; }\n\n"
          << "fluxRequired     { default no; }\n\n"
          << "wallDist         { method meshWave; }\n";
        return;
    }

    if (m_isBuoyant) {
        const bool bSteady = !m_isTransient;
        o << "FoamFile\n{ version 2.0; format ascii; class dictionary; object fvSchemes; }\n\n"
          << "ddtSchemes      { default " << (bSteady ? "steadyState" : "Euler") << "; }\n\n"
          << "gradSchemes     { default Gauss linear; }\n\n"
          << "divSchemes\n{\n"
          << "    default                                 none;\n"
          << "    div(phi,U)                              Gauss linearUpwind grad(U);\n"
          << "    div(phi,h)                              Gauss upwind;\n"
          << "    div(phi,K)                              Gauss upwind;\n"
          << "    div(phi,Ekp)                            Gauss upwind;\n"
          << "    div(phiv,p)                             Gauss upwind;\n"
          << "    div(phi,e)                              Gauss upwind;\n"
          << "    div(phi,k)                              bounded Gauss upwind;\n"
          << "    div(phi,omega)                          bounded Gauss upwind;\n"
          << "    div(phi,epsilon)                        bounded Gauss upwind;\n"
          << "    div(phi,nuTilda)                        bounded Gauss upwind;\n"
          << "    div(((rho*nuEff)*dev2(T(grad(U)))))     Gauss linear;\n"
          << "}\n\n"
          << "laplacianSchemes     { default Gauss linear limited 0.333; }\n\n"
          << "interpolationSchemes { default linear; }\n\n"
          << "snGradSchemes        { default limited 0.333; }\n\n"
          << "fluxRequired         { default no; p_rgh; }\n\n"
          << "wallDist             { method meshWave; }\n";
        return;
    }

    const bool steady = !m_isTransient;
    o << "FoamFile\n{ version 2.0; format ascii; class dictionary; object fvSchemes; }\n\n"
      << "ddtSchemes      { default " << (steady ? "steadyState" : "backward") << "; }\n\n"
      << "gradSchemes     { default Gauss linear; }\n\n"
      << "divSchemes\n{\n"
      << "    default                                 none;\n"
      << "    div(phi,U)                              " << divUScheme << ";\n";

    if (m_isCompressible) {
        o << "    div(phi,h)                              Gauss linearUpwind grad(h);\n"
          << "    div(phi,K)                              Gauss linearUpwind grad(K);\n"
          << "    div(phi,Ekp)                            Gauss linearUpwind grad(Ekp);\n"
          << "    div(phiv,p)                             Gauss linearUpwind grad(p);\n"
          << "    div(phi,e)                              Gauss linearUpwind grad(e);\n"
          << "    turbulence                              bounded Gauss upwind;\n"
          << "    div(phi,k)                              $turbulence;\n"
          << "    div(phi,omega)                          $turbulence;\n"
          << "    div(phi,epsilon)                        $turbulence;\n"
          << "    div(phi,nuTilda)                        $turbulence;\n"
          << "    div(((rho*nuEff)*dev2(T(grad(U)))))     Gauss linear;\n";
    } else {
        o << "    turbulence                              bounded Gauss upwind;\n"
          << "    div(phi,k)                              $turbulence;\n"
          << "    div(phi,omega)                          $turbulence;\n"
          << "    div(phi,epsilon)                        $turbulence;\n"
          << "    div(phi,nuTilda)                        $turbulence;\n"
          << "    div((nuEff*dev2(T(grad(U)))))           Gauss linear;\n";
    }

    o << "}\n\n"
      << "laplacianSchemes     { default Gauss linear corrected; }\n\n"
      << "interpolationSchemes { default linear; }\n\n"
      << "snGradSchemes        { default corrected; }\n\n"
      << "fluxRequired         { default no; p; }\n\n"
      << "wallDist             { method meshWave; }\n";
}

void SolverConfigDialog::writeFvSolution()
{
    QString solPath = m_caseDir + "/system/fvSolution";
    QFile::remove(solPath);
    QFile f(solPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&f);

    const int nNonOrth = m_nNonOrth ? m_nNonOrth->value() : 1;
    const double relP  = m_relaxP   ? m_relaxP->value()   : 0.3;
    const double relU  = m_relaxU   ? m_relaxU->value()   : 0.5;

    o << "FoamFile\n{ version 2.0; format ascii; class dictionary; object fvSolution; }\n\n"
      << "solvers\n{\n";

    if (m_isCentral) {
        o << "    \"(rho|rhoU|rhoE)\" { solver diagonal; }\n"
          << "    U  { solver smoothSolver; smoother GaussSeidel; nSweeps 2; tolerance 1e-9; relTol 0.01; }\n"
          << "    e  { $U; tolerance 1e-10; relTol 0; }\n";
        if (!m_isLaminar)
            o << "    \"(k|omega|epsilon|nuTilda)\" { solver smoothSolver; smoother GaussSeidel; tolerance 1e-8; relTol 0.1; }\n";
    } else if (m_isBuoyant) {
        o << "    \"rho.*\" { solver diagonal; }\n";
        if (m_isTransient) {
            o << "    p_rgh       { solver GAMG; smoother GaussSeidel; tolerance 1e-8; relTol 0.01; }\n"
              << "    p_rghFinal  { $p_rgh; relTol 0; }\n"
              << "    \"(U|h|k|omega|epsilon)\" { solver PBiCGStab; preconditioner DILU; tolerance 1e-7; relTol 0.1; }\n"
              << "    \"(U|h|k|omega|epsilon)Final\" { $U; relTol 0; }\n";
        } else {
            o << "    p_rgh       { solver GAMG; smoother GaussSeidel; tolerance 1e-7; relTol 0.01; }\n"
              << "    p_rghFinal  { $p_rgh; relTol 0; }\n"
              << "    \"(U|h|k|omega|epsilon)\" { solver PBiCGStab; preconditioner DILU; tolerance 1e-7; relTol 0.05; }\n";
        }
    } else {
        o << "    p       { solver GAMG; smoother GaussSeidel; tolerance 1e-7; relTol 0.01; }\n"
          << "    pFinal  { $p; relTol 0; }\n";
        if (m_isTransient)
            o << "    U  { solver PBiCGStab; preconditioner DILU; tolerance 1e-8; relTol 0.1; }\n";
        else
            o << "    U  { solver smoothSolver; smoother symGaussSeidel; tolerance 1e-8; relTol 0.1; }\n";
        o << "    UFinal { $U; relTol 0; }\n";
        if (!m_isLaminar)
            o << "    \"(k|omega|epsilon|nuTilda|kFinal|omegaFinal|epsilonFinal|nuTildaFinal)\""
              << " { solver smoothSolver; smoother GaussSeidel; tolerance 1e-8; relTol 0.1; }\n";
        if (m_isCompressible) {
            o << "    h       { solver smoothSolver; smoother GaussSeidel; tolerance 1e-8; relTol 0.1; }\n"
              << "    hFinal  { $h; relTol 0; }\n"
              << "    \"rho.*\" { solver diagonal; }\n";
        }
    }

    o << "}\n\n";

    if (m_solver == "icoFoam" || m_solver == "pisoFoam") {
        const int nCorr = m_nCorrectors ? m_nCorrectors->value() : 2;
        o << "PISO\n{\n"
          << "    nCorrectors             " << nCorr    << ";\n"
          << "    nNonOrthogonalCorrectors " << nNonOrth << ";\n"
          << "}\n";
    } else if (m_isTransient) {
        const int nCorr      = m_nCorrectors ? m_nCorrectors->value() : 2;
        o << "PIMPLE\n{\n"
          << "    nOuterCorrectors        " << (m_isBuoyant ? 1 : 2) << ";\n"
          << "    nCorrectors             " << nCorr    << ";\n"
          << "    nNonOrthogonalCorrectors " << nNonOrth << ";\n";
        if (m_isBuoyant) {
            const double refP = m_refPressure ? m_refPressure->value() : 1e5;
            o << "    pRefCell    0;\n"
              << "    pRefValue   " << refP << ";\n";
        }
        o << "}\n\n"
          << "relaxationFactors\n{\n"
          << "    equations { \".*\" 1; }\n"
          << "}\n";
    } else if (!m_isCentral) {
        o << "SIMPLE\n{\n"
          << (m_isBuoyant ? "    momentumPredictor no;\n" : "")
          << (m_isCompressible && !m_isBuoyant ? "    consistent yes;\n" : "")
          << "    nNonOrthogonalCorrectors " << nNonOrth << ";\n";
        if (m_isBuoyant)
            o << "    residualControl { p_rgh 1e-4; U 1e-4; h 1e-4; }\n";
        else if (m_isCompressible)
            o << "    pMinFactor 0.1;\n    pMaxFactor 2.0;\n"
              << "    residualControl { p 1e-4; U 1e-4; h 1e-4; }\n";
        else
            o << "    residualControl { p 1e-5; U 1e-5; }\n";
        o << "}\n\n"
          << "relaxationFactors\n{\n";
        if (m_isBuoyant)
            o << "    fields  { rho 1; p_rgh 0.7; }\n"
              << "    equations { U 0.2; h 0.3; k 0.5; omega 0.5; epsilon 0.5; }\n";
        else if (m_isCompressible)
            o << "    fields  { p " << relP << "; rho 0.05; }\n"
              << "    equations { U " << relU << "; h 0.5; k 0.5; omega 0.5; epsilon 0.5; }\n";
        else
            o << "    fields  { p " << relP << "; }\n"
              << "    equations { U " << relU << "; k 0.5; omega 0.5; epsilon 0.5; }\n";
        o << "}\n";
    }
}

void SolverConfigDialog::writeZeroFields(const QList<PatchInfo>& patches)
{
    const double vel    = m_inletVel ? m_inletVel->value() : 30.0;
    const double refP   = (m_isCompressible && m_refPressure) ? m_refPressure->value() : 0.0;
    const double refT   = (m_isCompressible && m_refTemp)     ? m_refTemp->value()     : 300.0;

    {
        QString flowAxis = QSettings().value("domainSetup/flowAxis", "-X").toString();
        double  aoaDeg   = QSettings().value("domainSetup/aoa",      0.0).toDouble();
        double  aoaRad   = aoaDeg * M_PI / 180.0;

        auto makeVec = [&](double sign) -> QString {
            double u0 = sign * vel * std::cos(aoaRad);
            double u1 = vel * std::sin(aoaRad);
            if (flowAxis == "+X" || flowAxis == "-X")
                return QString("(%1 0 %2)").arg(u0, 0,'g',6).arg(u1, 0,'g',6);
            if (flowAxis == "+Y" || flowAxis == "-Y")
                return QString("(%2 %1 0)").arg(u0, 0,'g',6).arg(u1, 0,'g',6);
            return QString("(0 %2 %1)").arg(u0, 0,'g',6).arg(u1, 0,'g',6);
        };
        double sign = (flowAxis == "-X" || flowAxis == "-Y" || flowAxis == "-Z") ? -1.0 : 1.0;
        QString uVec = makeVec(sign);

        QFile::remove(m_caseDir + "/0/U");
        QFile f(m_caseDir + "/0/U");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream o(&f);
            o << "FoamFile\n{ version 2.0; format ascii; class volVectorField; object U; }\n\n"
              << "dimensions [0 1 -1 0 0 0 0];\n"
              << "internalField uniform " << uVec << ";\n";
            writeBoundaryField(o, patches, [&](const QString& name, const QString& type) -> QString {
                if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                    return "type " + type + ";";
                if (type == "wall") return "type noSlip;";
                if (name == "outlet") return "type zeroGradient;";
                return QString("type fixedValue; value uniform %1;").arg(uVec);
            });
        }
    }

    {
        QFile::remove(m_caseDir + "/0/p");
        QFile f(m_caseDir + "/0/p");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream o(&f);
            if (m_isCompressible) {
                o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object p; }\n\n"
                  << "dimensions [1 -1 -2 0 0 0 0];\n"
                  << "internalField uniform " << refP << ";\n";
                if (m_isCentral) {
                    writeBoundaryField(o, patches, [&](const QString& name, const QString& type) -> QString {
                        if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                            return "type " + type + ";";
                        if (type == "wall") return "type zeroGradient;";
                        if (name == "outlet") return "type zeroGradient;";
                        return QString("type fixedValue; value uniform %1;").arg(refP);
                    });
                } else {
                writeBoundaryField(o, patches, [&](const QString& name, const QString& type) -> QString {
                    if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                        return "type " + type + ";";
                    if (name == "outlet")
                        return QString("type fixedValue; value uniform %1;").arg(refP);
                    return "type zeroGradient;";
                });
                }
            } else {
                o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object p; }\n\n"
                  << "dimensions [0 2 -2 0 0 0 0];\n"
                  << "internalField uniform 0;\n";
                writeBoundaryField(o, patches, [](const QString& name, const QString& type) -> QString {
                    if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                        return "type " + type + ";";
                    if (name == "outlet") return "type fixedValue; value uniform 0;";
                    return "type zeroGradient;";
                });
            }
        }
    }

    if (!m_isLaminar) {
        const bool isRAS = !(m_simType && m_simType->currentData().toString() == "LES");
        const QString rasModel = m_rasModel ? m_rasModel->currentData().toString() : "kOmegaSST";
        const bool isSA = isRAS && rasModel == "SpalartAllmaras";
        const bool isKE = isRAS && rasModel == "kEpsilon";
        const double k0 = 1.5 * std::pow(0.05 * vel, 2.0);

        double nu_est;
        if (m_isCompressible) {
            const double mu   = m_mu ? m_mu->value() : 1.8e-5;
            const double molW = m_molWeight ? m_molWeight->value() : 28.97;
            const double R    = 8314.46 / molW;
            const double rho  = (m_refPressure ? m_refPressure->value() : 101325.0)
                                / (R * std::max(refT, 1.0));
            nu_est = mu / std::max(rho, 1e-10);
        } else {
            nu_est = m_nu ? m_nu->value() : 1.5e-5;
        }
        const double omega0 = k0 / (10.0 * nu_est);
        const double nut0   = 10.0 * nu_est;

        {
            QFile::remove(m_caseDir + "/0/k");
            QFile f(m_caseDir + "/0/k");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream o(&f);
                o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object k; }\n\n"
                  << "dimensions [0 2 -2 0 0 0 0];\n"
                  << "internalField uniform " << k0 << ";\n";
                writeBoundaryField(o, patches, [&](const QString& name, const QString& type) -> QString {
                    if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                        return "type " + type + ";";
                    if (type == "wall")
                        return QString("type kqRWallFunction; value uniform %1;").arg(k0);
                    if (name == "inlet")
                        return QString("type fixedValue; value uniform %1;").arg(k0);
                    return "type zeroGradient;";
                });
            }
        }

        {
            QFile::remove(m_caseDir + "/0/omega");
            QFile f(m_caseDir + "/0/omega");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream o(&f);
                o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object omega; }\n\n"
                  << "dimensions [0 0 -1 0 0 0 0];\n"
                  << "internalField uniform " << omega0 << ";\n";
                writeBoundaryField(o, patches, [&](const QString& name, const QString& type) -> QString {
                    if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                        return "type " + type + ";";
                    if (type == "wall")
                        return QString("type omegaWallFunction; value uniform %1;").arg(omega0);
                    if (name == "inlet")
                        return QString("type fixedValue; value uniform %1;").arg(omega0);
                    return "type zeroGradient;";
                });
            }
        }

        {
            QFile::remove(m_caseDir + "/0/nut");
            QFile f(m_caseDir + "/0/nut");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream o(&f);
                o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object nut; }\n\n"
                  << "dimensions [0 2 -1 0 0 0 0];\n"
                  << "internalField uniform " << nut0 << ";\n";
                writeBoundaryField(o, patches, [&](const QString& name, const QString& type) -> QString {
                    if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                        return "type " + type + ";";
                    if (type == "wall")
                        return isSA ? "type nutUSpaldingWallFunction; value uniform 0;"
                                    : "type nutkWallFunction; value uniform 0;";
                    return "type calculated; value uniform 0;";
                });
            }
        }

        if (isSA) {
            const double nuTilda0 = 5.0 * nu_est;
            QFile::remove(m_caseDir + "/0/nuTilda");
            QFile f(m_caseDir + "/0/nuTilda");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream o(&f);
                o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object nuTilda; }\n\n"
                  << "dimensions [0 2 -1 0 0 0 0];\n"
                  << "internalField uniform " << nuTilda0 << ";\n";
                writeBoundaryField(o, patches, [&](const QString& name, const QString& type) -> QString {
                    if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                        return "type " + type + ";";
                    if (type == "wall")
                        return "type fixedValue; value uniform 0;";
                    if (name == "inlet")
                        return QString("type fixedValue; value uniform %1;").arg(nuTilda0);
                    return "type zeroGradient;";
                });
            }
        }

        if (isKE) {
            const double eps0 = 0.09 * k0 * omega0;
            QFile::remove(m_caseDir + "/0/epsilon");
            QFile f(m_caseDir + "/0/epsilon");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream o(&f);
                o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object epsilon; }\n\n"
                  << "dimensions [0 2 -3 0 0 0 0];\n"
                  << "internalField uniform " << eps0 << ";\n";
                writeBoundaryField(o, patches, [&](const QString& name, const QString& type) -> QString {
                    if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                        return "type " + type + ";";
                    if (type == "wall")
                        return QString("type epsilonWallFunction; value uniform %1;").arg(eps0);
                    if (name == "inlet")
                        return QString("type fixedValue; value uniform %1;").arg(eps0);
                    return "type zeroGradient;";
                });
            }
        }

        if (m_isCompressible) {
            QFile::remove(m_caseDir + "/0/mut");
            QFile f(m_caseDir + "/0/mut");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream o(&f);
                o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object mut; }\n\n"
                  << "dimensions [1 -1 -1 0 0 0 0];\n"
                  << "internalField uniform 0;\n";
                writeBoundaryField(o, patches, [](const QString& name, const QString& type) -> QString {
                    if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                        return "type " + type + ";";
                    if (type == "wall")
                        return "type mutkWallFunction; value uniform 0;";
                    return "type calculated; value uniform 0;";
                });
            }
        }
    }

    if (!m_isCompressible) return;

    {
        QFile::remove(m_caseDir + "/0/T");
        QFile f(m_caseDir + "/0/T");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream o(&f);
            o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object T; }\n\n"
              << "dimensions [0 0 0 1 0 0 0];\n"
              << "internalField uniform " << refT << ";\n";
            writeBoundaryField(o, patches, [&](const QString& name, const QString& type) -> QString {
                if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                    return "type " + type + ";";
                if (name == "inlet")
                    return QString("type fixedValue; value uniform %1;").arg(refT);
                return "type zeroGradient;";
            });
        }
    }

    {
        QFile::remove(m_caseDir + "/0/alphat");
        QFile f(m_caseDir + "/0/alphat");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream o(&f);
            o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object alphat; }\n\n"
              << "dimensions [1 -1 -1 0 0 0 0];\n"
              << "internalField uniform 0;\n";
            writeBoundaryField(o, patches, [](const QString& name, const QString& type) -> QString {
                if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                    return "type " + type + ";";
                if (type == "wall")
                    return "type compressible::alphatWallFunction; value uniform 0;";
                return "type zeroGradient;";
            });
        }
    }

    if (m_isCentral) {
        const double molW = m_molWeight ? m_molWeight->value() : 28.97;
        const double R    = 8314.46 / molW;
        const double rho0 = refP / (R * refT);
        QFile::remove(m_caseDir + "/0/rho");
        QFile f(m_caseDir + "/0/rho");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream o(&f);
            o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object rho; }\n\n"
              << "dimensions [1 -3 0 0 0 0 0];\n"
              << "internalField uniform " << rho0 << ";\n";
            writeBoundaryField(o, patches, [](const QString&, const QString& type) -> QString {
                if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                    return "type " + type + ";";
                return "type zeroGradient;";
            });
        }
    }

    if (m_isBuoyant) {
        QFile::remove(m_caseDir + "/0/p_rgh");
        QFile f(m_caseDir + "/0/p_rgh");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream o(&f);
            o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object p_rgh; }\n\n"
              << "dimensions [1 -1 -2 0 0 0 0];\n"
              << "internalField uniform 0;\n";
            writeBoundaryField(o, patches, [](const QString& name, const QString& type) -> QString {
                if (type == "symmetry" || type == "symmetryPlane" || type == "empty")
                    return "type " + type + ";";
                if (name == "outlet") return "type fixedValue; value uniform 0;";
                return "type fixedFluxPressure; value uniform 0;";
            });
        }
    }
}

void SolverConfigDialog::writeRadiationProperties()
{
    QFile f(m_caseDir + "/constant/radiationProperties");
    if (f.exists()) return;
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&f);
    o << "FoamFile\n{ version 2.0; format ascii; class dictionary; object radiationProperties; }\n\n"
      << "radiation       off;\n\n"
      << "radiationModel  none;\n\n"
      << "solverFreq      10;\n\n"
      << "absorptionEmissionModel none;\n\n"
      << "scatterModel    none;\n\n"
      << "sootModel       none;\n";
}

void SolverConfigDialog::writeGravity()
{
    const QString gVal = m_gravityDir ? m_gravityDir->currentText() : "(0 -9.81 0)";
    QFile f(m_caseDir + "/constant/g");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&f);
    o << "FoamFile\n{ version 2.0; format ascii;"
      << " class uniformDimensionedVectorField; object g; }\n\n"
      << "dimensions [0 1 -2 0 0 0 0];\n"
      << "value " << gVal << ";\n";
}
