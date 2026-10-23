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

#include "DomainSetupDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTimer>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QMessageBox>
#include <QSettings>
#include <QRegularExpression>
#include "BCEditorDialog.h"   // OFDict scanner
#include <algorithm>
#include <cmath>
#include <optional>

// STL bounds parser

bool DomainSetupDialog::computeBounds()
{
    QFile f(m_stlPath);
    if (!f.open(QIODevice::ReadOnly)) return false;

    m_gx0 = m_gy0 = m_gz0 =  1e30;
    m_gx1 = m_gy1 = m_gz1 = -1e30;
    bool hasVerts = false;

    QByteArray head = f.read(256);
    bool isAscii = head.startsWith("solid") && !head.contains('\0');
    f.seek(0);

    if (isAscii) {
        QTextStream ts(&f);
        QString line;
        while (ts.readLineInto(&line)) {
            line = line.trimmed();
            if (!line.startsWith("vertex ")) continue;
            QStringList p = line.split(' ', Qt::SkipEmptyParts);
            if (p.size() < 4) continue;
            double x = p[1].toDouble(), y = p[2].toDouble(), z = p[3].toDouble();
            m_gx0=qMin(m_gx0,x); m_gx1=qMax(m_gx1,x);
            m_gy0=qMin(m_gy0,y); m_gy1=qMax(m_gy1,y);
            m_gz0=qMin(m_gz0,z); m_gz1=qMax(m_gz1,z);
            hasVerts = true;
        }
    } else {
        f.read(80);
        quint32 nTri = 0;
        f.read(reinterpret_cast<char*>(&nTri), 4);
        for (quint32 i = 0; i < nTri; ++i) {
            float buf[12];
            if (f.read(reinterpret_cast<char*>(buf), 48) < 48) break;
            f.read(2);
            for (int v = 1; v <= 3; ++v) {
                double x=buf[v*3], y=buf[v*3+1], z=buf[v*3+2];
                m_gx0=qMin(m_gx0,x); m_gx1=qMax(m_gx1,x);
                m_gy0=qMin(m_gy0,y); m_gy1=qMax(m_gy1,y);
                m_gz0=qMin(m_gz0,z); m_gz1=qMax(m_gz1,z);
                hasVerts = true;
            }
        }
    }

    if (!hasVerts) return false;
    m_Lc = qMax(m_gx1-m_gx0, qMax(m_gy1-m_gy0, m_gz1-m_gz0));
    return m_Lc > 0;
}

// axis-aware domain bounds

void DomainSetupDialog::getDomainBounds(double& x0, double& x1,
                                         double& y0, double& y1,
                                         double& z0, double& z1) const
{
    double up   = m_upstreamMult->value();
    double down = m_downstreamMult->value();
    double side = m_sideMult->value();
    double cx = (m_gx0 + m_gx1) / 2;
    double cy = (m_gy0 + m_gy1) / 2;
    double cz = (m_gz0 + m_gz1) / 2;

    QString axis = m_flowAxis->currentData().toString();

    if (axis == "+X") {
        x0 = m_gx0 - up   * m_Lc;   x1 = m_gx1 + down * m_Lc;
        y0 = cy - side * m_Lc;       y1 = cy + side * m_Lc;
        z0 = cz - side * m_Lc;       z1 = cz + side * m_Lc;
    } else if (axis == "-X") {
        x0 = m_gx0 - down * m_Lc;   x1 = m_gx1 + up   * m_Lc;
        y0 = cy - side * m_Lc;       y1 = cy + side * m_Lc;
        z0 = cz - side * m_Lc;       z1 = cz + side * m_Lc;
    } else if (axis == "+Y") {
        x0 = cx - side * m_Lc;       x1 = cx + side * m_Lc;
        y0 = m_gy0 - up   * m_Lc;   y1 = m_gy1 + down * m_Lc;
        z0 = cz - side * m_Lc;       z1 = cz + side * m_Lc;
    } else if (axis == "-Y") {
        x0 = cx - side * m_Lc;       x1 = cx + side * m_Lc;
        y0 = m_gy0 - down * m_Lc;   y1 = m_gy1 + up   * m_Lc;
        z0 = cz - side * m_Lc;       z1 = cz + side * m_Lc;
    } else if (axis == "+Z") {
        x0 = cx - side * m_Lc;       x1 = cx + side * m_Lc;
        y0 = cy - side * m_Lc;       y1 = cy + side * m_Lc;
        z0 = m_gz0 - up   * m_Lc;   z1 = m_gz1 + down * m_Lc;
    } else {  // -Z
        x0 = cx - side * m_Lc;       x1 = cx + side * m_Lc;
        y0 = cy - side * m_Lc;       y1 = cy + side * m_Lc;
        z0 = m_gz0 - down * m_Lc;   z1 = m_gz1 + up   * m_Lc;
    }
}

// construction

DomainSetupDialog::DomainSetupDialog(const QString& stlPath,
                                     const QString& caseDir,
                                     QWidget* parent)
    : QDialog(parent), m_stlPath(stlPath), m_caseDir(caseDir)
{
    m_geoName = QFileInfo(stlPath).completeBaseName();
    setWindowTitle("Domain Setup: " + QFileInfo(stlPath).fileName());
    setMinimumWidth(440);
    setModal(true);

    bool hasBounds = computeBounds();

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

    // header
    root->addWidget(new QLabel(
        "<b style='font-size:13px;'>Aerodynamic Domain Setup</b>", this));

    // geometry info
    auto* geoBox  = new QGroupBox("Geometry", this);
    auto* geoForm = new QFormLayout(geoBox);
    geoForm->addRow("File:", new QLabel(QFileInfo(stlPath).fileName(), this));
    if (hasBounds) {
        geoForm->addRow("Extents:", new QLabel(
            QString("%1 × %2 × %3 m")
            .arg(m_gx1-m_gx0,0,'f',3).arg(m_gy1-m_gy0,0,'f',3).arg(m_gz1-m_gz0,0,'f',3), this));
        geoForm->addRow("Char. length:", new QLabel(
            QString("%1 m").arg(m_Lc,0,'f',3), this));
    } else {
        geoForm->addRow("", new QLabel("Could not read bounds", this));
        m_Lc = 1.0;
        m_gx0=m_gy0=m_gz0=-0.5; m_gx1=m_gy1=m_gz1=0.5;
    }
    root->addWidget(geoBox);

    // flow axis - locked to -X for V1; geometry must be oriented nose-forward before importing
    m_flowAxis = new QComboBox(this);  // kept for API compat, not shown
    m_flowAxis->addItem("-X", "-X");
    auto* axisBox  = new QGroupBox("Airflow Direction", this);
    auto* axisForm = new QFormLayout(axisBox);
    axisForm->addRow("Inlet axis:", new QLabel("-X  (orient geometry nose-forward before importing)", this));
    m_aoa = new QDoubleSpinBox(this);
    m_aoa->setRange(-90.0, 90.0);
    m_aoa->setSingleStep(0.5);
    m_aoa->setDecimals(1);
    m_aoa->setSuffix(" °");
    m_aoa->setValue(0.0);
    m_aoa->setToolTip("Angle of attack - rotates the inlet velocity vector (pitch about Y axis)");
    axisForm->addRow("Angle of attack:", m_aoa);
    root->addWidget(axisBox);

    // domain extents
    auto* domBox  = new QGroupBox("Domain Extents  (× char. length)", this);
    auto* domForm = new QFormLayout(domBox);

    auto mkSpin = [&](double v, double lo, double hi) {
        auto* s = new QDoubleSpinBox(this);
        s->setRange(lo, hi); s->setSingleStep(0.5); s->setValue(v);
        s->setDecimals(1); s->setSuffix("  ×");
        return s;
    };
    m_upstreamMult   = mkSpin(5.0,  1.0, 50.0);
    m_downstreamMult = mkSpin(15.0, 1.0, 50.0);
    m_sideMult       = mkSpin(5.0,  1.0, 50.0);
    domForm->addRow("Upstream:",   m_upstreamMult);
    domForm->addRow("Downstream:", m_downstreamMult);
    domForm->addRow("Sides:",      m_sideMult);

    m_domainLabel = new QLabel(this);
    m_domainLabel->setStyleSheet("color:#aaa; font-size:11px; font-family:monospace;");
    domForm->addRow("Bounds:", m_domainLabel);
    root->addWidget(domBox);

    // mesh
    auto* meshBox  = new QGroupBox("Mesh", this);
    auto* meshForm = new QFormLayout(meshBox);

    m_cellSize = new QDoubleSpinBox(this);
    m_cellSize->setRange(0.001, 100.0);
    m_cellSize->setDecimals(3);
    m_cellSize->setSuffix(" m");
    m_cellSize->setValue(qMax(m_Lc / 4.0, 0.01));
    meshForm->addRow("Base cell size:", m_cellSize);

    m_cellsLabel = new QLabel(this);
    m_cellsLabel->setStyleSheet("color:#aaa; font-size:11px;");
    meshForm->addRow("Est. cells:", m_cellsLabel);
    root->addWidget(meshBox);

    // refinement
    auto* refBox  = new QGroupBox("Refinement", this);
    auto* refForm = new QFormLayout(refBox);

    m_refinement = new QComboBox(this);
    m_refinement->addItems({"Coarse  (level 3-4)", "Medium  (level 5-6)", "Fine    (level 6-7)"});
    m_refinement->setCurrentIndex(1);
    refForm->addRow("Surface level:", m_refinement);

    m_addLayers = new QCheckBox("Add boundary layers", this);
    m_addLayers->setChecked(true);
    m_nLayers = new QSpinBox(this);
    m_nLayers->setRange(1, 20); m_nLayers->setValue(5);
    refForm->addRow(m_addLayers, m_nLayers);
    root->addWidget(refBox);

    // buttons
    auto* btnRow  = new QHBoxLayout;
    auto* notNow  = new QPushButton("Not Now", this);
    auto* generate = new QPushButton("Generate Files", this);
    generate->setDefault(true);
    generate->setStyleSheet(
        "QPushButton { background:#f5c500; color:#1a1a1a; font-weight:bold; "
        "padding:6px 16px; border-radius:3px; }"
        "QPushButton:hover { background:#ffd740; }");
    btnRow->addWidget(notNow);
    btnRow->addStretch();
    btnRow->addWidget(generate);
    root->addLayout(btnRow);

    connect(notNow,   &QPushButton::clicked, this, &QDialog::reject);
    connect(generate, &QPushButton::clicked, this, &DomainSetupDialog::onGenerate);

    // Live preview: all parameter spinners + axis combo
    auto upd = [this](double){ updatePreview(); };
    for (auto* s : {m_upstreamMult, m_downstreamMult, m_sideMult, m_cellSize})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, upd);
    connect(m_flowAxis, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ updatePreview(); });

    // Defer the initial preview emit to the next event-loop tick.
    // The PropertiesPanel calls connect() *after* constructing this dialog,
    // so an immediate updatePreview() would fire before any slot is connected
    // and the viewport would never show the initial cyan box + cone.
    QTimer::singleShot(0, this, &DomainSetupDialog::updatePreview);
}

void DomainSetupDialog::updatePreview()
{
    double x0, x1, y0, y1, z0, z1;
    getDomainBounds(x0, x1, y0, y1, z0, z1);

    m_domainLabel->setText(
        QString("X [%1, %2]  Y [%3, %4]  Z [%5, %6]")
        .arg(x0,0,'f',2).arg(x1,0,'f',2)
        .arg(y0,0,'f',2).arg(y1,0,'f',2)
        .arg(z0,0,'f',2).arg(z1,0,'f',2));

    double cs = m_cellSize->value();
    if (cs > 0) {
        long nx = qMax(1L, lround((x1-x0)/cs));
        long ny = qMax(1L, lround((y1-y0)/cs));
        long nz = qMax(1L, lround((z1-z0)/cs));
        long total = nx * ny * nz;
        QString suffix = total > 1000000 ? QString("~%1 M").arg(total/1000000.0, 0,'f',1)
                       : total > 1000    ? QString("~%1 k").arg(total/1000)
                       : QString::number(total);
        m_cellsLabel->setText(QString("%1 × %2 × %3  =  %4").arg(nx).arg(ny).arg(nz).arg(suffix));
    }

    emit previewChanged(x0, y0, z0, x1, y1, z1,
                        m_flowAxis->currentData().toString());
}

// file generation

void DomainSetupDialog::onGenerate()
{
    if (m_caseDir.isEmpty()) {
        QMessageBox::warning(this, "No Case",
            "Open or create an OpenFOAM case first, then re-import the geometry.");
        return;
    }

    QDir().mkpath(m_caseDir + "/system");
    QDir().mkpath(m_caseDir + "/constant/triSurface");

    QStringList failed;
    if (!writeBlockMeshDict())             failed << "system/blockMeshDict";
    if (!writeSnappyHexMeshDict())         failed << "system/snappyHexMeshDict";
    if (!writeSurfaceFeatureExtractDict()) failed << "system/surfaceFeatureExtractDict";

    if (!failed.isEmpty()) {
        QMessageBox::warning(this, "Write Error",
            "Could not write:\n\n  • " + failed.join("\n  • "));
        return;
    }

    const QString axis = "-X";   // V1: always -X - orient geometry nose-forward before importing
    double  aoa  = m_aoa ? m_aoa->value() : 0.0;

    // Persist axis + AoA so other panels (viewport, CakeMeshPanel) stay in sync
    QSettings().setValue("domainSetup/flowAxis", axis);
    QSettings().setValue("domainSetup/aoa", aoa);

    // Rewrite 0/U with the velocity vector for the new axis so the next solver run
    // uses the correct direction without needing a separate SolverConfig re-apply.
    for (const QString& uPath : {m_caseDir + "/0/U",
                                  m_caseDir + "/0.orig/U",
                                  m_caseDir + "/0.org/U"}) {
        QFile uFile(uPath);
        if (!uFile.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString txt = uFile.readAll();
        uFile.close();

        // Only internalField and the inlet patch's value are rotated: walls
        // (fixedValue uniform (0 0 0)) and other patches must stay untouched
        using namespace OFDict;
        static const QRegularExpression reUni(
            R"(^uniform\s*\(\s*([-+\d.eE]+)\s+([-+\d.eE]+)\s+([-+\d.eE]+)\s*\)$)");
        struct Slot { int start, end; double mag; };
        auto slotOf = [&](const DictEntry* e) -> std::optional<Slot> {
            if (!e || e->braceOpen >= 0) return std::nullopt;
            auto mm = reUni.match(entryValue(txt, *e));
            if (!mm.hasMatch()) return std::nullopt;
            double a = mm.captured(1).toDouble(), b = mm.captured(2).toDouble(),
                   c = mm.captured(3).toDouble();
            // Replace just the value text after the keyword
            int vs = skipWsComments(txt, e->keyEnd, e->end);
            int ve = txt.lastIndexOf(';', e->end - 1);
            if (ve < vs) return std::nullopt;
            return Slot{vs, ve, std::sqrt(a*a + b*b + c*c)};
        };

        const QList<DictEntry> top = topLevelEntries(txt, 0, txt.size());
        std::optional<Slot> internal = slotOf(findEntry(top, "internalField"));
        QList<Slot> inletSlots;
        if (const DictEntry* bf = findEntry(top, "boundaryField"); bf && bf->braceOpen >= 0 && bf->braceClose >= 0) {
            const QList<DictEntry> patches = topLevelEntries(txt, bf->braceOpen + 1, bf->braceClose);
            if (const DictEntry* in = findEntry(patches, "inlet"); in && in->braceOpen >= 0 && in->braceClose >= 0) {
                const QList<DictEntry> body = topLevelEntries(txt, in->braceOpen + 1, in->braceClose);
                for (const char* k : {"value", "freestreamValue", "inletValue"})
                    if (auto s = slotOf(findEntry(body, k))) inletSlots << *s;
            }
        }

        // Speed from the inlet (a zero internalField is a legitimate start-from-rest)
        double spd = 0.0;
        for (const Slot& s : inletSlots) spd = std::max(spd, s.mag);
        if (spd < 1e-9 && internal) spd = internal->mag;
        if (spd < 1e-9) continue;

        double aoaRad = aoa * M_PI / 180.0;
        double u0 = spd * std::cos(aoaRad);
        double u1 = spd * std::sin(aoaRad);
        double sign = (axis == "-X" || axis == "-Y" || axis == "-Z") ? -1.0 : 1.0;
        QString newVec;
        if      (axis == "+X" || axis == "-X") newVec = QString("(%1 0 %2)").arg(sign*u0,0,'g',6).arg(u1,0,'g',6);
        else if (axis == "+Y" || axis == "-Y") newVec = QString("(%2 %1 0)").arg(sign*u0,0,'g',6).arg(u1,0,'g',6);
        else                                   newVec = QString("(0 %2 %1)").arg(sign*u0,0,'g',6).arg(u1,0,'g',6);

        // Back to front so earlier offsets stay valid; zero-valued entries
        // (start from rest, wall-like inletValue) keep their value
        QList<Slot> edits = inletSlots;
        if (internal) edits << *internal;
        std::sort(edits.begin(), edits.end(),
                  [](const Slot& a, const Slot& b) { return a.start > b.start; });
        for (const Slot& s : edits)
            if (s.mag >= 1e-9)
                txt.replace(s.start, s.end - s.start, "uniform " + newVec);

        if (uFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream(&uFile) << txt;
        }
        break;  // only update the first 0/U found
    }

    auto btn = QMessageBox::question(this, "Files Generated",
        QString("3 mesh configuration files written to:\n%1/system\n\n"
                "  • blockMeshDict\n"
                "  • snappyHexMeshDict\n"
                "  • surfaceFeatureExtractDict\n\n"
                "Flow axis: %2    AoA: %3 °\n\n"
                "Suggested workflow:\n"
                "  1. blockMesh\n"
                "  2. surfaceFeatureExtract %4\n"
                "  3. snappyHexMesh -overwrite\n\n"
                "Load the geometry to workspace now?")
        .arg(m_caseDir, axis, QString::number(aoa, 'f', 1), m_geoName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    emit filesGenerated(m_caseDir);
    if (btn == QMessageBox::Yes)
        emit loadRequested(m_caseDir, m_stlPath);
    accept();
}

static QString foamHeader(const QString& cls, const QString& obj)
{
    return QString(
        "FoamFile\n{\n"
        "    version     2.0;\n"
        "    format      ascii;\n"
        "    class       %1;\n"
        "    object      %2;\n"
        "}\n\n").arg(cls).arg(obj);
}

bool DomainSetupDialog::writeBlockMeshDict()
{
    double x0, x1, y0, y1, z0, z1;
    getDomainBounds(x0, x1, y0, y1, z0, z1);

    double cs = m_cellSize->value();
    int nx = qMax(4, (int)round((x1-x0)/cs));
    int ny = qMax(4, (int)round((y1-y0)/cs));
    int nz = qMax(4, (int)round((z1-z0)/cs));

    auto v = [](double a, double b, double c) {
        return QString("    (%1 %2 %3)\n").arg(a,0,'f',6).arg(b,0,'f',6).arg(c,0,'f',6);
    };

    // inlet / outlet / symmetry side patches based on flow axis
    // Vertex indices:
    //   v0=(x0,y0,z0) v1=(x1,y0,z0) v2=(x1,y1,z0) v3=(x0,y1,z0)
    //   v4=(x0,y0,z1) v5=(x1,y0,z1) v6=(x1,y1,z1) v7=(x0,y1,z1)
    // Face lists (outward normals via RHR):
    //   X-min(-X): (0 4 7 3)    X-max(+X): (1 2 6 5)
    //   Y-min(-Y): (0 1 5 4)    Y-max(+Y): (3 7 6 2)
    //   Z-min(-Z): (0 3 2 1)    Z-max(+Z): (4 5 6 7)

    // Side patches use the same names as BlockMeshWriter / CaseGenerator's 0/
    // files (top=+Y, bottom=-Y, front=+Z, back=-Z) so every patch has a BC.
    // X faces are only sides for Y/Z flow axes, which the UI does not offer.
    struct Face { QString name, verts; };
    QList<Face> faces = {
        {"left",   "(0 4 7 3)"}, {"right", "(1 2 6 5)"},   // -X, +X
        {"bottom", "(0 1 5 4)"}, {"top",   "(3 7 6 2)"},   // -Y, +Y
        {"back",   "(0 3 2 1)"}, {"front", "(4 5 6 7)"},   // -Z, +Z
    };
    QString axis = m_flowAxis->currentData().toString();

    // Index of the min/max face along the flow axis
    int axisIdx = axis.endsWith('X') ? 0 : axis.endsWith('Y') ? 2 : 4;
    bool positive = axis.startsWith('+');
    QString inletFace  = faces[axisIdx + (positive ? 0 : 1)].verts;
    QString outletFace = faces[axisIdx + (positive ? 1 : 0)].verts;

    QString sidePatches;
    for (int i = 0; i < faces.size(); ++i) {
        if (i == axisIdx || i == axisIdx + 1) continue;
        sidePatches += QString("    %1\n    {\n        type symmetry;\n        faces ( %2 );\n    }\n")
                           .arg(faces[i].name, faces[i].verts);
    }

    QString text = foamHeader("dictionary", "blockMeshDict");
    text += "scale   1;\n\nvertices\n(\n";
    text += v(x0,y0,z0);  // 0
    text += v(x1,y0,z0);  // 1
    text += v(x1,y1,z0);  // 2
    text += v(x0,y1,z0);  // 3
    text += v(x0,y0,z1);  // 4
    text += v(x1,y0,z1);  // 5
    text += v(x1,y1,z1);  // 6
    text += v(x0,y1,z1);  // 7
    text += ");\n\n";
    text += QString("blocks\n(\n    hex (0 1 2 3 4 5 6 7) (%1 %2 %3) simpleGrading (1 1 1)\n);\n\n")
            .arg(nx).arg(ny).arg(nz);
    text += "edges ();\n\n";
    text += "boundary\n(\n";
    text += "    inlet\n    {\n        type patch;\n        faces ( " + inletFace + " );\n    }\n";
    text += "    outlet\n    {\n        type patch;\n        faces ( " + outletFace + " );\n    }\n";
    text += sidePatches;
    text += ");\n";

    QFile f(m_caseDir + "/system/blockMeshDict");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(text.toUtf8());
    return true;
}

bool DomainSetupDialog::writeSnappyHexMeshDict()
{
    int surfMin, surfMax, refRegion;
    switch (m_refinement->currentIndex()) {
    case 0: surfMin=3; surfMax=4; refRegion=3; break;
    case 2: surfMin=6; surfMax=7; refRegion=5; break;
    default:surfMin=5; surfMax=6; refRegion=4; break;
    }

    bool layers  = m_addLayers->isChecked();
    int  nLayers = m_nLayers->value();

    double x0, x1, y0, y1, z0, z1;
    getDomainBounds(x0, x1, y0, y1, z0, z1);
    double cx = (x0+x1)/2, cy = (y0+y1)/2, cz = (z0+z1)/2;

    // locationInMesh: near the inlet face, inside the domain and outside the geometry
    double locX, locY, locZ;
    double frac = 0.08;
    QString axis = m_flowAxis->currentData().toString();
    if (axis == "+X") { locX = x0 + (x1-x0)*frac; locY = cy; locZ = cz; }
    else if (axis == "-X") { locX = x1 - (x1-x0)*frac; locY = cy; locZ = cz; }
    else if (axis == "+Y") { locX = cx; locY = y0 + (y1-y0)*frac; locZ = cz; }
    else if (axis == "-Y") { locX = cx; locY = y1 - (y1-y0)*frac; locZ = cz; }
    else if (axis == "+Z") { locX = cx; locY = cy; locZ = z0 + (z1-z0)*frac; }
    else                   { locX = cx; locY = cy; locZ = z1 - (z1-z0)*frac; }

    QString layersSect;
    if (layers) {
        layersSect = QString(
            "    layers\n    {\n"
            "        \"%1_.*\"\n        {\n"
            "            nSurfaceLayers %2;\n"
            "        }\n"
            "    }\n").arg(m_geoName).arg(nLayers);
    }

    QString text = foamHeader("dictionary", "snappyHexMeshDict");
    text += QString(
        "castellatedMesh  true;\n"
        "snap             true;\n"
        "addLayers        %1;\n\n"
        "geometry\n{\n"
        "    %2.stl\n    {\n"
        "        type triSurfaceMesh;\n"
        "        name %2;\n"
        "    }\n"
        "}\n\n"
        "castellatedMeshControls\n{\n"
        "    maxLocalCells         1000000;\n"
        "    maxGlobalCells        2000000;\n"
        "    minRefinementCells    0;\n"
        "    maxLoadUnbalance      0.10;\n"
        "    nCellsBetweenLevels   3;\n\n"
        "    features\n    (\n"
        "        { file \"%2.eMesh\";  level %3; }\n"
        "    );\n\n"
        "    refinementSurfaces\n    {\n"
        "        %2\n        {\n"
        "            level (%4 %5);\n"
        "            patchInfo { type wall; }\n"
        "        }\n"
        "    }\n\n"
        "    resolveFeatureAngle  30;\n\n"
        "    refinementRegions\n    {\n"
        "        %2\n        {\n"
        "            mode  inside;\n"
        "            levels (( 1E15  %6 ));\n"
        "        }\n"
        "    }\n\n"
        "    locationInMesh  (%7 %8 %9);\n"
        "    allowFreeStandingZoneFaces  true;\n"
        "}\n\n"
        "snapControls\n{\n"
        "    nSmoothPatch           3;\n"
        "    tolerance              2.0;\n"
        "    nSolveIter             100;\n"
        "    nRelaxIter             5;\n"
        "    nFeatureSnapIter       10;\n"
        "    implicitFeatureSnap    false;\n"
        "    explicitFeatureSnap    true;\n"
        "    multiRegionFeatureSnap false;\n"
        "}\n\n"
        "addLayersControls\n{\n"
        "    relativeSizes            true;\n"
        "%10"
        "    expansionRatio           1.2;\n"
        "    finalLayerThickness      0.3;\n"
        "    minThickness             0.1;\n"
        "    nGrow                    0;\n"
        "    featureAngle             60;\n"
        "    slipFeatureAngle         30;\n"
        "    nRelaxIter               3;\n"
        "    nSmoothSurfaceNormals    1;\n"
        "    nSmoothNormals           3;\n"
        "    nSmoothThickness         10;\n"
        "    maxFaceThicknessRatio    0.5;\n"
        "    maxThicknessToMedialRatio 0.3;\n"
        "    minMedialAxisAngle       90;\n"
        "    nBufferCellsNoExtrude    0;\n"
        "    nLayerIter               50;\n"
        "}\n\n"
        "meshQualityControls\n{\n"
        "    maxNonOrtho             65;\n"
        "    maxBoundarySkewness     20;\n"
        "    maxInternalSkewness     4;\n"
        "    maxConcave              80;\n"
        "    minVol                  1e-13;\n"
        "    minTetQuality           1e-30;\n"
        "    minArea                 -1;\n"
        "    minTwist                0.05;\n"
        "    minDeterminant          0.001;\n"
        "    minFaceWeight           0.05;\n"
        "    minVolRatio             0.01;\n"
        "    minTriangleTwist        -1;\n"
        "    nSmoothScale            4;\n"
        "    errorReduction          0.75;\n"
        "    relaxed { maxNonOrtho 75; }\n"
        "}\n\n"
        "debug  0;\n"
        "mergeTolerance  1e-6;\n"
    )
    .arg(layers ? "true" : "false")   // %1
    .arg(m_geoName)                   // %2
    .arg(surfMax)                     // %3 feature level
    .arg(surfMin).arg(surfMax)        // %4 %5 surface refinement
    .arg(refRegion)                   // %6
    .arg(locX,0,'f',4).arg(locY,0,'f',4).arg(locZ,0,'f',4)  // %7 %8 %9
    .arg(layersSect);                 // %10

    QFile f(m_caseDir + "/system/snappyHexMeshDict");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(text.toUtf8());
    return true;
}

bool DomainSetupDialog::writeSurfaceFeatureExtractDict()
{
    QString text = foamHeader("dictionary", "surfaceFeatureExtractDict");
    text += QString(
        "%1.stl\n"
        "{\n"
        "    extractionMethod  extractFromSurface;\n\n"
        "    extractFromSurfaceCoeffs\n"
        "    {\n"
        "        includedAngle  150;\n"
        "    }\n\n"
        "    writeObj  yes;\n"
        "}\n"
    ).arg(m_geoName);

    QFile f(m_caseDir + "/system/surfaceFeatureExtractDict");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(text.toUtf8());
    return true;
}
