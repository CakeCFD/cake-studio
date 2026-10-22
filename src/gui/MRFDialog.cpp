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

#include "MRFDialog.h"
#include "BCEditorDialog.h"   // OFDict scanner
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <cmath>

static constexpr double RPM_TO_RADS = M_PI / 30.0;

// helpers

static QDoubleSpinBox* makeSpin(double lo, double hi, double val, int dec = 4)
{
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(dec);
    s->setValue(val);
    s->setSingleStep(0.1);
    return s;
}

// constructor

MRFDialog::MRFDialog(const QString& caseDir, QWidget* parent)
    : QDialog(parent), m_caseDir(caseDir)
{
    setWindowTitle("MRF: Rotating Reference Frames");
    setMinimumSize(680, 520);

    auto* root = new QHBoxLayout;   // no parent: added to outer below

    // Left: zone list
    auto* leftCol = new QVBoxLayout;
    leftCol->addWidget(new QLabel("Rotating zones:"));

    m_list = new QListWidget;
    m_list->setFixedWidth(180);
    leftCol->addWidget(m_list, 1);

    auto* btnRow = new QHBoxLayout;
    auto* btnAdd = new QPushButton("+ Add");
    auto* btnRem = new QPushButton("Remove");
    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnRem);
    leftCol->addLayout(btnRow);
    root->addLayout(leftCol);

    // Right: detail form
    auto* rightCol = new QVBoxLayout;

    m_detailBox = new QGroupBox("Zone settings");
    m_detailBox->setEnabled(false);
    auto* form = new QFormLayout(m_detailBox);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText("MRF1");
    form->addRow("Zone name:", m_nameEdit);

    m_zoneEdit = new QLineEdit;
    m_zoneEdit->setPlaceholderText("rotorZone  (must exist as a cellZone in the mesh)");
    form->addRow("Cell zone:", m_zoneEdit);

    // Origin
    auto* origRow = new QHBoxLayout;
    m_ox = makeSpin(-1e4, 1e4, 0.0); m_oy = makeSpin(-1e4, 1e4, 0.0); m_oz = makeSpin(-1e4, 1e4, 0.0);
    origRow->addWidget(new QLabel("x")); origRow->addWidget(m_ox);
    origRow->addWidget(new QLabel("y")); origRow->addWidget(m_oy);
    origRow->addWidget(new QLabel("z")); origRow->addWidget(m_oz);
    form->addRow("Origin (m):", origRow);

    // Axis
    auto* axRow = new QHBoxLayout;
    m_ax = makeSpin(-1, 1, 0.0); m_ay = makeSpin(-1, 1, 0.0); m_az = makeSpin(-1, 1, 1.0);
    axRow->addWidget(new QLabel("x")); axRow->addWidget(m_ax);
    axRow->addWidget(new QLabel("y")); axRow->addWidget(m_ay);
    axRow->addWidget(new QLabel("z")); axRow->addWidget(m_az);
    form->addRow("Rotation axis:", axRow);

    m_rpm = makeSpin(-1e6, 1e6, 1000.0, 1);
    m_rpm->setSuffix(" RPM");
    form->addRow("Speed:", m_rpm);

    m_noRotEdit = new QLineEdit;
    m_noRotEdit->setPlaceholderText("eg. inlet outlet  (leave blank if none)");
    form->addRow("Non-rotating patches:", m_noRotEdit);

    // Info label
    auto* hint = new QLabel(
        "ℹ  The cell zone must be created during meshing\n"
        "   (snappyHexMesh refinementRegions to cellZone).\n"
        "   Positive RPM = right-hand rule about axis.");
    hint->setStyleSheet("color: #6c7086; font-size: 11px;");
    hint->setWordWrap(true);

    rightCol->addWidget(m_detailBox);
    rightCol->addWidget(hint);
    rightCol->addStretch();
    root->addLayout(rightCol, 1);

    // Bottom bar
    auto* outer = new QVBoxLayout;
    outer->addLayout(root);

    m_status = new QLabel;
    m_status->setStyleSheet("color: #6c7086; font-size: 11px; padding: 2px;");
    outer->addWidget(m_status);

    auto* btns = new QHBoxLayout;
    auto* btnCancel = new QPushButton("Cancel");
    auto* btnApply  = new QPushButton("Apply");
    btns->addStretch();
    btns->addWidget(btnCancel);
    btns->addWidget(btnApply);
    outer->addLayout(btns);

    setLayout(outer);

    connect(btnAdd,    &QPushButton::clicked,               this, &MRFDialog::onAdd);
    connect(btnRem,    &QPushButton::clicked,               this, &MRFDialog::onRemove);
    connect(m_list,    &QListWidget::currentRowChanged,      this, &MRFDialog::onSelectionChanged);
    connect(btnCancel, &QPushButton::clicked,               this, &QDialog::reject);
    connect(btnApply,  &QPushButton::clicked,               this, &MRFDialog::onApply);

    loadFromFile();
}

// file I/O

void MRFDialog::loadFromFile()
{
    QString path = m_caseDir + "/constant/MRFProperties";
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        m_zones = parseFile(QString::fromUtf8(f.readAll()));

    m_list->clear();
    for (const MRFZone& z : m_zones)
        m_list->addItem(z.name);
}

QList<MRFZone> MRFDialog::parseFile(const QString& text)
{
    QList<MRFZone> zones;

    // Each depth-0 dict is a zone; nested dicts (eg. omega { type ...; })
    // belong to their zone and are not zones themselves
    for (const OFDict::DictEntry& e : OFDict::topLevelEntries(text, 0, text.size())) {
        if (e.braceOpen < 0 || e.braceClose < 0 || e.key == "FoamFile") continue;

        const QString body = text.mid(e.braceOpen + 1, e.braceClose - e.braceOpen - 1);
        const QList<OFDict::DictEntry> entries =
            OFDict::topLevelEntries(body, 0, body.size());

        MRFZone z;
        z.name = e.key;

        auto val = [&](const QString& key) -> QString {
            const OFDict::DictEntry* ke = OFDict::findEntry(entries, key);
            if (!ke) return QString();
            if (ke->braceOpen >= 0)   // dict form: hand back its contents
                return body.mid(ke->braceOpen + 1, ke->braceClose - ke->braceOpen - 1);
            return OFDict::entryValue(body, *ke);
        };

        z.cellZone = val("cellZone");
        const QString act = val("active");
        if (!act.isEmpty()) z.active = act;

        // origin ( x y z )
        QRegularExpression vecRe(R"(origin\s+\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))");
        auto vm = vecRe.match(body);
        if (vm.hasMatch()) {
            z.originX = vm.captured(1).toDouble();
            z.originY = vm.captured(2).toDouble();
            z.originZ = vm.captured(3).toDouble();
        }

        // axis ( x y z )
        QRegularExpression axRe(R"(axis\s+\(\s*([\d.eE+\-]+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)\s*\))");
        auto am = axRe.match(body);
        if (am.hasMatch()) {
            z.axisX = am.captured(1).toDouble();
            z.axisY = am.captured(2).toDouble();
            z.axisZ = am.captured(3).toDouble();
        }

        // omega: stored as rad/s, convert to RPM for display
        QString omStr = val("omega");
        // "omega constant 104.72", "omega 104.72" or "omega { type constant; value 104.72; }"
        static const QRegularExpression omRe(
            R"((?<![\w.])[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)");
        auto om = omRe.match(omStr);
        if (om.hasMatch())
            z.rpm = om.captured(0).toDouble() / RPM_TO_RADS;

        // nonRotatingPatches ( a b c )
        QRegularExpression nrRe(R"(nonRotatingPatches\s+\(([^)]*)\))");
        auto nr = nrRe.match(body);
        if (nr.hasMatch()) z.nonRotPatches = nr.captured(1).simplified();

        zones.append(z);
    }
    return zones;
}

QString MRFDialog::buildFile() const
{
    QString out;
    out += "FoamFile\n{\n"
           "    version     2.0;\n"
           "    format      ascii;\n"
           "    class       dictionary;\n"
           "    object      MRFProperties;\n"
           "}\n\n";

    for (const MRFZone& z : m_zones) {
        double omega = z.rpm * RPM_TO_RADS;
        out += z.name + "\n{\n";
        out += QString("    cellZone            %1;\n").arg(z.cellZone);
        out += QString("    active              %1;\n").arg(z.active);
        out += QString("    nonRotatingPatches  (%1);\n").arg(z.nonRotPatches);
        out += QString("    origin              (%1 %2 %3);\n")
                   .arg(z.originX).arg(z.originY).arg(z.originZ);
        out += QString("    axis                (%1 %2 %3);\n")
                   .arg(z.axisX).arg(z.axisY).arg(z.axisZ);
        out += QString("    omega               constant %1;  // %2 RPM\n")
                   .arg(omega, 0, 'g', 8).arg(z.rpm, 0, 'f', 1);
        out += "}\n\n";
    }
    return out;
}

void MRFDialog::writeZones()
{
    QString path = m_caseDir + "/constant/MRFProperties";

    if (m_zones.isEmpty()) {
        // No zones: remove the file so OpenFOAM runs without MRF.
        // Leaving an empty file causes OF to initialise MRFZoneList and
        // throw spurious "cannot find cellZone" errors on non-rotating cases.
        QFile::remove(path);
        m_status->setText("MRFProperties removed: no rotating zones.");
        return;
    }

    QDir().mkpath(m_caseDir + "/constant");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Write Error",
            "Could not write constant/MRFProperties");
        return;
    }
    f.write(buildFile().toUtf8());
    m_status->setText("Written: constant/MRFProperties  (" +
                      QString::number(m_zones.size()) + " zone(s))");
}

// UI helpers

void MRFDialog::saveToDetail(int idx)
{
    if (idx < 0 || idx >= m_zones.size()) return;
    MRFZone& z = m_zones[idx];
    z.name          = m_nameEdit->text().trimmed();
    z.cellZone      = m_zoneEdit->text().trimmed();
    z.originX       = m_ox->value(); z.originY = m_oy->value(); z.originZ = m_oz->value();
    z.axisX         = m_ax->value(); z.axisY   = m_ay->value(); z.axisZ   = m_az->value();
    z.rpm           = m_rpm->value();
    z.nonRotPatches = m_noRotEdit->text().trimmed();
    if (!z.name.isEmpty())
        m_list->item(idx)->setText(z.name);
}

void MRFDialog::loadToDetail(int idx)
{
    if (idx < 0 || idx >= m_zones.size()) { m_detailBox->setEnabled(false); return; }
    const MRFZone& z = m_zones[idx];
    m_nameEdit->setText(z.name);
    m_zoneEdit->setText(z.cellZone);
    m_ox->setValue(z.originX); m_oy->setValue(z.originY); m_oz->setValue(z.originZ);
    m_ax->setValue(z.axisX);   m_ay->setValue(z.axisY);   m_az->setValue(z.axisZ);
    m_rpm->setValue(z.rpm);
    m_noRotEdit->setText(z.nonRotPatches);
    m_detailBox->setEnabled(true);
}

// slots

void MRFDialog::onAdd()
{
    if (m_currentIdx >= 0) saveToDetail(m_currentIdx);

    MRFZone z;
    z.name = QString("MRF%1").arg(m_zones.size() + 1);
    m_zones.append(z);
    m_list->addItem(z.name);
    m_list->setCurrentRow(m_zones.size() - 1);
}

void MRFDialog::onRemove()
{
    int row = m_list->currentRow();
    if (row < 0 || row >= m_zones.size()) return;
    m_zones.removeAt(row);
    delete m_list->takeItem(row);
    m_currentIdx = -1;
    int newRow = qMin(row, m_list->count() - 1);
    if (newRow >= 0) m_list->setCurrentRow(newRow);
    else             m_detailBox->setEnabled(false);
}

void MRFDialog::onSelectionChanged()
{
    int row = m_list->currentRow();
    if (m_currentIdx >= 0 && m_currentIdx < m_zones.size())
        saveToDetail(m_currentIdx);
    m_currentIdx = row;
    loadToDetail(row);
}

void MRFDialog::onApply()
{
    if (m_currentIdx >= 0) saveToDetail(m_currentIdx);

    for (const MRFZone& z : m_zones) {
        if (z.cellZone.isEmpty()) {
            QMessageBox::warning(this, "Missing cell zone",
                QString("Zone '%1' has no cell zone name.").arg(z.name));
            return;
        }
    }

    writeZones();
    accept();
}
