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

#include "PhysicsPanel.h"
#include "CitationManager.h"
#include <QJsonObject>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QMessageBox>
#include <QStandardItemModel>
#include <cmath>

PhysicsPanel::PhysicsPanel(const QString& caseDir, QWidget* parent)
    : QDialog(parent), m_caseDir(caseDir)
{
    setWindowTitle("Physics Settings");
    setMinimumWidth(380);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    // Solver
    auto* solverGroup = new QGroupBox("Solver");
    auto* solverForm  = new QFormLayout(solverGroup);

    m_solverCombo = new QComboBox;
    m_solverCombo->addItem("simpleFoam  (incompressible RANS)",          QString("simpleFoam"));
    m_solverCombo->addItem("buoyantSimpleFoam  (thermal RANS)",          QString("buoyantSimpleFoam"));
    m_solverCombo->addItem("rhoSimpleFoam  (compressible RANS)",         QString("rhoSimpleFoam"));
    m_solverCombo->addItem("pimpleFoam  (incompressible LES/transient)", QString("pimpleFoam"));
    m_solverCombo->addItem("buoyantPimpleFoam  (thermal LES/transient)", QString("buoyantPimpleFoam"));
    m_solverCombo->setToolTip(
        "Sets the OpenFOAM solver and configures fvSchemes accordingly.\n"
        "Thermal solvers also require constant/thermophysicalProperties\n"
        "(Physics menu → Thermal Properties).");
    solverForm->addRow("Solver:", m_solverCombo);

    m_thermalNote = new QLabel(
        "Thermal solver: also configure Physics → Thermal Properties");
    m_thermalNote->setStyleSheet("color: #f5c500; font-size: 11px;");
    m_thermalNote->setWordWrap(true);
    m_thermalNote->hide();
    solverForm->addRow("", m_thermalNote);

    root->addWidget(solverGroup);

    // Turbulence
    auto* turbGroup = new QGroupBox("Turbulence");
    auto* turbForm  = new QFormLayout(turbGroup);

    m_simTypeCombo = new QComboBox;
    m_simTypeCombo->addItems({"RANS", "LES / SGS"});
    turbForm->addRow("Sim type:", m_simTypeCombo);

    m_modelCombo = new QComboBox;
    m_modelCombo->addItems({"kOmegaSST", "kEpsilon", "SpalartAllmaras", "laminar"});
    turbForm->addRow("Model:", m_modelCombo);

    m_turbBox = new QGroupBox("Turbulence Intensity");
    auto* turbIntForm = new QFormLayout(m_turbBox);

    m_kSpin = new QDoubleSpinBox; m_kSpin->setDecimals(4);
    m_kSpin->setRange(1e-10, 1e6); m_kSpin->setValue(0.0006);
    m_kSpin->setToolTip("Turbulent kinetic energy k [m²/s²]");
    turbIntForm->addRow("k:", m_kSpin);

    m_omegaLabel   = new QLabel("omega:");
    m_epsilonLabel = new QLabel("epsilon:");

    m_omegaSpin = new QDoubleSpinBox; m_omegaSpin->setDecimals(2);
    m_omegaSpin->setRange(1e-6, 1e9); m_omegaSpin->setValue(100.0);
    m_omegaSpin->setToolTip("Specific dissipation rate omega [1/s]");
    turbIntForm->addRow(m_omegaLabel, m_omegaSpin);

    m_epsilonSpin = new QDoubleSpinBox; m_epsilonSpin->setDecimals(4);
    m_epsilonSpin->setRange(1e-10, 1e9); m_epsilonSpin->setValue(0.001);
    m_epsilonSpin->setToolTip("Dissipation rate epsilon [m²/s³]");
    turbIntForm->addRow(m_epsilonLabel, m_epsilonSpin);

    turbForm->addRow(m_turbBox);
    root->addWidget(turbGroup);

    // Fluid
    auto* fluidGroup = new QGroupBox("Fluid Properties");
    auto* fluidForm  = new QFormLayout(fluidGroup);

    m_nuSpin = new QDoubleSpinBox;
    m_nuSpin->setDecimals(8);
    m_nuSpin->setRange(1e-10, 1.0);
    m_nuSpin->setSingleStep(1e-6);
    m_nuSpin->setValue(1.5e-5);
    m_nuSpin->setToolTip("Kinematic viscosity nu [m²/s]   (air ~1.5e-5, water ~1e-6)");
    fluidForm->addRow("nu (m²/s):", m_nuSpin);

    m_rhoSpin = new QDoubleSpinBox;
    m_rhoSpin->setDecimals(4);
    m_rhoSpin->setRange(1e-4, 1e5);
    m_rhoSpin->setValue(1.225);
    m_rhoSpin->setToolTip("Density rho [kg/m³]   (air ~1.225, water ~998)\n"
                          "For compressible solvers (rhoSimpleFoam/buoyant*) this is the\n"
                          "reference density; actual rho is computed from thermophysicalProperties.");
    fluidForm->addRow("rho (kg/m³):", m_rhoSpin);
    root->addWidget(fluidGroup);

    // Numerical Schemes
    auto* schemesGroup = new QGroupBox("Numerical Schemes");
    auto* schemesForm  = new QFormLayout(schemesGroup);

    m_divSchemeCombo = new QComboBox;
    m_divSchemeCombo->addItem("linearUpwind  (default)",          QString("linearUpwind"));
    m_divSchemeCombo->addItem("TENO5  (CakeCFD)",    QString("teno"));
    m_divSchemeCombo->addItem("TENO6  (CakeCFD)",    QString("teno6"));
    m_divSchemeCombo->setToolTip(
        "Divergence scheme for div(phi,U).\n"
        "TENO5/TENO6 require libtenoScheme.so in $FOAM_USER_LIBBIN.\n"
        "TENO + LES is supported but requires DNS-quality mesh and CFL < 0.2.");
    schemesForm->addRow("div(phi,U):", m_divSchemeCombo);

    auto* tenoNote = new QLabel("TENO requires libtenoScheme.so in $FOAM_USER_LIBBIN");
    tenoNote->setStyleSheet("color: gray; font-size: 11px;");
    schemesForm->addRow("", tenoNote);
    root->addWidget(schemesGroup);

    // Inlet BC
    auto* inletGroup = new QGroupBox("Inlet Velocity");
    auto* inletForm  = new QFormLayout(inletGroup);

    auto makeUSpin = []{
        auto* s = new QDoubleSpinBox;
        s->setRange(-1000, 1000); s->setDecimals(3); s->setSingleStep(1.0);
        return s;
    };
    m_uxSpin = makeUSpin(); m_uxSpin->setValue(1.0);
    m_uySpin = makeUSpin();
    m_uzSpin = makeUSpin();
    inletForm->addRow("Ux (m/s):", m_uxSpin);
    inletForm->addRow("Uy (m/s):", m_uySpin);
    inletForm->addRow("Uz (m/s):", m_uzSpin);
    root->addWidget(inletGroup);

    // Reynolds Number & Reference Quantities
    auto* reGroup  = new QGroupBox("Reynolds Number & Reference Quantities");
    auto* reLayout = new QFormLayout(reGroup);

    m_lengthSpin = new QDoubleSpinBox;
    m_lengthSpin->setRange(1e-6, 1e4); m_lengthSpin->setDecimals(4);
    m_lengthSpin->setValue(1.0); m_lengthSpin->setSingleStep(0.1);
    m_lengthSpin->setToolTip("Reference length L_ref [m] : used for Re and moment coefficients");
    reLayout->addRow("L_ref (m):", m_lengthSpin);

    m_arefSpin = new QDoubleSpinBox;
    m_arefSpin->setRange(0, 1e6); m_arefSpin->setDecimals(6);
    m_arefSpin->setValue(0.0);
    m_arefSpin->setToolTip("Reference area A_ref [m²] : used for Cd, Cl, Cm (0 = not set)");
    reLayout->addRow("A_ref (m²):", m_arefSpin);

    m_reLabel = new QLabel("-");
    m_reLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #64b5f6;");
    reLayout->addRow("Re:", m_reLabel);
    root->addWidget(reGroup);

    // Buttons
    auto* btnBox = new QDialogButtonBox;
    auto* applyBtn = btnBox->addButton("Apply to Case", QDialogButtonBox::AcceptRole);
    btnBox->addButton(QDialogButtonBox::Close);
    root->addWidget(btnBox);

    connect(applyBtn,        &QPushButton::clicked, this, &PhysicsPanel::onApply);
    connect(btnBox,          &QDialogButtonBox::rejected, this, [this](){
        onApply();   // always write settings to case on close
        reject();
    });
    connect(m_solverCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PhysicsPanel::onSolverChanged);
    connect(m_simTypeCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PhysicsPanel::onSimTypeChanged);
    connect(m_modelCombo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PhysicsPanel::onModelChanged);
    connect(m_divSchemeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        bool isTeno = (m_divSchemeCombo->currentData().toString() != "linearUpwind");
        if (isTeno && isLES()) {
            QMessageBox::warning(this, "TENO + LES Warning",
                "TENO5/TENO6 with LES is supported but extremely demanding:\n\n"
                "  • CFL < 0.2  (deltaT ~1e-6 for flight-scale cases)\n"
                "  • y⁺ < 1 required across the entire surface\n"
                "  • Cell count typically 50-100× a RANS mesh\n\n"
                "For external aerodynamics, TENO + RANS (simpleFoam) gives the\n"
                "accuracy benefit of high-order convection at a practical cost.");
        }
        // Re-enable LES option regardless (warning-only, not blocked)
        auto* model = qobject_cast<QStandardItemModel*>(m_simTypeCombo->model());
        if (model) {
            auto* lesItem = model->item(1);
            if (lesItem) lesItem->setEnabled(true);
        }
    });
    // Also warn when the user switches TO LES while TENO is already selected
    connect(m_simTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        bool isTeno = (m_divSchemeCombo->currentData().toString() != "linearUpwind");
        if (idx == 1 && isTeno) {   // idx 1 = LES
            QMessageBox::warning(this, "TENO + LES Warning",
                "TENO5/TENO6 with LES is supported but extremely demanding:\n\n"
                "  • CFL < 0.2  (deltaT ~1e-6 for flight-scale cases)\n"
                "  • y⁺ < 1 required across the entire surface\n"
                "  • Cell count typically 50-100× a RANS mesh\n\n"
                "RANS + TENO gives the same high-order accuracy benefit at\n"
                "a fraction of the cost for external aero.");
        }
    });

    connect(m_uxSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PhysicsPanel::updateRe);
    connect(m_uySpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PhysicsPanel::updateRe);
    connect(m_uzSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PhysicsPanel::updateRe);
    connect(m_nuSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PhysicsPanel::updateRe);
    connect(m_lengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PhysicsPanel::updateRe);

    onSolverChanged(0);
    loadFromCase();
    updateRe();
}

bool PhysicsPanel::isLES() const
{
    return m_simTypeCombo->currentIndex() == 1;
}

void PhysicsPanel::onSolverChanged(int)
{
    QString solver = m_solverCombo->currentData().toString();

    bool isLESsolver = solver.contains("pimple", Qt::CaseInsensitive)
                    || solver.contains("Pimple");
    bool isThermal   = solver.startsWith("buoyant") || solver == "rhoSimpleFoam";
    // Sync simType combo
    {
        QSignalBlocker b(m_simTypeCombo);
        m_simTypeCombo->setCurrentIndex(isLESsolver ? 1 : 0);
    }
    onSimTypeChanged(isLESsolver ? 1 : 0);

    m_thermalNote->setVisible(isThermal);
}

void PhysicsPanel::onSimTypeChanged(int idx)
{
    bool les = (idx == 1);

    // Sync solver combo to LES/RANS without re-entering onSolverChanged
    {
        QSignalBlocker b(m_solverCombo);
        QString cur = m_solverCombo->currentData().toString();
        bool curIsLES = cur.contains("pimple", Qt::CaseInsensitive);
        if (les && !curIsLES) {
            m_solverCombo->setCurrentIndex(3); // pimpleFoam
        } else if (!les && curIsLES) {
            m_solverCombo->setCurrentIndex(0); // simpleFoam
        }
    }

    m_modelCombo->blockSignals(true);
    m_modelCombo->clear();
    if (les) {
        m_modelCombo->addItems({
            "Smagorinsky", "dynamicKEqn", "WALE", "dynamicLagrangian"
        });
        emit solverRecommended(m_solverCombo->currentData().toString());
    } else {
        m_modelCombo->addItems({
            "kOmegaSST", "kEpsilon", "SpalartAllmaras", "laminar"
        });
        emit solverRecommended(m_solverCombo->currentData().toString());
    }
    m_modelCombo->blockSignals(false);
    onModelChanged(0);
}

void PhysicsPanel::onModelChanged(int)
{
    QString model = m_modelCombo->currentText();
    if (isLES()) {
        bool hasK = (model == "dynamicKEqn");
        m_turbBox->setVisible(hasK);
        m_omegaLabel->setVisible(false);
        m_omegaSpin->setVisible(false);
        m_epsilonLabel->setVisible(false);
        m_epsilonSpin->setVisible(false);
    } else {
        bool isLaminar = (model == "laminar");
        bool isKEps    = (model == "kEpsilon");
        m_turbBox->setVisible(!isLaminar);
        m_omegaLabel->setVisible(!isKEps);
        m_omegaSpin->setVisible(!isKEps);
        m_epsilonLabel->setVisible(isKEps);
        m_epsilonSpin->setVisible(isKEps);
    }
}

void PhysicsPanel::updateRe()
{
    double umag = std::sqrt(m_uxSpin->value()*m_uxSpin->value()
                          + m_uySpin->value()*m_uySpin->value()
                          + m_uzSpin->value()*m_uzSpin->value());
    double nu = m_nuSpin->value();
    double L  = m_lengthSpin->value();
    if (nu < 1e-15) { m_reLabel->setText("-"); return; }
    double Re = umag * L / nu;
    m_reLabel->setText(QString("%1").arg(Re, 0, 'g', 5));
}

// Read helpers

QString PhysicsPanel::readDictValue(const QString& filePath, const QString& key) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString txt = f.readAll();
    QRegularExpression re(key + R"(\s+([^\s;]+)\s*;)");
    auto m = re.match(txt);
    return m.hasMatch() ? m.captured(1) : QString{};
}

void PhysicsPanel::loadFromCase()
{
    if (m_caseDir.isEmpty()) return;

    migrateTenolibsToControlDict(m_caseDir);

    // Detect solver from controlDict application field first
    QString app = readDictValue(m_caseDir + "/system/controlDict", "application");
    if (!app.isEmpty()) {
        int idx = m_solverCombo->findData(app);
        if (idx >= 0) {
            QSignalBlocker b(m_solverCombo);
            m_solverCombo->setCurrentIndex(idx);
            m_thermalNote->setVisible(
                app.startsWith("buoyant") || app == "rhoSimpleFoam");
        }
    }

    // Detect sim type + model from momentumTransport or turbulenceProperties
    for (const QString& fname : {"momentumTransport", "turbulenceProperties"}) {
        QString simType = readDictValue(m_caseDir + "/constant/" + fname, "simulationType");
        if (simType == "LES") {
            QSignalBlocker b1(m_simTypeCombo), b2(m_solverCombo);
            m_simTypeCombo->setCurrentIndex(1);
            QString lesModel = readDictValue(m_caseDir + "/constant/" + fname, "LESModel");
            // Sync solver to LES variant if not already
            QString cur = m_solverCombo->currentData().toString();
            if (!cur.contains("pimple", Qt::CaseInsensitive))
                m_solverCombo->setCurrentIndex(3); // pimpleFoam

            m_modelCombo->blockSignals(true);
            m_modelCombo->clear();
            m_modelCombo->addItems({"Smagorinsky", "dynamicKEqn", "WALE", "dynamicLagrangian"});
            m_modelCombo->blockSignals(false);
            int midx = m_modelCombo->findText(lesModel);
            if (midx >= 0) m_modelCombo->setCurrentIndex(midx);
            break;
        }
        if (simType == "laminar") {
            QSignalBlocker b(m_simTypeCombo);
            m_simTypeCombo->setCurrentIndex(0);
            m_modelCombo->setCurrentIndex(m_modelCombo->findText("laminar"));
            break;
        }
        QString rasModel = readDictValue(m_caseDir + "/constant/" + fname, "model");
        if (!rasModel.isEmpty()) {
            QSignalBlocker b(m_simTypeCombo);
            m_simTypeCombo->setCurrentIndex(0);
            int idx = m_modelCombo->findText(rasModel);
            if (idx >= 0) m_modelCombo->setCurrentIndex(idx);
            break;
        }
    }

    // nu
    for (const QString& fname : {"transportProperties", "physicalProperties"}) {
        QString val = readDictValue(m_caseDir + "/constant/" + fname, "nu");
        if (!val.isEmpty()) {
            bool ok; double v = val.toDouble(&ok);
            if (ok) m_nuSpin->setValue(v);
            break;
        }
    }

    // Inlet velocity from 0/U
    QFile uf(m_caseDir + "/0/U");
    if (uf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString txt = uf.readAll();
        QRegularExpression re(R"(uniform\s*\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))");
        auto m = re.match(txt);
        if (m.hasMatch()) {
            m_uxSpin->setValue(m.captured(1).toDouble());
            m_uySpin->setValue(m.captured(2).toDouble());
            m_uzSpin->setValue(m.captured(3).toDouble());
        }
    }

    // Div scheme from system/fvSchemes
    QFile fvf(m_caseDir + "/system/fvSchemes");
    if (fvf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString txt = fvf.readAll();
        // Look for div(phi,U) line
        QRegularExpression re(R"(div\(phi,U\)\s+Gauss\s+(\w+))");
        auto m = re.match(txt);
        if (m.hasMatch()) {
            QString s = m.captured(1);
            int idx = m_divSchemeCombo->findData(s);
            if (idx >= 0) m_divSchemeCombo->setCurrentIndex(idx);
        }
    }

    onModelChanged(m_modelCombo->currentIndex());
    loadCakeProperties();
    updateRe();
}

void PhysicsPanel::loadCakeProperties()
{
    QString path = m_caseDir + "/system/cakeProperties";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith("//") || line.isEmpty()) continue;
        QStringList p = line.split(QRegularExpression("\\s+"));
        if (p.size() < 2) continue;
        double val = p[1].remove(';').toDouble();
        if      (p[0] == "rho")  m_rhoSpin ->setValue(val);
        else if (p[0] == "Aref") m_arefSpin->setValue(val);
        else if (p[0] == "Lref") m_lengthSpin->setValue(val);
    }
}

void PhysicsPanel::writeCakeProperties() const
{
    QDir(m_caseDir + "/system").mkpath(".");
    QFile f(m_caseDir + "/system/cakeProperties");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&f);
    o << "// Cake Studio reference quantities\n\n";
    o << "rho   " << m_rhoSpin ->value() << ";\n";
    o << "Aref  " << m_arefSpin->value() << ";\n";
    o << "Lref  " << m_lengthSpin->value() << ";\n";
    o << "Uref  0;\n";   // 0 = auto from inlet
    f.close();

    // Patch rhoInf in controlDict forces function object
    QString cdPath = m_caseDir + "/system/controlDict";
    QFile cd(cdPath);
    if (cd.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString txt = cd.readAll();
        cd.close();
        QString rhoStr = QString::number(m_rhoSpin->value(), 'g', 8);
        txt.replace(QRegularExpression(R"(rhoInf\s+[\d.eE+\-]+\s*;)"),
                    QString("rhoInf          %1;").arg(rhoStr));
        if (cd.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&cd) << txt;
    }
}

// Write helpers

bool PhysicsPanel::writeMomentumTransport()
{
    QString model = m_modelCombo->currentText();
    bool laminar  = (model == "laminar");

    QString content;
    QTextStream ts(&content);
    ts << "FoamFile\n{\n"
       << "    version     2.0;\n"
       << "    format      ascii;\n"
       << "    class       dictionary;\n"
       << "    location    \"constant\";\n"
       << "    object      momentumTransport;\n"
       << "}\n\n";

    if (laminar) {
        ts << "simulationType  laminar;\n";
    } else if (isLES()) {
        ts << "simulationType  LES;\n\n"
           << "LES\n{\n"
           << "    LESModel     " << model << ";\n"
           << "    turbulence   on;\n"
           << "    printCoeffs  on;\n"
           << "    delta        cubeRootVol;\n"
           << "    cubeRootVolCoeffs { deltaCoeff 1; }\n"
           << "}\n";
    } else {
        ts << "simulationType  RAS;\n\n"
           << "RAS\n{\n"
           << "    model       " << model << ";\n"
           << "    turbulence  on;\n"
           << "    printCoeffs on;\n"
           << "}\n";
    }

    QFile f(m_caseDir + "/constant/momentumTransport");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&f) << content;
    return true;
}

bool PhysicsPanel::writeTransportProperties()
{
    QString nu = QString::number(m_nuSpin->value(), 'e', 6);
    QString content;
    QTextStream ts(&content);
    ts << "FoamFile\n{\n"
       << "    version 2.0;\n    format ascii;\n"
       << "    class dictionary;\n    object transportProperties;\n"
       << "}\n\n"
       << "transportModel  Newtonian;\n"
       << "nu  " << nu << ";\n";

    QFile f(m_caseDir + "/constant/transportProperties");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&f) << content;
    return true;
}

bool PhysicsPanel::writeInletVelocity()
{
    QString path = m_caseDir + "/0/U";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QString txt = f.readAll();
    f.close();

    QString newVec = QString("(%1 %2 %3)")
        .arg(m_uxSpin->value()).arg(m_uySpin->value()).arg(m_uzSpin->value());

    QRegularExpression re(R"((uniform\s*\()[\d.eE+\-]+\s+[\d.eE+\-]+\s+[\d.eE+\-]+(\s*\)))");
    txt.replace(re, "uniform " + newVec);

    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&f) << txt;
    return true;
}

// fvSchemes builder

QString PhysicsPanel::buildFvSchemes(const QString& solver, const QString& scheme)
{
    QString divU;
    if (scheme == "teno")       divU = "Gauss teno";
    else if (scheme == "teno6") divU = "Gauss teno6";
    else                        divU = "Gauss linearUpwindV grad(U)";

    bool lesMode   = solver.contains("pimple", Qt::CaseInsensitive);
    bool isThermal = solver.startsWith("buoyant") || solver == "rhoSimpleFoam";
    bool isTeno    = (scheme == "teno" || scheme == "teno6");

    QString content;
    QTextStream o(&content);

    o << "FoamFile\n{\n"
      << "    version     2.0;\n"
      << "    format      ascii;\n"
      << "    class       dictionary;\n"
      << "    object      fvSchemes;\n"
      << "}\n\n";

    o << "ddtSchemes\n{\n";
    if (lesMode) o << "    default         Euler;\n";
    else         o << "    default         steadyState;\n";
    o << "}\n\n";

    o << "gradSchemes\n{\n"
      << "    default         Gauss linear;\n"
      << "    grad(U)         Gauss linear;\n"
      << "    grad(p)         Gauss linear;\n"
      << "}\n\n";

    o << "divSchemes\n{\n"
      << "    default         none;\n"
      << "    div(phi,U)      " << divU << ";\n";

    if (lesMode) {
        o << "    div(phi,k)      Gauss upwind;\n"
          << "    div(phi,B)      Gauss upwind;\n"
          << "    div(B)          Gauss linear;\n"
          << "    div(phi,K)      Gauss upwind;\n"
          << "    div(phi,nuTilda) Gauss upwind;\n";
    } else {
        o << "    div(phi,k)      Gauss upwind;\n"
          << "    div(phi,omega)  Gauss upwind;\n"
          << "    div(phi,epsilon) Gauss upwind;\n"
          << "    div(phi,nuTilda) Gauss upwind;\n";
    }

    if (isThermal) {
        o << "    div(phi,h)      Gauss upwind;\n"
          << "    div(phi,e)      Gauss upwind;\n"
          << "    div(phi,K)      Gauss upwind;\n"
          << "    div(((rho*nuEff)*dev2(T(grad(U))))) Gauss linear;\n";
    } else {
        o << "    div((nuEff*dev(T(grad(U))))) Gauss linear;\n";
    }

    o << "}\n\n";

    o << "laplacianSchemes\n{\n"
      << "    default         Gauss linear corrected;\n"
      << "}\n\n";

    o << "interpolationSchemes\n{\n"
      << "    default         linear;\n"
      << "}\n\n";

    o << "snGradSchemes\n{\n"
      << "    default         corrected;\n"
      << "}\n";

    if (lesMode) {
        o << "\nwallDist\n{\n"
          << "    method meshWave;\n"
          << "}\n";
    }

    return content;
}

bool PhysicsPanel::writeDivScheme()
{
    QString scheme = m_divSchemeCombo->currentData().toString();
    QString solver = m_solverCombo->currentData().toString();
    QString path   = m_caseDir + "/system/fvSchemes";

    QDir(m_caseDir + "/system").mkpath(".");

    // If fvSchemes doesn't exist yet, write a complete template
    if (!QFile::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream(&f) << buildFvSchemes(solver, scheme);
        return true;
    }

    // Patch existing file: replace div(phi,U) line and libs entry
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QString txt = f.readAll();
    f.close();

    // Patch div(phi,U) line
    QString divLine;
    if (scheme == "teno")
        divLine = "    div(phi,U)      Gauss teno;";
    else if (scheme == "teno6")
        divLine = "    div(phi,U)      Gauss teno6;";
    else
        divLine = "    div(phi,U)      Gauss linearUpwindV grad(U);";

    txt.replace(QRegularExpression(R"([ \t]*div\(phi,U\)[^\n]+)"), divLine);

    // Remove any stale libs line that may have been written by an older version
    txt.replace(QRegularExpression(R"(\nlibs\s*\(\s*"libtenoScheme\.so"\s*\)\s*;\n?)"), "\n");

    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&f) << txt;
    f.close();

    // libs must live in controlDict (not fvSchemes) to be loaded by OF 2412.
    bool isTeno = (scheme == "teno" || scheme == "teno6");
    QString cdPath = m_caseDir + "/system/controlDict";
    QFile cd(cdPath);
    if (cd.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString cdTxt = cd.readAll();
        cd.close();
        bool hasLib = cdTxt.contains("libtenoScheme.so");
        if (isTeno && !hasLib) {
            // Append a libs entry (or insert into existing one)
            if (cdTxt.contains(QRegularExpression(R"(\blibs\s*\()"))) {
                cdTxt.replace(QRegularExpression(R"(\blibs\s*\()"), "libs\n(\n    \"libtenoScheme.so\"\n    ");
            } else {
                cdTxt += "\nlibs (\"libtenoScheme.so\");\n";
            }
        } else if (!isTeno && hasLib) {
            cdTxt.replace(QRegularExpression(R"(\s*"libtenoScheme\.so"\n?)"), "");
            cdTxt.replace(QRegularExpression(R"(\blibs\s*\(\s*\)\s*;)"), "");
        }
        if (cd.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&cd) << cdTxt;
    }
    return true;
}

bool PhysicsPanel::writeControlDictTimeSettings()
{
    QString cdPath = m_caseDir + "/system/controlDict";
    QFile f(cdPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QString txt = f.readAll();
    f.close();

    QString scheme = m_divSchemeCombo->currentData().toString(); // "linearUpwind" | "teno" | "teno6"
    QString solver = m_solverCombo->currentData().toString();
    bool les    = isLES();
    bool steady = !solver.contains("pimple", Qt::CaseInsensitive);
    bool teno   = (scheme == "teno" || scheme == "teno6");

    // Determine time-stepping parameters
    // RANS: pseudo-time, no CFL control needed
    // LES + linearUpwind: standard Courant control, maxCo 0.5
    // LES + TENO5/6: tighter Courant needed, maxCo 0.4 (lower numerical dissipation)
    QString deltaT, endTime, writeControl, writeInterval, adjustTS, maxCo, maxDeltaT;

    if (steady) {
        deltaT        = "1";
        endTime       = "1000";
        adjustTS      = "no";
        maxCo         = "0.5";
        maxDeltaT     = "1";
        writeControl  = "timeStep";
        writeInterval = "100";
    } else if (teno) {
        // LES + TENO5 or TENO6: low dissipation → tight CFL
        deltaT        = "1e-5";
        endTime       = "0.5";
        adjustTS      = "yes";
        maxCo         = "0.4";
        maxDeltaT     = "5e-5";
        writeControl  = "adjustableRunTime";
        writeInterval = "0.05";
    } else {
        // LES + linearUpwind: standard transient
        deltaT        = "1e-4";
        endTime       = "0.5";
        adjustTS      = "yes";
        maxCo         = "0.5";
        maxDeltaT     = "2e-4";
        writeControl  = "adjustableRunTime";
        writeInterval = "0.05";
    }

    auto patch = [&](const QString& key, const QString& val) {
        txt.replace(QRegularExpression("(" + key + R"(\s+)[^\s;]+(\s*;))"),
                    key + "          " + val + ";");
    };

    patch("deltaT",         deltaT);
    patch("adjustTimeStep", adjustTS);
    patch("maxCo",          maxCo);
    patch("maxDeltaT",      maxDeltaT);
    patch("writeControl",   writeControl);
    patch("writeInterval",  writeInterval);
    // Always force startTime=0 so transient runs never start from a stale/diverged snapshot
    patch("startFrom",      "startTime");

    // If adjustTimeStep / maxCo / maxDeltaT don't exist yet, insert before runTimeModifiable
    if (!txt.contains("adjustTimeStep"))
        txt.replace("runTimeModifiable",
                    "adjustTimeStep  " + adjustTS  + ";\n"
                    "maxCo           " + maxCo     + ";\n"
                    "maxDeltaT       " + maxDeltaT + ";\n\n"
                    "runTimeModifiable");

    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&f) << txt;
    f.close();

    // Patch fvSchemes: ddtScheme + gradient scheme.
    // Use line-level replacement so we never accidentally touch other one-liner blocks.
    QString schemesPath = m_caseDir + "/system/fvSchemes";
    QFile sf(schemesPath);
    if (sf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString stxt = sf.readAll();
        sf.close();

        // ddtSchemes: `backward` for LES, `steadyState` for RANS
        QString ddt = steady ? "steadyState" : "backward";
        int pos = stxt.indexOf("ddtSchemes");
        if (pos >= 0) {
            int lineEnd = stxt.indexOf('\n', pos);
            if (lineEnd < 0) lineEnd = stxt.length();
            stxt.replace(pos, lineEnd - pos, "ddtSchemes      { default " + ddt + "; }");
        }

        // gradSchemes: TENO needs cellLimited to prevent gradient overshoots;
        // linearUpwind is stable with plain Gauss linear
        QString grad = teno ? "cellLimited Gauss linear 1" : "Gauss linear";
        pos = stxt.indexOf("gradSchemes");
        if (pos >= 0) {
            int lineEnd = stxt.indexOf('\n', pos);
            if (lineEnd < 0) lineEnd = stxt.length();
            stxt.replace(pos, lineEnd - pos, "gradSchemes     { default " + grad + "; }");
        }

        if (sf.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&sf) << stxt;
    }

    // Patch fvSolution relaxation: TENO needs tighter factors than linearUpwind
    QString solPath = m_caseDir + "/system/fvSolution";
    QFile solF(solPath);
    if (solF.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString stxt = solF.readAll();
        solF.close();
        if (teno) {
            stxt.replace(QRegularExpression(R"(\bfields\s*\{[^}]+\})"),
                         "fields      { p 0.2; }");
            stxt.replace(QRegularExpression(R"(\bequations\s*\{[^}]+\})"),
                         "equations   { U 0.3; k 0.4; omega 0.4; epsilon 0.4; nuTilda 0.4; }");
        } else {
            stxt.replace(QRegularExpression(R"(\bfields\s*\{[^}]+\})"),
                         "fields      { p 0.3; }");
            stxt.replace(QRegularExpression(R"(\bequations\s*\{[^}]+\})"),
                         "equations   { U 0.5; k 0.5; omega 0.5; epsilon 0.5; nuTilda 0.5; }");
        }
        if (solF.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&solF) << stxt;
    }

    return true;
}

// Ensures UFinal, pFinal, and turbulence Final entries exist in fvSolution/solvers.
// Patches the live file in-place: safe to call on old cases that predate the template fix.
static void patchFvSolutionFinals(const QString& caseDir)
{
    QString path = caseDir + "/system/fvSolution";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString txt = f.readAll();
    f.close();

    bool changed = false;

    // Helper: insert `insertion` immediately after the closing `}` of the block that starts with `key`
    auto insertAfterBlock = [&](const QString& key, const QString& insertion) {
        QRegularExpression re("\\b" + key + "\\s*\\{[^}]+\\}");
        auto m = re.match(txt);
        if (m.hasMatch()) {
            txt.insert(m.capturedEnd(0), "\n" + insertion);
            changed = true;
        }
    };

    if (!txt.contains("UFinal"))
        insertAfterBlock("U", "    UFinal  { $U; relTol 0; }");

    if (!txt.contains("pFinal"))
        insertAfterBlock("p", "    pFinal  { $p; relTol 0; }");

    if (!txt.contains("kFinal")) {
        // Turbulence finals: append after the last solver block before the closing }
        int closingBrace = txt.lastIndexOf(QRegularExpression("\\}\\s*\\}"));
        if (closingBrace > 0) {
            txt.insert(closingBrace,
                "    \"(kFinal|omegaFinal|epsilonFinal|nuTildaFinal)\"\n"
                "        { solver smoothSolver; smoother GaussSeidel;"
                " tolerance 1e-8; relTol 0; }\n");
            changed = true;
        }
    }

    if (changed) {
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&f) << txt;
    }
}

void PhysicsPanel::onApply()
{
    if (m_caseDir.isEmpty()) {
        QMessageBox::warning(this, "No Case", "Open a case directory first.");
        return;
    }

    patchFvSolutionFinals(m_caseDir);

    QStringList errors;
    if (!writeMomentumTransport())          errors << "momentumTransport";
    if (!writeTransportProperties())        errors << "transportProperties";
    if (!writeInletVelocity())              errors << "0/U";
    if (!writeDivScheme())                  errors << "fvSchemes";
    if (!writeControlDictTimeSettings())    errors << "controlDict (time)";

    // Fire citation for selected model
    auto& cm = CitationManager::instance();
    QString model = m_modelCombo->currentText().toLower();
    if (isLES()) {
        if (model == "smagorinsky")       cm.cite("smagorinsky",       m_caseDir);
        if (model == "dynamickEqn")       cm.cite("dynamickEqn",       m_caseDir);
        if (model == "wale")              cm.cite("wale",               m_caseDir);
        if (model == "dynamiclagrangian") cm.cite("dynamiclagrangian",  m_caseDir);
    } else {
        if (model == "komegasst")         cm.cite("komegasst",          m_caseDir);
        if (model == "kepsilon")          cm.cite("kepsilon",           m_caseDir);
    }

    writeCakeProperties();

    // Patch controlDict application field with selected solver
    QString solver = m_solverCombo->currentData().toString();
    QString cdPath = m_caseDir + "/system/controlDict";
    QFile cdf(cdPath);
    if (cdf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString txt = cdf.readAll();
        cdf.close();
        txt.replace(QRegularExpression(R"(\bapplication\s+\w+\s*;)"),
                    "application    " + solver + ";");
        if (cdf.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&cdf) << txt;
    }

    // Emit so MainWindow toolbar / run button can pick up the right solver
    emit solverRecommended(solver);

    if (!errors.isEmpty())
        QMessageBox::warning(this, "Write Error",
            "Could not write: " + errors.join(", ") + "\nCheck case directory.");
    else
        QMessageBox::information(this, "Applied",
            "Physics settings written to case.\nRerun the solver to apply.");
}

// project save / restore

static QString readFirstMatch(const QString& path, const QRegularExpression& re)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    auto m = re.match(f.readAll());
    return m.hasMatch() ? m.captured(1).trimmed() : QString{};
}

QJsonObject PhysicsPanel::snapshotFromCase(const QString& caseDir)
{
    QJsonObject o;

    // solver (controlDict application field)
    o["solver"] = readFirstMatch(caseDir + "/system/controlDict",
                                 QRegularExpression(R"(\bapplication\s+(\w+)\s*;)"));

    // div scheme
    o["div_scheme"] = readFirstMatch(caseDir + "/system/fvSchemes",
                                     QRegularExpression(R"(div\(phi,U\)\s+Gauss\s+(\w+))"));

    // turbulence model
    o["turb_model"] = readFirstMatch(caseDir + "/constant/momentumTransport",
                                     QRegularExpression(R"(\bmodel\s+(\w+)\s*;)"));

    // nu from transportProperties
    o["nu"] = readFirstMatch(caseDir + "/constant/transportProperties",
                             QRegularExpression(R"(\bnu\s+\[.*?\]\s+([\d.eE+\-]+))"))
              .toDouble();

    // rho, Aref, Lref from cakeProperties
    auto readCakeProp = [&](const QString& key) -> double {
        return readFirstMatch(caseDir + "/system/cakeProperties",
                              QRegularExpression(key + R"(\s+([\d.eE+\-]+)\s*;)")).toDouble();
    };
    o["rho"]   = readCakeProp("rho");
    o["a_ref"] = readCakeProp("Aref");
    o["l_ref"] = readCakeProp("Lref");

    // inlet velocity from 0/U (inlet patch)
    {
        QFile f(caseDir + "/0/U");
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString txt = f.readAll();
            QRegularExpression re(R"(inlet\b[^}]*value\s+uniform\s+\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))");
            auto m = re.match(txt);
            if (m.hasMatch()) {
                o["ux"] = m.captured(1).toDouble();
                o["uy"] = m.captured(2).toDouble();
                o["uz"] = m.captured(3).toDouble();
            }
        }
    }

    return o;
}

void PhysicsPanel::restoreToCase(const QString& caseDir, const QJsonObject& o)
{
    if (caseDir.isEmpty() || o.isEmpty()) return;

    // Apply div scheme + libs to fvSchemes
    QString scheme = o["div_scheme"].toString();
    QString solver = o["solver"].toString();
    if (!scheme.isEmpty()) {
        QString path = caseDir + "/system/fvSchemes";
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString txt = f.readAll();
            f.close();

            QString divLine;
            if (scheme == "teno")       divLine = "    div(phi,U)      Gauss teno;";
            else if (scheme == "teno6") divLine = "    div(phi,U)      Gauss teno6;";
            else                        divLine = "    div(phi,U)      Gauss linearUpwindV grad(U);";
            txt.replace(QRegularExpression(R"([ \t]*div\(phi,U\)[^\n]+)"), divLine);

            // Remove any stale libs line from fvSchemes
            txt.replace(QRegularExpression(R"(\nlibs\s*\(\s*"libtenoScheme\.so"\s*\)\s*;\n?)"), "\n");

            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                QTextStream(&f) << txt;
        }

        // Patch libs in controlDict
        bool isTeno = (scheme == "teno" || scheme == "teno6");
        QString cdPath = caseDir + "/system/controlDict";
        QFile cd(cdPath);
        if (cd.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString cdTxt = cd.readAll();
            cd.close();
            bool hasLib = cdTxt.contains("libtenoScheme.so");
            if (isTeno && !hasLib) {
                if (cdTxt.contains(QRegularExpression(R"(\blibs\s*\()")))
                    cdTxt.replace(QRegularExpression(R"(\blibs\s*\()"), "libs\n(\n    \"libtenoScheme.so\"\n    ");
                else
                    cdTxt += "\nlibs (\"libtenoScheme.so\");\n";
            } else if (!isTeno && hasLib) {
                cdTxt.replace(QRegularExpression(R"(\s*"libtenoScheme\.so"\n?)"), "");
                cdTxt.replace(QRegularExpression(R"(\blibs\s*\(\s*\)\s*;)"), "");
            }
            if (cd.open(QIODevice::WriteOnly | QIODevice::Text))
                QTextStream(&cd) << cdTxt;
        }
    }

    // Restore solver in controlDict
    if (!solver.isEmpty()) {
        QFile f(caseDir + "/system/controlDict");
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString txt = f.readAll();
            f.close();
            txt.replace(QRegularExpression(R"(\bapplication\s+\w+\s*;)"),
                        "application    " + solver + ";");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                QTextStream(&f) << txt;
        }
    }

    // Restore rho/Aref/Lref in cakeProperties
    {
        QDir(caseDir + "/system").mkpath(".");
        QFile f(caseDir + "/system/cakeProperties");
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream o2(&f);
            o2 << "// Cake Studio reference quantities\n\n";
            o2 << "rho   " << o["rho"].toDouble(1.225) << ";\n";
            o2 << "Aref  " << o["a_ref"].toDouble()    << ";\n";
            o2 << "Lref  " << o["l_ref"].toDouble(1.0) << ";\n";
            o2 << "Uref  0;\n";
        }
    }

    // Restore inlet velocity in 0/U
    if (o.contains("ux")) {
        QFile f(caseDir + "/0/U");
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString txt = f.readAll();
            f.close();
            QString val = QString("(%1 %2 %3)")
                .arg(o["ux"].toDouble()).arg(o["uy"].toDouble()).arg(o["uz"].toDouble());
            txt.replace(QRegularExpression(R"((inlet\b[^}]*value\s+uniform\s+)\([^)]+\))"),
                        "\\1" + val);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                QTextStream(&f) << txt;
        }
    }
}

void PhysicsPanel::migrateTenolibsToControlDict(const QString& caseDir)
{
    QString fvPath = caseDir + "/system/fvSchemes";
    QString cdPath = caseDir + "/system/controlDict";

    QFile fvf(fvPath);
    if (!fvf.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString fvTxt = fvf.readAll();
    fvf.close();

    if (!fvTxt.contains("libtenoScheme.so")) return;  // nothing to migrate

    // Strip the stale libs line from fvSchemes
    fvTxt.replace(QRegularExpression(R"(\nlibs\s*\(\s*"libtenoScheme\.so"\s*\)\s*;\n?)"), "\n");
    if (fvf.open(QIODevice::WriteOnly | QIODevice::Text))
        QTextStream(&fvf) << fvTxt;

    // Add it to controlDict if not already there
    QFile cdf(cdPath);
    if (!cdf.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString cdTxt = cdf.readAll();
    cdf.close();

    if (cdTxt.contains("libtenoScheme.so")) return;  // already correct

    if (cdTxt.contains(QRegularExpression(R"(\blibs\s*\()")))
        cdTxt.replace(QRegularExpression(R"(\blibs\s*\()"),
                      "libs\n(\n    \"libtenoScheme.so\"\n    ");
    else
        cdTxt += "\nlibs (\"libtenoScheme.so\");\n";

    if (cdf.open(QIODevice::WriteOnly | QIODevice::Text))
        QTextStream(&cdf) << cdTxt;
}
