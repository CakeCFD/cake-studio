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

    QStringList entries;
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
        if (!fi.fileName().startsWith('.') && !SKIP.contains(fi.fileName()))
            entries << fi.fileName();
    entries.sort();

    for (const QString& name : entries)
        loadField(name);
}

namespace OFDict {

int skipWsComments(const QString& t, int i, int end)
{
    while (i < end) {
        const QChar c = t[i];
        if (c.isSpace()) { ++i; continue; }
        if (c == '/' && i + 1 < end && t[i + 1] == '/') {
            while (i < end && t[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < end && t[i + 1] == '*') {
            int e = t.indexOf("*/", i + 2);
            i = (e < 0 || e + 2 > end) ? end : e + 2;
            continue;
        }
        break;
    }
    return i;
}

int skipString(const QString& t, int i, int end)
{
    ++i;
    while (i < end) {
        if (t[i] == '\\') { i += 2; continue; }
        if (t[i] == '"')  return i + 1;
        ++i;
    }
    return end;
}

int matchBrace(const QString& t, int open, int end)
{
    int depth = 0, i = open;
    while (i < end) {
        const QChar c = t[i];
        if (c == '"') { i = skipString(t, i, end); continue; }
        if (c == '/' && i + 1 < end && (t[i + 1] == '/' || t[i + 1] == '*')) {
            i = skipWsComments(t, i, end);
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return i;
        ++i;
    }
    return -1;
}

QList<DictEntry> topLevelEntries(const QString& t, int begin, int end)
{
    QList<DictEntry> out;
    int i = begin;
    while (true) {
        i = skipWsComments(t, i, end);
        if (i >= end) break;

        DictEntry e;
        e.start = i;
        if (t[i] == '#') {
            int k = i + 1;
            while (k < end && (t[k].isLetterOrNumber() || t[k] == '_')) ++k;
            e.key = t.mid(i, k - i);
            e.keyEnd = k;
            int nl = t.indexOf('\n', k);
            if (nl < 0 || nl > end) nl = end;
            int br = t.indexOf('{', k);
            if (br >= 0 && br < nl) {
                int c = matchBrace(t, br, end);
                i = c < 0 ? end : c + 1;
            } else {
                i = nl;
            }
            e.end = i;
            out << e;
            continue;
        }

        if (t[i] == '"') {
            i = skipString(t, i, end);
            e.key = t.mid(e.start + 1, i - e.start - 2);
        } else {
            while (i < end && !t[i].isSpace() && t[i] != '{' && t[i] != '}'
                   && t[i] != ';' && t[i] != '"')
                ++i;
            e.key = t.mid(e.start, i - e.start);
            if (e.key.isEmpty()) { ++i; continue; }
        }
        e.keyEnd = i;

        int j = skipWsComments(t, i, end);
        if (j < end && t[j] == '{') {
            e.braceOpen  = j;
            e.braceClose = matchBrace(t, j, end);
            e.end = e.braceClose < 0 ? end : e.braceClose + 1;
            i = e.end;
            out << e;
            continue;
        }

        int depth = 0;
        while (j < end) {
            const QChar c = t[j];
            if (c == '"') { j = skipString(t, j, end); continue; }
            if (c == '/' && j + 1 < end && (t[j + 1] == '/' || t[j + 1] == '*')) {
                j = skipWsComments(t, j, end);
                continue;
            }
            if (c == '(' || c == '{') ++depth;
            else if (c == ')' || c == '}') --depth;
            else if (c == ';' && depth <= 0) break;
            ++j;
        }
        e.end = j < end ? j + 1 : end;
        i = e.end;
        out << e;
    }
    return out;
}

const DictEntry* findEntry(const QList<DictEntry>& entries, const QString& key)
{
    for (const DictEntry& e : entries)
        if (e.key == key) return &e;
    return nullptr;
}

QString entryValue(const QString& t, const DictEntry& e)
{
    int stop = e.end;
    if (stop > e.keyEnd && t[stop - 1] == ';') --stop;
    return t.mid(e.keyEnd, stop - e.keyEnd).trimmed();
}

QString lineIndent(const QString& t, int pos)
{
    int ls = pos > 0 ? t.lastIndexOf('\n', pos - 1) + 1 : 0;
    int k = ls;
    while (k < t.size() && (t[k] == ' ' || t[k] == '\t')) ++k;
    return t.mid(ls, k - ls);
}

}

using namespace OFDict;

static bool looksLikeVectorField(const QString& text)
{
    QRegularExpression dimRe(R"(dimensions\s*\[([^\]]+)\])");
    auto m = dimRe.match(text);
    if (!m.hasMatch()) return false;
    QStringList d = m.captured(1).trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
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

    if (!fd.originalText.contains("boundaryField")) return;

    m_fields[fieldName] = fd;
    buildTab(fieldName, m_fields[fieldName]);
}

QList<BCPatch> BCEditorDialog::parsePatches(const QString& text)
{
    QList<BCPatch> patches;

    const QList<DictEntry> top = topLevelEntries(text, 0, text.size());
    const DictEntry* bf = findEntry(top, "boundaryField");
    if (!bf || bf->braceOpen < 0 || bf->braceClose < 0) return patches;

    for (const DictEntry& e : topLevelEntries(text, bf->braceOpen + 1, bf->braceClose)) {
        if (e.braceOpen < 0 || e.braceClose < 0 || e.key.isEmpty()) continue;

        const QList<DictEntry> body = topLevelEntries(text, e.braceOpen + 1, e.braceClose);

        BCPatch patch;
        patch.name       = e.key;
        patch.braceOpen  = e.braceOpen;
        patch.braceClose = e.braceClose;

        const DictEntry* te = findEntry(body, "type");
        patch.type = te ? entryValue(text, *te) : QString("zeroGradient");

        if (const DictEntry* ve = findEntry(body, "value")) {
            static const QRegularExpression uniRe(R"(^uniform\s+(.+)$)",
                QRegularExpression::DotMatchesEverythingOption);
            auto vm = uniRe.match(entryValue(text, *ve));
            if (vm.hasMatch())
                patch.value = vm.captured(1).simplified();
            else
                patch.nonUniform = true;
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

    fd.table   = table;
    fd.patches = patches;
    lay->addWidget(table, 1);
    m_tabs->addTab(w, fieldName);
}

bool BCEditorDialog::writeField(FieldData& fd, const QList<BCPatch>& patches, QString& errOut)
{
    const QList<BCPatch> orig = parsePatches(fd.originalText);
    if (orig.size() != patches.size()) {
        errOut = QString("File changed since it was loaded: expected %1 patches, found %2")
                     .arg(patches.size()).arg(orig.size());
        return false;
    }

    QString result = fd.originalText;
    int changed = 0;
    for (int k = orig.size() - 1; k >= 0; --k) {
        const BCPatch& o = orig[k];
        const BCPatch& p = patches[k];
        const bool typeChanged  = p.type != o.type;
        const bool wantsValue   = !p.nonUniform && !p.value.isEmpty()
                                  && !NO_VALUE_TYPES.contains(p.type);
        const bool valueChanged = wantsValue && p.value.simplified() != o.value;
        if (!typeChanged && !valueChanged) continue;
        ++changed;

        auto bodyEntries = [&]() {
            int close = matchBrace(result, o.braceOpen, result.size());
            return topLevelEntries(result, o.braceOpen + 1,
                                   close < 0 ? result.size() : close);
        };

        if (typeChanged) {
            const QList<DictEntry> body = bodyEntries();
            if (const DictEntry* te = findEntry(body, "type")) {
                result.replace(te->start, te->end - te->start,
                               QString("type            %1;").arg(p.type));
            } else {
                QString ind = lineIndent(result, o.braceOpen) + "    ";
                result.insert(o.braceOpen + 1,
                              QString("\n%1type            %2;").arg(ind, p.type));
            }
        }
        if (valueChanged) {
            const QList<DictEntry> body = bodyEntries();
            const DictEntry* te = findEntry(body, "type");
            if (const DictEntry* ve = findEntry(body, "value")) {
                result.replace(ve->start, ve->end - ve->start,
                               QString("value           uniform %1;").arg(p.value));
            } else if (te) {
                result.insert(te->end, QString("\n%1value           uniform %2;")
                                           .arg(lineIndent(result, te->start), p.value));
            } else {
                QString ind = lineIndent(result, o.braceOpen) + "    ";
                result.insert(o.braceOpen + 1,
                              QString("\n%1value           uniform %2;").arg(ind, p.value));
            }
        }
    }

    if (changed == 0) return true;

    QList<BCPatch> check = parsePatches(result);
    if (check.size() != orig.size()) {
        errOut = QString("Post-write validation failed: expected %1 patches, got %2")
                     .arg(orig.size()).arg(check.size());
        return false;
    }

    QString bakPath = fd.path + ".bak";
    QFile::remove(bakPath);
    QFile::copy(fd.path, bakPath);

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
