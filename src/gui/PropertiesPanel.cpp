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
#include "PropertiesPanel.h"
#include "DomainSetupDialog.h"
#include "themes/ThemeEngine.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QFrame>
#include <QScrollArea>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <QPushButton>
#include <QHBoxLayout>
#include <memory>

struct TC {
    QString accent;
    QString label;
    QString value;
    QString muted;
    QString sep;
    QString btnFg;
    QString btnHover;
    QString editBg;
    QString ok;
    QString err;
};

static TC tc() {
    const ThemeColors& c = ThemeEngine::instance().colors();
    return { c.accent.name(), c.textMuted.name(), c.text.name(), c.textDim.name(),
             c.border.name(), c.onAccent.name(), c.accentHover.name(),
             c.base.name(), c.success.name(), c.danger.name() };
}

static QString slurp(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLocal8Bit(f.readAll());
}

static QString match(const QString& text, const QString& pattern, int g = 1)
{
    QRegularExpression re(pattern);
    auto m = re.match(text);
    return m.hasMatch() ? m.captured(g) : QString("-");
}

static QStringList matchAll(const QString& text, const QString& pattern, int g = 1)
{
    QStringList out;
    QRegularExpression re(pattern);
    auto it = re.globalMatch(text);
    while (it.hasNext()) out << it.next().captured(g);
    return out;
}

static QString dictVal(const QString& text, const QString& key)
{
    return match(text, QRegularExpression::escape(key) + R"(\s+([^\s;(]+)\s*;)");
}

static QString fmtSize(qint64 bytes)
{
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024) + " KB";
    return QString::number(bytes / 1024 / 1024) + " MB";
}

static bool writeKey(const QString& filePath, const QString& key, const QString& value)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QString text = QString::fromLocal8Bit(f.readAll());
    f.close();

    QRegularExpression re(QRegularExpression::escape(key) + R"(\s+[^;]+;)");
    auto m = re.match(text);
    if (!m.hasMatch()) return false;

    text = text.left(m.capturedStart()) + key + "  " + value + ";" + text.mid(m.capturedEnd());

    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(text.toLocal8Bit());
    return true;
}

static void writeTurbulenceDicts(const QString& caseDir, const QString& model)
{
    static const QStringList les = {"kOmegaSSTDES","Smagorinsky","WALE","dynamicKEqn","dynamicLagrangian"};
    QString body;
    if (model == "laminar")
        body = "simulationType laminar;\n";
    else if (les.contains(model))
        body = "simulationType LES;\n\nLES\n{\n    LESModel     " + model + ";\n"
               "    turbulence   on;\n    printCoeffs  on;\n    delta        cubeRootVol;\n"
               "    cubeRootVolCoeffs { deltaCoeff 1; }\n}\n";
    else
        body = "simulationType RAS;\n\nRAS\n{\n    model       " + model + ";\n"
               "    turbulence  on;\n    printCoeffs on;\n}\n";

    for (const QString& name : { QString("turbulenceProperties"), QString("momentumTransport") }) {
        QFile f(caseDir + "/constant/" + name);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            f.write(("FoamFile\n{ version 2.0; format ascii; class dictionary; object "
                     + name + "; }\n\n" + body).toLocal8Bit());
    }
}

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(m_scroll);

    clear();
}

void PropertiesPanel::initContainer()
{
    auto* w = new QWidget;
    m_vlay = new QVBoxLayout(w);
    m_vlay->setContentsMargins(10, 8, 10, 8);
    m_vlay->setSpacing(0);
    m_vlay->addStretch(1);

    m_scroll->setWidget(w);
    m_content = w;
    m_form = nullptr;
}

void PropertiesPanel::buildBlankUI()
{
    initContainer();
    auto* w = m_content;

    const TC& t = tc();
    auto* ph = new QWidget(w);
    auto* phLayout = new QVBoxLayout(ph);
    phLayout->setAlignment(Qt::AlignCenter);
    phLayout->setSpacing(12);

    auto* icon = new QLabel("○", ph);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QString("QLabel { color: %1; font-size: 24px; }").arg(t.muted));
    phLayout->addWidget(icon);

    auto* titleLbl = new QLabel("No Selection", ph);
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(t.value));
    phLayout->addWidget(titleLbl);

    auto* hint = new QLabel(
        "Open a case or click an item\nin the Case panel to inspect it.", ph);
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; }").arg(t.muted));
    phLayout->addWidget(hint);

    auto* sep = new QFrame(ph);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QString("QFrame { color: %1; }").arg(t.sep));
    phLayout->addWidget(sep);

    struct TipItem { const char* label; const char* desc; };
    static const TipItem tips[] = {
        { "Case",       "endTime, solver, turbulence model" },
        { "Geometry",   "bounding box, triangle count, domain setup" },
        { "Mesh",       "quality, cell count, boundary patches" },
        { "Results",    "fields, residuals, force coefficients" },
        { "Dict files", "editable key-value entries with live write-back" },
    };
    for (auto& tip : tips) {
        auto* row = new QWidget(ph);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 2, 0, 2);
        rl->setSpacing(8);
        auto* lbl = new QLabel(tip.label, row);
        lbl->setFixedWidth(72);
        lbl->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; font-weight: bold; "
                           "letter-spacing: 0.5px; }").arg(t.accent));
        auto* val = new QLabel(tip.desc, row);
        val->setWordWrap(true);
        val->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }").arg(t.muted));
        rl->addWidget(lbl);
        rl->addWidget(val, 1);
        phLayout->addWidget(row);
    }

    m_vlay->insertStretch(0, 1);
    m_vlay->insertWidget(1, ph);
}

void PropertiesPanel::clear()
{
    m_lastType = -1;
    buildBlankUI();
}

void PropertiesPanel::onThemeChanged()
{
    if (m_lastType >= 0)
        showItem(m_lastType, m_lastPath, m_lastCaseDir);
    else
        buildBlankUI();
}

void PropertiesPanel::addSection(const QString& title)
{
    if (m_form) m_vlay->insertSpacing(m_vlay->count() - 1, 8);
    const TC& t = tc();
    auto* lbl = new QLabel(title.toUpper(), m_content);
    lbl->setStyleSheet(QString(
        "QLabel { color: %1; font-size: 10px; font-weight: bold; "
        "letter-spacing: 1px; padding: 10px 0 3px 0; "
        "border-bottom: 1px solid %2; }").arg(t.accent, t.sep));
    m_vlay->insertWidget(m_vlay->count() - 1, lbl);

    auto* container = new QWidget(m_content);
    m_form = new QFormLayout(container);
    m_form->setContentsMargins(0, 4, 0, 4);
    m_form->setSpacing(4);
    m_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_vlay->insertWidget(m_vlay->count() - 1, container);
}

void PropertiesPanel::addRow(const QString& label, const QString& value, bool mono)
{
    if (!m_form) addSection("Info");
    const TC& t = tc();
    auto* lbl = new QLabel(label + ":", m_content);
    lbl->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; min-width: 110px; }").arg(t.label));
    auto* val = new QLabel(value, m_content);
    val->setWordWrap(true);
    val->setTextInteractionFlags(Qt::TextSelectableByMouse);
    val->setStyleSheet(mono
        ? QString("QLabel { color: %1; font-size: 11px; font-family: monospace; }").arg(t.muted)
        : QString("QLabel { color: %1; font-size: 11px; }").arg(t.value));
    m_form->addRow(lbl, val);
}

void PropertiesPanel::addBadge(const QString& text, bool good)
{
    if (!m_form) addSection("Info");
    const TC& t = tc();
    auto* val = new QLabel(text, m_content);
    val->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: bold; }")
                       .arg(good ? t.ok : t.err));
    m_form->addRow(new QLabel("", m_content), val);
}

void PropertiesPanel::addButton(const QString& label, std::function<void()> onClick)
{
    m_form = nullptr;
    const TC& t = tc();
    auto* btn = new QPushButton(label, m_content);
    btn->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; font-weight: bold; "
        "font-size: 11px; padding: 5px 10px; border-radius: 3px; margin-top: 8px; }"
        "QPushButton:hover { background: %3; }").arg(t.accent, t.btnFg, t.btnHover));
    auto fn = std::make_shared<std::function<void()>>(std::move(onClick));
    connect(btn, &QPushButton::clicked, btn, [fn]{ if (*fn) (*fn)(); });
    m_vlay->insertWidget(m_vlay->count() - 1, btn);
}

void PropertiesPanel::addEditableRow(const QString& label, const QString& value,
                                     std::function<void(const QString&)> onCommit)
{
    if (!m_form) addSection("Info");

    const TC& t = tc();
    auto* lbl = new QLabel(label + ":", m_content);
    lbl->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; min-width: 110px; }").arg(t.label));

    auto* edit = new QLineEdit(value, m_content);
    auto normalSS = QString(
        "QLineEdit { background: transparent; border: 1px solid transparent; "
        "border-radius: 2px; color: %1; font-size: 11px; padding: 1px 4px; }"
        "QLineEdit:focus { background: %2; border: 1px solid %3; }"
        "QLineEdit:hover { border: 1px solid %4; }").arg(t.value, t.editBg, t.accent, t.sep);
    edit->setStyleSheet(normalSS);

    auto fn = std::make_shared<std::function<void(const QString&)>>(std::move(onCommit));
    connect(edit, &QLineEdit::editingFinished, edit, [edit, fn, normalSS, accent=t.accent, editBg=t.editBg]() {
        if (!*fn) return;
        (*fn)(edit->text());
        edit->setStyleSheet(QString(
            "QLineEdit { background: %1; border: 1px solid %2; "
            "border-radius: 2px; color: %2; font-size: 11px; padding: 1px 4px; }")
            .arg(editBg, accent));
        QTimer::singleShot(600, edit, [edit, normalSS](){
            if (!edit) return;
            edit->setStyleSheet(normalSS);
        });
    });

    m_form->addRow(lbl, edit);
}

void PropertiesPanel::addComboRow(const QString& label, const QStringList& options,
                                  const QString& current,
                                  std::function<void(const QString&)> onCommit)
{
    if (!m_form) addSection("Info");

    const TC& t = tc();
    auto* lbl = new QLabel(label + ":", m_content);
    lbl->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; min-width: 110px; }").arg(t.label));

    auto* combo = new QComboBox(m_content);
    combo->addItems(options);
    if (!current.isEmpty() && current != "-" && !options.contains(current))
        combo->addItem(current);
    int idx = combo->findText(current);
    if (idx >= 0) combo->setCurrentIndex(idx);
    combo->setStyleSheet(QString(
        "QComboBox { background: transparent; border: 1px solid transparent; "
        "border-radius: 2px; color: %1; font-size: 11px; padding: 1px 4px; }"
        "QComboBox:hover, QComboBox:focus { background: %2; border: 1px solid %3; }"
        "QComboBox::drop-down { border: none; width: 16px; }"
        "QComboBox QAbstractItemView { background: %2; color: %1; "
        "selection-background-color: %3; selection-color: %4; }")
        .arg(t.value, t.editBg, t.accent, t.btnFg));

    auto fn = std::make_shared<std::function<void(const QString&)>>(std::move(onCommit));
    connect(combo, &QComboBox::currentTextChanged, combo, [fn](const QString& v) {
        if (*fn) (*fn)(v);
    });

    m_form->addRow(lbl, combo);
}

void PropertiesPanel::showItem(int itemType, const QString& path, const QString& caseDir)
{
    m_lastType    = itemType;
    m_lastPath    = path;
    m_lastCaseDir = caseDir;
    m_caseDir     = caseDir;
    initContainer();
    switch (static_cast<CaseItemType>(itemType)) {
    case TypeRoot:
    case TypeSection:   populateCase(caseDir);          break;
    case TypeMeshInfo:  populateMesh(caseDir);           break;
    case TypeGeoFile:   populateGeo(path, caseDir);     break;
    case TypeResultStep:
        populateResult(caseDir, QFileInfo(path).fileName());
        break;
    case TypeBCFile:
    case TypeSysFile:
        populateDict(path);
        break;
    }
}

void PropertiesPanel::populateCase(const QString& caseDir)
{
    QString cdPath = caseDir + "/system/controlDict";
    QString cd = slurp(cdPath);

    addSection("Case");
    addRow("Name", QFileInfo(caseDir).fileName());
    addRow("Path", caseDir, true);

    addSection("Control");
    addRow("Solver", dictVal(cd, "application"));
    addEditableRow("Start time",     dictVal(cd, "startTime"),   [cdPath](const QString& v){ writeKey(cdPath, "startTime", v); });
    addEditableRow("End time",       dictVal(cd, "endTime"),     [cdPath](const QString& v){ writeKey(cdPath, "endTime", v); });
    addEditableRow("Δt",             dictVal(cd, "deltaT"),      [cdPath](const QString& v){ writeKey(cdPath, "deltaT", v); });
    addEditableRow("Write interval", dictVal(cd, "writeInterval"),[cdPath](const QString& v){ writeKey(cdPath, "writeInterval", v); });

    static QRegularExpression numRe(R"(^\d+(\.\d+)?$)");
    QStringList steps;
    for (const QString& e : QDir(caseDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (numRe.match(e).hasMatch() && e.toDouble() > 0) steps << e;

    addSection("Results");
    addRow("Time steps", steps.isEmpty() ? "none" : QString::number(steps.size()));
    if (!steps.isEmpty()) addRow("Latest", steps.last() + " s");

    QString turbPath = caseDir + "/constant/turbulenceProperties";
    if (!QFile::exists(turbPath)) turbPath = caseDir + "/constant/momentumTransport";
    QString mtt = slurp(turbPath);
    if (!mtt.isEmpty()) {
        QString tpPath = caseDir + "/constant/transportProperties";
        if (!QFile::exists(tpPath)) tpPath = caseDir + "/constant/physicalProperties";
        QString tp = slurp(tpPath);

        QString turb = dictVal(mtt, "simulationType") == "laminar" ? QString("laminar") : dictVal(mtt, "model");
        if (turb == "-") turb = dictVal(mtt, "LESModel");
        if (turb == "-") turb = dictVal(mtt, "RASModel");

        addSection("Physics");
        addComboRow("Turbulence", {"laminar","kOmegaSST","kEpsilon","SpalartAllmaras","kOmegaSSTDES",
                                   "Smagorinsky","WALE","dynamicKEqn","dynamicLagrangian"},
                    turb, [caseDir](const QString& v){ writeTurbulenceDicts(caseDir, v); });
        if (!tp.isEmpty())
            addEditableRow("ν (m²/s)", dictVal(tp, "nu"), [tpPath](const QString& v){ writeKey(tpPath, "nu", v); });
    }
}

void PropertiesPanel::populateMesh(const QString& caseDir)
{
    addSection("Mesh");

    QString log = slurp(caseDir + "/log.checkMesh");
    if (!log.isEmpty()) {
        addRow("Points",         match(log, R"(points:\s+(\d+))"));
        addRow("Faces",          match(log, R"(\bfaces:\s+(\d+))"));
        addRow("Cells",          match(log, R"(cells:\s+(\d+))"));
        addRow("Internal faces", match(log, R"(internal faces:\s+(\d+))"));

        addSection("Quality");
        double nonOrtho = match(log, R"(Max non-orthogonality = ([\d.]+))").toDouble();
        double skewness = match(log, R"(Max skewness = ([\d.]+))").toDouble();
        addRow("Max non-orthogonality", QString::number(nonOrtho, 'f', 2) + "°");
        addRow("Max skewness",          QString::number(skewness, 'f', 4));

        bool badNonOrtho = nonOrtho > 70.0;
        bool badSkew     = skewness > 4.0;
        bool meshOk = log.contains("Mesh OK") || log.contains("No errors");
        if (meshOk && !badNonOrtho && !badSkew)
            addBadge("Mesh OK", true);
        else if (!meshOk)
            addBadge("✗  Mesh has errors: check log.checkMesh", false);
        else
            addBadge("⚠  High non-orthogonality or skewness", false);
    } else {
        addRow("checkMesh log", "not found: run checkMesh for full stats");
    }

    addSection("Files");
    QDir pmDir(caseDir + "/constant/polyMesh");
    if (pmDir.exists()) {
        qint64 total = 0;
        QStringList names;
        for (const QFileInfo& fi : pmDir.entryInfoList(QDir::Files)) {
            total += fi.size();
            names << fi.fileName();
        }
        addRow("Total size", fmtSize(total));
        addRow("Files", names.join(", "), true);
    }

    addSection("Patches");
    QString boundary = slurp(caseDir + "/constant/polyMesh/boundary");
    if (!boundary.isEmpty()) {
        QRegularExpression patchRe(R"((\w+)\n\s*\{\s*\n\s*type\s+(\w+))");
        auto it = patchRe.globalMatch(boundary);
        while (it.hasNext()) {
            auto m = it.next();
            addRow(m.captured(1), m.captured(2));
        }
    }
}

void PropertiesPanel::populateGeo(const QString& path, const QString& caseDir)
{
    QFileInfo fi(path);
    addSection("Geometry");
    addRow("File",   fi.fileName());
    addRow("Format", fi.suffix().toUpper());
    addRow("Size",   fmtSize(fi.size()));
    addRow("Path",   fi.dir().absolutePath(), true);

    if (path.endsWith(".stl", Qt::CaseInsensitive)) {
        QString text = slurp(path);
        bool binary = !text.startsWith("solid") || text.contains('\0');
        if (!binary) {
            int tris = text.count("facet normal");
            addRow("Triangles", QString::number(tris));

            QRegularExpression vertRe(R"(vertex\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+))");
            double xmin=1e30,xmax=-1e30,ymin=1e30,ymax=-1e30,zmin=1e30,zmax=-1e30;
            bool hasVerts = false;
            auto it = vertRe.globalMatch(text);
            while (it.hasNext()) {
                auto m = it.next();
                double x=m.captured(1).toDouble(), y=m.captured(2).toDouble(), z=m.captured(3).toDouble();
                xmin=qMin(xmin,x); xmax=qMax(xmax,x);
                ymin=qMin(ymin,y); ymax=qMax(ymax,y);
                zmin=qMin(zmin,z); zmax=qMax(zmax,z);
                hasVerts = true;
            }
            if (hasVerts) {
                addSection("Bounds");
                addRow("X", QString("[%1,  %2]").arg(xmin,0,'f',4).arg(xmax,0,'f',4));
                addRow("Y", QString("[%1,  %2]").arg(ymin,0,'f',4).arg(ymax,0,'f',4));
                addRow("Z", QString("[%1,  %2]").arg(zmin,0,'f',4).arg(zmax,0,'f',4));
                addRow("Extents", QString("%1 * %2 * %3 m")
                    .arg(xmax-xmin,0,'f',4).arg(ymax-ymin,0,'f',4).arg(zmax-zmin,0,'f',4));
            }
        } else {
            addRow("Encoding", "Binary");
            qint64 n = (fi.size() - 84) / 50;
            if (n > 0) addRow("Triangles", QString::number(n));
        }

        if (!caseDir.isEmpty()) {
            QString stlPath = path;
            QString cd      = caseDir;
            addButton("Setup Aerodynamic Domain ->", [stlPath, cd, this]() {
                auto* dlg = new DomainSetupDialog(stlPath, cd, this);
                connect(dlg, &DomainSetupDialog::loadRequested,
                        this, &PropertiesPanel::loadDomainRequested);
                connect(dlg, &DomainSetupDialog::previewChanged,
                        this, &PropertiesPanel::domainPreviewChanged);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
            });
            addButton("Load Aerodynamic Domain to Workspace", [stlPath, cd, this](){
                QString startDir = cd.isEmpty() ? QFileInfo(stlPath).absolutePath() : cd;
                QString chosen = FileBrowserDialog::getOpenFileName(
                    this, "Select geometry to load", startDir,
                    "Geometry files (*.stl *.obj);;STL (*.stl);;OBJ (*.obj)");
                if (!chosen.isEmpty())
                    emit loadDomainRequested(cd, chosen);
            });
        }
    }
}

void PropertiesPanel::populateResult(const QString& caseDir, const QString& timeStep)
{
    addSection("Time Step");
    addRow("Time", timeStep + " s");

    QDir stepDir(caseDir + "/" + timeStep);
    QStringList fields = stepDir.entryList(QDir::Files);

    addSection("Fields");
    for (const QString& field : fields) {
        QString text = slurp(stepDir.filePath(field));
        if (text.isEmpty()) { addRow(field, "-"); continue; }
        auto ms = QRegularExpression(R"(internalField\s+uniform\s+([\d.eE+\-]+)\s*;)").match(text);
        if (ms.hasMatch()) { addRow(field, ms.captured(1)); continue; }
        auto mv = QRegularExpression(
            R"(internalField\s+uniform\s+\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))").match(text);
        if (mv.hasMatch()) {
            addRow(field, QString("(%1  %2  %3)").arg(mv.captured(1)).arg(mv.captured(2)).arg(mv.captured(3)));
            continue;
        }
        auto dim = QRegularExpression(R"(dimensions\s+(\[[^\]]+\]))").match(text);
        addRow(field, dim.hasMatch() ? "non-uniform  " + dim.captured(1) : "non-uniform");
    }

    QStringList logs;
    for (const QString& f : QDir(caseDir).entryList({"log.*"}, QDir::Files)) logs << f;
    if (!logs.isEmpty()) {
        QString log = slurp(caseDir + "/" + logs.first());
        QRegularExpression resRe(R"(Solving for (\w+),.*?Final residual = ([\d.eE+\-]+))");
        QMap<QString,QString> lastRes;
        auto it = resRe.globalMatch(log);
        while (it.hasNext()) { auto m=it.next(); lastRes[m.captured(1)]=m.captured(2); }
        if (!lastRes.isEmpty()) {
            addSection("Final Residuals");
            for (auto it2=lastRes.begin(); it2!=lastRes.end(); ++it2)
                addRow(it2.key(), it2.value(), true);
        }
    }
}

void PropertiesPanel::populateDict(const QString& path)
{
    QFileInfo fi(path);
    QString name = fi.fileName();
    QString text = slurp(path);

    addSection(name);
    addRow("Size", fmtSize(fi.size()));

    if (name == "controlDict") {
        addSection("Settings");
        addRow("Solver", dictVal(text, "application"));
        addEditableRow("Start time",     dictVal(text, "startTime"),   [path](const QString& v){ writeKey(path, "startTime", v); });
        addEditableRow("End time",       dictVal(text, "endTime"),     [path](const QString& v){ writeKey(path, "endTime", v); });
        addEditableRow("Δt",             dictVal(text, "deltaT"),      [path](const QString& v){ writeKey(path, "deltaT", v); });
        addEditableRow("Write interval", dictVal(text, "writeInterval"),[path](const QString& v){ writeKey(path, "writeInterval", v); });
        addEditableRow("Write format",   dictVal(text, "writeFormat"), [path](const QString& v){ writeKey(path, "writeFormat", v); });

    } else if (name == "fvSchemes") {
        addSection("Divergence");
        QRegularExpression divRe(R"(div\(([^)]+)\)\s+(\w+[^;]+);)");
        auto it = divRe.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            QString key = "div(" + m.captured(1) + ")";
            QString val = m.captured(2).simplified();
            addEditableRow(key, val, [path, key, val](const QString& v){
                QFile f(path);
                if (!f.open(QIODevice::ReadOnly)) return;
                QString t = QString::fromLocal8Bit(f.readAll()); f.close();
                QString escaped = QRegularExpression::escape(key);
                QRegularExpression re(escaped + R"(\s+[^;]+;)");
                t.replace(re, key + "  " + v + ";");
                if (f.open(QIODevice::WriteOnly|QIODevice::Truncate)) f.write(t.toLocal8Bit());
            });
        }
        addSection("Gradient");
        addRow("grad(U)", match(text, R"(grad\(U\)\s+([^;]+);)"));
        addRow("grad(p)", match(text, R"(grad\(p\)\s+([^;]+);)"));

    } else if (name == "fvSolution") {
        addSection("SIMPLE / PISO");
        addEditableRow("nNonOrthCorr",  dictVal(text, "nNonOrthogonalCorrectors"), [path](const QString& v){ writeKey(path, "nNonOrthogonalCorrectors", v); });
        addEditableRow("nCorrectors",   dictVal(text, "nCorrectors"),  [path](const QString& v){ writeKey(path, "nCorrectors", v); });
        addEditableRow("pRef cell",     dictVal(text, "pRefCell"),     [path](const QString& v){ writeKey(path, "pRefCell", v); });
        addEditableRow("pRef value",    dictVal(text, "pRefValue"),    [path](const QString& v){ writeKey(path, "pRefValue", v); });

    } else if (name == "snappyHexMeshDict") {
        addSection("Stages");
        auto boolVal = [&](const QString& k) -> QString {
            QString v = dictVal(text, k);
            return (v == "true" || v == "1") ? "true" : "false";
        };
        addComboRow("castellatedMesh", {"true","false"}, boolVal("castellatedMesh"),
                    [path](const QString& v){ writeKey(path, "castellatedMesh", v); });
        addComboRow("snap", {"true","false"}, boolVal("snap"),
                    [path](const QString& v){ writeKey(path, "snap", v); });
        addComboRow("addLayers", {"true","false"}, boolVal("addLayers"),
                    [path](const QString& v){ writeKey(path, "addLayers", v); });
        addSection("Refinement");
        addEditableRow("maxLocalCells",     dictVal(text, "maxLocalCells"),     [path](const QString& v){ writeKey(path, "maxLocalCells", v); });
        addEditableRow("maxGlobalCells",    dictVal(text, "maxGlobalCells"),    [path](const QString& v){ writeKey(path, "maxGlobalCells", v); });
        addEditableRow("minRefinement",     dictVal(text, "minRefinementCells"),[path](const QString& v){ writeKey(path, "minRefinementCells", v); });
        addEditableRow("nCellsBetween",     dictVal(text, "nCellsBetweenLevels"),[path](const QString& v){ writeKey(path, "nCellsBetweenLevels", v); });

    } else if (name == "blockMeshDict") {
        addSection("Block Mesh");
        addEditableRow("convertToMeters", dictVal(text, "convertToMeters"), [path](const QString& v){ writeKey(path, "convertToMeters", v); });
        int nBlocks = text.count("hex (");
        addRow("Blocks", nBlocks > 0 ? QString::number(nBlocks) : "-");

    } else if (name == "U" || name == "p" || name == "k" ||
               name == "omega" || name == "epsilon" || name == "nut" ||
               name == "nuTilda") {
        addSection("Field Info");
        addRow("Dimensions", match(text, R"(dimensions\s+(\[[^\]]+\]))"));
        addEditableRow("internalField", dictVal(text, "internalField"), [path](const QString& v){ writeKey(path, "internalField", v); });
        addSection("Boundary Conditions");
        QRegularExpression patchRe(R"((\w+)\n\s*\{[^}]+type\s+(\w+)[^}]*\})");
        auto it = patchRe.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            auto val = QRegularExpression(R"(\bvalue\s+uniform\s+([^;]+);)").match(m.captured(0));
            addRow(m.captured(1), val.hasMatch()
                ? m.captured(2) + "  " + val.captured(1).simplified()
                : m.captured(2));
        }

    } else {
        addSection("Contents");
        QRegularExpression kvRe(R"(^\s{0,4}(\w+)\s+([^\s{;][^;]*);)",
                                QRegularExpression::MultilineOption);
        auto it = kvRe.globalMatch(text);
        int count = 0;
        while (it.hasNext() && count < 25) {
            auto m = it.next();
            QString k = m.captured(1);
            if (k == "FoamFile" || k == "version" || k == "format" ||
                k == "class" || k == "location" || k == "object") continue;
            addEditableRow(k, m.captured(2).simplified(),
                           [path, k](const QString& v){ writeKey(path, k, v); });
            ++count;
        }
    }
}
