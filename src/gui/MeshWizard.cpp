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

#include "FileBrowserDialog.h"
#include "MeshWizard.h"
#include "CADGeometry.h"
#include "CaseGenerator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QProcess>
#include <QDateTime>
#include <QApplication>
#include <QDir>
#include <QFileInfo>

// helpers
static QDoubleSpinBox* dblSpin(double val, double lo, double hi, double step,
                                int dec = 4) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi); s->setSingleStep(step);
    s->setDecimals(dec); s->setValue(val);
    return s;
}
static QSpinBox* intSpin(int val, int lo, int hi) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi); s->setValue(val);
    return s;
}

MeshWizard::MeshWizard(QWidget* parent, const QString& projectDir) : QDialog(parent)
{
    setWindowTitle("Cake CFD: Mesh Setup Wizard");
    setMinimumSize(700, 650);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto* tabs = new QTabWidget;
    buildGeometryTab(tabs);
    buildMeshTab(tabs);
    buildPhysicsTab(tabs);
    root->addWidget(tabs, 1);

    buildBottomBar();
    root->addWidget(m_log);

    // status + buttons row
    auto* bar = new QHBoxLayout;
    bar->addWidget(m_status, 1);
    bar->addWidget(m_btnGenerate);
    bar->addWidget(m_btnRunMesh);
    bar->addWidget(m_btnRunSolver);
    root->addLayout(bar);

    // Pre-fill output dir and case name from the active project directory
    if (!projectDir.isEmpty()) {
        QString norm = QDir(projectDir).absolutePath(); // strip trailing slash
        QFileInfo fi(norm);
        m_outputDir->setText(fi.absolutePath());   // parent dir
        m_caseName->setText(fi.fileName());         // project folder name = case name
    }
}

// Geometry tab
void MeshWizard::buildGeometryTab(QTabWidget* tabs)
{
    auto* w   = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setSpacing(10);

    // File picker row
    auto* row = new QHBoxLayout;
    m_geoPath = new QLineEdit;
    m_geoPath->setPlaceholderText("Path to STEP / IGES / BREP file…");
    auto* browseBtn = new QPushButton("Browse…");
    auto* loadBtn   = new QPushButton("Load");
    loadBtn->setDefault(false);
    row->addWidget(m_geoPath, 1);
    row->addWidget(browseBtn);
    row->addWidget(loadBtn);
    lay->addLayout(row);

    // Stats area
    auto* statsBox = new QGroupBox("Geometry Info");
    auto* statsLay = new QVBoxLayout(statsBox);
    m_geoStats = new QLabel("No geometry loaded.");
    m_geoStats->setTextFormat(Qt::RichText);
    m_geoStats->setWordWrap(true);
    m_geoStats->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    statsLay->addWidget(m_geoStats);
    lay->addWidget(statsBox, 1);

    connect(browseBtn, &QPushButton::clicked, this, &MeshWizard::onBrowseGeometry);
    connect(loadBtn,   &QPushButton::clicked, this, &MeshWizard::onLoadGeometry);

    tabs->addTab(w, "① Geometry");
}

// Mesh tab
void MeshWizard::buildMeshTab(QTabWidget* tabs)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* w   = new QWidget;
    auto* lay = new QVBoxLayout(w);

    // Background mesh
    auto* bgBox  = new QGroupBox("Background Mesh");
    auto* bgForm = new QFormLayout(bgBox);
    m_padding  = dblSpin(2.0, 0.1, 20.0, 0.5, 2);
    m_bgCells  = intSpin(20, 5, 200);
    bgForm->addRow("Domain padding (×bbox):", m_padding);
    bgForm->addRow("Background cells / axis:", m_bgCells);

    // Surface refinement
    auto* refBox  = new QGroupBox("Surface Refinement");
    auto* refForm = new QFormLayout(refBox);
    m_maxGlobal    = intSpin(2000000, 100000, 50000000);
    m_maxLocal     = intSpin(100000,  10000,  5000000);
    m_refMin       = intSpin(2, 0, 8);
    m_refMax       = intSpin(3, 0, 8);
    m_featureAngle = intSpin(30, 10, 90);
    refForm->addRow("Max global cells:",       m_maxGlobal);
    refForm->addRow("Max local cells:",        m_maxLocal);
    refForm->addRow("Refinement level min:",   m_refMin);
    refForm->addRow("Refinement level max:",   m_refMax);
    refForm->addRow("Resolve feature angle °:",m_featureAngle);

    // Boundary layers
    auto* layBox  = new QGroupBox("Boundary Layers");
    auto* layForm = new QFormLayout(layBox);
    m_nLayers         = intSpin(2, 0, 10);
    m_layerExpansion  = dblSpin(1.2, 1.0, 3.0, 0.05, 2);
    m_firstLayerThick = dblSpin(0.3, 0.01, 1.0, 0.05, 3);
    m_minThickness    = dblSpin(0.1, 0.001, 0.5, 0.01, 3);
    layForm->addRow("Number of layers:",       m_nLayers);
    layForm->addRow("Expansion ratio:",        m_layerExpansion);
    layForm->addRow("First layer thickness:",  m_firstLayerThick);
    layForm->addRow("Min thickness:",          m_minThickness);

    lay->addWidget(bgBox);
    lay->addWidget(refBox);
    lay->addWidget(layBox);
    lay->addStretch();
    scroll->setWidget(w);
    tabs->addTab(scroll, "② Mesh");
}

// Physics tab
void MeshWizard::buildPhysicsTab(QTabWidget* tabs)
{
    auto* w    = new QWidget;
    auto* lay  = new QVBoxLayout(w);

    // Solver
    auto* solBox  = new QGroupBox("Solver");
    auto* solForm = new QFormLayout(solBox);
    m_solver = new QComboBox;
    m_solver->addItems({"simpleFoam", "icoFoam", "pimpleFoam", "rhoSimpleFoam"});
    m_turbulence = new QComboBox;
    m_turbulence->addItems({"kOmegaSST", "laminar", "SpalartAllmaras", "kEpsilon"});
    m_velocity = dblSpin(10.0, 0.001, 1000.0, 1.0, 3);
    m_nu       = dblSpin(1.5e-5, 1e-8, 1.0, 1e-6, 8);
    solForm->addRow("Solver:",            m_solver);
    solForm->addRow("Turbulence model:",  m_turbulence);
    solForm->addRow("Inflow velocity (m/s):", m_velocity);
    solForm->addRow("Kinematic viscosity ν:", m_nu);

    // Output
    auto* outBox  = new QGroupBox("Output");
    auto* outForm = new QFormLayout(outBox);
    m_caseName  = new QLineEdit("cfdCase");
    m_outputDir = new QLineEdit;
    m_outputDir->setPlaceholderText("/home/user/cfd_runs");
    auto* outBrowse = new QPushButton("Browse…");
    auto* outRow    = new QHBoxLayout;
    outRow->addWidget(m_outputDir, 1);
    outRow->addWidget(outBrowse);
    m_cores = intSpin(4, 1, 256);
    outForm->addRow("Case name:", m_caseName);
    outForm->addRow("Output directory:", outRow);
    outForm->addRow("CPU cores:", m_cores);

    connect(outBrowse, &QPushButton::clicked, this, &MeshWizard::onBrowseOutputDir);

    lay->addWidget(solBox);
    lay->addWidget(outBox);
    lay->addStretch();
    tabs->addTab(w, "③ Physics");
}

// Bottom bar
void MeshWizard::buildBottomBar()
{
    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(160);
    m_log->setFont(QFont("Monospace", 9));

    m_status      = new QLabel("Ready.");
    m_btnGenerate  = new QPushButton("Generate Case");
    m_btnRunMesh   = new QPushButton("Run Mesh");
    m_btnRunSolver = new QPushButton("Run Solver");

    m_btnRunMesh->setEnabled(false);
    m_btnRunSolver->setEnabled(false);

    connect(m_btnGenerate,  &QPushButton::clicked, this, &MeshWizard::onGenerateCase);
    connect(m_btnRunMesh,   &QPushButton::clicked, this, &MeshWizard::onRunMesh);
    connect(m_btnRunSolver, &QPushButton::clicked, this, &MeshWizard::onRunSolver);
}

// Slots
void MeshWizard::onBrowseGeometry()
{
    QString path = FileBrowserDialog::getOpenFileName(
        this, "Open CAD file", QString(),
        "CAD Files (*.step *.stp *.iges *.igs *.brep *.brp);;All Files (*)");
    if (!path.isEmpty()) m_geoPath->setText(path);
}

void MeshWizard::onLoadGeometry()
{
    QString path = m_geoPath->text().trimmed();
    if (path.isEmpty()) { log("No file selected."); return; }

    m_status->setText("Loading geometry…");
    m_geoStats->setText("Loading…");
    QApplication::processEvents();

    auto geo = CFD::CADGeometry::fromFile(path);
    if (!geo->isValid()) {
        m_geoStats->setText("<b style='color:red'>Failed:</b> " +
                            geo->importResult().errorMsg);
        m_status->setText("Load failed.");
        return;
    }

    m_geo = std::shared_ptr<CFD::CADGeometry>(geo.release());
    const auto& r = m_geo->importResult();
    auto b = m_geo->bounds();

    QString info =
        QString("<b>Solids:</b> %1 &nbsp; <b>Faces:</b> %2 &nbsp; "
                "<b>Volume:</b> %3 m³<br>"
                "<b>VTK points:</b> %4<br>"
                "<b>Bounds X:</b> [%5, %6]&nbsp; "
                "<b>Y:</b> [%7, %8]&nbsp; "
                "<b>Z:</b> [%9, %10]")
        .arg(r.numSolids).arg(r.numFaces)
        .arg(r.volume, 0, 'g', 5)
        .arg(m_geo->polyData()->GetNumberOfPoints())
        .arg(b[0],0,'f',3).arg(b[1],0,'f',3)
        .arg(b[2],0,'f',3).arg(b[3],0,'f',3)
        .arg(b[4],0,'f',3).arg(b[5],0,'f',3);

    m_geoStats->setText(info);
    m_status->setText("Geometry loaded OK.");
    log("Loaded: " + path);
}

void MeshWizard::onBrowseOutputDir()
{
    QString dir = FileBrowserDialog::getExistingDirectory(this, "Select output directory");
    if (!dir.isEmpty()) m_outputDir->setText(dir);
}

void MeshWizard::onGenerateCase()
{
    if (!m_geo || !m_geo->isValid()) {
        log("Load a geometry file first (① Geometry tab)."); return;
    }
    QString outDir  = m_outputDir->text().trimmed();
    QString caseName = m_caseName->text().trimmed();
    if (outDir.isEmpty() || caseName.isEmpty()) {
        log("Set case name and output directory (③ Physics tab)."); return;
    }

    setRunning(true);
    m_status->setText("Generating case…");

    CFD::CaseParams cp;
    cp.caseName       = caseName;
    cp.caseDir        = outDir + "/" + caseName;
    cp.meshPadding    = m_padding->value();
    cp.bgMeshCells    = m_bgCells->value();
    cp.nBoundaryLayers = m_nLayers->value();
    cp.inflowVelocity = m_velocity->value();
    cp.nu             = m_nu->value();
    // BlockMeshWriter puts the inlet on the X-max face, so the flow runs in -X
    cp.flowAxis       = "-X";

    // snappyHexMesh controls from the Mesh tab
    cp.maxGlobalCells       = m_maxGlobal->value();
    cp.maxLocalCells        = m_maxLocal->value();
    cp.surfaceRefinementMin = m_refMin->value();
    cp.surfaceRefinementMax = m_refMax->value();
    cp.resolveFeatureAngle  = m_featureAngle->value();
    cp.layerExpansionRatio  = m_layerExpansion->value();
    cp.firstLayerThickness  = m_firstLayerThick->value();
    cp.minLayerThickness    = m_minThickness->value();

    static const QMap<QString,CFD::SolverType> solvers = {
        {"simpleFoam",   CFD::SolverType::SimpleFoam},
        {"icoFoam",      CFD::SolverType::IcoFoam},
        {"pimpleFoam",   CFD::SolverType::PimpleFoam},
        {"rhoSimpleFoam",CFD::SolverType::RhoSimpleFoam},
    };
    static const QMap<QString,CFD::TurbulenceModel> turbs = {
        {"laminar",        CFD::TurbulenceModel::Laminar},
        {"SpalartAllmaras",CFD::TurbulenceModel::SpalartAllmaras},
        {"kOmegaSST",      CFD::TurbulenceModel::KOmegaSST},
        {"kEpsilon",       CFD::TurbulenceModel::KEpsilon},
    };
    cp.solver     = solvers.value(m_solver->currentText(),     CFD::SolverType::SimpleFoam);
    cp.turbulence = turbs.value(m_turbulence->currentText(), CFD::TurbulenceModel::KOmegaSST);

    auto res = CFD::CaseGenerator::generate(*m_geo, cp);
    if (!res.success) {
        log("ERROR: " + res.errorMsg);
        m_status->setText("Case generation failed.");
        setRunning(false);
        return;
    }

    m_caseDir = res.caseDir;
    log("Case written: " + m_caseDir);
    m_status->setText("Case ready. Click Run Mesh.");
    m_btnRunMesh->setEnabled(true);
    setRunning(false);
}

void MeshWizard::onRunMesh()
{
    if (m_caseDir.isEmpty()) { log("Generate a case first."); return; }
    setRunning(true);
    m_btnRunMesh->setEnabled(false);

    QString base = shellPrefix(m_caseDir);

    const QStringList steps = { "surfaceFeatureExtract", "blockMesh",
                                "snappyHexMesh -overwrite", "checkMesh" };
    for (const QString& step : steps) {
        log(" " + step + " ");
        if (!runCommand(base + step + " 2>&1")) {
            m_status->setText("Mesh failed at " + step.section(' ', 0, 0) + ".");
            m_btnRunMesh->setEnabled(true);
            setRunning(false);
            return;
        }
    }

    m_status->setText("Mesh complete.");
    m_btnRunSolver->setEnabled(true);
    setRunning(false);
}

void MeshWizard::onRunSolver()
{
    if (m_caseDir.isEmpty()) return;
    setRunning(true);
    m_btnRunSolver->setEnabled(false);

    QString solverBin = m_solver->currentText();
    QString base = shellPrefix(m_caseDir);

    log(" " + solverBin + " ");
    const bool ok = runCommand(base + solverBin + " 2>&1");

    m_status->setText(ok ? "Solver finished." : "Solver failed.");
    m_btnRunSolver->setEnabled(true);
    setRunning(false);
}

// helpers
void MeshWizard::log(const QString& msg)
{
    m_log->append("[" + QDateTime::currentDateTime().toString("hh:mm:ss") + "] " + msg);
    QApplication::processEvents();
}

bool MeshWizard::runCommand(const QString& cmd)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    // Pass the command as a single argv entry because QProcess::start(QString) splits
    // on whitespace and only honours double quotes, which breaks bash -c '...'.
    proc.start("bash", { "-c", cmd });
    if (!proc.waitForStarted(5000)) {
        log("  [FAILED] could not start bash: " + proc.errorString());
        return false;
    }

    // Keep partial lines between reads so output split across chunks is not mangled
    QString pending;
    auto drain = [&](bool flushAll) {
        pending += QString::fromLocal8Bit(proc.readAllStandardOutput());
        int nl;
        while ((nl = pending.indexOf('\n')) >= 0) {
            const QString line = pending.left(nl);
            pending.remove(0, nl + 1);
            if (!line.trimmed().isEmpty()) m_log->append("  " + line);
        }
        if (flushAll && !pending.trimmed().isEmpty()) {
            m_log->append("  " + pending);
            pending.clear();
        }
    };

    // Poll until the process exits; a quiet stretch (snappy, long solver
    // iterations) must not end the loop, and the GUI stays responsive.
    while (proc.state() != QProcess::NotRunning) {
        proc.waitForReadyRead(100);
        drain(false);
        QApplication::processEvents();
    }
    proc.waitForFinished(-1);
    drain(true);

    const bool ok = (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0);
    log(ok ? "  [OK]" : "  [FAILED] exit=" + QString::number(proc.exitCode()));
    return ok;
}

QString MeshWizard::shellPrefix(const QString& caseDir)
{
    // Single-quote the case dir for bash; an embedded ' becomes '\''
    QString q = caseDir;
    q.replace("'", "'\\''");
    return "source /usr/lib/openfoam/openfoam2412/etc/bashrc 2>/dev/null && cd '"
           + q + "' && ";
}

void MeshWizard::setRunning(bool on)
{
    m_btnGenerate->setEnabled(!on);
    QApplication::processEvents();
}
