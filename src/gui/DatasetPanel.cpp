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
#include "DatasetPanel.h"
#include "CitationManager.h"
#include "ApiFormatDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QScrollArea>
#include <QFrame>
#include <QInputDialog>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QClipboard>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QTabWidget>
#include <QToolButton>
#include <QMenu>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QRegularExpression>
#include <cmath>

// inline result canvas

class ResultCanvas : public QWidget {
public:
    explicit ResultCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(90);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setData(const QVector<double>& data, int components, const QString& label) {
        m_data = data; m_components = qMax(1, components); m_label = label; update();
    }
    void clear() { m_data.clear(); update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().window());

        QRect r = rect().adjusted(6, 6, -6, -16);
        if (m_data.isEmpty()) {
            p.setPen(palette().mid().color());
            p.drawText(rect(), Qt::AlignCenter, "No data yet");
            return;
        }

        int n = m_data.size() / m_components;
        if (n == 0) return;

        double vmin = m_data[0], vmax = m_data[0];
        for (double v : m_data) { vmin = qMin(vmin,v); vmax = qMax(vmax,v); }
        if (std::abs(vmax - vmin) < 1e-30) vmax = vmin + 1.0;

        static const QColor cols[] = {
            QColor(239,68,68), QColor(34,197,94), QColor(59,130,246),
            QColor(249,115,22), QColor(168,85,247), QColor(6,182,212),
            QColor(234,179,8), QColor(236,72,153), QColor(20,184,166)
        };

        double dy = r.height() / (vmax - vmin);
        for (int c = 0; c < m_components; ++c) {
            QColor col = cols[c % 9];
            p.setPen(QPen(col, 1.5));
            QPolygonF poly;
            for (int i = 0; i < n; ++i) {
                double val = m_data[i * m_components + c];
                double x = r.left() + (n > 1 ? r.width() * i / double(n-1) : r.width()/2.0);
                double y = r.bottom() - (val - vmin) * dy;
                poly << QPointF(x, y);
            }
            if (n == 1) {
                p.drawEllipse(QPointF(poly[0].x(), poly[0].y()), 3, 3);
            } else {
                p.drawPolyline(poly);
            }
        }

        p.setPen(palette().text().color());
        QFont sm = font(); sm.setPointSize(8); p.setFont(sm);
        p.drawText(r.adjusted(0,-2,0,0), Qt::AlignTop|Qt::AlignLeft, m_label);
        p.drawText(r, Qt::AlignTop|Qt::AlignRight, QString::number(vmax, 'g', 4));
        p.drawText(r, Qt::AlignBottom|Qt::AlignRight, QString::number(vmin, 'g', 4));

        // Axis line
        p.setPen(QPen(palette().mid().color(), 1));
        if (vmin < 0 && vmax > 0) {
            double y0 = r.bottom() + vmin * dy;
            p.drawLine(QLineF(r.left(), y0, r.right(), y0));
        }
    }
private:
    QVector<double> m_data;
    int     m_components = 1;
    QString m_label;
};

// polyMesh helpers

static QMap<QString, QPair<int,int>> parseBoundary(const QString& caseDir)
{
    QMap<QString, QPair<int,int>> out;
    QFile f(caseDir + "/constant/polyMesh/boundary");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QString txt = QString::fromUtf8(f.readAll());
    // Strip C++ comments
    txt.replace(QRegularExpression("//[^\n]*"), "");

    // Match patch blocks that contain nFaces and startFace (in either order)
    QRegularExpression re(
        R"(\b(\w+)\s*\{[^{}]*\bnFaces\s+(\d+)\s*;[^{}]*\bstartFace\s+(\d+)\s*;[^{}]*\}|)"
        R"(\b(\w+)\s*\{[^{}]*\bstartFace\s+(\d+)\s*;[^{}]*\bnFaces\s+(\d+)\s*;[^{}]*\})");
    auto it = re.globalMatch(txt);
    while (it.hasNext()) {
        auto m = it.next();
        if (!m.captured(1).isEmpty())
            out[m.captured(1)] = { m.captured(3).toInt(), m.captured(2).toInt() };
        else
            out[m.captured(4)] = { m.captured(5).toInt(), m.captured(6).toInt() };
    }
    out.remove("FoamFile");
    return out;
}

static QVector<std::array<double,3>> parsePoints(const QString& caseDir)
{
    QVector<std::array<double,3>> pts;
    QFile f(caseDir + "/constant/polyMesh/points");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return pts;
    QTextStream in(&f);
    bool inside = false;
    static const QRegularExpression rePt(
        R"(\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))");
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        int ci = line.indexOf("//"); if (ci >= 0) line = line.left(ci).trimmed();
        if (!inside) { if (line == "(") inside = true; continue; }
        if (line == ")" || line == ");") break;
        auto m = rePt.match(line);
        if (m.hasMatch())
            pts.append({ m.captured(1).toDouble(),
                         m.captured(2).toDouble(),
                         m.captured(3).toDouble() });
    }
    return pts;
}

static QVector<QVector<int>> parseFaces(const QString& caseDir)
{
    QVector<QVector<int>> faces;
    QFile f(caseDir + "/constant/polyMesh/faces");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return faces;
    QTextStream in(&f);
    bool inside = false;
    static const QRegularExpression reFace(R"(\d+\(([^)]+)\))");
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        int ci = line.indexOf("//"); if (ci >= 0) line = line.left(ci).trimmed();
        if (!inside) { if (line == "(") inside = true; continue; }
        if (line == ")" || line == ");") break;
        auto m = reFace.match(line);
        if (!m.hasMatch()) continue;
        QVector<int> face;
        for (const QString& s : m.captured(1).split(
                 QRegularExpression("\\s+"), Qt::SkipEmptyParts))
            face << s.toInt();
        if (!face.isEmpty()) faces << face;
    }
    return faces;
}

// construction

DatasetPanel::DatasetPanel(QWidget* parent)
    : QWidget(parent)
    , m_client(new CFD::DatasetClient(this))
    , m_settings("CakeCFD", "DatasetPanel")
{
    buildUI();

    connect(m_client, &CFD::DatasetClient::datasetListReady,
            this, &DatasetPanel::onDatasetListReady);
    connect(m_client, &CFD::DatasetClient::queryFinished,
            this, &DatasetPanel::onQueryFinished);
    connect(m_client, &CFD::DatasetClient::queryRangeFinished,
            this, &DatasetPanel::onQueryRangeFinished);
    connect(m_client, &CFD::DatasetClient::queryRangeProgress,
            this, &DatasetPanel::onQueryRangeProgress);
    connect(m_client, &CFD::DatasetClient::error,
            this, &DatasetPanel::onError);

    m_endpointEdit->setText(m_settings.value("endpoint", "").toString());
    m_tokenEdit->setText(m_settings.value("token", "").toString());
    m_spatialInterp->setCurrentText(m_settings.value("spatialInterp", "Lag4").toString());
    m_temporalInterp->setCurrentText(m_settings.value("temporalInterp", "None").toString());

    // Only auto-refresh if an endpoint is already configured - avoids noisy startup errors
    // when the panel is first opened with no settings saved yet.
    if (!m_endpointEdit->text().trimmed().isEmpty())
        onRefreshDatasets();
    restoreResults();
}

void DatasetPanel::setCaseDir(const QString& dir)
{
    m_activeCaseDir = dir;
    // Reload patch list for this case
    if (m_patchCombo) {
        m_patchCombo->clear();
        m_patchCombo->addItem("(choose patch)");
        for (const QString& p : getPatchNames(dir))
            m_patchCombo->addItem(p);
    }
}

void DatasetPanel::buildUI()
{
    // Scroll area owns the content so the dock can shrink freely
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto* content = new QWidget;
    scroll->setWidget(content);

    auto* root = new QVBoxLayout(content);
    root->setSpacing(6);

    // API Settings
    auto* apiBox = new QGroupBox("API Settings");
    auto* apiL   = new QVBoxLayout(apiBox);

    auto* epRow = new QHBoxLayout;
    epRow->addWidget(new QLabel("Endpoint:"));
    m_endpointEdit = new QLineEdit;
    m_endpointEdit->setPlaceholderText("https://your-turbulence-server.example.com/api  (or SOAP .asmx endpoint)");
    epRow->addWidget(m_endpointEdit);
    apiL->addLayout(epRow);

    auto* tokRow = new QHBoxLayout;
    tokRow->addWidget(new QLabel("Token:      "));
    m_tokenEdit = new QLineEdit;
    m_tokenEdit->setEchoMode(QLineEdit::Password);
    m_tokenEdit->setPlaceholderText("API token / Bearer key for your server");
    tokRow->addWidget(m_tokenEdit);
    apiL->addLayout(tokRow);

    auto* interpRow = new QHBoxLayout;
    interpRow->addWidget(new QLabel("Spatial interp:"));
    m_spatialInterp = new QComboBox;
    m_spatialInterp->addItems({"Lag4", "Lag6", "Lag8", "None"});
    interpRow->addWidget(m_spatialInterp);
    interpRow->addSpacing(12);
    interpRow->addWidget(new QLabel("Temporal:"));
    m_temporalInterp = new QComboBox;
    m_temporalInterp->addItems({"None", "PCHIP"});
    interpRow->addWidget(m_temporalInterp);
    interpRow->addStretch();
    apiL->addLayout(interpRow);

    auto* apiBtnRow = new QHBoxLayout;

    auto* saveBtn    = new QPushButton("Save");
    saveBtn->setToolTip("Save API endpoint/token/interpolation settings for this session");
    auto* refreshBtn = new QPushButton("Refresh Datasets");
    auto* fmtBtn     = new QPushButton("Response Format...");
    fmtBtn->setToolTip("Configure custom server response parsing");
    apiBtnRow->addWidget(saveBtn);
    apiBtnRow->addWidget(refreshBtn);
    apiBtnRow->addWidget(fmtBtn);
    apiBtnRow->addStretch();
    apiL->addLayout(apiBtnRow);
    root->addWidget(apiBox);

    connect(saveBtn,    &QPushButton::clicked, this, &DatasetPanel::onSaveSettings);
    connect(refreshBtn, &QPushButton::clicked, this, &DatasetPanel::onRefreshDatasets);
    connect(fmtBtn,     &QPushButton::clicked, this, &DatasetPanel::onEditApiFormat);

    // Dataset
    auto* dbBox = new QGroupBox("Dataset");
    auto* dbL   = new QVBoxLayout(dbBox);

    m_datasetCombo = new QComboBox;
    dbL->addWidget(m_datasetCombo);

    m_datasetDesc = new QLabel("-");
    m_datasetDesc->setWordWrap(true);
    m_datasetDesc->setStyleSheet("font-size:11px;");
    dbL->addWidget(m_datasetDesc);

    auto* fieldRow = new QHBoxLayout;
    fieldRow->addWidget(new QLabel("Field:"));
    m_fieldCombo = new QComboBox;
    fieldRow->addWidget(m_fieldCombo, 1);
    dbL->addLayout(fieldRow);

    // Time: single vs range
    auto* tmodeRow = new QHBoxLayout;
    m_rbSingle = new QRadioButton("Single time:");
    m_rbRange  = new QRadioButton("Time range:");
    m_rbSingle->setChecked(true);
    auto* modeGrp = new QButtonGroup(this);
    modeGrp->addButton(m_rbSingle);
    modeGrp->addButton(m_rbRange);
    tmodeRow->addWidget(m_rbSingle);
    tmodeRow->addWidget(m_rbRange);
    tmodeRow->addStretch();
    dbL->addLayout(tmodeRow);

    // Single time widget
    m_singleWidget = new QWidget;
    auto* singleL = new QHBoxLayout(m_singleWidget);
    singleL->setContentsMargins(0,0,0,0);
    m_timeSpin = new QDoubleSpinBox;
    m_timeSpin->setDecimals(4);
    m_timeSpin->setSingleStep(0.002);
    m_timeSpin->setRange(0, 1e6);
    m_timeRangeHint = new QLabel;
    m_timeRangeHint->setStyleSheet("font-size:11px;");
    singleL->addWidget(m_timeSpin);
    singleL->addWidget(m_timeRangeHint);
    singleL->addStretch();
    dbL->addWidget(m_singleWidget);

    // Range widget
    m_rangeWidget = new QWidget;
    auto* rangeL = new QHBoxLayout(m_rangeWidget);
    rangeL->setContentsMargins(0,0,0,0);
    m_tStart = new QDoubleSpinBox; m_tStart->setDecimals(4); m_tStart->setPrefix("from ");
    m_tEnd   = new QDoubleSpinBox; m_tEnd->setDecimals(4);   m_tEnd->setPrefix("to ");
    m_tStep  = new QDoubleSpinBox; m_tStep->setDecimals(4);  m_tStep->setPrefix("step ");
    m_tStep->setValue(0.002); m_tStep->setMinimum(1e-6);
    m_tStepCount = new QLabel;
    m_tStepCount->setStyleSheet("font-size:11px;");
    rangeL->addWidget(m_tStart);
    rangeL->addWidget(m_tEnd);
    rangeL->addWidget(m_tStep);
    rangeL->addWidget(m_tStepCount);
    rangeL->addStretch();
    m_rangeWidget->hide();
    dbL->addWidget(m_rangeWidget);

    root->addWidget(dbBox);

    connect(m_datasetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DatasetPanel::onDatasetChanged);
    connect(m_rbSingle, &QRadioButton::toggled, this, &DatasetPanel::onTimeModeChanged);
    auto rangeUpdate = [this]{ onTimeRangeChanged(); };
    connect(m_tStart, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, rangeUpdate);
    connect(m_tEnd,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, rangeUpdate);
    connect(m_tStep,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, rangeUpdate);

    // Query Points
    auto* qBox = new QGroupBox("Query Points");
    auto* qL   = new QVBoxLayout(qBox);

    auto* patchRow = new QHBoxLayout;
    patchRow->addWidget(new QLabel("From mesh patch:"));
    m_patchCombo = new QComboBox;
    m_patchCombo->addItem("(choose patch)");
    m_patchCombo->setMinimumWidth(120);
    auto* clearPtsBtn    = new QPushButton("Clear Points");
    clearPtsBtn->setToolTip("Delete all current query points");
    auto* appendPatchBtn = new QPushButton("+ Append Patch");
    appendPatchBtn->setToolTip("Append face centers from selected patch to existing points");
    patchRow->addWidget(m_patchCombo);
    patchRow->addWidget(clearPtsBtn);
    patchRow->addWidget(appendPatchBtn);
    patchRow->addStretch();
    qL->addLayout(patchRow);

    m_ptCountLbl = new QLabel;
    m_ptCountLbl->setStyleSheet("font-size:11px; color:gray;");
    qL->addWidget(new QLabel("Points: one  x y z  per line:"));
    m_pointsEdit = new QPlainTextEdit;
    m_pointsEdit->setMaximumHeight(80);
    m_pointsEdit->setFont(QFont("Monospace", 9));
    m_pointsEdit->setPlaceholderText("0.5 0.5 0.5\n0.5 0.5 1.0\n0.5 0.5 1.5");
    qL->addWidget(m_pointsEdit);

    auto* wrapRow = new QHBoxLayout;
    wrapRow->addWidget(m_ptCountLbl);
    wrapRow->addStretch();
    m_autowrapBtn = new QPushButton("Autowrap...");
    m_autowrapBtn->setToolTip("Wrap coordinates into the dataset's periodic domain");
    m_autowrapBtn->setEnabled(false);
    wrapRow->addWidget(m_autowrapBtn);
    qL->addLayout(wrapRow);

    m_queryBtn      = new QPushButton("Query");
    m_queryBtn->setStyleSheet("font-weight:bold;");
    m_cancelBtn     = new QPushButton("Cancel");
    m_cancelBtn->setEnabled(false);
    m_exportBtn     = new QPushButton("Import to CakeCFD");
    m_exportRansBtn = new QPushButton("Export RANS BCs");
    m_copyPyBtn     = new QPushButton("Copy Python");
    m_viewReqBtn    = new QPushButton("View Request");
    m_viewReqBtn->setToolTip("Show the raw SOAP/REST request + curl/Python equivalents");
    m_exportBtn->setEnabled(false);
    m_exportRansBtn->setEnabled(false);
    m_copyPyBtn->setEnabled(false);

    // Row 1: primary actions
    auto* qBtnRow = new QHBoxLayout;
    qBtnRow->addWidget(m_queryBtn);
    qBtnRow->addWidget(m_cancelBtn);
    qBtnRow->addStretch();
    qL->addLayout(qBtnRow);

    // Row 2: secondary actions: each button expands equally with panel width
    auto* qBtnRow2 = new QHBoxLayout;
    qBtnRow2->setSpacing(4);
    for (auto* btn : {m_viewReqBtn, m_exportBtn, m_exportRansBtn, m_copyPyBtn}) {
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        qBtnRow2->addWidget(btn);
    }
    qL->addLayout(qBtnRow2);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setVisible(false);
    qL->addWidget(m_progress);
    root->addWidget(qBox);

    connect(m_queryBtn,     &QPushButton::clicked, this, &DatasetPanel::onQueryClicked);
    connect(m_cancelBtn,    &QPushButton::clicked, this, &DatasetPanel::onCancelClicked);
    connect(m_exportBtn,    &QPushButton::clicked, this, &DatasetPanel::onExportToBoundary);
    connect(m_exportRansBtn,&QPushButton::clicked, this, &DatasetPanel::onExportRansBcs);
    connect(m_copyPyBtn,    &QPushButton::clicked, this, &DatasetPanel::onCopyPython);
    connect(m_viewReqBtn,   &QPushButton::clicked, this, &DatasetPanel::onViewRequest);
    connect(clearPtsBtn,     &QPushButton::clicked, this, [this]{ m_pointsEdit->clear(); });
    connect(appendPatchBtn,  &QPushButton::clicked, this, [this]{ onLoadPatchPoints(true);  });
    connect(m_pointsEdit, &QPlainTextEdit::textChanged, this, [this]{
        m_settings.setValue("points", m_pointsEdit->toPlainText());
        int n = 0;
        for (const QString& line : m_pointsEdit->toPlainText().split('\n', Qt::SkipEmptyParts))
            if (line.trimmed().split(' ', Qt::SkipEmptyParts).size() >= 3) ++n;
        m_ptCountLbl->setText(n > 0 ? QString("%1 points queued").arg(n) : "");
        m_autowrapBtn->setEnabled(n > 0);
    });
    connect(m_autowrapBtn, &QPushButton::clicked, this, &DatasetPanel::onAutowrap);

    // Output
    auto* outBox = new QGroupBox("Output");
    auto* outL   = new QVBoxLayout(outBox);

    m_canvas = new ResultCanvas;
    outL->addWidget(m_canvas);

    m_statsLabel = new QLabel;
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setStyleSheet("font-size:11px; font-family:monospace;");
    outL->addWidget(m_statsLabel);

    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(160);
    m_log->setFont(QFont("Monospace", 9));
    outL->addWidget(m_log);

    auto* logBtnRow = new QHBoxLayout;
    auto* clearBtn     = new QPushButton("Clear");
    auto* exportCsvBtn = new QPushButton("Export CSV");
    logBtnRow->addStretch();
    logBtnRow->addWidget(clearBtn);
    logBtnRow->addWidget(exportCsvBtn);
    outL->addLayout(logBtnRow);
    root->addWidget(outBox);
    root->addStretch();

    connect(clearBtn, &QPushButton::clicked, this, [this]{
        m_log->clear(); m_canvas->clear(); m_statsLabel->clear();
    });
    connect(exportCsvBtn, &QPushButton::clicked, this, [this] {
        QString path = FileBrowserDialog::getSaveFileName(
            this, "Export CSV", QDir::homePath(), "CSV (*.csv)");
        if (path.isEmpty()) return;
        QFile f(path); if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&f) << m_log->toPlainText();
    });
}

// slots

void DatasetPanel::onSaveSettings()
{
    m_settings.setValue("endpoint",      m_endpointEdit->text().trimmed());
    m_settings.setValue("token",         m_tokenEdit->text().trimmed());
    m_settings.setValue("spatialInterp", m_spatialInterp->currentText());
    m_settings.setValue("temporalInterp",m_temporalInterp->currentText());

    static const QMap<QString,int> siMap = {{"Lag4",4},{"Lag6",6},{"Lag8",8},{"None",0}};
    m_client->setEndpoint(m_endpointEdit->text().trimmed());
    m_client->setToken(m_tokenEdit->text().trimmed());
    m_client->setSpatialInterp(
        static_cast<CFD::DatasetClient::SpatialInterp>(
            siMap.value(m_spatialInterp->currentText(), 4)));
    m_client->setTemporalInterp(
        m_temporalInterp->currentText() == "PCHIP"
            ? CFD::DatasetClient::TemporalInterp::PCHIP
            : CFD::DatasetClient::TemporalInterp::None);
    log("[Settings saved]");
}

void DatasetPanel::onRefreshDatasets()
{
    m_client->setEndpoint(m_endpointEdit->text().trimmed());
    m_client->setToken(m_tokenEdit->text().trimmed());
    m_client->fetchDatasetList();
}

void DatasetPanel::onDatasetListReady(const QList<CFD::DatasetInfo>& datasets)
{
    m_datasets = datasets;
    m_datasetCombo->blockSignals(true);
    m_datasetCombo->clear();
    for (const auto& d : datasets)
        m_datasetCombo->addItem(d.description.isEmpty() ? d.id
                                : d.id + " :  " + d.description);
    m_datasetCombo->blockSignals(false);
    if (!datasets.isEmpty()) onDatasetChanged(0);
    log(QString("[%1 datasets loaded]").arg(datasets.size()));
}

void DatasetPanel::onDatasetChanged(int idx)
{
    if (idx < 0 || idx >= m_datasets.size()) return;
    const auto& d = m_datasets[idx];
    m_datasetDesc->setText(d.description);
    m_fieldCombo->clear();
    m_fieldCombo->addItems(d.fields);

    auto setRange = [](QDoubleSpinBox* s, double lo, double hi, double step) {
        s->setRange(lo, hi > 0 ? hi : 1e6);
        if (step > 0) s->setSingleStep(step);
        s->setValue(lo);
    };
    setRange(m_timeSpin, d.tMin, d.tMax, d.dt);
    setRange(m_tStart, d.tMin, d.tMax, d.dt);
    setRange(m_tEnd,   d.tMin, d.tMax, d.dt);
    m_tEnd->setValue(qMin(d.tMin + d.dt * 10, d.tMax > 0 ? d.tMax : d.tMin + d.dt * 10));
    if (d.dt > 0) m_tStep->setValue(d.dt);
    m_timeRangeHint->setText(
        d.tMax > 0
        ? QString("t: %1: %2  (dt=%3)").arg(d.tMin).arg(d.tMax).arg(d.dt)
        : "");
    onTimeRangeChanged();
}

void DatasetPanel::onTimeModeChanged()
{
    m_singleWidget->setVisible(m_rbSingle->isChecked());
    m_rangeWidget->setVisible(!m_rbSingle->isChecked());
}

void DatasetPanel::onTimeRangeChanged()
{
    double tStart = m_tStart->value();
    double tEnd   = m_tEnd->value();
    double tStep  = m_tStep->value();
    if (tStep <= 0 || tEnd <= tStart) { m_tStepCount->clear(); return; }
    int n = static_cast<int>((tEnd - tStart) / tStep) + 1;
    m_tStepCount->setText(QString("(%1 snapshots)").arg(n));
}

void DatasetPanel::onQueryClicked()
{
    if (m_tokenEdit->text().trimmed().isEmpty()) {
        log("[ERROR] Set your API token first."); return;
    }
    onSaveSettings();

    QVector<double> pts;
    for (const QString& line :
         m_pointsEdit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
        auto parts = line.trimmed().split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 3)
            pts << parts[0].toDouble() << parts[1].toDouble() << parts[2].toDouble();
    }
    if (pts.isEmpty()) { log("[ERROR] No valid points (format: x y z per line)."); return; }

    m_lastPoints = pts;
    m_queryBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_exportBtn->setEnabled(false);
    m_exportRansBtn->setEnabled(false);
    m_copyPyBtn->setEnabled(false);
    m_canvas->clear();
    m_statsLabel->clear();

    int idx = m_datasetCombo->currentIndex();
    QString dataset = (idx >= 0 && idx < m_datasets.size())
                      ? m_datasets[idx].id : m_datasetCombo->currentText();
    QString field   = m_fieldCombo->currentText();

    // Record last query for Python export
    m_lastQuery.dataset  = dataset;
    m_lastQuery.field    = field;
    m_lastQuery.endpoint = m_endpointEdit->text().trimmed();
    m_lastQuery.points   = pts;
    m_lastQuery.spatialInterp  = QMap<QString,int>{{"Lag4",4},{"Lag6",6},{"Lag8",8},{"None",0}}
                                 .value(m_spatialInterp->currentText(), 4);
    m_lastQuery.temporalInterp = m_temporalInterp->currentText() == "PCHIP" ? 1 : 0;

    if (m_rbSingle->isChecked()) {
        double t = m_timeSpin->value();
        m_lastQuery.isRange = false;
        m_lastQuery.time = t;
        m_progress->setRange(0, 0); m_progress->setVisible(true);
        log(QString("Querying %1 / %2  t=%3  (%4 points)...")
            .arg(dataset, field).arg(t).arg(pts.size()/3));
        m_client->queryField(dataset, field, t, pts);
    } else {
        double tS = m_tStart->value(), tE = m_tEnd->value(), tSt = m_tStep->value();
        QVector<double> times;
        for (double t = tS; t <= tE + tSt*0.001; t += tSt) times << t;
        m_lastQuery.isRange = true;
        m_lastQuery.times   = times;
        m_progress->setRange(0, times.size()); m_progress->setValue(0);
        m_progress->setVisible(true);
        log(QString("Range query: %1 / %2  %3 to %4  (%5 steps, %6 points each)...")
            .arg(dataset, field).arg(tS).arg(tE).arg(times.size()).arg(pts.size()/3));
        m_client->queryFieldRange(dataset, field, times, pts);
    }
}

void DatasetPanel::onCancelClicked()
{
    m_client->cancelRange();
    m_queryBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progress->setVisible(false);
    log("[Cancelled]");
}

void DatasetPanel::onQueryFinished(const QString& dataset, const QString& field,
                                    double time, const QVector<double>& result)
{
    m_queryBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progress->setVisible(false);

    m_lastSingleResult  = result;
    m_lastRangeResults  = { result };
    m_lastRangeTimes    = { time };
    m_lastField         = field;
    m_lastTime          = time;
    m_lastComponents    = CFD::DatasetClient::componentCount(field);

    m_exportBtn->setEnabled(true);
    m_copyPyBtn->setEnabled(true);

    if (CitationManager::instance().hasCitation("custom_endpoint"))
        CitationManager::instance().cite("custom_endpoint");
    for (const QString& msg : CitationManager::instance().consumePending()) log(msg);

    log(QString("=== %1 / %2  t=%3 ===").arg(dataset, field).arg(time));
    int stride = m_lastComponents;
    for (int i = 0; i < result.size(); i += stride) {
        QStringList vals;
        for (int j = 0; j < stride && i+j < result.size(); ++j)
            vals << QString::number(result[i+j], 'g', 6);
        log("  " + vals.join("  "));
    }

    m_canvas->setData(result, m_lastComponents,
                      QString("%1 / t=%2").arg(field).arg(time));

    if (field == "velocity" && result.size() >= 3) {
        m_lastStats = CFD::DatasetClient::computeTurbStats(result);
        updateTurbStats();
        m_exportRansBtn->setEnabled(true);
    }
    saveResults();
}

void DatasetPanel::onQueryRangeFinished(const QString& dataset, const QString& field,
                                         const QVector<double>& times,
                                         const QList<QVector<double>>& results)
{
    m_queryBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progress->setVisible(false);

    m_lastRangeResults = results;
    m_lastRangeTimes   = times;
    m_lastField        = field;
    m_lastComponents   = CFD::DatasetClient::componentCount(field);

    m_exportBtn->setEnabled(true);
    m_copyPyBtn->setEnabled(true);

    log(QString("=== %1 / %2  range done: %3 snapshots ===")
        .arg(dataset, field).arg(results.size()));

    // Show last snapshot in canvas
    if (!results.isEmpty()) {
        m_lastSingleResult = results.last();
        m_canvas->setData(m_lastSingleResult, m_lastComponents,
                          QString("%1 / t=%2 (last of %3)")
                          .arg(field).arg(times.last()).arg(results.size()));
    }

    // Turb stats across all snapshots combined
    if (field == "velocity") {
        QVector<double> allVel;
        for (const auto& r : results) allVel += r;
        m_lastStats = CFD::DatasetClient::computeTurbStats(allVel);
        updateTurbStats();
        m_exportRansBtn->setEnabled(true);
    }

    if (CitationManager::instance().hasCitation("custom_endpoint"))
        CitationManager::instance().cite("custom_endpoint");
    for (const QString& msg : CitationManager::instance().consumePending()) log(msg);
    saveResults();
}

void DatasetPanel::onQueryRangeProgress(int step, int total)
{
    m_progress->setMaximum(total);
    m_progress->setValue(step);
    if (step % 5 == 0 || step == total)
        log(QString("  [%1/%2 snapshots received]").arg(step).arg(total));
}

void DatasetPanel::updateTurbStats()
{
    const auto& s = m_lastStats;
    m_statsLabel->setText(
        QString("k = %1 m²/s²   I = %2%   epsilon = %3 m²/s³   omega = %4 s⁻¹"
                "   |U| = %5 m/s")
        .arg(s.k, 0, 'g', 4)
        .arg(s.I * 100, 0, 'f', 2)
        .arg(s.epsilon, 0, 'g', 4)
        .arg(s.omega, 0, 'g', 4)
        .arg(s.Umag, 0, 'g', 4));
}

void DatasetPanel::onExportToBoundary()
{
    if (m_lastRangeResults.isEmpty()) { log("[ERROR] Run a query first."); return; }

    int ret = QMessageBox::question(this, "Import to CakeCFD",
        "This will write DNS inflow data to <b>constant/boundaryData/</b> and patch "
        "the inlet BC to <b>timeVaryingMappedFixedValue</b>.<br><br>"
        "The CakeCFD solver will automatically use this data as the inlet boundary condition.",
        QMessageBox::Ok | QMessageBox::Cancel);
    if (ret != QMessageBox::Ok) return;

    QString caseDir = m_activeCaseDir;
    if (caseDir.isEmpty())
        caseDir = FileBrowserDialog::getExistingDirectory(
            this, "Select case directory", QDir::homePath());
    if (caseDir.isEmpty()) return;

    bool ok;
    QString patch = QInputDialog::getText(
        this, "Patch Name", "Inlet patch name:", QLineEdit::Normal, "inlet", &ok);
    if (!ok || patch.isEmpty()) return;

    static const QMap<QString,QString> fieldToOF = {
        {"velocity","U"}, {"pressure","p"}, {"temperature","T"},
        {"vorticity","vorticity"}, {"magnetic","B"},
        {"velocity_gradient","gradU"}, {"pressure_gradient","gradP"}
    };
    QString ofField = fieldToOF.value(m_lastField, m_lastField);

    if (!writeBoundaryData(caseDir, patch, m_lastPoints,
                           m_lastRangeResults, m_lastRangeTimes, m_lastComponents)) {
        log("[ERROR] Failed to write boundaryData."); return;
    }
    if (!patchInletBC(caseDir, patch, ofField))
        log("[WARNING] Could not auto-patch 0/" + ofField + ": do it manually.");

    log(QString("[OK] Written: %1/constant/boundaryData/%2/").arg(caseDir, patch));
    log(QString("[OK] 0/%1 patched to timeVaryingMappedFixedValue").arg(ofField));
}

void DatasetPanel::onExportRansBcs()
{
    if (m_lastStats.k == 0) { log("[ERROR] No velocity data for turb stats."); return; }

    QString caseDir = m_activeCaseDir;
    if (caseDir.isEmpty())
        caseDir = FileBrowserDialog::getExistingDirectory(
            this, "Select case directory", QDir::homePath());
    if (caseDir.isEmpty()) return;

    int ret = QMessageBox::question(this, "Export RANS BCs",
        QString("This will overwrite <b>0/k</b>, <b>0/epsilon</b>, and <b>0/omega</b> "
                "in <b>%1</b> with turbulence statistics derived from the queried DNS data."
                "<br><br>"
                "<table>"
                "<tr><td>k&nbsp;&nbsp;</td><td><b>%2</b> m²/s²</td></tr>"
                "<tr><td>ε&nbsp;&nbsp;</td><td><b>%3</b> m²/s³</td></tr>"
                "<tr><td>ω&nbsp;&nbsp;</td><td><b>%4</b> s⁻¹</td></tr>"
                "</table>")
            .arg(caseDir)
            .arg(m_lastStats.k,       0, 'g', 4)
            .arg(m_lastStats.epsilon, 0, 'g', 4)
            .arg(m_lastStats.omega,   0, 'g', 4),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (ret != QMessageBox::Ok) return;

    if (writeRansBcs(caseDir, m_lastStats)) {
        log(QString("[OK] RANS BCs written to %1/0/  (k=%2, eps=%3, omega=%4)")
            .arg(caseDir)
            .arg(m_lastStats.k, 0, 'g', 4)
            .arg(m_lastStats.epsilon, 0, 'g', 4)
            .arg(m_lastStats.omega, 0, 'g', 4));
    } else {
        log("[ERROR] Failed to write RANS BC files.");
    }
}

void DatasetPanel::onCopyPython()
{
    QString code = generatePythonCode();
    QApplication::clipboard()->setText(code);
    log("[OK] Python code copied to clipboard.");
}

void DatasetPanel::onLoadPatchPoints(bool append)
{
    QString caseDir = m_activeCaseDir;
    if (caseDir.isEmpty()) {
        caseDir = FileBrowserDialog::getExistingDirectory(
            this, "Select case directory", QDir::homePath());
        if (caseDir.isEmpty()) return;
        m_activeCaseDir = caseDir;
    }

    QString patch = m_patchCombo->currentText();
    if (patch.isEmpty() || patch.startsWith("(")) {
        if (m_activeCaseDir.isEmpty())
            log("[ERROR] Open a case directory first (case must be loaded before patches are available).");
        else
            log("[ERROR] Select a patch from the dropdown first.");
        return;
    }

    log(QString("Reading face centers for patch '%1'...").arg(patch));
    QVector<double> centers = readPatchFaceCenters(caseDir, patch);
    if (centers.isEmpty()) {
        log("[ERROR] Could not read face centers: check constant/polyMesh/.");
        return;
    }

    int n = centers.size() / 3;
    QStringList lines;
    for (int i = 0; i < n; ++i)
        lines << QString("%1 %2 %3")
                 .arg(centers[i*3],   0, 'f', 8)
                 .arg(centers[i*3+1], 0, 'f', 8)
                 .arg(centers[i*3+2], 0, 'f', 8);
    if (append && !m_pointsEdit->toPlainText().trimmed().isEmpty())
        m_pointsEdit->appendPlainText(lines.join('\n'));
    else
        m_pointsEdit->setPlainText(lines.join('\n'));
    log(QString("[OK] %1 %2 face centers from patch '%3'")
        .arg(append ? "Appended" : "Loaded").arg(n).arg(patch));
}

void DatasetPanel::onAutowrap()
{
    // Parse current points
    QVector<double> pts;
    for (const QString& line : m_pointsEdit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
        auto p = line.trimmed().split(' ', Qt::SkipEmptyParts);
        if (p.size() >= 3) pts << p[0].toDouble() << p[1].toDouble() << p[2].toDouble();
    }
    if (pts.isEmpty()) return;

    static constexpr double kTwoPi = 6.283185307179586;

    // Config dialog
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Autowrap Configuration");
    dlg->setFixedWidth(360);

    auto* vl = new QVBoxLayout(dlg);

    // Method selector
    auto* methodRow = new QHBoxLayout;
    methodRow->addWidget(new QLabel("Method:"));
    auto* rbMod   = new QRadioButton("Periodic (mod)");
    auto* rbScale = new QRadioButton("Linear scale");
    rbMod->setChecked(true);
    auto* modeGrp = new QButtonGroup(dlg);
    modeGrp->addButton(rbMod); modeGrp->addButton(rbScale);
    methodRow->addWidget(rbMod); methodRow->addWidget(rbScale);
    methodRow->addStretch();
    vl->addLayout(methodRow);

    auto* modHint = new QLabel("Periodic: maps each coordinate via modulo - preserves turbulence statistics.");
    modHint->setWordWrap(true);
    modHint->setStyleSheet("font-size:11px; color:gray;");
    vl->addWidget(modHint);

    // Domain bounds
    vl->addWidget(new QLabel("Target domain bounds:"));
    auto* form = new QFormLayout;
    auto mkSpin = [](double v) {
        auto* s = new QDoubleSpinBox;
        s->setRange(-1e9, 1e9); s->setDecimals(6); s->setValue(v);
        return s;
    };
    auto* xMin = mkSpin(0);        auto* xMax = mkSpin(kTwoPi);
    auto* yMin = mkSpin(0);        auto* yMax = mkSpin(kTwoPi);
    auto* zMin = mkSpin(0);        auto* zMax = mkSpin(kTwoPi);
    auto mkRow = [&](QDoubleSpinBox* lo, QDoubleSpinBox* hi) {
        auto* r = new QHBoxLayout; r->addWidget(lo); r->addWidget(new QLabel("to")); r->addWidget(hi);
        return r;
    };
    form->addRow("X:", mkRow(xMin, xMax));
    form->addRow("Y:", mkRow(yMin, yMax));
    form->addRow("Z:", mkRow(zMin, zMax));
    vl->addLayout(form);

    // Preview label
    auto* previewLbl = new QLabel;
    previewLbl->setStyleSheet("font-size:11px; color:gray;");
    previewLbl->setWordWrap(true);
    vl->addWidget(previewLbl);

    // Compute + show preview
    auto updatePreview = [&]{
        double xS = xMax->value()-xMin->value();
        double yS = yMax->value()-yMin->value();
        double zS = zMax->value()-zMin->value();
        if (xS <= 0 || yS <= 0 || zS <= 0) {
            previewLbl->setText("Domain size must be positive.");
            return;
        }
        int n = pts.size() / 3;
        double sxMin=pts[0],sxMax=pts[0],syMin=pts[1],syMax=pts[1],szMin=pts[2],szMax=pts[2];
        for (int i=0;i<n;++i){
            sxMin=qMin(sxMin,pts[i*3]); sxMax=qMax(sxMax,pts[i*3]);
            syMin=qMin(syMin,pts[i*3+1]); syMax=qMax(syMax,pts[i*3+1]);
            szMin=qMin(szMin,pts[i*3+2]); szMax=qMax(szMax,pts[i*3+2]);
        }
        previewLbl->setText(
            QString("Source bbox: X[%1, %2]  Y[%3, %4]  Z[%5, %6]\n"
                    "%7 points will be remapped to [%8, %9]³ approx.")
            .arg(sxMin,0,'f',3).arg(sxMax,0,'f',3)
            .arg(syMin,0,'f',3).arg(syMax,0,'f',3)
            .arg(szMin,0,'f',3).arg(szMax,0,'f',3)
            .arg(n)
            .arg(xMin->value(),0,'f',3).arg(xMax->value(),0,'f',3));
    };
    connect(xMin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),dlg,[&]{ updatePreview(); });
    connect(xMax,QOverload<double>::of(&QDoubleSpinBox::valueChanged),dlg,[&]{ updatePreview(); });
    connect(yMin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),dlg,[&]{ updatePreview(); });
    connect(yMax,QOverload<double>::of(&QDoubleSpinBox::valueChanged),dlg,[&]{ updatePreview(); });
    connect(zMin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),dlg,[&]{ updatePreview(); });
    connect(zMax,QOverload<double>::of(&QDoubleSpinBox::valueChanged),dlg,[&]{ updatePreview(); });
    updatePreview();

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    vl->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    if (dlg->exec() != QDialog::Accepted) return;

    // Apply wrapping
    double xS = xMax->value() - xMin->value();
    double yS = yMax->value() - yMin->value();
    double zS = zMax->value() - zMin->value();
    if (xS <= 0 || yS <= 0 || zS <= 0) return;

    bool doMod = rbMod->isChecked();
    int n = pts.size() / 3;

    // For linear scale: compute source bbox
    double srcXMin=pts[0],srcXMax=pts[0],srcYMin=pts[1],srcYMax=pts[1],srcZMin=pts[2],srcZMax=pts[2];
    if (!doMod) {
        for (int i=0;i<n;++i){
            srcXMin=qMin(srcXMin,pts[i*3]); srcXMax=qMax(srcXMax,pts[i*3]);
            srcYMin=qMin(srcYMin,pts[i*3+1]); srcYMax=qMax(srcYMax,pts[i*3+1]);
            srcZMin=qMin(srcZMin,pts[i*3+2]); srcZMax=qMax(srcZMax,pts[i*3+2]);
        }
    }

    auto wrap = [](double v, double lo, double sz) -> double {
        double r = std::fmod(v - lo, sz);
        if (r < 0) r += sz;
        return lo + r;
    };

    QStringList lines;
    for (int i = 0; i < n; ++i) {
        double x = pts[i*3], y = pts[i*3+1], z = pts[i*3+2];
        if (doMod) {
            x = wrap(x, xMin->value(), xS);
            y = wrap(y, yMin->value(), yS);
            z = wrap(z, zMin->value(), zS);
        } else {
            double sxS = srcXMax - srcXMin; if (sxS < 1e-12) sxS = 1;
            double syS = srcYMax - srcYMin; if (syS < 1e-12) syS = 1;
            double szS = srcZMax - srcZMin; if (szS < 1e-12) szS = 1;
            x = xMin->value() + (x - srcXMin) / sxS * xS;
            y = yMin->value() + (y - srcYMin) / syS * yS;
            z = zMin->value() + (z - srcZMin) / szS * zS;
        }
        lines << QString("%1 %2 %3").arg(x,0,'f',8).arg(y,0,'f',8).arg(z,0,'f',8);
    }

    m_pointsEdit->setPlainText(lines.join('\n'));
    log(QString("[Autowrap] %1 points remapped via %2 -> domain [%3, %4]^3")
        .arg(n).arg(doMod ? "mod" : "scale")
        .arg(xMin->value(),0,'f',4).arg(xMax->value(),0,'f',4));
}

void DatasetPanel::onError(const QString& msg)
{
    m_queryBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progress->setVisible(false);
    log("[ERROR] " + msg);
}


void DatasetPanel::onEditApiFormat()
{
    ApiFormatDialog dlg(m_settings, this);
    if (dlg.exec() == QDialog::Accepted) {
        CitationManager::Citation c = dlg.citationData();
        if (!c.authors.isEmpty() || !c.doi.isEmpty())
            CitationManager::instance().registerCustom("custom_endpoint", c);
    }
}

void DatasetPanel::saveResults()
{
    QJsonArray arr;
    for (double v : m_lastSingleResult) arr.append(v);
    m_settings.setValue("result/data",       QJsonDocument(arr).toJson(QJsonDocument::Compact));
    m_settings.setValue("result/field",      m_lastField);
    m_settings.setValue("result/time",       m_lastTime);
    m_settings.setValue("result/components", m_lastComponents);
    m_settings.setValue("result/stats/k",       m_lastStats.k);
    m_settings.setValue("result/stats/I",       m_lastStats.I);
    m_settings.setValue("result/stats/epsilon", m_lastStats.epsilon);
    m_settings.setValue("result/stats/omega",   m_lastStats.omega);
    m_settings.setValue("result/stats/Umag",    m_lastStats.Umag);
    m_settings.setValue("result/log",           m_log->toHtml());
}

void DatasetPanel::restoreResults()
{
    m_pointsEdit->setPlainText(m_settings.value("points", "").toString());

    QByteArray raw = m_settings.value("result/data", "").toByteArray();
    if (raw.isEmpty()) return;
    auto doc = QJsonDocument::fromJson(raw);
    if (!doc.isArray()) return;

    for (const auto& v : doc.array()) m_lastSingleResult << v.toDouble();
    m_lastField      = m_settings.value("result/field",      "velocity").toString();
    m_lastTime       = m_settings.value("result/time",       0.0).toDouble();
    m_lastComponents = m_settings.value("result/components", 3).toInt();

    m_lastStats.k       = m_settings.value("result/stats/k",       0.0).toDouble();
    m_lastStats.I       = m_settings.value("result/stats/I",       0.0).toDouble();
    m_lastStats.epsilon = m_settings.value("result/stats/epsilon", 0.0).toDouble();
    m_lastStats.omega   = m_settings.value("result/stats/omega",   0.0).toDouble();
    m_lastStats.Umag    = m_settings.value("result/stats/Umag",    0.0).toDouble();

    m_canvas->setData(m_lastSingleResult, m_lastComponents,
                      QString("%1 / t=%2 [restored]").arg(m_lastField).arg(m_lastTime));

    if (m_lastStats.k > 0) { updateTurbStats(); m_exportRansBtn->setEnabled(true); }
    if (!m_lastSingleResult.isEmpty()) {
        // Reconstruct a single-entry range so Import to CakeCFD works on restore
        m_lastRangeResults = { m_lastSingleResult };
        m_lastRangeTimes   = { m_lastTime };
        m_exportBtn->setEnabled(true);
        m_copyPyBtn->setEnabled(true);
    }

    QString logHtml = m_settings.value("result/log", "").toString();
    if (!logHtml.isEmpty()) m_log->setHtml(logHtml);
}

void DatasetPanel::log(const QString& msg) { m_log->append(msg); }

// export helpers

bool DatasetPanel::writeBoundaryData(const QString& caseDir, const QString& patch,
                                      const QVector<double>& points,
                                      const QList<QVector<double>>& valuesByTime,
                                      const QVector<double>& times,
                                      int components)
{
    QString baseDir = caseDir + "/constant/boundaryData/" + patch;
    QDir().mkpath(baseDir);
    int nPts = points.size() / 3;

    // points file
    {
        QFile f(baseDir + "/points");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{\n    version 2.0; format ascii;\n"
          << "    class vectorField; object points;\n}\n" << nPts << "\n(\n";
        for (int i = 0; i < nPts; ++i)
            o << QString("  (%1 %2 %3)\n")
                 .arg(points[i*3]).arg(points[i*3+1]).arg(points[i*3+2]);
        o << ")\n";
    }

    QString cls   = (components == 3) ? "vectorField" : "scalarField";
    QString fname = (components == 3) ? "U" : "p";

    for (int ti = 0; ti < times.size() && ti < valuesByTime.size(); ++ti) {
        QString timeDir = baseDir + "/" + QString::number(times[ti], 'f', 4);
        QDir().mkpath(timeDir);
        const auto& vals = valuesByTime[ti];
        QFile f(timeDir + "/" + fname);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) continue;
        QTextStream o(&f);
        o << "FoamFile\n{\n    version 2.0; format ascii;\n"
          << "    class " << cls << "; object " << fname << ";\n}\n"
          << nPts << "\n(\n";
        for (int i = 0; i < nPts; ++i) {
            if (components == 3)
                o << QString("  (%1 %2 %3)\n")
                     .arg(i*3   < vals.size() ? vals[i*3]   : 0.0)
                     .arg(i*3+1 < vals.size() ? vals[i*3+1] : 0.0)
                     .arg(i*3+2 < vals.size() ? vals[i*3+2] : 0.0);
            else
                o << "  " << (i < vals.size() ? vals[i] : 0.0) << "\n";
        }
        o << ")\n";
    }
    return true;
}

bool DatasetPanel::patchInletBC(const QString& caseDir, const QString& patch,
                                 const QString& field)
{
    QString bcFile = caseDir + "/0/" + field;
    QFile f(bcFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QString src = QString::fromUtf8(f.readAll());
    f.close();

    QRegularExpression re(
        patch + R"(\s*\{[^}]*\})",
        QRegularExpression::DotMatchesEverythingOption);
    QString repl = patch + "\n    {\n"
        "        type            timeVaryingMappedFixedValue;\n"
        "        setAverage      off;\n"
        "        offset          " + (field == "U" ? "(0 0 0)" : "0") + ";\n"
        "        fieldName       " + field + ";\n"
        "        perturb         0;\n"
        "    }";
    QString patched = src;
    patched.replace(re, repl);
    if (patched == src) return false;
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&f) << patched;
    return true;
}

bool DatasetPanel::writeRansBcs(const QString& caseDir, const CFD::TurbStats& s)
{
    QDir d0(caseDir + "/0");
    if (!d0.exists()) return false;

    auto fh = [](const QString& cls, const QString& obj) {
        return QString("FoamFile\n{\n    version 2.0;\n    format ascii;\n"
                       "    class %1; object %2;\n}\n\n").arg(cls, obj);
    };
    auto writeScalar = [&](const QString& name, const QString& dims,
                           double val) -> bool {
        QFile f(caseDir + "/0/" + name);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << fh("volScalarField", name)
          << "dimensions " << dims << ";\n"
          << "internalField uniform " << QString::number(val, 'g', 6) << ";\n"
          << "boundaryField\n{\n"
          << "    inlet  { type fixedValue; value uniform " << val << "; }\n"
          << "    outlet { type zeroGradient; }\n"
          << "    #include \"include/wallBC_" << name << "\"\n"
          // The include ends with a ".*" wildcard so the geometry wall patch (named
          // after the case) picks up the wall function without knowing its name. That
          // wildcard also swallows the symmetry domain faces, and OpenFOAM aborts on a
          // wall function applied to a symmetry patch. Among regex entries the LAST
          // match wins, so re-declare them after the include to take them back.
          << "    \"(top|bottom|front|back|sides)\" { type symmetry; }\n"
          << "}\n";
        return true;
    };

    // Create 0/include/ with wildcard wall-function entries if not already there
    QDir(caseDir + "/0/include").mkpath(".");
    auto writeWallBC = [&](const QString& name, const QString& wallType) {
        QString path = caseDir + "/0/include/wallBC_" + name;
        if (QFile::exists(path)) return;  // don't overwrite user-customised files
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream(&f) << "\".*\"\n{\n"
                        << "    type            " << wallType << ";\n"
                        << "    value           $internalField;\n"
                        << "}\n";
    };
    writeWallBC("k",       "kqRWallFunction");
    writeWallBC("epsilon", "epsilonWallFunction");
    writeWallBC("omega",   "omegaWallFunction");

    bool ok = true;
    ok &= writeScalar("k",       "[0 2 -2 0 0 0 0]", s.k);
    ok &= writeScalar("epsilon", "[0 2 -3 0 0 0 0]", s.epsilon > 0 ? s.epsilon : 1e-4);
    ok &= writeScalar("omega",   "[0 0 -1 0 0 0 0]", s.omega   > 0 ? s.omega   : 1.0);
    return ok;
}

QStringList DatasetPanel::getPatchNames(const QString& caseDir)
{
    return parseBoundary(caseDir).keys();
}

QVector<double> DatasetPanel::readPatchFaceCenters(const QString& caseDir,
                                                    const QString& patch)
{
    auto boundary = parseBoundary(caseDir);
    if (!boundary.contains(patch)) return {};
    auto [startFace, nFaces] = boundary[patch];

    auto pts   = parsePoints(caseDir);
    auto faces = parseFaces(caseDir);
    if (pts.isEmpty() || faces.isEmpty()) return {};

    QVector<double> centers;
    centers.reserve(nFaces * 3);
    for (int fi = startFace; fi < startFace + nFaces && fi < faces.size(); ++fi) {
        const auto& face = faces[fi];
        double cx = 0, cy = 0, cz = 0;
        for (int vi : face) {
            if (vi < pts.size()) {
                cx += pts[vi][0]; cy += pts[vi][1]; cz += pts[vi][2];
            }
        }
        int nv = face.size();
        if (nv > 0) centers << cx/nv << cy/nv << cz/nv;
    }
    return centers;
}

// Request viewer

static QString buildSoapXml(const QString& method, const QString& token,
                              const QString& dataset, double time,
                              const QVector<double>& points, int si, int ti,
                              const QString& ns)
{
    QString pts;
    for (int i = 0; i + 2 < points.size(); i += 3)
        pts += QString("        <tns:Point3>"
                       "<tns:x>%1</tns:x>"
                       "<tns:y>%2</tns:y>"
                       "<tns:z>%3</tns:z>"
                       "</tns:Point3>\n")
               .arg(points[i]).arg(points[i+1]).arg(points[i+2]);

    return QString(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<soap:Envelope\n"
        "    xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\"\n"
        "    xmlns:tns=\"%8\">\n"
        "  <soap:Body>\n"
        "    <tns:%1>\n"
        "      <tns:authToken>%2</tns:authToken>\n"
        "      <tns:dataset>%3</tns:dataset>\n"
        "      <tns:time>%4</tns:time>\n"
        "      <tns:spatialInterpolation>%5</tns:spatialInterpolation>\n"
        "      <tns:temporalInterpolation>%6</tns:temporalInterpolation>\n"
        "      <tns:points>\n"
        "%7"
        "      </tns:points>\n"
        "    </tns:%1>\n"
        "  </soap:Body>\n"
        "</soap:Envelope>")
        .arg(method, token.isEmpty() ? "YOUR_TOKEN_HERE" : token,
             dataset).arg(time).arg(si).arg(ti).arg(pts).arg(ns);
}

void DatasetPanel::onViewRequest()
{
    static const QMap<QString,QString> fieldToMethod = {
        {"velocity","GetVelocity"}, {"pressure","GetPressure"},
        {"vorticity","GetVorticity"}, {"magnetic","GetMagneticField"},
        {"temperature","GetTemperature"}, {"velocity_gradient","GetVelocityGradient"},
        {"pressure_gradient","GetPressureGradient"}
    };
    static const QMap<QString,int> siMap = {{"Lag4",4},{"Lag6",6},{"Lag8",8},{"None",0}};

    QString endpoint = m_endpointEdit->text().trimmed();
    if (endpoint.isEmpty()) { log("[ERROR] Set an endpoint URL first."); return; }
    QString token   = m_tokenEdit->text().trimmed();
    QString dataset = m_datasetCombo->currentIndex() >= 0 && m_datasetCombo->currentIndex() < m_datasets.size()
                      ? m_datasets[m_datasetCombo->currentIndex()].id
                      : m_datasetCombo->currentText();
    QString field   = m_fieldCombo->currentText();
    QString method  = fieldToMethod.value(field, "GetVelocity");
    double  t       = m_timeSpin->value();
    int     si      = siMap.value(m_spatialInterp->currentText(), 4);
    int     ti      = m_temporalInterp->currentText() == "PCHIP" ? 1 : 0;

    // Collect points
    QVector<double> pts;
    for (const QString& line :
         m_pointsEdit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
        auto p = line.trimmed().split(' ', Qt::SkipEmptyParts);
        if (p.size() >= 3) pts << p[0].toDouble() << p[1].toDouble() << p[2].toDouble();
    }
    if (pts.isEmpty()) pts << 0.5 << 0.5 << 0.5;  // placeholder if none entered

    QString ns = endpoint.endsWith(".asmx", Qt::CaseInsensitive)
                 ? QUrl(endpoint).adjusted(QUrl::RemoveFilename | QUrl::RemoveQuery).toString()
                 : endpoint + "/";
    QString soapXml = buildSoapXml(method, token, dataset, t, pts, si, ti, ns);
    QString soapAction = QString("\"%1%2\"").arg(ns, method);

    // curl script
    QString curlScript = QString(
        "# Save the SOAP body to request.xml first, then:\n\n"
        "curl -X POST \\\n"
        "  \"%1\" \\\n"
        "  -H \"Content-Type: text/xml; charset=utf-8\" \\\n"
        "  -H \"SOAPAction: %2\" \\\n"
        "  --data-binary @request.xml\n\n"
        "#: or inline (single point example) :\n"
        "curl -X POST \\\n"
        "  \"%1\" \\\n"
        "  -H \"Content-Type: text/xml; charset=utf-8\" \\\n"
        "  -H \"SOAPAction: %2\" \\\n"
        "  -d '%3'")
        .arg(endpoint, soapAction, soapXml);

    // Python requests script
    QString pyScript = QString(
        "\"\"\"\n"
        "Raw HTTP query script: CakeCFD Studio export.\n"
        "Works against any SOAP-compatible turbulence database or REST server.\n"
        "Adapt the REST JSON section at the bottom for custom servers.\n"
        "\"\"\"\n\n"
        "import requests\n\n"
        "#  Server settings \n"
        "ENDPOINT = \"%1\"\n"
        "# SOAPAction tells the server which RPC method to invoke.\n"
        "# Format: \"http://<server-namespace>/<MethodName>\"\n"
        "# Change the method name to match what your server exposes\n"
        "# (e.g. GetPressure, GetVorticity, GetVelocityGradient …)\n"
        "SOAP_ACTION = %2\n\n"
        "#  Auth token \n"
        "# Whatever API key / Bearer token your server expects.\n"
        "# Keep this out of version control: use an env var in production:\n"
        "#   import os; AUTH_TOKEN = os.environ[\"TURBDB_TOKEN\"]\n"
        "AUTH_TOKEN = \"%4\"\n\n"
        "#  SOAP request body \n"
        "# This is the full XML envelope sent as the POST body.\n"
        "# <tns:dataset>    : dataset name on the server\n"
        "# <tns:time>       : simulation time to query\n"
        "# <tns:spatialInterpolation> : 0=None, 4=Lag4, 6=Lag6, 8=Lag8\n"
        "# <tns:temporalInterpolation>: 0=None, 1=PCHIP\n"
        "# <tns:Point3>     : one query point; add as many as needed\n"
        "SOAP_BODY = \"\"\"%3\"\"\"\n\n"
        "#  Send the request \n"
        "resp = requests.post(\n"
        "    ENDPOINT,\n"
        "    headers={\n"
        "        \"Content-Type\": \"text/xml; charset=utf-8\",\n"
        "        # SOAPAction header is required: servers use it to route the call\n"
        "        \"SOAPAction\": SOAP_ACTION,\n"
        "        \"User-Agent\": \"CakeCFD/1.0\",\n"
        "    },\n"
        "    data=SOAP_BODY.encode(\"utf-8\"),\n"
        "    timeout=60,  # seconds: increase for large point batches\n"
        ")\n"
        "print(\"Status:\", resp.status_code)\n"
        "print(resp.text[:4000])  # trim for readability; remove slice for full response\n\n"
        "#  Parsing the response \n"
        "# SOAP XML response typically looks like:\n"
        "#   <GetVelocityResponse>\n"
        "#     <GetVelocityResult>\n"
        "#       <Vector3><x>…</x><y>…</y><z>…</z></Vector3>\n"
        "#     </GetVelocityResult>\n"
        "#   </GetVelocityResponse>\n"
        "# import xml.etree.ElementTree as ET\n"
        "# root = ET.fromstring(resp.text)\n"
        "# for v in root.iter('Vector3'):\n"
        "#     print(v.find('x').text, v.find('y').text, v.find('z').text)\n\n"
        "# ═══════════════════════════════════════════════════════════════════════\n"
        "# REST JSON server (alternative to SOAP)\n"
        "# ═══════════════════════════════════════════════════════════════════════\n"
        "# If your server speaks JSON instead of SOAP, use this block instead.\n"
        "# Adjust the key names to match your API spec.\n"
        "# import json\n"
        "# body = {\n"
        "#     \"token\": AUTH_TOKEN,       # or \"apiKey\", \"Authorization\", etc.\n"
        "#     \"dataset\": \"%5\",\n"
        "#     \"field\": \"%6\",           # field name your server understands\n"
        "#     \"time\": %7,\n"
        "#     \"spatialInterpolation\": \"%8\",\n"
        "#     \"points\": [               # list of query locations\n")
        .arg(endpoint, soapAction, soapXml,
             token.isEmpty() ? "YOUR_TOKEN_HERE" : token,
             dataset, field).arg(t)
        .arg(m_spatialInterp->currentText());

    for (int i = 0; i + 2 < pts.size(); i += 3)
        pyScript += QString("#         {\"x\": %1, \"y\": %2, \"z\": %3},\n")
                    .arg(pts[i]).arg(pts[i+1]).arg(pts[i+2]);

    pyScript +=
        "#     ]\n"
        "# }\n"
        "# resp = requests.post(ENDPOINT + \"/query\", json=body)\n"
        "# data = resp.json()\n"
        "# # Navigate your server's response structure:\n"
        "# for point_result in data[\"results\"]:\n"
        "#     print(point_result[\"vx\"], point_result[\"vy\"], point_result[\"vz\"])\n";

    // dialog
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Query Request Script: " + field + " / " + dataset);
    dlg->resize(720, 560);

    auto* vl = new QVBoxLayout(dlg);

    auto* tabs = new QTabWidget(dlg);
    QFont mono("Monospace"); mono.setStyleHint(QFont::TypeWriter); mono.setPointSize(10);

    auto makeTab = [&](const QString& label, const QString& text) {
        auto* ed = new QPlainTextEdit;
        ed->setFont(mono);
        ed->setPlainText(text);
        ed->setLineWrapMode(QPlainTextEdit::NoWrap);
        tabs->addTab(ed, label);
        return ed;
    };

    makeTab("SOAP XML",        soapXml);
    makeTab("curl",            curlScript);
    makeTab("Python requests", pyScript);

    vl->addWidget(tabs);

    auto* hint = new QLabel(
        "Edit any tab and Save to write to disk, or Save Globally to keep it as a reusable script.", dlg);
    hint->setStyleSheet("font-size:11px; color:#888;");
    hint->setWordWrap(true);
    vl->addWidget(hint);

    // Button row: Save (pick location) | Save Globally (to ~/.config/CakeCFD/scripts/) | Close
    auto* btnRow    = new QHBoxLayout;
    auto* btnSave   = new QPushButton("Save", dlg);
    btnSave->setToolTip("Save the current tab's script to a location you choose");
    auto* btnGlobal = new QPushButton("Save Globally", dlg);
    btnGlobal->setToolTip(
        "Save to your global scripts folder so it's always available.\n"
        "Stored in: ~/.config/CakeCFD/scripts/");
    auto* btnClose  = new QPushButton("Close", dlg);
    btnRow->addWidget(btnSave);
    btnRow->addWidget(btnGlobal);
    btnRow->addStretch();
    btnRow->addWidget(btnClose);
    vl->addLayout(btnRow);

    auto saveScript = [dlg, tabs](const QString& destDir) {
        auto* ed = qobject_cast<QPlainTextEdit*>(tabs->currentWidget());
        if (!ed) return;
        int idx    = tabs->currentIndex();
        QString suffix = idx == 2 ? ".py" : idx == 1 ? ".sh" : ".xml";
        QString filter = idx == 2 ? "Python (*.py)" : idx == 1 ? "Shell (*.sh)" : "XML (*.xml)";
        QString path;
        if (destDir.isEmpty()) {
            path = FileBrowserDialog::getSaveFileName(
                dlg, "Save Request Script", QDir::homePath() + "/request" + suffix, filter);
            if (path.isEmpty()) return;
        } else {
            QDir().mkpath(destDir);
            path = destDir + "/request" + suffix;
        }
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(ed->toPlainText().toUtf8());
            if (!destDir.isEmpty())
                QMessageBox::information(dlg, "Saved Globally",
                    "Script saved to:\n" + path);
        } else {
            QMessageBox::warning(dlg, "Save Failed", "Could not write to:\n" + path);
        }
    };

    connect(btnSave,   &QPushButton::clicked, dlg, [saveScript]{ saveScript(QString{}); });
    connect(btnGlobal, &QPushButton::clicked, dlg, [saveScript]{
        saveScript(QDir::homePath() + "/.config/CakeCFD/scripts");
    });
    connect(btnClose,  &QPushButton::clicked, dlg, &QDialog::close);

    dlg->show();
}

// Python code generation

QString DatasetPanel::generatePythonCode() const
{
    static const QMap<QString,QString> soapMethod = {
        {"velocity","GetVelocity"}, {"pressure","GetPressure"},
        {"vorticity","GetVorticity"}, {"magnetic","GetMagneticField"},
        {"temperature","GetTemperature"}, {"velocity_gradient","GetVelocityGradient"},
        {"pressure_gradient","GetPressureGradient"}
    };

    const auto& q = m_lastQuery;
    QString code;
    QTextStream o(&code);

    bool useSoap = q.endpoint.endsWith(".asmx", Qt::CaseInsensitive);
    QString ns   = useSoap
                   ? QUrl(q.endpoint).adjusted(QUrl::RemoveFilename | QUrl::RemoveQuery).toString()
                   : q.endpoint + "/";
    QString method = soapMethod.value(q.field, "GetVelocity");

    o << "# Generated by CakeCFD Studio\n"
      << "# Endpoint: " << q.endpoint << "\n\n"
      << "import requests\nimport numpy as np\n\n"
      << "ENDPOINT  = '" << q.endpoint << "'\n"
      << "AUTH_TOKEN = 'YOUR_TOKEN_HERE'  # replace with your server token\n"
      << "DATASET   = '" << q.dataset << "'\n"
      << "FIELD     = '" << q.field   << "'\n\n"
      << "POINTS = np.array([\n";

    for (int i = 0; i + 2 < q.points.size(); i += 3)
        o << "    [" << q.points[i] << ", "
          << q.points[i+1] << ", " << q.points[i+2] << "],\n";
    o << "], dtype=np.float64)\n\n";

    if (useSoap) {
        o << "#  SOAP query \n"
          << "def query_soap(time):\n"
          << "    pts_xml = ''.join(\n"
          << "        f'<Point3><x>{p[0]}</x><y>{p[1]}</y><z>{p[2]}</z></Point3>'\n"
          << "        for p in POINTS)\n"
          << "    body = f\"\"\"\n"
          << "<?xml version='1.0' encoding='utf-8'?>\n"
          << "<soap:Envelope xmlns:soap='http://schemas.xmlsoap.org/soap/envelope/'\n"
          << "               xmlns:tns='" << ns << "'>\n"
          << "  <soap:Body>\n"
          << "    <tns:" << method << ">\n"
          << "      <tns:authToken>{AUTH_TOKEN}</tns:authToken>\n"
          << "      <tns:dataset>{DATASET}</tns:dataset>\n"
          << "      <tns:time>{time}</tns:time>\n"
          << "      <tns:spatialInterp>" << q.spatialInterp << "</tns:spatialInterp>\n"
          << "      <tns:temporalInterp>" << q.temporalInterp << "</tns:temporalInterp>\n"
          << "      <tns:points>{pts_xml}</tns:points>\n"
          << "    </tns:" << method << ">\n"
          << "  </soap:Body>\n"
          << "</soap:Envelope>\"\"\"\n"
          << "    r = requests.post(ENDPOINT, data=body.encode('utf-8'),\n"
          << "        headers={'Content-Type': 'text/xml; charset=utf-8',\n"
          << "                 'SOAPAction': '\"" << ns << method << "\"',\n"
          << "                 'User-Agent': 'CakeCFD/1.0'}, timeout=60)\n"
          << "    r.raise_for_status()\n"
          << "    return r.text\n\n";

        if (!q.isRange) {
            o << "print(query_soap(" << q.time << "))\n";
        } else {
            o << "TIMES = [";
            for (int i = 0; i < q.times.size(); ++i) { if (i) o << ", "; o << q.times[i]; }
            o << "]\nfor t in TIMES:\n    print(f't={t}', query_soap(t))\n";
        }
    } else {
        o << "#  REST JSON query \n"
          << "def query_rest(time):\n"
          << "    body = {\n"
          << "        'token': AUTH_TOKEN, 'dataset': DATASET, 'field': FIELD,\n"
          << "        'time': time, 'spatialInterp': " << q.spatialInterp << ",\n"
          << "        'temporalInterp': " << q.temporalInterp << ",\n"
          << "        'points': [{'x': float(p[0]), 'y': float(p[1]), 'z': float(p[2])}\n"
          << "                   for p in POINTS],\n"
          << "    }\n"
          << "    r = requests.post(ENDPOINT + '/query', json=body,\n"
          << "        headers={'Authorization': f'Token {AUTH_TOKEN}',\n"
          << "                 'User-Agent': 'CakeCFD/1.0'}, timeout=60)\n"
          << "    r.raise_for_status()\n"
          << "    return r.json()\n\n";

        if (!q.isRange) {
            o << "print(query_rest(" << q.time << "))\n";
        } else {
            o << "TIMES = [";
            for (int i = 0; i < q.times.size(); ++i) { if (i) o << ", "; o << q.times[i]; }
            o << "]\nfor t in TIMES:\n    print(f't={t}', query_rest(t))\n";
        }
    }

    return code;
}
