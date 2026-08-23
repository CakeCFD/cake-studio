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

#include "BCEditorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QHeaderView>
#include <QPushButton>
#include <QMessageBox>
#include <QLabel>
#include <QFile>
#include <QDir>
#include <QSaveFile>
#include <QFileInfo>
#include <QRegularExpression>

const QSet<QString> BCEditorDialog::NO_VALUE_TYPES = {
    "zeroGradient", "noSlip", "symmetry", "slip", "empty",
    "cyclic", "cyclicAMI", "cyclicACMI", "wedge", "wall",
    "processor", "processorCyclic", "calculated"
};

const QStringList BCEditorDialog::BC_TYPES = {
    "zeroGradient", "fixedValue", "noSlip", "symmetry", "slip", "empty",
    "cyclic", "cyclicAMI", "wedge", "wall",
    "inletOutlet", "outletInlet", "totalPressure",
    "pressureInletOutletVelocity", "freestreamVelocity", "freestream",
    "fixedGradient", "uniformFixedValue",
    "epsilonWallFunction", "omegaWallFunction", "kqRWallFunction",
    "nutkWallFunction", "nutLowReWallFunction", "nutUSpaldingWallFunction",
    "turbulentIntensityKineticEnergyInlet", "turbulentMixingLengthFrequencyInlet",
    "calculated", "processor"
};

BCEditorDialog::BCEditorDialog(const QString& caseDir, QWidget* parent)
    : QDialog(parent), m_caseDir(caseDir)
{
    setWindowTitle("Boundary Condition Editor: " + QDir(caseDir).dirName());
    setMinimumSize(860, 580);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(6);

    m_tabs = new QTabWidget;
    root->addWidget(m_tabs, 1);

    discoverFields();

    if (m_tabs->count() == 0)
        root->addWidget(new QLabel("No boundary condition files found in 0/."));

    m_statusLbl = new QLabel;
    m_statusLbl->setStyleSheet("font-size:11px; padding:2px 0;");
    root->addWidget(m_statusLbl);

    auto* bar = new QHBoxLayout;
    bar->addStretch(1);
    auto* btnCancel = new QPushButton("Cancel");
    auto* btnApply  = new QPushButton("Apply All");
    btnApply->setDefault(true);
    bar->addWidget(btnCancel);
    bar->addWidget(btnApply);
    root->addLayout(bar);

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnApply,  &QPushButton::clicked, this, &BCEditorDialog::onApply);
}

void BCEditorDialog::discoverFields()
{
    QDir dir(m_caseDir + "/0");
    if (!dir.exists()) return;

    static const QSet<QString> SKIP = {
        "include", "initialConditions", "boundaryConditions"
    };

    // Sort so the tab order is predictable
    QStringList entries;
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
        if (!fi.fileName().startsWith('.') && !SKIP.contains(fi.fileName()))
            entries << fi.fileName();
    entries.sort();

    for (const QString& name : entries)
        loadField(name);
}

static bool looksLikeVectorField(const QString& text)
{
    QRegularExpression dimRe(R"(dimensions\s*\[([^\]]+)\])");
    auto m = dimRe.match(text);
    if (!m.hasMatch()) return false;
    QStringList d = m.captured(1).trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    // velocity: [0 1 -1 0 0 0 0]
    return d.size() >= 3 && d[1] == "1" && d[2] == "-1";
}

void BCEditorDialog::loadField(const QString& fieldName)
{
    QString path = m_caseDir + "/0/" + fieldName;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    FieldData fd;
    fd.path         = path;
    fd.originalText = QString::fromUtf8(f.readAll());
    fd.isVector     = looksLikeVectorField(fd.originalText);
    f.close();

    // Must have a boundaryField section to be editable
    if (!fd.originalText.contains("boundaryField")) return;

    m_fields[fieldName] = fd;
    buildTab(fieldName, m_fields[fieldName]);
}

QList<BCPatch> BCEditorDialog::parsePatches(const QString& text)
{
    QList<BCPatch> patches;

    int bfIdx = text.indexOf("boundaryField");
    if (bfIdx < 0) return patches;
    int braceOpen = text.indexOf('{', bfIdx);
    if (braceOpen < 0) return patches;

    int depth = 0, i = braceOpen, sectionEnd = text.size() - 1;
    while (i < text.size()) {
        if (text[i] == '{')      ++depth;
        else if (text[i] == '}') { if (!--depth) { sectionEnd = i; break; } }
        ++i;
    }

    QString section = text.mid(braceOpen + 1, sectionEnd - braceOpen - 1);

    // Matches both multi-line ("name\n{") and inline ("name {") OF patch format
    static QRegularExpression patchRe(
        R"((?:^|[\n\r])[ \t]*([\w/.-]+)[ \t]*(?:[\r\n]+[ \t]*)?\{)",
        QRegularExpression::MultilineOption);
    auto it = patchRe.globalMatch(section);
    while (it.hasNext()) {
        auto match = it.next();
        QString patchName = match.captured(1).trimmed();
        if (patchName.isEmpty()) continue;

        int patchBrace = match.capturedEnd() - 1;
        int d = 0, j = patchBrace, patchEnd = patchBrace;
        while (j < section.size()) {
            if (section[j] == '{')      ++d;
            else if (section[j] == '}') { if (!--d) { patchEnd = j; break; } }
            ++j;
        }
        QString body = section.mid(patchBrace + 1, patchEnd - patchBrace - 1);

        BCPatch patch;
        patch.name = patchName;

        QRegularExpression typeRe(R"(\btype\s+(\w+)\s*;)");
        auto tm = typeRe.match(body);
        patch.type = tm.hasMatch() ? tm.captured(1) : "zeroGradient";

        QRegularExpression uniRe(R"(\bvalue\s+uniform\s+([^;]+?)\s*;)");
        auto vm = uniRe.match(body);
        if (vm.hasMatch()) {
            patch.value = vm.captured(1).trimmed();
        } else {
            QRegularExpression anyValRe(R"(\bvalue\s+(nonuniform|List|#codeStream))");
            patch.nonUniform = anyValRe.match(body).hasMatch();
        }

        patches.append(patch);
    }
    return patches;
}

void BCEditorDialog::syncValueWidget(QWidget* w, const QString& type, bool isVector)
{
    auto* edit = qobject_cast<QLineEdit*>(w);
    if (!edit) return;
    bool needsVal = !NO_VALUE_TYPES.contains(type);
    edit->setEnabled(needsVal);
    if (!needsVal)
        edit->setPlaceholderText("not required for " + type);
    else if (isVector)
        edit->setPlaceholderText("eg. (10 0 0)");
    else
        edit->setPlaceholderText("eg. 0");
}

void BCEditorDialog::buildTab(const QString& fieldName, FieldData& fd)
{
    QList<BCPatch> patches = parsePatches(fd.originalText);
    if (patches.isEmpty()) return;

    auto* w   = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    auto* info = new QLabel(QString("%1 patch(es)  |  %2")
        .arg(patches.size())
        .arg(fd.isVector ? "Vector field [m/s]" : "Scalar field"));
    info->setStyleSheet("font-size:11px;");
    lay->addWidget(info);

    auto* table = new QTableWidget(patches.size(), 3);
    table->setHorizontalHeaderLabels({"Patch", "Type", "Value (uniform)"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->resizeSection(1, 260);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false);
    table->verticalHeader()->setDefaultSectionSize(32);
    table->setStyleSheet(
        "QTableWidget { border:none; }"
        "QTableWidget::item { padding: 4px 8px; }"
        "QHeaderView::section { padding:5px 8px; border:none; font-weight:bold; }");

    for (int row = 0; row < patches.size(); ++row) {
        const BCPatch& p = patches[row];

        auto* nameItem = new QTableWidgetItem("  " + p.name);
        table->setItem(row, 0, nameItem);

        auto* combo = new QComboBox;
        combo->addItems(BC_TYPES);
        if (!BC_TYPES.contains(p.type)) combo->addItem(p.type);
        combo->setCurrentText(p.type);
        table->setCellWidget(row, 1, combo);

        QLineEdit* valEdit;
        if (p.nonUniform) {
            valEdit = new QLineEdit("(nonuniform; edit source file directly)");
            valEdit->setEnabled(false);
            valEdit->setStyleSheet("font-style:italic;");
        } else {
            valEdit = new QLineEdit(p.value);
            syncValueWidget(valEdit, p.type, fd.isVector);
        }
        table->setCellWidget(row, 2, valEdit);

        const bool isVec = fd.isVector;
        connect(combo, &QComboBox::currentTextChanged,
                this, [this, valEdit, isVec](const QString& t) {
                    syncValueWidget(valEdit, t, isVec);
                });
    }

    fd.table = table;
    lay->addWidget(table, 1);
    m_tabs->addTab(w, fieldName);
}

bool BCEditorDialog::writeField(FieldData& fd, const QList<BCPatch>& patches, QString& errOut)
{
    int bfIdx = fd.originalText.indexOf("boundaryField");
    if (bfIdx < 0) { errOut = "boundaryField section not found"; return false; }
    int braceOpen = fd.originalText.indexOf('{', bfIdx);
    if (braceOpen < 0) { errOut = "Opening brace not found"; return false; }

    int depth = 0, i = braceOpen, sectionEnd = -1;
    while (i < fd.originalText.size()) {
        if (fd.originalText[i] == '{')      ++depth;
        else if (fd.originalText[i] == '}') { if (!--depth) { sectionEnd = i; break; } }
        ++i;
    }
    if (sectionEnd < 0) { errOut = "Unmatched braces in boundaryField"; return false; }

    QString newBF = "boundaryField\n{\n";
    for (const BCPatch& p : patches) {
        newBF += QString("    %1\n    {\n").arg(p.name);
        newBF += QString("        type            %1;\n").arg(p.type);
        if (!p.nonUniform && !p.value.isEmpty() && !NO_VALUE_TYPES.contains(p.type))
            newBF += QString("        value           uniform %1;\n").arg(p.value);
        newBF += "    }\n";
    }
    newBF += "}";

    QString result = fd.originalText.left(bfIdx)
                   + newBF
                   + fd.originalText.mid(sectionEnd + 1);

    // Validate: re-parse and confirm patch count matches
    QList<BCPatch> check = parsePatches(result);
    if (check.size() != patches.size()) {
        errOut = QString("Post-write validation failed: expected %1 patches, got %2")
                     .arg(patches.size()).arg(check.size());
        return false;
    }

    // Backup original
    QString bakPath = fd.path + ".bak";
    QFile::remove(bakPath);
    QFile::copy(fd.path, bakPath);

    // Atomic write
    QSaveFile sf(fd.path);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errOut = "Cannot open file for writing: " + fd.path;
        return false;
    }
    sf.write(result.toUtf8());
    if (!sf.commit()) {
        errOut = "Atomic commit failed for: " + fd.path;
        return false;
    }

    fd.originalText = result;
    return true;
}

void BCEditorDialog::onApply()
{
    QStringList failed;
    int written = 0;

    for (auto it = m_fields.begin(); it != m_fields.end(); ++it) {
        FieldData& fd = it.value();
        if (!fd.table) continue;

        QList<BCPatch> patches;
        for (int row = 0; row < fd.table->rowCount(); ++row) {
            BCPatch p;
            p.name = fd.table->item(row, 0)->text();
            if (auto* c = qobject_cast<QComboBox*>(fd.table->cellWidget(row, 1)))
                p.type = c->currentText();
            else
                p.type = "zeroGradient";
            if (auto* e = qobject_cast<QLineEdit*>(fd.table->cellWidget(row, 2))) {
                p.nonUniform = !e->isEnabled() && e->text().contains("nonuniform");
                if (!p.nonUniform) p.value = e->text().trimmed();
            }
            patches.append(p);
        }

        QString err;
        if (!writeField(fd, patches, err))
            failed << it.key() + ": " + err;
        else
            ++written;
    }

    if (!failed.isEmpty()) {
        QMessageBox::warning(this, "Write Error",
            "Failed to write the following fields:\n\n" + failed.join("\n") +
            "\n\nOriginals preserved in .bak files.");
        m_statusLbl->setText(QString("Wrote %1 field(s), %2 failed.")
                             .arg(written).arg(failed.size()));
    } else {
        m_statusLbl->setText(
            QString("All %1 field(s) written and validated. Backups saved as .bak").arg(written));
        accept();
    }
}
