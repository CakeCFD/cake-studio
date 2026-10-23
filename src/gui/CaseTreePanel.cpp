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
#include "CaseTreePanel.h"
#include "ViewportWidget.h"
#include "DomainSetupDialog.h"
#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QTreeWidgetItem>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QClipboard>
#include <QApplication>
#include <QTextStream>

// helpers

// Convert a Linux path to its Windows UNC equivalent via wslpath, then open
// in Windows Explorer. Falls back to xdg-open on native Linux (wslpath absent).
static void openInFileManager(const QString& path)
{
    QString target = path.isEmpty() ? QDir::homePath() : path;

    QProcess wp;
    wp.start("wslpath", {"-w", target});
    if (wp.waitForFinished(2000) && wp.exitCode() == 0) {
        QString winPath = QString::fromUtf8(wp.readAllStandardOutput()).trimmed();
        if (!winPath.isEmpty()) {
            QProcess::startDetached("explorer.exe", {winPath});
            return;
        }
    }
    QProcess::startDetached("xdg-open", {target});
}

// Resolve a drop URL to a usable Linux path.
// Windows Explorer drops come as file:///C:/...: convert drive letter to WSL mount.
static QString resolveDropUrl(const QUrl& url)
{
    QString p = url.toLocalFile();
    if (p.isEmpty()) p = url.path();

    // C:/path/file.stl  →  /mnt/c/path/file.stl
    static QRegularExpression driveRe("^([A-Za-z]):[/\\\\](.*)$");
    auto m = driveRe.match(p);
    if (m.hasMatch())
        return "/mnt/" + m.captured(1).toLower() + "/" + m.captured(2).replace('\\', '/');

    return p;
}

static QString itemPath(QTreeWidgetItem* item) {
    return item ? item->data(0, RolePath).toString() : QString{};
}
static CaseItemType itemType(QTreeWidgetItem* item) {
    return item ? (CaseItemType)item->data(0, RoleType).toInt() : TypeRoot;
}

// construction

CaseTreePanel::CaseTreePanel(QWidget* parent) : QWidget(parent)
{
    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    m_statusLabel = new QLabel("No case loaded");
    m_statusLabel->setStyleSheet("color: #666; font-size: 11px; padding: 2px 4px;");
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setAnimated(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setStyleSheet("QTreeWidget { font-size: 11px; border: none; }"
                          "QTreeWidget::item { padding: 2px 0; }"
                          "QTreeWidget::item:hover { background: rgba(255,255,255,0.05); }");
    // Qt delivers drag events to the tree's internal viewport child, not to this widget.
    // Install an event filter on it so drops work when the user drags files onto the panel.
    m_tree->viewport()->setAcceptDrops(true);
    m_tree->viewport()->installEventFilter(this);
    layout->addWidget(m_tree, 1);

    auto* btnRow = new QHBoxLayout;
    auto* refreshBtn = new QPushButton("Refresh");
    refreshBtn->setFixedHeight(22);
    btnRow->addWidget(refreshBtn);
    layout->addLayout(btnRow);

    connect(refreshBtn, &QPushButton::clicked, this, &CaseTreePanel::refresh);
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &CaseTreePanel::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &CaseTreePanel::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &CaseTreePanel::onContextMenu);
}

// public slots

void CaseTreePanel::setCaseDir(const QString& dir)
{
    m_caseDir = dir;
    populate();
    emit itemSelected((int)TypeRoot, dir, dir);

    // Watch key subdirectories for live auto-refresh
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);

        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(600);   // debounce: wait 600ms after last event
        connect(m_refreshTimer, &QTimer::timeout, this, &CaseTreePanel::refresh);

        connect(m_watcher, &QFileSystemWatcher::directoryChanged,
                this, [this](const QString&) { m_refreshTimer->start(); });
        connect(m_watcher, &QFileSystemWatcher::fileChanged,
                this, [this](const QString&) { m_refreshTimer->start(); });
    }

    // Re-watch the new case directories
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());

    for (const QString& sub : {"constant/triSurface", "constant/polyMesh",
                                "0", "system", "constant", "."}) {
        QString path = dir + "/" + sub;
        if (QDir(path).exists())
            m_watcher->addPath(path);
    }
}

void CaseTreePanel::refresh() { populate(); }

// populate

QTreeWidgetItem* CaseTreePanel::addSection(const QString& label, CaseItemType sectionType)
{
    auto* item = new QTreeWidgetItem(m_tree, QStringList{label});
    QFont f = item->font(0); f.setBold(true); item->setFont(0, f);
    item->setForeground(0, QColor(0x64, 0xb5, 0xf6));
    item->setData(0, RoleType, (int)TypeSection);
    item->setData(0, RoleStepIndex, (int)sectionType); // reuse field to ID which section
    return item;
}

void CaseTreePanel::populate()
{
    m_tree->clear();
    m_timeSteps.clear();

    if (m_caseDir.isEmpty() || !QDir(m_caseDir).exists()) {
        m_statusLabel->setText("No case loaded");
        return;
    }

    m_statusLabel->setText(QFileInfo(m_caseDir).fileName());

    // Geometry
    auto* geoSec = addSection("Geometry", TypeSection);
    QString triSurf = m_caseDir + "/constant/triSurface";
    QDir tsDir(triSurf);
    if (tsDir.exists()) {
        for (const QString& f : tsDir.entryList({"*.stl","*.STL","*.obj","*.OBJ"}, QDir::Files)) {
            auto* fi = new QTreeWidgetItem(geoSec, QStringList{f});
            fi->setData(0, RolePath, triSurf + "/" + f);
            fi->setData(0, RoleType, (int)TypeGeoFile);
            fi->setForeground(0, QColor(0xa5, 0xd6, 0xa7));
            fi->setToolTip(0, "Click to load  |  Right-click for options");
        }
    }
    if (geoSec->childCount() == 0) {
        auto* ph = new QTreeWidgetItem(geoSec, QStringList{"(empty; right-click to add)"});
        ph->setForeground(0, QColor(0x55, 0x55, 0x66));
        ph->setData(0, RoleType, (int)TypeSection);
    }
    geoSec->setExpanded(true);

    // Mesh
    auto* meshSec = addSection("Mesh", TypeSection);
    bool hasMesh = QDir(m_caseDir + "/constant/polyMesh").exists();
    if (hasMesh) {
        auto* mi = new QTreeWidgetItem(meshSec, QStringList{"polyMesh  [ready]"});
        mi->setForeground(0, QColor(0xa5, 0xd6, 0xa7));
        mi->setData(0, RoleType, (int)TypeMeshInfo);
        mi->setData(0, RolePath, m_caseDir + "/constant/polyMesh");
        QFileInfo ownerInfo(m_caseDir + "/constant/polyMesh/owner");
        if (ownerInfo.exists()) {
            auto* sz = new QTreeWidgetItem(meshSec,
                QStringList{QString("size: %1 KB").arg(ownerInfo.size() / 1024)});
            sz->setForeground(0, QColor(0x66, 0x66, 0x77));
            sz->setData(0, RoleType, (int)TypeMeshInfo);
        }
    } else {
        auto* nm = new QTreeWidgetItem(meshSec, QStringList{"(not generated: run blockMesh)"});
        nm->setForeground(0, QColor(0x88, 0x44, 0x44));
        nm->setData(0, RoleType, (int)TypeMeshInfo);
    }
    meshSec->setExpanded(hasMesh);

    // Boundary Conditions
    auto* bcSec = addSection("Boundary Conditions", TypeSection);
    QDir zeroDir(m_caseDir + "/0");
    if (zeroDir.exists()) {
        for (const QString& f : zeroDir.entryList(QDir::Files)) {
            auto* fi = new QTreeWidgetItem(bcSec, QStringList{f});
            fi->setData(0, RolePath, m_caseDir + "/0/" + f);
            fi->setData(0, RoleType, (int)TypeBCFile);
            fi->setToolTip(0, "Click to edit  |  Right-click for options");
        }
    } else {
        new QTreeWidgetItem(bcSec, QStringList{"(no 0/ directory)"});
    }
    bcSec->setExpanded(false);

    // Results
    auto* resSec = addSection("Results", TypeSection);
    static QRegularExpression numRe("^\\d+(\\.\\d+)?$");
    for (const QString& e : QDir(m_caseDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok; double v = e.toDouble(&ok);
        if (ok && v > 0) m_timeSteps << e;
    }
    // Numeric order (0.5 < 2 < 10): indices must match ViewportWidget's time list
    std::sort(m_timeSteps.begin(), m_timeSteps.end(),
              [](const QString& a, const QString& b) { return a.toDouble() < b.toDouble(); });
    if (!m_timeSteps.isEmpty()) {
        for (int i = 0; i < m_timeSteps.size(); ++i) {
            auto* ti = new QTreeWidgetItem(resSec, QStringList{"t = " + m_timeSteps[i]});
            ti->setData(0, RolePath, m_caseDir + "/" + m_timeSteps[i]);
            ti->setData(0, RoleType, (int)TypeResultStep);
            ti->setData(0, RoleStepIndex, i);
            ti->setForeground(0, QColor(0xff, 0xe0, 0x82));
            ti->setToolTip(0, "Click to load this time step");
        }
        resSec->setExpanded(true);
    } else {
        auto* nr = new QTreeWidgetItem(resSec, QStringList{"(no results yet)"});
        nr->setForeground(0, QColor(0x55, 0x55, 0x66));
        nr->setData(0, RoleType, (int)TypeSection);
    }

    // System
    auto* sysSec = addSection("System", TypeSection);
    QDir sysDir(m_caseDir + "/system");
    if (sysDir.exists()) {
        for (const QString& f : sysDir.entryList(QDir::Files)) {
            auto* fi = new QTreeWidgetItem(sysSec, QStringList{f});
            fi->setData(0, RolePath, m_caseDir + "/system/" + f);
            fi->setData(0, RoleType, (int)TypeSysFile);
            fi->setToolTip(0, "Click to edit  |  Right-click for options");
        }
    }
    sysSec->setExpanded(false);
}

// single click

void CaseTreePanel::onItemClicked(QTreeWidgetItem* item, int)
{
    if (!item) return;
    CaseItemType t = itemType(item);

    // Always broadcast the selection for the Properties panel
    emit itemSelected((int)t, itemPath(item), m_caseDir);

    switch (t) {
    case TypeGeoFile:
    {
        QString path = itemPath(item);
        if (!m_viewport) break;
        if (path.endsWith(".stl", Qt::CaseInsensitive)) m_viewport->loadSTL(path);
        else if (path.endsWith(".obj", Qt::CaseInsensitive)) m_viewport->loadOBJ(path);
        break;
    }
    case TypeResultStep:
        if (m_viewport)
            m_viewport->loadAtStep(item->data(0, RoleStepIndex).toInt());
        break;
    case TypeBCFile:
    case TypeSysFile:
        openFileEditor(itemPath(item));
        break;
    default: break;
    }
}

void CaseTreePanel::onItemDoubleClicked(QTreeWidgetItem* item, int)
{
    // Double-click on files opens editor; on geo loads viewport (already done by click)
    CaseItemType t = itemType(item);
    if (t == TypeBCFile || t == TypeSysFile)
        openFileEditor(itemPath(item));
}

// context menu

void CaseTreePanel::onContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    QMenu menu(this);

    if (!item) {
        // Clicked blank space
        menu.addAction("Refresh", this, &CaseTreePanel::refresh);
        menu.addAction("Open in File Manager", this, [this]{
            openInFileManager(m_caseDir);
        });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
        return;
    }

    CaseItemType t = itemType(item);

    // Geo file
    if (t == TypeGeoFile) {
        menu.addAction("Load in Viewport", this, [this, item]{
            QString p = itemPath(item);
            if (!m_viewport) return;
            if (p.endsWith(".stl", Qt::CaseInsensitive)) m_viewport->loadSTL(p);
            else m_viewport->loadOBJ(p);
        });
        menu.addAction("Show in File Manager", this, [item]{
            openInFileManager(QFileInfo(itemPath(item)).dir().absolutePath());
        });
        menu.addSeparator();
        menu.addAction("Delete", this, [this, item]{ deleteItem(item); });
    }

    // Mesh info
    else if (t == TypeMeshInfo) {
        menu.addAction("Show in File Manager", this, [item]{
            openInFileManager(itemPath(item));
        });
    }

    // BC file
    else if (t == TypeBCFile) {
        menu.addAction("Open Editor", this, [this, item]{ openFileEditor(itemPath(item)); });
        menu.addAction("Copy Path",   this, [item]{ QApplication::clipboard()->setText(itemPath(item)); });
        menu.addSeparator();
        menu.addAction("Delete", this, [this, item]{ deleteItem(item); });
    }

    // Sys file
    else if (t == TypeSysFile) {
        menu.addAction("Open Editor", this, [this, item]{ openFileEditor(itemPath(item)); });
        menu.addAction("Copy Path",   this, [item]{ QApplication::clipboard()->setText(itemPath(item)); });
        menu.addSeparator();
        menu.addAction("Delete", this, [this, item]{ deleteItem(item); });
    }

    // Result step
    else if (t == TypeResultStep) {
        int idx = item->data(0, RoleStepIndex).toInt();
        menu.addAction("Load This Step", this, [this, idx]{
            if (m_viewport) m_viewport->loadAtStep(idx);
        });
        menu.addSeparator();
        menu.addAction("Delete Time Directory", this, [this, item]{
            QString path = itemPath(item);
            if (QMessageBox::question(this, "Delete", "Delete " + path + "?",
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                QDir(path).removeRecursively();
                refresh();
            }
        });
    }

    // Section header
    else if (t == TypeSection) {
        int sectionHint = item->data(0, RoleStepIndex).toInt();
        QString label = item->text(0);
        // Placeholder child "(empty: ...)" inside a section: use parent's label
        if (label.startsWith("(") && item->parent())
            label = item->parent()->text(0);

        if (label == "Geometry") {
            menu.addAction("Import STL / OBJ...", this, &CaseTreePanel::addGeometryFile);
            menu.addSeparator();
            menu.addAction("Expand All",   this, [item]{ item->setExpanded(true); });
            menu.addAction("Collapse All", this, [item]{ item->setExpanded(false); });
        }
        else if (label == "Boundary Conditions") {
            menu.addAction("New BC File...", this, [this]{
                addDictFile(m_caseDir + "/0", "newField");
            });
        }
        else if (label == "System") {
            menu.addAction("New Dict File...", this, [this]{
                addDictFile(m_caseDir + "/system", "newDict");
            });
        }
        else if (label == "Results") {
            if (!m_timeSteps.isEmpty()) {
                menu.addAction("Load Latest", this, [this]{
                    if (m_viewport) m_viewport->loadAtStep(m_timeSteps.size() - 1);
                });
                menu.addSeparator();
                menu.addAction("Delete All Results...", this, [this]{
                    if (QMessageBox::question(this, "Delete All Results",
                            "Delete all numeric time directories in " + m_caseDir + "?",
                            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
                    QDir d(m_caseDir);
                    for (const QString& ts : m_timeSteps)
                        QDir(d.filePath(ts)).removeRecursively();
                    refresh();
                });
            }
        }

        menu.addSeparator();
        menu.addAction("Refresh", this, &CaseTreePanel::refresh);
    }

    if (!menu.actions().isEmpty())
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// file editor

void CaseTreePanel::openFileEditor(const QString& path)
{
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Cannot Open", "Could not read: " + path);
        return;
    }
    QString content = f.readAll();
    f.close();

    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QFileInfo(path).fileName());
    dlg->resize(700, 500);

    auto* layout = new QVBoxLayout(dlg);

    auto* edit = new QPlainTextEdit(dlg);
    QFont mono("Monospace"); mono.setStyleHint(QFont::TypeWriter); mono.setPointSize(10);
    edit->setFont(mono);
    edit->setPlainText(content);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(edit);

    auto* pathLabel = new QLabel(path, dlg);
    pathLabel->setStyleSheet("color: #666; font-size: 10px;");
    layout->addWidget(pathLabel);

    // Button row: Save (to case file) | Save Globally (to user templates) | Close
    auto* btnRow       = new QHBoxLayout;
    auto* btnSave      = new QPushButton("Save", dlg);
    btnSave->setToolTip("Write back to the case file");
    auto* btnGlobal    = new QPushButton("Save Globally", dlg);
    btnGlobal->setToolTip(
        "Copy this file to your global templates folder so new projects start with it.\n"
        "Stored in: ~/.config/CakeCFD/templates/system/");
    auto* btnClose     = new QPushButton("Close", dlg);
    btnRow->addWidget(btnSave);
    btnRow->addWidget(btnGlobal);
    btnRow->addStretch();
    btnRow->addWidget(btnClose);
    layout->addLayout(btnRow);

    QString fileName = QFileInfo(path).fileName();

    connect(btnSave, &QPushButton::clicked, dlg, [path, edit, dlg] {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(edit->toPlainText().toUtf8());
            f.close();
        } else {
            QMessageBox::warning(dlg, "Save Failed", "Could not write to " + path);
        }
    });

    connect(btnGlobal, &QPushButton::clicked, dlg, [fileName, edit, dlg] {
        QString templDir = QDir::homePath() + "/.config/CakeCFD/templates/system";
        QDir().mkpath(templDir);
        QString dest = templDir + "/" + fileName;
        QFile::remove(dest);
        QFile f(dest);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(edit->toPlainText().toUtf8());
            f.close();
            QMessageBox::information(dlg, "Saved Globally",
                "Template saved to:\n" + dest +
                "\n\nNew projects will use this as the default " + fileName + ".");
        } else {
            QMessageBox::warning(dlg, "Save Failed", "Could not write to:\n" + dest);
        }
    });

    connect(btnClose, &QPushButton::clicked, dlg, &QDialog::close);

    dlg->show();
}

// delete

void CaseTreePanel::deleteItem(QTreeWidgetItem* item)
{
    QString path = itemPath(item);
    if (path.isEmpty()) return;
    if (QMessageBox::question(this, "Delete",
            "Delete " + QFileInfo(path).fileName() + "?",
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    QFile::remove(path);
    refresh();
}

// add file

void CaseTreePanel::addGeometryFile()
{
    if (m_caseDir.isEmpty()) return;
    QString src = FileBrowserDialog::getOpenFileName(
        this, "Import Geometry", QDir::homePath(),
        "Geometry Files (*.stl *.STL *.obj *.OBJ)");
    if (src.isEmpty()) return;

    // Wipe all existing geometry so only one mesh lives in the project at a time,
    // except the chosen file itself when it already lives in triSurface
    QString triSurfPath = m_caseDir + "/constant/triSurface";
    QString dest = triSurfPath + "/" + QFileInfo(src).fileName();
    const QString srcCanon = QFileInfo(src).canonicalFilePath();
    const bool inPlace = (srcCanon == QFileInfo(dest).canonicalFilePath());
    QDir triSurf(triSurfPath);
    if (triSurf.exists()) {
        for (const QFileInfo& fi : triSurf.entryInfoList({"*.stl","*.STL","*.obj","*.OBJ"}, QDir::Files)) {
            if (fi.canonicalFilePath() == srcCanon) continue;
            triSurf.remove(fi.fileName());
        }
    }

    QDir().mkpath(triSurfPath);
    if (!inPlace) {
        QFile::remove(dest);
        QFile::copy(src, dest);
    }
    refresh();
    if (m_viewport) {
        if (dest.endsWith(".stl", Qt::CaseInsensitive)) m_viewport->loadSTL(dest);
        else m_viewport->loadOBJ(dest);
    }
}

void CaseTreePanel::addDictFile(const QString& dir, const QString& defaultName)
{
    if (m_caseDir.isEmpty()) return;
    bool ok;
    QString name = QInputDialog::getText(this, "New File", "File name:",
                                         QLineEdit::Normal, defaultName, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QString path = dir + "/" + name.trimmed();
    if (QFile::exists(path)) {
        QMessageBox::warning(this, "Exists", "File already exists.");
        return;
    }
    QDir().mkpath(dir);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << "FoamFile\n{\n    version 2.0;\n    format  ascii;\n"
           << "    class   dictionary;\n    object  " << name << ";\n}\n\n// ---\n\n";
        f.close();
    }
    refresh();
    openFileEditor(path);
}

// drag and drop

// The QTreeWidget's internal viewport child intercepts all drag events before they
// reach this widget. Forward them here.
bool CaseTreePanel::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_tree->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            dragEnterEvent(static_cast<QDragEnterEvent*>(event));
            return true;
        }
        if (event->type() == QEvent::Drop) {
            dropEvent(static_cast<QDropEvent*>(event));
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void CaseTreePanel::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls()) return;
    for (const QUrl& u : e->mimeData()->urls()) {
        QString p = resolveDropUrl(u);
        if (p.endsWith(".stl", Qt::CaseInsensitive) ||
            p.endsWith(".obj", Qt::CaseInsensitive)) {
            e->acceptProposedAction();
            return;
        }
    }
}

void CaseTreePanel::dropEvent(QDropEvent* e)
{
    if (m_caseDir.isEmpty()) { e->ignore(); return; }
    QDir().mkpath(m_caseDir + "/constant/triSurface");

    QString lastDropped;
    for (const QUrl& u : e->mimeData()->urls()) {
        QString src = resolveDropUrl(u);
        if (!src.endsWith(".stl", Qt::CaseInsensitive) &&
            !src.endsWith(".obj", Qt::CaseInsensitive)) continue;

        QString dest = m_caseDir + "/constant/triSurface/" + QFileInfo(src).fileName();
        // Dropping a file that already lives in triSurface: removing dest would delete the source
        if (QFileInfo(src).canonicalFilePath() != QFileInfo(dest).canonicalFilePath()) {
            if (QFile::exists(dest)) QFile::remove(dest);
            QFile::copy(src, dest);
        }

        if (m_viewport) {
            if (dest.endsWith(".stl", Qt::CaseInsensitive)) m_viewport->loadSTL(dest);
            else m_viewport->loadOBJ(dest);
        }
        lastDropped = dest;
    }

    refresh();
    e->acceptProposedAction();

    // Update the Properties panel to show the dropped geometry immediately
    if (!lastDropped.isEmpty())
        emit itemSelected((int)TypeGeoFile, lastDropped, m_caseDir);

    // Offer domain setup for STL files (the geometry snappyHexMesh needs)
    if (!lastDropped.isEmpty() && lastDropped.endsWith(".stl", Qt::CaseInsensitive)) {
        auto* dlg = new DomainSetupDialog(lastDropped, m_caseDir, this);
        connect(dlg, &DomainSetupDialog::filesGenerated, this, &CaseTreePanel::refresh);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
}
