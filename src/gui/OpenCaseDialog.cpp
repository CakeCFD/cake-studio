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
#include "OpenCaseDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <algorithm>

OpenCaseDialog::OpenCaseDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Open Case");
    setMinimumWidth(520);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);

    // Path row
    auto* pathGroup = new QGroupBox("Case Directory");
    auto* pathRow   = new QHBoxLayout(pathGroup);

    m_pathEdit = new QLineEdit;
    m_pathEdit->setPlaceholderText("Select an OpenFOAM case directory...");
    m_pathEdit->setReadOnly(true);

    auto* browseBtn = new QPushButton("Browse...");
    browseBtn->setFixedWidth(90);

    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(browseBtn);
    root->addWidget(pathGroup);

    // Case preview
    auto* previewGroup = new QGroupBox("Case Contents");
    auto* form = new QFormLayout(previewGroup);

    auto makeInfo = [](const QString& placeholder) {
        auto* l = new QLabel(placeholder);
        l->setStyleSheet("color: #6c7086; font-size: 11px;");
        return l;
    };

    m_stlInfo     = makeInfo("-");
    m_meshInfo    = makeInfo("-");
    m_resultsInfo = makeInfo("-");

    form->addRow("Geometry (STL):",   m_stlInfo);
    form->addRow("Mesh:",             m_meshInfo);
    form->addRow("Results:",          m_resultsInfo);

    root->addWidget(previewGroup);

    // Buttons
    auto* btns = new QDialogButtonBox;
    m_loadBtn  = btns->addButton("Load Case", QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    m_loadBtn->setEnabled(false);
    root->addWidget(btns);

    connect(browseBtn, &QPushButton::clicked,     this, &OpenCaseDialog::onBrowse);
    connect(m_pathEdit, &QLineEdit::textChanged,  this, &OpenCaseDialog::onDirChanged);
    connect(btns,  &QDialogButtonBox::accepted,   this, &QDialog::accept);
    connect(btns,  &QDialogButtonBox::rejected,   this, &QDialog::reject);
}

QString OpenCaseDialog::selectedDir() const
{
    return m_pathEdit->text();
}

void OpenCaseDialog::onBrowse()
{
    QString dir = FileBrowserDialog::getExistingDirectory(
        this, "Select OpenFOAM Case Directory", QDir::homePath());
    if (!dir.isEmpty())
        m_pathEdit->setText(dir);
}

void OpenCaseDialog::onDirChanged(const QString& path)
{
    scanCase(path);
}

void OpenCaseDialog::scanCase(const QString& dir)
{
    auto ok  = [](const QString& s) { return "<span style='color:#a6e3a1;'>" + s + "</span>"; };
    auto warn = [](const QString& s) { return "<span style='color:#f38ba8;'>" + s + "</span>"; };

    bool valid = false;

    // STL geometry
    QDir triSurf(dir + "/constant/triSurface");
    QStringList stls = triSurf.entryList({"*.stl","*.STL"}, QDir::Files);
    if (!stls.isEmpty()) {
        m_stlInfo->setText(ok(stls.join(", ")));
        valid = true;
    } else {
        m_stlInfo->setText(warn("None found in constant/triSurface"));
    }

    // Mesh
    bool hasMesh = QFileInfo(dir + "/constant/polyMesh/points").exists()
                || QFileInfo(dir + "/constant/polyMesh/faces").exists();
    m_meshInfo->setText(hasMesh ? ok("polyMesh present") : warn("No polyMesh found (run blockMesh/snappyHexMesh first)"));
    if (hasMesh) valid = true;

    // Results (time directories)
    QDir caseDir(dir);
    QStringList timeDirs;
    for (const QString& entry : caseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isNum = false;
        entry.toDouble(&isNum);
        if (isNum && entry != "0") timeDirs << entry;
    }
    if (!timeDirs.isEmpty()) {
        // Numeric order: as strings "1000" < "200"
        std::sort(timeDirs.begin(), timeDirs.end(),
                  [](const QString& a, const QString& b) { return a.toDouble() < b.toDouble(); });
        m_resultsInfo->setText(ok(QString("%1 time step(s): latest: %2")
                                  .arg(timeDirs.size()).arg(timeDirs.last())));
        valid = true;
    } else {
        m_resultsInfo->setText(warn("No result time directories found"));
    }

    // Enable Load only if at least something useful was found
    m_loadBtn->setEnabled(valid && QDir(dir).exists());

    // Allow text to render as HTML
    m_stlInfo->setTextFormat(Qt::RichText);
    m_meshInfo->setTextFormat(Qt::RichText);
    m_resultsInfo->setTextFormat(Qt::RichText);
}
