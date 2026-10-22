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

#include "MeshQualityDialog.h"
#include "themes/ThemeEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QRegularExpression>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QFont>
#include <QColor>

MeshQualityDialog::MeshQualityDialog(const QString& caseDir, QWidget* parent)
    : QDialog(parent), m_caseDir(caseDir)
{
    setWindowTitle("Mesh Quality: checkMesh  |  " + QDir(caseDir).dirName());
    setMinimumSize(860, 620);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(6);

    m_status = new QLabel("Click 'Run checkMesh' to analyse the mesh.");
    m_status->setStyleSheet(
        "padding:5px 8px; background:palette(alternate-base); border-radius:4px;");
    root->addWidget(m_status);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 0);   // indeterminate
    m_progress->setMaximumHeight(4);
    m_progress->setTextVisible(false);
    m_progress->hide();
    root->addWidget(m_progress);

    auto* split = new QSplitter(Qt::Vertical);

    // Summary table
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({"Metric", "Value", "OpenFOAM Guideline", "Status"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setMinimumHeight(180);
    split->addWidget(m_table);

    // Raw log
    m_rawText = new QPlainTextEdit;
    m_rawText->setReadOnly(true);
    m_rawText->setMaximumBlockCount(10000);
    QFont mono("Monospace"); mono.setPointSize(8);
    m_rawText->setFont(mono);
    split->addWidget(m_rawText);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    root->addWidget(split, 1);

    auto* bar = new QHBoxLayout;
    m_btnRun  = new QPushButton("Run checkMesh");
    m_btnSave = new QPushButton("Save Report");
    m_btnSave->setEnabled(false);
    auto* btnClose = new QPushButton("Close");
    bar->addWidget(m_btnRun);
    bar->addWidget(m_btnSave);
    bar->addStretch();
    bar->addWidget(btnClose);
    root->addLayout(bar);

    connect(m_btnRun,  &QPushButton::clicked, this, &MeshQualityDialog::onRun);
    connect(m_btnSave, &QPushButton::clicked, this, &MeshQualityDialog::onSave);
    connect(btnClose,  &QPushButton::clicked, this, &QDialog::accept);
}

void MeshQualityDialog::onRun()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) return;

    m_rawOutput.clear();
    m_rawText->clear();
    m_table->setRowCount(0);
    m_status->setText("Running checkMesh...");
    m_btnRun->setEnabled(false);
    m_btnSave->setEnabled(false);
    m_progress->show();

    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(m_caseDir);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_proc, &QProcess::readyReadStandardOutput,
            this, &MeshQualityDialog::onReadOutput);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onFinished(code); });

    const QString ofBashrc = "/usr/lib/openfoam/openfoam2412/etc/bashrc";
    // Paths go in as positional args ($1, $2), never spliced into the script,
    // so case dirs containing quotes or spaces are safe
    const QString cmd = R"(source "$1" 2>/dev/null; cd "$2" && checkMesh -allGeometry -allTopology 2>&1)";
    m_proc->start("bash", {"-c", cmd, "bash", ofBashrc, m_caseDir});
}

void MeshQualityDialog::onReadOutput()
{
    QString s = QString::fromUtf8(m_proc->readAllStandardOutput());
    m_rawOutput += s;
    m_rawText->appendPlainText(s);
}

void MeshQualityDialog::onFinished(int code)
{
    m_progress->hide();
    m_btnRun->setEnabled(true);
    if (!m_rawOutput.isEmpty()) {
        parseAndDisplay(m_rawOutput);
        m_btnSave->setEnabled(true);
    }
    bool meshOK = m_rawOutput.contains("Mesh OK", Qt::CaseInsensitive);
    m_status->setText(code == 0
        ? (meshOK ? "checkMesh: Mesh OK; no issues found."
                  : "checkMesh completed: review warnings above.")
        : "checkMesh finished with errors: see log below.");
    const ThemeColors& tc = ThemeEngine::instance().colors();
    const QColor tone = meshOK ? tc.success : (code == 0 ? tc.warning : tc.danger);
    m_status->setStyleSheet(QString(
        "padding:5px 8px; border-radius:4px; border:1px solid %1; background:rgba(%2,%3,%4,40);")
        .arg(tone.name()).arg(tone.red()).arg(tone.green()).arg(tone.blue()));
}

double MeshQualityDialog::extractDouble(const QString& text, const QString& pattern)
{
    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(text);
    return m.hasMatch() ? m.captured(1).toDouble() : -1.0;
}

QString MeshQualityDialog::extractString(const QString& text, const QString& pattern)
{
    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(text);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

void MeshQualityDialog::addRow(const Metric& m)
{
    int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(m.name));
    m_table->setItem(row, 1, new QTableWidgetItem(m.value));
    m_table->setItem(row, 2, new QTableWidgetItem(m.guideline));

    QColor bg, fg;
    QString label;
    switch (m.status) {
        case Metric::OK:    bg = {0x1e,0x3a,0x1e}; fg = {0xa5,0xd6,0xa7}; label = "OK";      break;
        case Metric::WARN:  bg = {0x3a,0x30,0x0a}; fg = {0xff,0xe0,0x82}; label = "WARNING"; break;
        case Metric::ERROR: bg = {0x3a,0x10,0x10}; fg = {0xef,0x9a,0x9a}; label = "ERROR";   break;
    }
    auto* s = new QTableWidgetItem(label);
    s->setBackground(bg);
    s->setForeground(fg);
    s->setTextAlignment(Qt::AlignCenter);
    s->setFont([&]{ QFont f; f.setBold(m.status != Metric::OK); return f; }());
    m_table->setItem(row, 3, s);
}

void MeshQualityDialog::parseAndDisplay(const QString& out)
{
    m_table->setRowCount(0);

    auto dbl = [&](const QString& p) { return extractDouble(out, p); };
    auto str = [&](const QString& p) { return extractString(out, p); };

    // Topology
    QString cells = str(R"(cells:\s*(\d+))");
    if (!cells.isEmpty())
        addRow({"Total cells", cells, "-", Metric::OK});

    QString faces = str(R"(faces:\s*(\d+))");
    if (!faces.isEmpty())
        addRow({"Total faces", faces, "-", Metric::OK});

    QString points = str(R"(points:\s*(\d+))");
    if (!points.isEmpty())
        addRow({"Total points", points, "-", Metric::OK});

    QString intFaces = str(R"(internal faces:\s*(\d+))");
    if (!intFaces.isEmpty())
        addRow({"Internal faces", intFaces, "-", Metric::OK});

    // Geometry. Floats may carry exponents (skewness 9.8e-15); patterns
    // accept both v2412 wording ("Mesh non-orthogonality Max: X average: Y",
    // "Min volume = X. Max volume = Y.") and the older "= value" wording.
    const QString F = R"(([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?))";
    auto dblAny = [&](std::initializer_list<QString> pats) {
        for (const QString& p : pats) {
            double v = dbl(QString(p).replace("%F", F));
            if (v >= 0) return v;
        }
        return -1.0;
    };

    double maxNO = dblAny({R"(non-orthogonality\s+Max:\s*%F)",
                           R"(Max non-orthogonality\s*[=:]\s*%F)"});
    if (maxNO >= 0) {
        Metric::Status s = maxNO < 70 ? Metric::OK : maxNO < 85 ? Metric::WARN : Metric::ERROR;
        addRow({"Max non-orthogonality", QString::number(maxNO,'f',2)+"°",
                "< 70° ideal, < 85° limit", s});
    }

    double avgNO = dblAny({R"(non-orthogonality\s+Max:\s*\S+\s+average:\s*%F)",
                           R"(Average non-orthogonality\s*[=:]\s*%F)"});
    if (avgNO >= 0) {
        addRow({"Avg non-orthogonality", QString::number(avgNO,'f',2)+"°",
                "< 40°", avgNO < 40 ? Metric::OK : Metric::WARN});
    }

    double skew = dblAny({R"(max skewness\s*[=:]\s*%F)"});
    if (skew >= 0) {
        addRow({"Max skewness", QString::number(skew,'f',3),
                "< 4 (hex), < 20 (tet)", skew < 4 ? Metric::OK : skew < 20 ? Metric::WARN : Metric::ERROR});
    }

    double ar = dblAny({R"(max aspect ratio\s*[=:]\s*%F)"});
    if (ar >= 0) {
        addRow({"Max aspect ratio", QString::number(ar,'f',1),
                "< 100 ideal", ar < 100 ? Metric::OK : ar < 1000 ? Metric::WARN : Metric::ERROR});
    }

    // Min volume can legitimately be negative (inverted cells), so match the
    // sign explicitly and keep -1 distinct from "not found"
    double minVol = -1.0;
    bool   haveMinVol = false;
    for (const QString& p : {QString(R"(\bMin volume\s*=\s*%F)"),
                             QString(R"(minimum volume\s*[=:]\s*%F)")}) {
        QRegularExpression re(QString(p).replace("%F", F),
                              QRegularExpression::CaseInsensitiveOption);
        auto m = re.match(out);
        if (m.hasMatch()) { minVol = m.captured(1).toDouble(); haveMinVol = true; break; }
    }
    if (haveMinVol) {
        addRow({"Min cell volume", QString::number(minVol,'e',3)+" m³",
                "> 0", minVol > 0 ? Metric::OK : Metric::ERROR});
    }

    double maxVol = dblAny({R"(\bMax volume\s*=\s*%F)", R"(maximum volume\s*[=:]\s*%F)"});
    if (maxVol >= 0)
        addRow({"Max cell volume", QString::number(maxVol,'e',3)+" m³", "-", Metric::OK});

    double totalVol = dblAny({R"(total volume\s*[=:]\s*%F)"});
    if (totalVol >= 0)
        addRow({"Total mesh volume", QString::number(totalVol,'e',4)+" m³", "-", Metric::OK});

    // Failed checks
    double negVol = dbl(R"((\d+)\s+cells with negative volume)");
    if (negVol < 0) negVol = dbl(R"(Number of negative volume cells\s*:\s*(\d+))");
    if (negVol > 0)
        addRow({"Negative volume cells", QString::number((int)negVol),
                "0", Metric::ERROR});

    double illegalFaces = dbl(R"((\d+)\s+illegal face)");
    if (illegalFaces > 0)
        addRow({"Illegal faces", QString::number((int)illegalFaces), "0", Metric::ERROR});

    double openEdges = dbl(R"((\d+)\s+open edge)");
    if (openEdges > 0)
        addRow({"Open edges", QString::number((int)openEdges), "0", Metric::WARN});

    // Overall
    bool meshOK = out.contains("Mesh OK", Qt::CaseInsensitive);
    addRow({"Overall result", meshOK ? "Mesh OK" : "Issues found",
            "No errors", meshOK ? Metric::OK : Metric::ERROR});
}

void MeshQualityDialog::onSave()
{
    if (m_rawOutput.isEmpty()) return;

    QString saveDir = m_caseDir + "/results/meshQuality";
    QDir().mkpath(saveDir);

    int n = 1;
    while (QFile::exists(saveDir + QString("/checkMesh_%1.txt").arg(n, 3, 10, QChar('0'))))
        ++n;
    QString path = saveDir + QString("/checkMesh_%1.txt").arg(n, 3, 10, QChar('0'));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save Failed", "Could not write to:\n" + path);
        return;
    }
    f.write(m_rawOutput.toUtf8());
    m_status->setText("Report saved: " + path);
}
