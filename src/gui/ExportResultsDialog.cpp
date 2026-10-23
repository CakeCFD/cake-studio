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
#include "ExportResultsDialog.h"
#include "RunLogger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringConverter>
#include <cmath>
#include "PlatformUtils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QFont>
#include <QListWidgetItem>
#include <QSettings>

// construction

ExportResultsDialog::ExportResultsDialog(const QString& caseDir, QWidget* parent)
    : QDialog(parent), m_caseDir(caseDir)
{
    setWindowTitle("Export Results");
    setMinimumWidth(560);
    setMinimumHeight(500);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(6);

    m_tabs = new QTabWidget;
    root->addWidget(m_tabs);

    // Tab 1: Export
    auto* exportTab = new QWidget;
    auto* exportLay = new QVBoxLayout(exportTab);
    exportLay->setSpacing(8);

    auto* infoBox  = new QGroupBox("Case");
    auto* infoForm = new QFormLayout(infoBox);
    infoForm->addRow("Directory:", new QLabel(caseDir));
    QString logName = QFileInfo(solverLogPath()).fileName();
    infoForm->addRow("Solver log:", new QLabel(logName.isEmpty() ? "(none found)" : logName));
    exportLay->addWidget(infoBox);

    auto* outBox = new QGroupBox("Output");
    auto* outForm = new QFormLayout(outBox);
    m_outputDir   = new QLineEdit(caseDir + "/results/exports");
    auto* browse  = new QPushButton("Browse…");
    auto* outRow  = new QHBoxLayout;
    outRow->addWidget(m_outputDir, 1);
    outRow->addWidget(browse);
    outForm->addRow("Directory:", outRow);
    exportLay->addWidget(outBox);
    connect(browse, &QPushButton::clicked, this, &ExportResultsDialog::onBrowseOutput);

    // Reference Quantities
    auto* refBox  = new QGroupBox("Reference Quantities  (for Re, Cd, Cl, Cm)");
    auto* refForm = new QFormLayout(refBox);
    refForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

    auto mkSpin = [](double min, double max, double def, int decimals, const QString& suffix) {
        auto* s = new QDoubleSpinBox;
        s->setRange(min, max);
        s->setValue(def);
        s->setDecimals(decimals);
        s->setSuffix(suffix);
        s->setFixedWidth(160);
        s->setSpecialValueText("(not set)");
        return s;
    };

    m_spRho  = mkSpin(0, 1e4,   1.225,  4, " kg/m³");
    m_spAref = mkSpin(0, 1e6,   0,      6, " m²");   // 0 = not set
    m_spLref = mkSpin(0, 1e4,   0,      4, " m");
    m_spUref = mkSpin(0, 1e4,   0,      3, " m/s");  // 0 = auto from inlet U

    m_spRho ->setMinimum(0.0001);   // keep 0 as special "not set" only for area/length
    m_spAref->setMinimum(0.0);
    m_spLref->setMinimum(0.0);
    m_spUref->setMinimum(0.0);

    refForm->addRow("Density rho:",         m_spRho);
    refForm->addRow("Reference area A_ref:", m_spAref);
    refForm->addRow("Reference length L_ref:", m_spLref);
    refForm->addRow("Reference velocity U_ref\n(0 = from inlet):", m_spUref);
    m_spAoa = mkSpin(-90.0, 90.0, 0.0, 2, " °");
    m_spAoa->setSpecialValueText("");  // no special text - 0° is valid
    m_spAoa->setToolTip("Angle of attack - auto-detected from 0/U if zero");
    refForm->addRow("Angle of attack AoA:", m_spAoa);
    exportLay->addWidget(refBox);
    loadCakeProperties();

    auto* fmtBox = new QGroupBox("Formats");
    auto* fmtLay = new QVBoxLayout(fmtBox);
    m_chkResiduals = new QCheckBox("Residuals history  :  residuals.csv");
    m_chkForces    = new QCheckBox("Forces history     :  forces.csv");
    m_chkReport    = new QCheckBox("Simulation report  :  report.md");
    m_chkSummary   = new QCheckBox("Results summary    :  results_summary.json");
    m_chkResiduals->setChecked(true);
    m_chkForces->setChecked(true);
    m_chkReport->setChecked(true);
    m_chkSummary->setChecked(true);
    fmtLay->addWidget(m_chkResiduals);
    fmtLay->addWidget(m_chkForces);
    fmtLay->addWidget(m_chkReport);
    fmtLay->addWidget(m_chkSummary);
    exportLay->addWidget(fmtBox);

    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(120);
    m_log->setFont(QFont("Monospace", 9));
    exportLay->addWidget(m_log);

    m_status    = new QLabel("Ready.");
    m_btnExport = new QPushButton("Export");
    m_btnExport->setDefault(true);
    m_btnOpenDir = new QPushButton("Open Directory");
    m_btnOpenDir->setEnabled(false);
    auto* closeBtn = new QPushButton("Close");
    auto* expBar   = new QHBoxLayout;
    expBar->addWidget(m_status, 1);
    expBar->addWidget(m_btnOpenDir);
    expBar->addWidget(m_btnExport);
    expBar->addWidget(closeBtn);
    exportLay->addLayout(expBar);

    connect(m_btnExport,   &QPushButton::clicked, this, &ExportResultsDialog::onExport);
    connect(m_btnOpenDir,  &QPushButton::clicked, this, &ExportResultsDialog::onOpenDir);
    connect(closeBtn,      &QPushButton::clicked, this, &QDialog::accept);

    m_tabs->addTab(exportTab, "Export");

    // Tab 2: Run History
    auto* runsTab = new QWidget;
    auto* runsLay = new QVBoxLayout(runsTab);
    runsLay->setSpacing(8);

    auto* runsHint = new QLabel("Select runs to include in generated reports:");
    runsHint->setWordWrap(true);
    runsLay->addWidget(runsHint);

    m_runsList = new QListWidget;
    m_runsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_runsList->setStyleSheet(
        "QListWidget { font-size: 12px; }"
        "QListWidget::item { padding: 2px 4px; }"
        "QListWidget::indicator {"
        "  width: 12px; height: 12px;"
        "  border: 1px solid #666;"
        "  border-radius: 2px;"
        "  background: #1a1a1a;"
        "}"
        "QListWidget::indicator:checked {"
        "  background: #f0a500;"
        "  border-color: #f0a500;"
        "}"
        "QListWidget::indicator:hover {"
        "  border-color: #f0a500;"
        "}"
    );
    runsLay->addWidget(m_runsList, 1);

    auto* rptBox   = new QGroupBox("Reports output directory");
    auto* rptForm  = new QFormLayout(rptBox);
    m_reportsDir   = new QLineEdit(caseDir + "/results/reports");
    auto* rptBrowse = new QPushButton("Browse…");
    auto* rptRow   = new QHBoxLayout;
    rptRow->addWidget(m_reportsDir, 1);
    rptRow->addWidget(rptBrowse);
    rptForm->addRow("Directory:", rptRow);
    runsLay->addWidget(rptBox);
    connect(rptBrowse, &QPushButton::clicked, this, &ExportResultsDialog::onBrowseReports);

    m_runsStatus     = new QLabel("No runs found: run the solver to log runs.");
    auto* genBtn     = new QPushButton("Generate Reports");
    m_btnOpenReports = new QPushButton("Open Directory");
    m_btnOpenReports->setEnabled(false);
    auto* rptBar  = new QHBoxLayout;
    rptBar->addWidget(m_runsStatus, 1);
    rptBar->addWidget(m_btnOpenReports);
    rptBar->addWidget(genBtn);
    runsLay->addLayout(rptBar);

    connect(genBtn, &QPushButton::clicked, this, &ExportResultsDialog::onGenerateReports);
    connect(m_btnOpenReports, &QPushButton::clicked, this, [this](){
        PlatformUtils::openDirectory(m_lastReportsDir);
    });
    connect(m_runsList, &QListWidget::itemClicked, this, [](QListWidgetItem* item){
        item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    });

    m_tabs->addTab(runsTab, "Run History");

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int idx){
        if (idx == 1) refreshRunsList();
    });
}

// slots

void ExportResultsDialog::onOpenDir()
{
    PlatformUtils::openDirectory(m_lastExportDir);
}

void ExportResultsDialog::onBrowseOutput()
{
    QString dir = FileBrowserDialog::getExistingDirectory(
        this, "Select export directory", m_outputDir->text());
    if (!dir.isEmpty()) m_outputDir->setText(dir);
}

// Returns the next auto-numbered subdirectory: <base>/export_001/, export_002/, …
static QString nextNumberedSubdir(const QString& base, const QString& prefix)
{
    QDir().mkpath(base);
    for (int n = 1; n <= 9999; ++n) {
        QString candidate = base + "/" + prefix + QString("_%1").arg(n, 3, 10, QChar('0'));
        if (!QDir(candidate).exists())
            return candidate;
    }
    return base + "/" + prefix + "_overflow";
}

void ExportResultsDialog::onExport()
{
    QString baseDir = m_outputDir->text().trimmed();
    if (baseDir.isEmpty()) { m_status->setText("Set an output directory."); return; }

    QString outDir = nextNumberedSubdir(baseDir, "export");
    QDir().mkpath(outDir);
    m_btnExport->setEnabled(false);
    m_log->clear();

    auto residuals = parseResiduals();
    auto forces    = parseForces();

    log(QString("Case: %1").arg(m_caseDir));
    log(QString("Found %1 residual steps, %2 force entries").arg(residuals.size()).arg(forces.size()));
    log(QString("Output: %1").arg(outDir));

    int ok = 0, fail = 0;

    if (m_chkResiduals->isChecked()) {
        if (residuals.isEmpty()) {
            log("  (no solver log found: residuals.csv skipped)");
        } else {
            if (writeResidualsCsv(outDir + "/residuals.csv", residuals)) { log("  residuals.csv"); ++ok; }
            else { log("  residuals.csv: write failed"); ++fail; }
        }
    }
    if (m_chkForces->isChecked()) {
        if (forces.isEmpty()) {
            log("  (no forces postProcessing found: forces.csv skipped)");
        } else {
            if (writeForcesCsv(outDir + "/forces.csv", forces)) { log("  forces.csv"); ++ok; }
            else { log("  forces.csv: write failed"); ++fail; }
        }
    }
    if (m_chkReport->isChecked()) {
        if (writeReport(outDir + "/report.md", residuals, forces)) { log("  report.md"); ++ok; }
        else { log("  report.md: write failed"); ++fail; }
    }
    if (m_chkSummary->isChecked()) {
        if (writeSummaryJson(outDir + "/results_summary.json", residuals, forces)) { log("  results_summary.json"); ++ok; }
        else { log("  results_summary.json: write failed"); ++fail; }
    }

    m_status->setText(fail == 0
        ? QString("Exported %1 file(s) to %2").arg(ok).arg(outDir)
        : QString("%1 succeeded, %2 failed").arg(ok).arg(fail));
    m_btnExport->setEnabled(true);
    if (ok > 0) {
        m_lastExportDir = outDir;
        m_btnOpenDir->setEnabled(true);
        saveCakeProperties();
    }
}

void ExportResultsDialog::onBrowseReports()
{
    QString dir = FileBrowserDialog::getExistingDirectory(
        this, "Select reports directory", m_reportsDir->text());
    if (!dir.isEmpty()) m_reportsDir->setText(dir);
}

void ExportResultsDialog::onGenerateReports()
{
    QStringList selected;
    for (int i = 0; i < m_runsList->count(); ++i) {
        auto* item = m_runsList->item(i);
        if (item->checkState() == Qt::Checked)
            selected << item->data(Qt::UserRole).toString();   // run directory path
    }

    if (selected.isEmpty()) {
        m_runsStatus->setText("No runs selected.");
        return;
    }

    QString baseDir = m_reportsDir->text().trimmed();
    if (baseDir.isEmpty()) { m_runsStatus->setText("Set an output directory."); return; }

    int ok = 0, fail = 0;
    QString lastDir;
    for (const QString& runDir : selected) {
        RunInfo info = RunLogger::readRunInfo(runDir);
        if (info.index == 0) { ++fail; continue; }
        // Each run gets its own numbered subfolder: report_001/, report_002/, …
        QString subDir = nextNumberedSubdir(baseDir, "report");
        QDir().mkpath(subDir);
        QString outPath = subDir + "/run_report.md";
        if (RunLogger::generateReport(info, outPath)) { ++ok; lastDir = subDir; }
        else ++fail;
    }

    m_runsStatus->setText(fail == 0
        ? QString("Generated %1 report(s) in %2").arg(ok).arg(baseDir)
        : QString("%1 generated, %2 failed").arg(ok).arg(fail));
    if (ok > 0) {
        m_lastReportsDir = baseDir;
        m_btnOpenReports->setEnabled(true);
    }
}

void ExportResultsDialog::refreshRunsList()
{
    m_runsList->clear();
    QList<RunInfo> runs = RunLogger::listRuns(m_caseDir);

    if (runs.isEmpty()) {
        m_runsStatus->setText("No runs found: run the solver to log runs.");
        return;
    }

    for (const RunInfo& info : runs) {
        QString ts = info.timestamp.left(16).replace('T', ' ');
        QString status = info.converged ? "converged" : "did not converge";
        QString label = QString("Run %1  |  %2  |  %3  |  %4 %5  |  %6 steps  |  %7")
            .arg(info.index, 3, 10, QChar('0'))
            .arg(ts)
            .arg(info.solver)
            .arg(info.simType)
            .arg(info.turbModel)
            .arg(info.nSteps)
            .arg(status);

        auto* item = new QListWidgetItem(label, m_runsList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, info.path);
    }

    m_runsStatus->setText(QString("%1 run(s) found. Select runs and click Generate Reports.").arg(runs.size()));
}

// parsers

QString ExportResultsDialog::detectSolver() const
{
    // Check controlDict application field first (most reliable)
    QFile cd(m_caseDir + "/system/controlDict");
    if (cd.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString txt = cd.readAll();
        QRegularExpression re(R"(\bapplication\s+(\w+)\s*;)");
        auto m = re.match(txt);
        if (m.hasMatch()) return m.captured(1);
    }

    // Fall back to log files
    for (const QString& s : {
            "simpleFoam", "buoyantSimpleFoam", "buoyantPimpleFoam",
            "rhoSimpleFoam", "pimpleFoam", "icoFoam", "rhoCentralFoam"}) {
        if (QFile::exists(m_caseDir + "/log." + s)) return s;
    }
    for (const QFileInfo& fi : QDir(m_caseDir).entryInfoList({"log.*"}, QDir::Files)) {
        QString name = fi.fileName();
        int dot = name.indexOf('.');
        if (dot >= 0) return name.mid(dot + 1);
    }
    return {};
}

QString ExportResultsDialog::solverLogPath() const
{
    QString s = detectSolver();
    return s.isEmpty() ? QString() : m_caseDir + "/log." + s;
}

QList<ExportResultsDialog::ResidualRow> ExportResultsDialog::parseResiduals() const
{
    QString logPath = solverLogPath();
    if (logPath.isEmpty() || !QFile::exists(logPath)) return {};

    QFile f(logPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};

    QList<ResidualRow> rows;
    ResidualRow current; current.step = 0; current.time = 0;
    bool inStep = false;

    static const QRegularExpression reTime(R"(^Time = ([\d.eE+\-]+))");
    static const QRegularExpression reRes(
        R"(Solving for (\w+),\s+Initial residual = ([\d.eE+\-]+))");

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine();
        auto mTime = reTime.match(line);
        if (mTime.hasMatch()) {
            if (inStep && !current.initResiduals.isEmpty()) rows.append(current);
            current.step++;
            current.time = mTime.captured(1).toDouble();
            current.initResiduals.clear();
            inStep = true;
            continue;
        }
        auto mRes = reRes.match(line);
        if (mRes.hasMatch() && inStep) {
            QString field = mRes.captured(1);
            if (!current.initResiduals.contains(field))
                current.initResiduals[field] = mRes.captured(2).toDouble();
        }
    }
    if (inStep && !current.initResiduals.isEmpty()) rows.append(current);
    return rows;
}

// OF 2412 force.dat / moment.dat column layout (after stripping parentheses):
//   col[0]=time  col[1..3]=total  col[4..6]=pressure  col[7..9]=viscous
static QStringList parseForceLine(const QString& raw)
{
    QString clean = raw;
    clean.replace('(', ' ').replace(')', ' ');
    return clean.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
}

// Turbulence dictionary: OF2412 ESI uses constant/turbulenceProperties,
// newer layouts use constant/momentumTransport
static QString turbulenceDictPath(const QString& caseDir)
{
    const QString mt = caseDir + "/constant/momentumTransport";
    if (QFile::exists(mt)) return mt;
    return caseDir + "/constant/turbulenceProperties";
}

QList<ExportResultsDialog::ForceRow> ExportResultsDialog::parseForces() const
{
    QDir ppDir(m_caseDir + "/postProcessing/forces");
    if (!ppDir.exists()) return {};

    // One subdirectory per (re)start time ("0", "500", "1000", ...). Visit
    // them in numeric order so later restarts override overlapping times.
    QList<QPair<double, QString>> timeDirs;
    for (const QFileInfo& td : ppDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        double t = td.fileName().toDouble(&ok);
        if (ok) timeDirs.append({t, td.filePath()});
    }
    if (timeDirs.isEmpty()) return {};
    std::sort(timeDirs.begin(), timeDirs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    QMap<double, ForceRow> byTime;
    for (const auto& td : timeDirs) {
        const QString& timeDir = td.second;

        // force.dat (try both spellings)
        QString forcePath = timeDir + "/forces.dat";
        if (!QFile::exists(forcePath)) forcePath = timeDir + "/force.dat";
        if (!QFile::exists(forcePath)) continue;

        // moment.dat (optional: separate file in OF 2412)
        QString momentPath = timeDir + "/moments.dat";
        if (!QFile::exists(momentPath)) momentPath = timeDir + "/moment.dat";
        bool hasMoments = QFile::exists(momentPath);

        // Parse force file into a time-keyed map first
        {
            QFile f(forcePath);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QTextStream in(&f);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith('#') || line.isEmpty()) continue;
                QStringList p = parseForceLine(line);
                if (p.size() < 10) continue;
                bool ok; ForceRow r;
                r.time = p[0].toDouble(&ok); if (!ok) continue;
                // p[1-3] = total, p[4-6] = pressure, p[7-9] = viscous
                r.fp_x = p[4].toDouble(); r.fp_y = p[5].toDouble(); r.fp_z = p[6].toDouble();
                r.fv_x = p[7].toDouble(); r.fv_y = p[8].toDouble(); r.fv_z = p[9].toDouble();
                byTime[r.time] = r;
            }
        }

        // Merge moments from moment.dat if present
        if (hasMoments) {
            QFile mf(momentPath);
            if (mf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&mf);
                while (!in.atEnd()) {
                    QString line = in.readLine().trimmed();
                    if (line.startsWith('#') || line.isEmpty()) continue;
                    QStringList p = parseForceLine(line);
                    if (p.size() < 10) continue;
                    bool ok; double t = p[0].toDouble(&ok); if (!ok) continue;
                    if (!byTime.contains(t)) continue;
                    // p[4-6] = pressure moment, p[7-9] = viscous moment
                    byTime[t].mp_x = p[4].toDouble(); byTime[t].mp_y = p[5].toDouble(); byTime[t].mp_z = p[6].toDouble();
                    byTime[t].mv_x = p[7].toDouble(); byTime[t].mv_y = p[8].toDouble(); byTime[t].mv_z = p[9].toDouble();
                }
            }
        }
    }

    return byTime.values();
}

// writers

bool ExportResultsDialog::writeResidualsCsv(const QString& path,
                                             const QList<ResidualRow>& rows) const
{
    if (rows.isEmpty()) { log("  (no residual data to export)"); return false; }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);
    o.setEncoding(QStringConverter::Utf8);

    QStringList fields;
    for (const auto& r : rows)
        for (const QString& k : r.initResiduals.keys())
            if (!fields.contains(k)) fields << k;

    o << "step,time," << fields.join(',') << "\n";
    for (const auto& r : rows) {
        o << r.step << "," << r.time;
        for (const QString& k : fields)
            o << "," << (r.initResiduals.contains(k)
                         ? QString::number(r.initResiduals[k], 'e', 6) : "");
        o << "\n";
    }
    return true;
}

bool ExportResultsDialog::writeForcesCsv(const QString& path,
                                          const QList<ForceRow>& rows) const
{
    if (rows.isEmpty()) { log("  (no force data to export)"); return false; }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);
    o.setEncoding(QStringConverter::Utf8);
    o << "time,Fx_pressure,Fy_pressure,Fz_pressure,Fx_viscous,Fy_viscous,Fz_viscous,"
         "Fx_total,Fy_total,Fz_total,Mx_pressure,My_pressure,Mz_pressure,"
         "Mx_viscous,My_viscous,Mz_viscous,Mx_total,My_total,Mz_total\n";
    for (const auto& r : rows)
        o << r.time
          << "," << r.fp_x << "," << r.fp_y << "," << r.fp_z
          << "," << r.fv_x << "," << r.fv_y << "," << r.fv_z
          << "," << r.fx()  << "," << r.fy()  << "," << r.fz()
          << "," << r.mp_x << "," << r.mp_y << "," << r.mp_z
          << "," << r.mv_x << "," << r.mv_y << "," << r.mv_z
          << "," << r.mx()  << "," << r.my()  << "," << r.mz() << "\n";
    return true;
}

bool ExportResultsDialog::writeReport(const QString& path,
                                       const QList<ResidualRow>& residuals,
                                       const QList<ForceRow>& forces) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);
    o.setEncoding(QStringConverter::Utf8);

    QString caseName   = QFileInfo(m_caseDir).fileName();
    QString solver     = detectSolver();
    QString mtPath     = turbulenceDictPath(m_caseDir);
    QString simType    = readDictValue(mtPath, "simulationType");
    QString turbModel, deltaModel;
    if (simType == "LES") {
        for (const QString& k : {"LESModel", "subgridStressModel", "model"}) {
            turbModel = readDictValue(mtPath, k);
            if (!turbModel.isEmpty()) break;
        }
        deltaModel = readDictValue(mtPath, "delta");
    } else if (simType == "RAS") {
        for (const QString& k : {"RASModel", "model"}) {
            turbModel = readDictValue(mtPath, k);
            if (!turbModel.isEmpty()) break;
        }
    } else {
        turbModel = readDictValue(mtPath, "model");
    }
    QString st = simType.isEmpty() ? "laminar" : simType;
    QString tm = turbModel.isEmpty() ? "(not specified)" : turbModel;
    QString nu      = readDictValue(m_caseDir + "/constant/transportProperties", "nu");
    QString inletU  = readInletU();
    // Auto-detect AoA from inlet velocity vector (Ux, Uy, Uz)
    double aoa = m_spAoa ? m_spAoa->value() : 0.0;
    if (aoa == 0.0 && !inletU.isEmpty()) {
        QRegularExpression reVec(R"(\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))");
        auto mv = reVec.match(inletU);
        if (mv.hasMatch()) {
            double ux = mv.captured(1).toDouble();
            double uz = mv.captured(3).toDouble();
            double mag = std::sqrt(ux*ux + uz*uz);
            // Measure the angle relative to the flow axis, not to +X. On a -X case
            // atan2(0, -U) is 180 deg, which the +-90 spinbox clamps to 90, so a
            // zero-AoA run reports and then saves an AoA of 90 deg.
            double sgn = (ux < 0.0) ? -1.0 : 1.0;
            if (mag > 1e-12) aoa = std::atan2(sgn*uz, sgn*ux) * 180.0 / M_PI;
        }
    }
    QString endTime = readDictValue(m_caseDir + "/system/controlDict", "endTime");
    QString nCores  = readDictValue(m_caseDir + "/system/decomposeParDict", "numberOfSubdomains");
    auto meshStats  = parseMeshStats();

    o << "# Cake Studio - Simulation Report\n\n";
    o << "| | |\n|---|---|\n";
    o << "| **Case** | " << caseName << " |\n";
    o << "| **Path** | " << m_caseDir << " |\n";
    o << "| **Exported** | " << QDateTime::currentDateTime().toString(Qt::ISODate) << " |\n";
    o << "| **Solver** | " << (solver.isEmpty() ? "(not detected)" : solver) << " |\n\n";

    o << "## Simulation Inputs\n\n";
    o << "| Parameter | Value |\n|---|---|\n";
    o << "| Simulation type | " << st << " |\n";
    if (st == "LES") {
        o << "| SGS model | " << tm << " |\n";
        if (!deltaModel.isEmpty())
            o << "| Delta (filter-width) model | " << deltaModel << " |\n";
    } else if (st == "RAS") {
        o << "| Turbulence model | " << tm << " |\n";
    } else {
        o << "| Turbulence | laminar (no model) |\n";
    }
    o << "| Inlet velocity U | " << (inletU.isEmpty()  ? "(not specified)" : inletU  + " m/s")   << " |\n";
    o << "| Angle of attack (AoA) | " << QString::number(aoa, 'f', 2) << " " << QChar(0x00B0) << " |\n";
    o << "| Kinematic viscosity nu | " << (nu.isEmpty() ? "(not specified)" : nu     + " m^2/s") << " |\n";
    if (!endTime.isEmpty()) o << "| End time / max iterations | " << endTime << " |\n";
    if (!nCores.isEmpty())  o << "| CPU cores | " << nCores << " |\n";

    // Div scheme from fvSchemes
    {
        QFile fvf(m_caseDir + "/system/fvSchemes");
        if (fvf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString fvTxt = fvf.readAll();
            QRegularExpression re(R"(div\(phi,U\)\s+Gauss\s+(\w+))");
            auto m = re.match(fvTxt);
            if (m.hasMatch()) {
                QString s = m.captured(1);
                QString label = (s == "teno")  ? "TENO5 (CakeCFD)" :
                                (s == "teno6") ? "TENO6 (CakeCFD)" :
                                                 s;
                o << "| Divergence scheme div(phi,U) | " << label << " |\n";
            }
        }
    }
    o << "\n";

    // Thermal section: only for solvers that use thermophysicalProperties
    static const QSet<QString> THERMAL_SOLVERS = {
        "buoyantSimpleFoam", "buoyantPimpleFoam", "rhoSimpleFoam", "rhoCentralFoam"
    };
    if (THERMAL_SOLVERS.contains(solver)) {
        QString tpPath = m_caseDir + "/constant/thermophysicalProperties";
        QFile tpFile(tpPath);
        if (tpFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString tp = QString::fromUtf8(tpFile.readAll());
            auto rv = [&](const QString& key) {
                QRegularExpression re(key + R"(\s+([^\s;]+)\s*;)");
                auto m = re.match(tp);
                return m.hasMatch() ? m.captured(1) : QString();
            };
            o << "## Thermal Properties\n\n";
            o << "| Parameter | Value |\n|---|---|\n";
            QString thermoT = rv("type");
            QString eos     = rv("equationOfState");
            QString transp  = rv("transport");
            QString thermo  = rv("thermo");
            QString molW    = rv("molWeight");
            QString cp      = rv("Cp");
            QString mu      = rv("mu");
            QString pr      = rv("Pr");
            QString as      = rv("As");
            QString ts      = rv("Ts");
            if (!thermoT.isEmpty()) o << "| Thermo type | " << thermoT << " |\n";
            if (!eos.isEmpty())     o << "| Equation of state | " << eos << " |\n";
            if (!transp.isEmpty())  o << "| Transport model | " << transp << " |\n";
            if (!thermo.isEmpty())  o << "| Thermodynamics | " << thermo << " |\n";
            if (!molW.isEmpty())    o << "| Molecular weight | " << molW << " g/mol |\n";
            if (!cp.isEmpty())      o << "| Cp | " << cp << " J/kg/K |\n";
            if (!mu.isEmpty())      o << "| Dynamic viscosity (mu) | " << mu << " Pa" << QChar(0x00B7) << "s |\n";
            if (!pr.isEmpty())      o << "| Prandtl number | " << pr << " |\n";
            if (!as.isEmpty())      o << "| Sutherland As | " << as << " |\n";
            if (!ts.isEmpty())      o << "| Sutherland Ts | " << ts << " K |\n";
            o << "\n";
        }
    }

    if (!meshStats.isEmpty()) {
        o << "## Mesh\n\n";
        o << "| Metric | Value |\n|---|---|\n";
        if (meshStats.contains("cells"))
            o << "| Total cells | " << meshStats["cells"] << " |\n";
        if (meshStats.contains("max_non_ortho"))
            o << "| Max non-orthogonality | " << meshStats["max_non_ortho"] << " deg |\n";
        if (meshStats.contains("max_skewness"))
            o << "| Max skewness | " << meshStats["max_skewness"] << " |\n";
        if (meshStats.contains("bbox"))
            o << "| Domain bounding box | " << meshStats["bbox"] << " |\n";
        o << "| checkMesh | " << (meshStats.value("ok") == "true" ? "OK" : "FAILED") << " |\n\n";
    }

    o << "## Convergence\n\n";
    if (residuals.isEmpty()) {
        o << "_No residual data found._\n\n";
    } else {
        const auto& last = residuals.last();
        o << "| Total steps | " << residuals.size() << " |\n";
        o << "|---|---|\n";
        o << "| Final time | " << last.time << " s |\n\n";
        o << "| Field | Final residual |\n|---|---|\n";
        // For laminar runs the k/ω/ε/nut fields belong to a previous turbulent log :
        // filter them so they don't pollute a laminar report.
        static const QSet<QString> turbFields = {"k","omega","epsilon","nut","nuTilda","R"};
        bool isLaminar = (st == "laminar");
        for (auto it = last.initResiduals.begin(); it != last.initResiduals.end(); ++it) {
            if (isLaminar && turbFields.contains(it.key())) continue;
            o << "| " << it.key() << " | " << QString::number(it.value(), 'e', 4) << " |\n";
        }
        o << "\n";
    }

    o << "## Forces & Aerodynamic Coefficients\n\n";
    if (forces.isEmpty()) {
        o << "_No forces data found._\n\n";
    } else {
        const auto& fr = forces.last();
        o << "Time: " << fr.time << " s\n\n";
        o << "| Component | Fx [N] | Fy [N] | Fz [N] |\n|---|---|---|---|\n";
        o << "| Pressure (wave/form) | " << fr.fp_x << " | " << fr.fp_y << " | " << fr.fp_z << " |\n";
        o << "| Viscous (skin friction) | " << fr.fv_x << " | " << fr.fv_y << " | " << fr.fv_z << " |\n";
        o << "| **Total** | **" << fr.fx() << "** | **" << fr.fy() << "** | **" << fr.fz() << "** |\n";
        // Use explicit Unicode for middle-dot to avoid Qt5 Latin-1 char* mangling
        const QString Nm = QString("N") + QChar(0x00B7) + "m";
        o << "| Moment [" << Nm << "] (pressure) | " << fr.mp_x << " | " << fr.mp_y << " | " << fr.mp_z << " |\n";
        o << "| Moment [" << Nm << "] (viscous) | " << fr.mv_x << " | " << fr.mv_y << " | " << fr.mv_z << " |\n\n";

        double rho  = m_spRho ->value();
        double Aref = m_spAref->value();
        double Lref = m_spLref->value();
        double Uref = m_spUref->value();

        if (Uref <= 0.0) {
            QString uStr = inletU;
            uStr.remove('(').remove(')');
            QStringList uc = uStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (uc.size() == 3) {
                double ux = uc[0].toDouble(), uy = uc[1].toDouble(), uz = uc[2].toDouble();
                Uref = std::sqrt(ux*ux + uy*uy + uz*uz);
            }
        }

        // Mach number: from thermophysicalProperties if available
        QString tpPath = m_caseDir + "/constant/thermophysicalProperties";
        QString cpStr  = readDictValue(tpPath, "Cp");
        QString mwStr  = readDictValue(tpPath, "molWeight");
        double mach = -1.0;
        if (!cpStr.isEmpty() && !mwStr.isEmpty() && Uref > 0) {
            double cp  = cpStr.toDouble();
            double mw  = mwStr.toDouble();
            double R   = 8314.46 / mw;          // specific gas constant J/kg/K
            double gamma = cp / (cp - R);
            double T   = 293.15;                 // assume standard conditions if not set
            double a   = std::sqrt(gamma * R * T);
            mach = Uref / a;
        }

        QString nuStr = readDictValue(m_caseDir + "/constant/transportProperties", "nu");
        double nuVal = nuStr.toDouble();

        o << "### Reference Quantities\n\n";
        o << "| Quantity | Value |\n|---|---|\n";
        o << "| Density rho | " << rho << " kg/m^3 |\n";
        o << "| Reference velocity U_ref | " << (Uref > 0 ? QString::number(Uref, 'g', 5) + " m/s" : "(not resolved)") << " |\n";
        if (mach >= 0)
            o << "| Mach number | " << QString::number(mach, 'f', 4)
              << (mach > 1.0 ? "  *(supersonic)*" : mach > 0.8 ? "  *(transonic)*" : "") << " |\n";
        o << "| Reference area A_ref | " << (Aref > 0 ? QString::number(Aref, 'g', 5) + " m^2" : "(not set)") << " |\n";
        o << "| Reference length L_ref | " << (Lref > 0 ? QString::number(Lref, 'g', 5) + " m"  : "(not set)") << " |\n";
        if (Uref > 0 && nuVal > 0 && Lref > 0)
            o << "| Reynolds number Re | " << QString::number(Uref * Lref / nuVal, 'g', 5) << " |\n";
        o << "\n";

        if (Uref > 0 && Aref > 0) {
            double q = 0.5 * rho * Uref * Uref;
            double Cd_total    = fr.fx() / (q * Aref);
            double Cd_pressure = fr.fp_x / (q * Aref);
            double Cd_viscous  = fr.fv_x / (q * Aref);
            double Cl          = fr.fz() / (q * Aref);

            o << "### Aerodynamic Coefficients\n\n";
            o << "| Coefficient | Value | Notes |\n|---|---|---|\n";
            o << "| **Cd (total drag)** | **" << QString::number(Cd_total, 'g', 5) << "** | Pressure + viscous |\n";
            o << "| Cd_pressure (wave/form drag) | " << QString::number(Cd_pressure, 'g', 5)
              << " | " << (mach > 1.0 ? "Dominated by wave drag at M>" + QString::number(mach,'f',2) : "Pressure drag") << " |\n";
            o << "| Cd_viscous (skin friction) | " << QString::number(Cd_viscous, 'g', 5) << " | Viscous drag |\n";
            o << "| Cy (side force) | " << QString::number(fr.fy() / (q * Aref), 'g', 5) << " | |\n";
            o << "| **Cl (lift)** | **" << QString::number(Cl, 'g', 5) << "** | |\n";
            if (Cl != 0 && Cd_total != 0)
                o << "| L/D ratio | " << QString::number(Cl / Cd_total, 'g', 5) << " | |\n";
            if (Lref > 0) {
                o << "| Cm (pitch) | " << QString::number(fr.my() / (q * Aref * Lref), 'g', 5) << " | |\n";
                o << "| Cn (yaw)   | " << QString::number(fr.mz() / (q * Aref * Lref), 'g', 5) << " | |\n";
            }
            o << "\n> q = 0.5 * rho * U_ref^2 = " << QString::number(q, 'g', 5) << " Pa\n\n";
        } else {
            o << "> Set A_ref (and optionally L_ref) in the Reference Quantities panel to compute Cd, Cl, Cm.\n\n";
        }
    }

    QStringList refs = citations(turbModel);
    o << "## References\n\n";
    for (int i = 0; i < refs.size(); ++i)
        o << (i + 1) << ". " << refs[i] << "\n";
    o << "\n---\n*Generated by Cake Studio*\n";
    return true;
}

bool ExportResultsDialog::writeSummaryJson(const QString& path,
                                            const QList<ResidualRow>& residuals,
                                            const QList<ForceRow>& forces) const
{
    QJsonObject root;
    root["case"]     = QFileInfo(m_caseDir).fileName();
    root["case_dir"] = m_caseDir;
    root["solver"]   = detectSolver();
    root["exported"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["n_steps"]  = residuals.size();

    if (!residuals.isEmpty()) {
        const auto& last = residuals.last();
        root["final_time"] = last.time;
        QJsonObject res;
        for (auto it = last.initResiduals.begin(); it != last.initResiduals.end(); ++it)
            res[it.key()] = it.value();
        root["final_residuals"] = res;
    }

    if (!forces.isEmpty()) {
        const auto& fr = forces.last();
        QJsonObject fobj;
        fobj["time"] = fr.time;
        fobj["Fx"] = fr.fx(); fobj["Fy"] = fr.fy(); fobj["Fz"] = fr.fz();
        fobj["Fx_pressure"] = fr.fp_x; fobj["Fy_pressure"] = fr.fp_y; fobj["Fz_pressure"] = fr.fp_z;
        fobj["Fx_viscous"]  = fr.fv_x; fobj["Fy_viscous"]  = fr.fv_y; fobj["Fz_viscous"]  = fr.fv_z;
        fobj["Mx"] = fr.mx(); fobj["My"] = fr.my(); fobj["Mz"] = fr.mz();
        root["forces_latest"] = fobj;

        double rho  = m_spRho ->value();
        double Aref = m_spAref->value();
        double Lref = m_spLref->value();
        double Uref = m_spUref->value();
        if (Uref <= 0.0) {
            QString uStr = readInletU();
            uStr.remove('(').remove(')');
            QStringList uc = uStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (uc.size() == 3) {
                double ux = uc[0].toDouble(), uy = uc[1].toDouble(), uz = uc[2].toDouble();
                Uref = std::sqrt(ux*ux + uy*uy + uz*uz);
            }
        }
        QJsonObject refObj;
        refObj["rho"]  = rho;
        refObj["Aref"] = Aref;
        refObj["Lref"] = Lref;
        refObj["Uref"] = Uref;
        root["reference"] = refObj;

        if (Uref > 0 && Aref > 0) {
            double q = 0.5 * rho * Uref * Uref;
            QJsonObject coeff;
            coeff["q_Pa"] = q;
            coeff["Cd"]          = fr.fx() / (q * Aref);
            coeff["Cd_pressure"] = fr.fp_x / (q * Aref);
            coeff["Cd_viscous"]  = fr.fv_x / (q * Aref);
            coeff["Cy"]          = fr.fy() / (q * Aref);
            coeff["Cl"]          = fr.fz() / (q * Aref);
            if (Lref > 0) {
                coeff["Cm"] = fr.my() / (q * Aref * Lref);
                coeff["Cn"] = fr.mz() / (q * Aref * Lref);
            }
            QString nuStr = readDictValue(m_caseDir + "/constant/transportProperties", "nu");
            double nuVal  = nuStr.toDouble();
            if (nuVal > 0 && Lref > 0)
                coeff["Re"] = Uref * Lref / nuVal;
            root["aero_coefficients"] = coeff;
        }
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson());
    return true;
}

void ExportResultsDialog::log(const QString& msg) const
{
    m_log->append(msg);
}

void ExportResultsDialog::loadCakeProperties()
{
    QString path = m_caseDir + "/system/cakeProperties";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith("//") || line.isEmpty()) continue;
        QStringList parts = line.split(QRegularExpression("\\s+"));
        if (parts.size() < 2) continue;
        QString key = parts[0];
        double  val = parts[1].replace(";", "").toDouble();
        if      (key == "rho")  m_spRho ->setValue(val);
        else if (key == "Aref") m_spAref->setValue(val);
        else if (key == "Lref") m_spLref->setValue(val);
        else if (key == "Uref") m_spUref->setValue(val);
        else if (key == "aoa"  && m_spAoa) m_spAoa->setValue(val);
    }
    // Auto-detect AoA from 0/U if not saved in cakeProperties
    if (m_spAoa && m_spAoa->value() == 0.0) {
        QString uStr = readInletU();
        if (!uStr.isEmpty()) {
            QRegularExpression rv(R"(\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))");
            auto mv = rv.match(uStr);
            if (mv.hasMatch()) {
                double ux = mv.captured(1).toDouble();
                double uz = mv.captured(3).toDouble();
                double mag = std::sqrt(ux*ux + uz*uz);
                if (mag > 1e-12) {
                    // Relative to the flow axis: see the matching note above - a -X
                    // case would otherwise detect 180 deg and clamp to 90.
                    double sgn = (ux < 0.0) ? -1.0 : 1.0;
                    double detected = std::atan2(sgn*uz, sgn*ux) * 180.0 / M_PI;
                    if (std::abs(detected) > 0.01) m_spAoa->setValue(detected);
                }
            }
        }
    }
    // Fall back to whatever was last set in Domain Setup dialog
    if (m_spAoa && m_spAoa->value() == 0.0) {
        double settingsAoa = QSettings().value("domainSetup/aoa", 0.0).toDouble();
        if (std::abs(settingsAoa) > 0.01) m_spAoa->setValue(settingsAoa);
    }
}

void ExportResultsDialog::saveCakeProperties() const
{
    QDir(m_caseDir + "/system").mkpath(".");
    QString path = m_caseDir + "/system/cakeProperties";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&f);
    o.setEncoding(QStringConverter::Utf8);
    o << "// Cake Studio reference quantities: used for Re, Cd, Cl, Cm in reports\n\n";
    o << "rho   " << m_spRho ->value() << ";\n";
    o << "Aref  " << m_spAref->value() << ";\n";
    o << "Lref  " << m_spLref->value() << ";\n";
    o << "Uref  " << m_spUref->value() << ";\n";
    if (m_spAoa) o << "aoa   " << m_spAoa->value() << ";\n";
}

// report helpers

QString ExportResultsDialog::readDictValue(const QString& filePath, const QString& key) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString txt = f.readAll();
    QRegularExpression re("\\b" + QRegularExpression::escape(key) + R"(\s+([^\s;{}\[\]()]+)\s*;)");
    auto m = re.match(txt);
    return m.hasMatch() ? m.captured(1) : QString{};
}

QString ExportResultsDialog::readInletU() const
{
    QFile f(m_caseDir + "/0/U");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString txt = f.readAll();
    QRegularExpression re(R"(uniform\s*\(\s*([\d.eE+\-]+\s+[\d.eE+\-]+\s+[\d.eE+\-]+)\s*\))");
    auto m = re.match(txt);
    return m.hasMatch() ? "(" + m.captured(1) + ")" : QString{};
}

QMap<QString,QString> ExportResultsDialog::parseMeshStats() const
{
    QMap<QString,QString> stats;
    for (const QString& name : {"log.checkMesh", "checkMesh.log"}) {
        QFile f(m_caseDir + "/" + name);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString txt = f.readAll();

        QRegularExpression reCells(R"(\bcells:\s+(\d+))");
        auto m = reCells.match(txt);
        if (m.hasMatch()) stats["cells"] = m.captured(1);

        // Floats with optional exponent (skewness is often eg. 9.8e-15)
        const QString F = R"(([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?))";
        QRegularExpression reNonOrtho(R"((?:non-orthogonality Max:|Max non-orthogonality\s*=)\s*)" + F);
        m = reNonOrtho.match(txt);
        if (m.hasMatch()) stats["max_non_ortho"] = m.captured(1);

        QRegularExpression reSkew(R"([Mm]ax skewness\s*[=:]\s*)" + F);
        m = reSkew.match(txt);
        if (m.hasMatch()) stats["max_skewness"] = m.captured(1);

        QRegularExpression reBbox(R"(Overall domain bounding box\s*\(([^)]+)\)\s*\(([^)]+)\))");
        m = reBbox.match(txt);
        if (m.hasMatch()) stats["bbox"] = "(" + m.captured(1) + ") (" + m.captured(2) + ")";

        stats["ok"] = txt.contains("Mesh OK") ? "true" : "false";
        break;
    }
    return stats;
}

QStringList ExportResultsDialog::citations(const QString& turbModel) const
{
    QString mtPath  = turbulenceDictPath(m_caseDir);
    QString simType = readDictValue(mtPath, "simulationType").toLower();

    QStringList refs;
    refs << "Weller et al. (1998). A tensorial approach to computational continuum mechanics "
            "using object-oriented techniques. *Computers in Physics*, 12(6):620-631. [OpenFOAM]";

    // Only cite TENO if it is actually used in fvSchemes
    QFile fvf(m_caseDir + "/system/fvSchemes");
    if (fvf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString fvTxt = fvf.readAll();
        QRegularExpression re(R"(div\(phi,U\)\s+Gauss\s+(teno\d?)\b)");
        auto m = re.match(fvTxt);
        if (m.hasMatch()) {
            QString s = m.captured(1);
            if (s == "teno6")
                refs << "Fu et al. (2016). A family of high-order targeted ENO schemes for "
                        "compressible-fluid simulations. *J. Comput. Phys.*, 305:333-359. [CakeCFD / TENO6]";
            else
                refs << "Fu et al. (2016). A family of high-order targeted ENO schemes for "
                        "compressible-fluid simulations. *J. Comput. Phys.*, 305:333-359. [CakeCFD / TENO5]";
        }
    }
    QString t = turbModel.toLower();

    if (simType == "ras") {
        if (t.contains("komegasst") || (t.contains("komega") && t.contains("sst")))
            refs << "Menter, F.R. (1994). Two-equation eddy-viscosity turbulence models for "
                    "engineering applications. *AIAA Journal*, 32(8):1598-1605. [k-omega SST]";
        else if (t.contains("komega"))
            refs << "Wilcox, D.C. (1988). Reassessment of the scale-determining equation for "
                    "advanced turbulence models. *AIAA Journal*, 26(11):1299-1310. [k-omega]";
        else if (t.contains("realizableke") || t.contains("realizablek"))
            refs << "Shih et al. (1995). A new k-epsilon eddy viscosity model for high Reynolds "
                    "number turbulent flows. *Computers & Fluids*, 24(3):227-238. [Realizable k-epsilon]";
        else if (t.contains("rngkepsilon") || t.contains("rngke"))
            refs << "Yakhot et al. (1992). Development of turbulence models for shear flows by "
                    "a double expansion technique. *Physics of Fluids A*, 4(7):1510-1520. [RNG k-epsilon]";
        else if (t.contains("kepsilon") || t.contains("keps"))
            refs << "Launder & Spalding (1974). The numerical computation of turbulent flows. "
                    "*Comput. Methods Appl. Mech. Eng.*, 3(2):269-289. [k-epsilon]";
        else if (t.contains("spalart") || t.contains("nutilda"))
            refs << "Spalart & Allmaras (1992). A one-equation turbulence model for aerodynamic "
                    "flows. *AIAA Paper* 92-0439. [Spalart-Allmaras]";
        else if (t.contains("v2f"))
            refs << "Durbin, P.A. (1995). Separated flow computations with the k-epsilon-v2 model. "
                    "*AIAA Journal*, 33(4):659-664. [v2-f]";
    } else if (simType == "les") {
        if (t.contains("dynamic") && t.contains("smagorinsky"))
            refs << "Germano et al. (1991). A dynamic subgrid-scale eddy viscosity model. "
                    "*Physics of Fluids A*, 3(7):1760-1765. [Dynamic Smagorinsky]";
        else if (t.contains("smagorinsky"))
            refs << "Smagorinsky, J. (1963). General circulation experiments with the primitive equations. "
                    "*Monthly Weather Review*, 91(3):99-164. [Smagorinsky SGS]";
        if (t.contains("wale"))
            refs << "Nicoud & Ducros (1999). Subgrid-scale stress modelling based on the square of "
                    "the velocity gradient tensor. *Flow, Turbulence and Combustion*, 62(3):183-200. [WALE]";
        if (t.contains("dynamiclagrangian"))
            refs << "Meneveau et al. (1996). A Lagrangian dynamic subgrid-scale model of turbulence. "
                    "*J. Fluid Mech.*, 319:353-385. [Dynamic Lagrangian]";
        if (t.contains("dynamickeqn") || (t.contains("dynamic") && t.contains("keqn")))
            refs << "Germano et al. (1991). A dynamic subgrid-scale eddy viscosity model. "
                    "*Physics of Fluids A*, 3(7):1760-1765. [Dynamic k-equation SGS]";
        else if (t.contains("keqn"))
            refs << "Schumann, U. (1975). Subgrid scale model for finite difference simulations "
                    "of turbulent flows in plane channels and annuli. *J. Comput. Phys.*, 18(4):376-404. [k-equation SGS]";
        if (t.contains("deardorff"))
            refs << "Deardorff, J.W. (1973). The use of subgrid transport equations in a "
                    "three-dimensional model of atmospheric turbulence. *J. Fluids Eng.*, 95(3):429-438. [Deardorff SGS]";
    }
    return refs;
}
