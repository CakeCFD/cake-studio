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

#include "ApiFormatDialog.h"
#include "DatasetClient.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSplitter>
#include <QTabWidget>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QFont>

static const char* k_defaultSample = R"({
  "data": [
    {"u": 0.312, "v": -0.051, "w": 0.008},
    {"u": 0.287, "v":  0.033, "w": -0.012}
  ]
})";

static const char* k_help =
    "JSON Path: dot-separated path to the array of points.\n"
    "  Leave blank if the root is already an array.\n"
    "  Examples:  data          data.results          hits.items\n\n"
    "Component Keys: comma-separated field names to extract per point,\n"
    "  in order. Use 3 keys for vectors, 1 for scalars.\n"
    "  Examples:  u,v,w          velocity.x,velocity.y,velocity.z          value\n\n"
    "For SOAP endpoints the built-in XML parser is used; this config applies only\n"
    "to REST/JSON API endpoints.";

ApiFormatDialog::ApiFormatDialog(QSettings& settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle("Custom API Response Format");
    setMinimumSize(640, 560);

    auto* root = new QVBoxLayout(this);

    auto* tabs = new QTabWidget;
    root->addWidget(tabs);

    auto* fmtTab = new QWidget;
    auto* fmtTabL = new QVBoxLayout(fmtTab);

    auto* helpLabel = new QLabel(k_help);
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet("color: gray; font-size: 11px; padding: 4px;");
    fmtTabL->addWidget(helpLabel);

    auto* fmtBox = new QGroupBox("Format");
    auto* fmtL   = new QVBoxLayout(fmtBox);

    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(new QLabel("JSON Path:       "));
    m_pathEdit = new QLineEdit;
    m_pathEdit->setPlaceholderText("data   (blank = root array)");
    m_pathEdit->setText(settings.value("customApi/jsonPath", "data").toString());
    pathRow->addWidget(m_pathEdit);
    fmtL->addLayout(pathRow);

    auto* keysRow = new QHBoxLayout;
    keysRow->addWidget(new QLabel("Component Keys:"));
    m_keysEdit = new QLineEdit;
    m_keysEdit->setPlaceholderText("u,v,w");
    m_keysEdit->setText(settings.value("customApi/componentKeys", "u,v,w").toString());
    keysRow->addWidget(m_keysEdit);
    fmtL->addLayout(keysRow);

    fmtTabL->addWidget(fmtBox);

    auto* testBox = new QGroupBox("Live Test");
    auto* testL   = new QVBoxLayout(testBox);

    auto* sp = new QSplitter(Qt::Vertical);

    auto* sampleWrap = new QWidget;
    auto* swL = new QVBoxLayout(sampleWrap);
    swL->setContentsMargins(0,0,0,0);
    swL->addWidget(new QLabel("Paste a sample API response:"));
    m_sampleEdit = new QPlainTextEdit;
    m_sampleEdit->setFont(QFont("Monospace", 9));
    m_sampleEdit->setPlainText(k_defaultSample);
    swL->addWidget(m_sampleEdit);
    sp->addWidget(sampleWrap);

    auto* resultWrap = new QWidget;
    auto* rwL = new QVBoxLayout(resultWrap);
    rwL->setContentsMargins(0,0,0,0);
    rwL->addWidget(new QLabel("Parsed doubles:"));
    m_resultEdit = new QPlainTextEdit;
    m_resultEdit->setFont(QFont("Monospace", 9));
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setMaximumHeight(120);
    rwL->addWidget(m_resultEdit);
    sp->addWidget(resultWrap);

    testL->addWidget(sp);

    auto* testBtnRow = new QHBoxLayout;
    auto* testBtn  = new QPushButton("Test Parse");
    auto* resetBtn = new QPushButton("Reset Sample");
    testBtnRow->addStretch();
    testBtnRow->addWidget(resetBtn);
    testBtnRow->addWidget(testBtn);
    testL->addLayout(testBtnRow);

    fmtTabL->addWidget(testBox);

    connect(testBtn,  &QPushButton::clicked, this, &ApiFormatDialog::onTest);
    connect(resetBtn, &QPushButton::clicked, this, &ApiFormatDialog::onReset);

    tabs->addTab(fmtTab, "Format & Test");

    auto* citeTab  = new QWidget;
    auto* citeTabL = new QVBoxLayout(citeTab);

    auto* citeHint = new QLabel(
        "Fill in the reference for this database so Cake can emit a proper citation\n"
        "when results are used in a case. All fields are optional but DOI is recommended.");
    citeHint->setWordWrap(true);
    citeHint->setStyleSheet("color: gray; font-size: 11px; padding: 4px;");
    citeTabL->addWidget(citeHint);

    auto* citeBox = new QGroupBox("Reference");
    auto* citeFL  = new QFormLayout(citeBox);
    citeFL->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto makeField = [&](const QString& key, const QString& placeholder) {
        auto* e = new QLineEdit;
        e->setPlaceholderText(placeholder);
        e->setText(settings.value("customApi/citation/" + key, "").toString());
        return e;
    };

    m_citeAuthors = makeField("authors", "Li, Y., Perlman, E., ...");
    m_citeTitle   = makeField("title",   "A public turbulence database cluster...");
    m_citeJournal = makeField("journal", "Journal of Turbulence");
    m_citeYear    = makeField("year",    "2008");
    m_citeVolume  = makeField("volume",  "9");
    m_citePages   = makeField("pages",   "N31");
    m_citeDoi     = makeField("doi",     "10.1080/14685240802376389");

    citeFL->addRow("Authors:",         m_citeAuthors);
    citeFL->addRow("Title:",           m_citeTitle);
    citeFL->addRow("Journal / Source:", m_citeJournal);
    citeFL->addRow("Year:",            m_citeYear);
    citeFL->addRow("Volume:",          m_citeVolume);
    citeFL->addRow("Pages:",           m_citePages);
    citeFL->addRow("DOI:",             m_citeDoi);

    citeTabL->addWidget(citeBox);
    citeTabL->addStretch();

    tabs->addTab(citeTab, "Citation");

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        m_settings.setValue("customApi/jsonPath",      m_pathEdit->text().trimmed());
        m_settings.setValue("customApi/componentKeys", m_keysEdit->text().trimmed());
        m_settings.setValue("customApi/citation/authors", m_citeAuthors->text().trimmed());
        m_settings.setValue("customApi/citation/title",   m_citeTitle->text().trimmed());
        m_settings.setValue("customApi/citation/journal", m_citeJournal->text().trimmed());
        m_settings.setValue("customApi/citation/year",    m_citeYear->text().trimmed());
        m_settings.setValue("customApi/citation/volume",  m_citeVolume->text().trimmed());
        m_settings.setValue("customApi/citation/pages",   m_citePages->text().trimmed());
        m_settings.setValue("customApi/citation/doi",     m_citeDoi->text().trimmed());
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString ApiFormatDialog::jsonPath() const      { return m_pathEdit->text().trimmed(); }

CitationManager::Citation ApiFormatDialog::citationData() const
{
    return {
        "custom_endpoint",
        "Custom Database",
        m_citeAuthors->text().trimmed(),
        m_citeTitle->text().trimmed(),
        m_citeJournal->text().trimmed(),
        m_citeYear->text().trimmed(),
        m_citeVolume->text().trimmed(),
        m_citePages->text().trimmed(),
        m_citeDoi->text().trimmed()
    };
}

QStringList ApiFormatDialog::componentKeys() const
{
    QStringList keys;
    for (const QString& k : m_keysEdit->text().split(','))
        if (!k.trimmed().isEmpty()) keys << k.trimmed();
    return keys;
}

void ApiFormatDialog::onTest()
{
    QByteArray raw = m_sampleEdit->toPlainText().toUtf8();
    QVector<double> vals = parseResponse(raw, jsonPath(), componentKeys());

    if (vals.isEmpty()) {
        m_resultEdit->setPlainText("[No values extracted: check path and keys]");
        return;
    }

    QStringList lines;
    int stride = componentKeys().size();
    if (stride < 1) stride = 1;
    for (int i = 0; i < vals.size(); i += stride) {
        QStringList row;
        for (int j = 0; j < stride && i+j < vals.size(); ++j)
            row << QString::number(vals[i+j], 'g', 6);
        lines << row.join("  ");
    }
    m_resultEdit->setPlainText(
        QString("%1 point(s), %2 component(s) each:\n").arg(vals.size()/stride).arg(stride)
        + lines.join('\n'));
}

void ApiFormatDialog::onReset()
{
    m_sampleEdit->setPlainText(k_defaultSample);
}

QVector<double> ApiFormatDialog::parseResponse(const QByteArray& json,
                                                const QString& path,
                                                const QStringList& keys) const
{
    QString err;
    QVector<double> result = CFD::DatasetClient::parseCustomJson(json, path, keys, &err);
    if (!err.isEmpty())
        m_resultEdit->setPlainText("[JSON parse error] " + err);
    return result;
}