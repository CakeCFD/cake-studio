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
#include "themes/ThemeEngine.h"
#include <QDir>
#include <QFileInfo>
#include "CakeMeshPanel.h"
#include "ViewportWidget.h"
#include "CADGeometry.h"
#include "CaseGenerator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QFile>
#include <QDateTime>
#include <QApplication>
#include <QSettings>
#include <QTimer>
#include <QFont>
#include <QClipboard>
#include <QToolTip>

// spin helpers

static QDoubleSpinBox* dbl(double val, double lo, double hi, double step, int dec = 4) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi); s->setSingleStep(step);
    s->setDecimals(dec); s->setValue(val);
    return s;
}
static QSpinBox* isp(int val, int lo, int hi) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi); s->setValue(val);
    return s;
}

// construction

CakeMeshPanel::CakeMeshPanel(QWidget* parent) : QWidget(parent)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &CakeMeshPanel::onProcessOutput);
    connect(m_process, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CakeMeshPanel::onProcessFinished);
    // finished() never fires when bash cannot be started, so fail the queue here and
    // the panel leaves the running state
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;  // other errors still emit finished()
        log("  [FAILED] could not start bash: " + m_process->errorString());
        m_queueFailed = true;
        m_cmdQueue.clear();
        runNextInQueue();
    });

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto* tabs = new QTabWidget;
    buildGeometryTab(tabs);
    buildMeshTab(tabs);
    buildPhysicsTab(tabs);
    root->addWidget(tabs, 1);

    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(200);
    m_log->setFont(QFont("Monospace", 9));
    root->addWidget(m_log);

    // Action bar with Generate → Run Mesh → Export to Cake Studio
    m_status      = new QLabel("Ready.");
    m_btnGenerate = new QPushButton("Generate Case");
    m_btnRunMesh  = new QPushButton("Run Mesh");
    m_btnExport   = new QPushButton("Export to Cake Studio");
    m_btnRunMesh->setEnabled(false);
    m_btnExport->setEnabled(false);
    ThemeEngine::setRole(m_btnExport, "primary");

    auto makeCopyBtn = [this](const QString& label, const QString& role,
                               const QStringList& keywords) {
        auto* btn = new QPushButton(label);
        ThemeEngine::setRole(btn, role);
        connect(btn, &QPushButton::clicked, this, [this, keywords, btn]() {
            QStringList lines = m_log->toPlainText().split('\n');
            QStringList blocks;
            bool inBlock = false;
            QStringList currentBlock;
            for (const QString& line : lines) {
                bool matches = false;
                for (const QString& kw : keywords)
                    if (line.contains(kw, Qt::CaseInsensitive)) { matches = true; break; }
                if (matches) {
                    if (!currentBlock.isEmpty()) blocks << currentBlock.join('\n') << "";
                    currentBlock.clear();
                    inBlock = true;
                }
                if (inBlock) {
                    if (line.trimmed().isEmpty() && !currentBlock.isEmpty()) {
                        blocks << currentBlock.join('\n') << "";
                        currentBlock.clear();
                        inBlock = false;
                    } else {
                        currentBlock << line;
                    }
                }
            }
            if (inBlock && !currentBlock.isEmpty()) blocks << currentBlock.join('\n');

            if (blocks.isEmpty()) {
                QToolTip::showText(btn->mapToGlobal(btn->rect().center()), "Nothing found", btn);
                return;
            }
            QApplication::clipboard()->setText(blocks.join('\n').trimmed());
            int count = std::count_if(blocks.begin(), blocks.end(), [](const QString& s){ return s.isEmpty(); }) + 1;
            QToolTip::showText(btn->mapToGlobal(btn->rect().center()),
                               QString("Copied %1 block(s)").arg(count), btn);
        });
        return btn;
    };

    auto* copyWarnBtn = makeCopyBtn("⚠ Copy Warnings", "copyWarn",
                                    {"Warning", "WARNING", "WARN", "warning"});
    auto* copyErrBtn  = makeCopyBtn("✕ Copy Errors",   "copyErr",
                                    {"Error", "ERROR", "FATAL", "FAILED", "error:", "fatal"});

    auto* bar = new QHBoxLayout;
    bar->addWidget(m_status, 1);
    bar->addWidget(copyWarnBtn);
    bar->addWidget(copyErrBtn);
    bar->addWidget(m_btnGenerate);
    bar->addWidget(m_btnRunMesh);
    bar->addWidget(m_btnExport);
    root->addLayout(bar);

    connect(m_btnGenerate, &QPushButton::clicked, this, &CakeMeshPanel::onGenerateCase);
    connect(m_btnRunMesh,  &QPushButton::clicked, this, &CakeMeshPanel::onRunMesh);
    connect(m_btnExport,   &QPushButton::clicked, this, &CakeMeshPanel::onExportToStudio);
}

void CakeMeshPanel::setViewport(ViewportWidget* vp) { m_viewport = vp; }

void CakeMeshPanel::setProjectDir(const QString& dir)
{
    if (dir.isEmpty()) return;
    QString norm = QDir(dir).absolutePath();
    QFileInfo fi(norm);
    if (m_outputDir) m_outputDir->setText(fi.absolutePath()); // parent dir
    if (m_caseName && m_caseName->text().trimmed().isEmpty())
        m_caseName->setText(fi.fileName());                   // project name
    // The geometry patch row is labelled from the case name, so refresh the table
    // once the new project's name is in place.
    if (m_bcTable) buildBCTable();
}
void CakeMeshPanel::onThemeChanged()
{
    // Rebuild BC table so separator and cell widgets pick up the new palette
    if (m_bcTable) buildBCTable();
}

QJsonObject CakeMeshPanel::getState() const
{
    QJsonObject o;
    // Geometry
    if (m_geoPath)         o["geo_path"]          = m_geoPath->text();
    // Mesh settings
    if (m_padding)         o["padding"]            = m_padding->value();
    if (m_bgCells)         o["bg_cells"]           = m_bgCells->value();
    if (m_maxGlobal)       o["max_global"]         = m_maxGlobal->value();
    if (m_maxLocal)        o["max_local"]          = m_maxLocal->value();
    if (m_refMin)          o["ref_min"]            = m_refMin->value();
    if (m_refMax)          o["ref_max"]            = m_refMax->value();
    if (m_featureAngle)    o["feature_angle"]      = m_featureAngle->value();
    if (m_nLayers)         o["n_layers"]           = m_nLayers->value();
    if (m_layerExpansion)  o["layer_expansion"]    = m_layerExpansion->value();
    if (m_firstLayerThick) o["first_layer_thick"]  = m_firstLayerThick->value();
    if (m_minThickness)    o["min_thickness"]      = m_minThickness->value();
    // Physics / BCs
    if (m_turbulence)      o["turbulence"]         = m_turbulence->currentText();
    if (m_velocity)        o["velocity"]           = m_velocity->value();
    if (m_aoa)             o["aoa"]                = m_aoa->value();
    if (m_nu)              o["nu"]                 = m_nu->value();
    if (m_caseName)        o["case_name"]          = m_caseName->text();
    if (m_outputDir)       o["output_dir"]         = m_outputDir->text();
    if (m_cores)           o["cores"]              = m_cores->value();
    if (!m_caseDir.isEmpty()) o["case_dir"]        = m_caseDir;
    return o;
}

void CakeMeshPanel::setState(const QJsonObject& o)
{
    // Set the geometry path, then defer the viewport load so VTK's OpenGL context
    // is guaranteed to be initialized before we call Render().
    if (o.contains("geo_path") && m_geoPath) {
        m_geoPath->setText(o["geo_path"].toString());
        if (!o["geo_path"].toString().isEmpty())
            QTimer::singleShot(0, this, &CakeMeshPanel::onLoadGeometry);
    }
    // Mesh settings
    if (o.contains("padding")           && m_padding)         m_padding->setValue(o["padding"].toDouble());
    if (o.contains("bg_cells")          && m_bgCells)         m_bgCells->setValue(o["bg_cells"].toInt());
    if (o.contains("max_global")        && m_maxGlobal)       m_maxGlobal->setValue(o["max_global"].toInt());
    if (o.contains("max_local")         && m_maxLocal)        m_maxLocal->setValue(o["max_local"].toInt());
    if (o.contains("ref_min")           && m_refMin)          m_refMin->setValue(o["ref_min"].toInt());
    if (o.contains("ref_max")           && m_refMax)          m_refMax->setValue(o["ref_max"].toInt());
    if (o.contains("feature_angle")     && m_featureAngle)    m_featureAngle->setValue(o["feature_angle"].toInt());
    if (o.contains("n_layers")          && m_nLayers)         m_nLayers->setValue(o["n_layers"].toInt());
    if (o.contains("layer_expansion")   && m_layerExpansion)  m_layerExpansion->setValue(o["layer_expansion"].toDouble());
    if (o.contains("first_layer_thick") && m_firstLayerThick) m_firstLayerThick->setValue(o["first_layer_thick"].toDouble());
    if (o.contains("min_thickness")     && m_minThickness)    m_minThickness->setValue(o["min_thickness"].toDouble());
    // Physics / BCs
    if (o.contains("turbulence") && m_turbulence)  m_turbulence->setCurrentText(o["turbulence"].toString());
    if (o.contains("velocity")   && m_velocity)    m_velocity->setValue(o["velocity"].toDouble());
    if (o.contains("aoa")        && m_aoa)         m_aoa->setValue(o["aoa"].toDouble());
    if (o.contains("nu")         && m_nu)          m_nu->setValue(o["nu"].toDouble());
    if (o.contains("case_name")  && m_caseName)    m_caseName->setText(o["case_name"].toString());
    if (o.contains("output_dir") && m_outputDir)   m_outputDir->setText(o["output_dir"].toString());
    if (o.contains("cores")      && m_cores)       m_cores->setValue(o["cores"].toInt());
    if (o.contains("case_dir")) {
        m_caseDir = o["case_dir"].toString();
        // Gate on a written case with a mesh on disk before unlocking Export.
        bool haveCase = !m_caseDir.isEmpty() &&
                        QFile::exists(m_caseDir + "/system/blockMeshDict");
        if (m_btnRunMesh) m_btnRunMesh->setEnabled(haveCase);
        if (m_btnExport)  m_btnExport->setEnabled(
            haveCase && QFile::exists(m_caseDir + "/constant/polyMesh/points"));
    }
}

void CakeMeshPanel::reset()
{
    if (m_geoPath)   m_geoPath->clear();
    if (m_geoStats)  m_geoStats->setText("No geometry loaded.");
    if (m_status)    m_status->setText("Ready.");
    if (m_caseName)  m_caseName->clear();
    if (m_outputDir) m_outputDir->clear();
    if (m_log)       m_log->clear();
    // Without this the stale case dir keeps Run Mesh / Export unlocked after
    // New Project, letting the user skip straight past Generate Case.
    m_caseDir.clear();
    if (m_btnRunMesh) m_btnRunMesh->setEnabled(false);
    if (m_btnExport)  m_btnExport->setEnabled(false);
    if (m_btnGenerate) m_btnGenerate->setEnabled(true);
    m_geo.reset();
    // Rebuild the BC table too, or the previous project's patch rows and its
    // geometry patch name survive into the new project.
    if (m_bcTable) buildBCTable();
}

// Geometry tab

void CakeMeshPanel::buildGeometryTab(QTabWidget* tabs)
{
    auto* w   = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setSpacing(8);

    auto* row    = new QHBoxLayout;
    m_geoPath    = new QLineEdit;
    m_geoPath->setPlaceholderText("Path to STEP / IGES / BREP / STL file…");
    auto* browse = new QPushButton("Browse…");
    auto* load   = new QPushButton("Load");
    row->addWidget(m_geoPath, 1);
    row->addWidget(browse);
    row->addWidget(load);
    lay->addLayout(row);

    auto* box = new QGroupBox("Geometry Info");
    auto* bl  = new QVBoxLayout(box);
    m_geoStats = new QLabel("No geometry loaded.");
    m_geoStats->setTextFormat(Qt::RichText);
    m_geoStats->setWordWrap(true);
    m_geoStats->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    bl->addWidget(m_geoStats);
    lay->addWidget(box, 1);

    connect(browse, &QPushButton::clicked, this, &CakeMeshPanel::onBrowseGeometry);
    connect(load,   &QPushButton::clicked, this, &CakeMeshPanel::onLoadGeometry);

    tabs->addTab(w, "① Geometry");
}

// Mesh tab

void CakeMeshPanel::buildMeshTab(QTabWidget* tabs)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* w   = new QWidget;
    auto* lay = new QVBoxLayout(w);

    auto* bgBox  = new QGroupBox("Background Mesh");
    auto* bgForm = new QFormLayout(bgBox);
    m_padding = dbl(2.0, 0.1, 20.0, 0.5, 2);
    m_bgCells = isp(20, 5, 200);
    bgForm->addRow("Domain padding (×bbox):", m_padding);
    bgForm->addRow("Background cells / axis:", m_bgCells);

    auto* refBox  = new QGroupBox("Surface Refinement");
    auto* refForm = new QFormLayout(refBox);
    m_maxGlobal    = isp(2000000, 100000, 50000000);
    m_maxLocal     = isp(100000,  10000,  5000000);
    m_refMin       = isp(2, 0, 8);
    m_refMax       = isp(3, 0, 8);
    m_featureAngle = isp(30, 10, 90);
    refForm->addRow("Max global cells:",        m_maxGlobal);
    refForm->addRow("Max local cells:",         m_maxLocal);
    refForm->addRow("Refinement level min:",    m_refMin);
    refForm->addRow("Refinement level max:",    m_refMax);
    refForm->addRow("Resolve feature angle °:", m_featureAngle);

    auto* layBox  = new QGroupBox("Boundary Layers");
    auto* layForm = new QFormLayout(layBox);
    m_nLayers         = isp(2, 0, 10);
    m_layerExpansion  = dbl(1.2, 1.0, 3.0, 0.05, 2);
    m_firstLayerThick = dbl(0.3, 0.01, 1.0, 0.05, 3);
    m_minThickness    = dbl(0.1, 0.001, 0.5, 0.01, 3);
    layForm->addRow("Number of layers:",      m_nLayers);
    layForm->addRow("Expansion ratio:",       m_layerExpansion);
    layForm->addRow("First layer thickness:", m_firstLayerThick);
    layForm->addRow("Min thickness:",         m_minThickness);

    lay->addWidget(bgBox);
    lay->addWidget(refBox);
    lay->addWidget(layBox);
    lay->addStretch();
    scroll->setWidget(w);
    tabs->addTab(scroll, "② Mesh");
}

// Physics / BCs tab

void CakeMeshPanel::buildPhysicsTab(QTabWidget* tabs)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* w   = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setSpacing(8);

    // Flow Settings
    auto* settBox  = new QGroupBox("Flow Settings");
    auto* settForm = new QFormLayout(settBox);
    m_turbulence = new QComboBox;
    m_turbulence->addItems({"kOmegaSST", "laminar", "SpalartAllmaras", "kEpsilon"});
    m_velocity = dbl(10.0, 0.001, 1000.0, 1.0, 3);
    m_aoa      = dbl(QSettings().value("domainSetup/aoa", 0.0).toDouble(), -90.0, 90.0, 0.5, 2);
    m_nu       = dbl(1.5e-5, 1e-8, 1.0, 1e-6, 8);
    settForm->addRow("Turbulence model:",             m_turbulence);
    settForm->addRow("Default inlet velocity (m/s):", m_velocity);
    settForm->addRow("Angle of attack (°):",          m_aoa);
    settForm->addRow("Kinematic viscosity ν (m²/s):", m_nu);

    // Boundary Conditions Table
    auto* bcBox = new QGroupBox("Boundary Conditions");
    auto* bcLay = new QVBoxLayout(bcBox);

    m_bcTable = new QTableWidget;
    m_bcTable->setColumnCount(3);
    m_bcTable->setHorizontalHeaderLabels({"Patch", "Type", "Value"});
    m_bcTable->horizontalHeader()->setStretchLastSection(false);
    m_bcTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_bcTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_bcTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_bcTable->verticalHeader()->setVisible(false);
    m_bcTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_bcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bcTable->setMinimumHeight(220);

    auto* bcHint = new QLabel("Domain faces auto-detected from blockMesh. "
                               "Geometry patch named after case name.");
    bcHint->setStyleSheet("color: palette(mid); font-size:11px;");
    bcHint->setWordWrap(true);
    bcLay->addWidget(m_bcTable);
    bcLay->addWidget(bcHint);

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
    m_cores = isp(4, 1, 256);
    outForm->addRow("Case name:",        m_caseName);
    outForm->addRow("Output directory:", outRow);
    outForm->addRow("CPU cores:",        m_cores);

    auto* fmtNote = new QLabel("Saves as OpenFOAM case (constant/, system/, 0/)");
    fmtNote->setStyleSheet("color: palette(mid); font-size:11px;");
    outForm->addRow("", fmtNote);

    connect(outBrowse, &QPushButton::clicked, this, &CakeMeshPanel::onBrowseOutputDir);

    // When case name changes, update the geometry patch row name (row 7)
    connect(m_caseName, &QLineEdit::textChanged, this, [this](const QString& name) {
        if (m_bcTable && m_bcTable->rowCount() > 7) {
            auto* item = m_bcTable->item(7, 0);
            if (item) item->setText(name.isEmpty() ? "cfdCase" : name);
        }
    });

    lay->addWidget(settBox);
    lay->addWidget(bcBox, 1);
    lay->addWidget(outBox);
    scroll->setWidget(w);
    tabs->addTab(scroll, "③ Physics && BCs");

    // Populate BC table after all widgets exist
    buildBCTable();
}

// BC Table construction

void CakeMeshPanel::buildBCTable()
{
    if (!m_bcTable) return;

    static const QStringList kTypes = {
        "Wall", "Velocity Inlet", "Pressure Outlet",
        "Symmetry", "Slip", "Zero Gradient"
    };

    // Never call setStyleSheet() on these cell widgets because it detaches them from the global theme.
    // Size them with setFont() and setFrame() only.
    auto shrinkFont = [](QWidget* w) {
        QFont f = w->font();
        f.setPointSize(8);
        w->setFont(f);
    };

    // Rows share the table background; the separator only mutes its text
    const ThemeColors& tc = ThemeEngine::instance().colors();

    struct RowDef { QString name; int typeIdx; bool showValue; double defaultVal; };

    const QString geomName = (m_caseName && !m_caseName->text().isEmpty())
                             ? m_caseName->text() : "cfdCase";
    const double  defV     = m_velocity ? m_velocity->value() : 10.0;

    // Domain rows then separator then geometry row
    const QList<RowDef> domainRows = {
        {"inlet",   1, true,  defV},   // Velocity Inlet
        {"outlet",  2, false, 0.0},    // Pressure Outlet
        {"top",     3, false, 0.0},    // Symmetry
        {"bottom",  3, false, 0.0},
        {"front",   3, false, 0.0},
        {"back",    3, false, 0.0},
    };

    m_bcTable->setRowCount(0);

    auto addRow = [&](const QString& patchName, int typeIdx,
                      bool valueEditable, double value,
                      bool isSeparator = false, bool isGeometry = false) {
        int row = m_bcTable->rowCount();
        m_bcTable->insertRow(row);

        if (isSeparator) {
            auto* sep = new QTableWidgetItem(" Geometry Surfaces ");
            sep->setFlags(Qt::ItemIsEnabled);
            sep->setTextAlignment(Qt::AlignCenter);
            sep->setForeground(tc.textMuted);
            m_bcTable->setItem(row, 0, sep);
            m_bcTable->setSpan(row, 0, 1, 3);
            m_bcTable->setRowHeight(row, 18);
            return;
        }

        // Column 0 holds the patch name
        auto* nameItem = new QTableWidgetItem(patchName);
        nameItem->setFlags(Qt::ItemIsEnabled);
        nameItem->setFont([&]{ QFont f; f.setFamily("Monospace"); f.setPointSize(9); return f; }());
        m_bcTable->setItem(row, 0, nameItem);

        // Column 1 holds the type combo
        auto* combo = new QComboBox;
        combo->addItems(kTypes);
        combo->setCurrentIndex(typeIdx);
        shrinkFont(combo);

        // Column 2 holds the value edit
        auto* valEdit = new QLineEdit(valueEditable ? QString::number(value) : "-");
        valEdit->setEnabled(valueEditable);
        valEdit->setAlignment(Qt::AlignRight);
        shrinkFont(valEdit);
        valEdit->setPlaceholderText("m/s");
        if (!valueEditable) valEdit->setFrame(false);

        // When type changes, enable/disable value and update placeholder
        connect(combo, &QComboBox::currentTextChanged, this,
                [valEdit](const QString& t) {
                    bool needsVal = (t == "Velocity Inlet");
                    valEdit->setEnabled(needsVal);
                    if (needsVal) {
                        valEdit->setPlaceholderText("m/s");
                        if (valEdit->text() == "-") valEdit->clear();
                        valEdit->setFrame(true);
                    } else {
                        valEdit->setText("-");
                        valEdit->setFrame(false);
                    }
                });

        m_bcTable->setCellWidget(row, 1, combo);
        m_bcTable->setCellWidget(row, 2, valEdit);

        // The geometry row is already marked by the tinted name cell above; tinting the
        // cell widgets too would need a widget stylesheet, which would override the
        // theme's own QComboBox/QLineEdit rules and strand them off-theme.
    };

    // Add domain rows
    for (const auto& r : domainRows)
        addRow(r.name, r.typeIdx, r.showValue, r.defaultVal);

    // Separator
    addRow("", 0, false, 0.0, /*isSeparator=*/true);

    // Geometry patch (named after caseName, Wall by default)
    addRow(geomName, 0, false, 0.0, /*isSeparator=*/false, /*isGeometry=*/true);
}

// Slots

void CakeMeshPanel::onBrowseGeometry()
{
    QString path = FileBrowserDialog::getOpenFileName(
        this, "Open geometry file", QString(),
        "CAD / Mesh Files (*.step *.stp *.iges *.igs *.brep *.brp *.stl *.STL);;All Files (*)");
    if (!path.isEmpty()) m_geoPath->setText(path);
}

void CakeMeshPanel::onLoadGeometry()
{
    QString path = m_geoPath->text().trimmed();
    if (path.isEmpty()) { log("No file selected."); return; }

    m_status->setText("Loading geometry…");
    m_geoStats->setText("Loading…");
    QApplication::processEvents();

    auto geo = CFD::CADGeometry::fromFile(path);
    if (!geo->isValid()) {
        m_geoStats->setText("<b style='color:" + ThemeEngine::instance().colors().danger.name() + "'>Failed:</b> " + geo->importResult().errorMsg);
        m_status->setText("Load failed.");
        return;
    }

    m_geo = std::shared_ptr<CFD::CADGeometry>(geo.release());
    const auto& r = m_geo->importResult();
    auto b = m_geo->bounds();

    m_geoStats->setText(
        QString("<b>Faces/Cells:</b> %1 &nbsp; <b>Solids:</b> %2<br>"
                "<b>Bounds X:</b> [%3, %4] &nbsp; <b>Y:</b> [%5, %6] &nbsp; <b>Z:</b> [%7, %8]")
        .arg(r.numFaces).arg(r.numSolids)
        .arg(b[0],0,'f',3).arg(b[1],0,'f',3)
        .arg(b[2],0,'f',3).arg(b[3],0,'f',3)
        .arg(b[4],0,'f',3).arg(b[5],0,'f',3));

    m_status->setText("Geometry loaded.");
    log("Loaded: " + path);

    if (m_viewport) {
        if (path.endsWith(".stl", Qt::CaseInsensitive))
            m_viewport->loadSTL(path);
        else if (path.endsWith(".obj", Qt::CaseInsensitive))
            m_viewport->loadOBJ(path);
    }
}

void CakeMeshPanel::onBrowseOutputDir()
{
    QString dir = FileBrowserDialog::getExistingDirectory(
        this, "Select output directory", QString());
    if (!dir.isEmpty()) m_outputDir->setText(dir);
}

void CakeMeshPanel::onGenerateCase()
{
    // Auto-load if path is set but the user never clicked Load
    if (!m_geo || !m_geo->isValid()) {
        QString path = m_geoPath->text().trimmed();
        if (!path.isEmpty() && QFile::exists(path)) {
            onLoadGeometry();
        }
        if (!m_geo || !m_geo->isValid()) {
            log("Load a geometry file first (① Geometry tab)."); return;
        }
    }
    QString outDir   = m_outputDir->text().trimmed();
    QString caseName = m_caseName->text().trimmed();
    if (outDir.isEmpty() || caseName.isEmpty()) {
        log("Set case name and output directory (③ Physics & BCs tab)."); return;
    }

    setRunning(true);
    m_status->setText("Generating case…");

    static const QMap<QString,CFD::TurbulenceModel> turbs = {
        {"laminar",         CFD::TurbulenceModel::Laminar},
        {"SpalartAllmaras", CFD::TurbulenceModel::SpalartAllmaras},
        {"kOmegaSST",       CFD::TurbulenceModel::KOmegaSST},
        {"kEpsilon",        CFD::TurbulenceModel::KEpsilon},
    };

    static const QMap<QString, CFD::BCType> kBCTypeMap = {
        {"Wall",            CFD::BCType::Wall},
        {"Velocity Inlet",  CFD::BCType::VelocityInlet},
        {"Pressure Outlet", CFD::BCType::PressureOutlet},
        {"Symmetry",        CFD::BCType::Symmetry},
        {"Slip",            CFD::BCType::Slip},
        {"Zero Gradient",   CFD::BCType::ZeroGradient},
    };

    CFD::CaseParams cp;
    cp.caseName        = caseName;
    cp.caseDir         = outDir + "/" + caseName;
    cp.meshPadding     = m_padding->value();
    cp.bgMeshCells     = m_bgCells->value();
    cp.nBoundaryLayers = m_nLayers->value();
    cp.inflowVelocity  = m_velocity->value();
    cp.aoaDeg          = m_aoa ? m_aoa->value() : 0.0;
    // V1 is -X everywhere; BlockMeshWriter places the inlet on the X-max face to
    // match. A "+X" default here would negate U against that mesh and reverse the flow.
    cp.flowAxis        = QSettings().value("domainSetup/flowAxis", "-X").toString();
    cp.nu              = m_nu->value();
    cp.solver          = CFD::SolverType::SimpleFoam; // Cake Studio handles solver selection
    cp.turbulence      = turbs.value(m_turbulence->currentText(), CFD::TurbulenceModel::KOmegaSST);

    // Read per-patch BCs from the table (skip separator rows which have no cell widget)
    if (m_bcTable) {
        for (int i = 0; i < m_bcTable->rowCount(); ++i) {
            auto* combo = qobject_cast<QComboBox*>(m_bcTable->cellWidget(i, 1));
            if (!combo) continue; // separator row
            auto* nameItem = m_bcTable->item(i, 0);
            if (!nameItem || nameItem->text().isEmpty()) continue;

            CFD::PatchBC bc;
            bc.name = nameItem->text();
            bc.type = kBCTypeMap.value(combo->currentText(), CFD::BCType::Wall);

            if (bc.type == CFD::BCType::VelocityInlet) {
                auto* valEdit = qobject_cast<QLineEdit*>(m_bcTable->cellWidget(i, 2));
                if (valEdit) {
                    bool ok = false;
                    double v = valEdit->text().toDouble(&ok);
                    bc.velocity = ok ? v : cp.inflowVelocity;
                }
            }
            cp.patchBCs.append(bc);
        }
    }

    auto res = CFD::CaseGenerator::generate(*m_geo, cp);
    if (!res.success) {
        log("ERROR: " + res.errorMsg);
        m_status->setText("Case generation failed.");
        setRunning(false);
        return;
    }

    m_caseDir = res.caseDir;
    log("Case written: " + m_caseDir);
    m_status->setText("Case ready: click Run Mesh.");
    m_btnRunMesh->setEnabled(true);
    // Regenerating invalidates any existing polyMesh, so Export must re-lock
    // until Run Mesh succeeds again.
    m_btnExport->setEnabled(false);
    setRunning(false);
}

void CakeMeshPanel::onRunMesh()
{
    if (m_caseDir.isEmpty()) { log("Generate a case first."); return; }

    QString env  = ofEnvPrefix();
    QString quoted = m_caseDir;
    quoted.replace("'", "'\"'\"'");   // close quote, literal ', reopen
    QString base = env + " && cd '" + quoted + "' && ";

    runQueue({
        base + "surfaceFeatureExtract 2>&1",
        base + "blockMesh 2>&1",
        base + "snappyHexMesh -overwrite 2>&1",
        base + "checkMesh 2>&1",
    }, [this](bool ok) {
        if (ok) {
            m_status->setText("Mesh complete: ready to export.");
            m_btnExport->setEnabled(true);
        } else {
            m_status->setText("Mesh FAILED: check log.");
        }
    });
}

void CakeMeshPanel::onExportToStudio()
{
    if (!m_caseDir.isEmpty())
        emit exportRequested(m_caseDir);
}

// Async process queue

void CakeMeshPanel::runQueue(QStringList cmds, std::function<void(bool)> onDone)
{
    if (m_process->state() != QProcess::NotRunning) {
        log("A command is already running."); return;
    }
    m_cmdQueue    = std::move(cmds);
    m_queueFailed = false;
    m_onQueueDone = std::move(onDone);
    setRunning(true);
    runNextInQueue();
}

void CakeMeshPanel::runNextInQueue()
{
    if (m_cmdQueue.isEmpty()) {
        setRunning(false);
        if (m_onQueueDone) { m_onQueueDone(!m_queueFailed); m_onQueueDone = nullptr; }
        return;
    }
    QString cmd = m_cmdQueue.takeFirst();
    log(" " + cmd.section("&&", -1).trimmed().section(' ', 0, 0) + " ");
    m_process->start("bash", {"-c", cmd});
}

void CakeMeshPanel::onProcessOutput()
{
    QByteArray data = m_process->readAllStandardOutput();
    for (const QByteArray& line : data.split('\n')) {
        QString s = QString::fromLocal8Bit(line).trimmed();
        if (!s.isEmpty()) m_log->append("  " + s);
    }
}

void CakeMeshPanel::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    onProcessOutput();
    if (status == QProcess::CrashExit) {
        // exitCode is meaningless (often 0) after a crash or kill
        log("  [FAILED] process crashed or was killed");
        m_queueFailed = true;
        m_cmdQueue.clear();
    } else if (exitCode != 0) {
        log("  [FAILED] exit=" + QString::number(exitCode));
        m_queueFailed = true;
        m_cmdQueue.clear();
    } else {
        log("  [OK]");
    }
    runNextInQueue();
}

// Helpers

QString CakeMeshPanel::ofEnvPrefix() const
{
    return "source /usr/lib/openfoam/openfoam2412/etc/bashrc 2>/dev/null";
}

void CakeMeshPanel::log(const QString& msg)
{
    m_log->append("[" + QDateTime::currentDateTime().toString("hh:mm:ss") + "] " + msg);
}

void CakeMeshPanel::setRunning(bool on)
{
    m_btnGenerate->setEnabled(!on);
    m_btnRunMesh->setEnabled(!on && !m_caseDir.isEmpty());
    // Export stays as-is and only unlocks after a successful mesh
}
