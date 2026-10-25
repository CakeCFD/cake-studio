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
#include "themes/ThemeEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QKeyEvent>
#include <QSettings>
#include <QApplication>
#include <QStyle>
#include <QRegularExpression>
#include <QFrame>
#include <QToolButton>
#include <QDateTime>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QStandardPaths>
#include <QFont>

static QString fmtSize(qint64 b)
{
    if (b < 0)       return {};
    if (b < 1024)    return QString("%1 B").arg(b);
    if (b < 1<<20)   return QString("%1 KB").arg(b >> 10);
    if (b < 1LL<<30) return QString("%1 MB").arg(b >> 20);
    return QString("%1 GB").arg(b >> 30);
}

FileBrowserDialog::FileBrowserDialog(Mode mode, const QString& startDir,
                                     const QString& filter, QWidget* parent)
    : QDialog(parent), m_mode(mode)
{
    setMinimumSize(820, 540);
    resize(940, 600);

    const ThemeColors& tc = ThemeEngine::instance().colors();
    m_textColor    = tc.text.name();
    m_subTextColor = tc.textMuted.name();
    m_dimTextColor = tc.textDim.name();
    m_accentColor  = tc.accent.name();

    if (!filter.isEmpty()) {
        for (const QString& sec : filter.split(";;"))
            if (!sec.trimmed().isEmpty())
                m_filterSections << sec.trimmed();
    }
    if (m_filterSections.isEmpty()) m_filterSections << "All Files (*)";
    m_extensions = parseExtensions(m_filterSections.first());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* bcRow = new QHBoxLayout;

    auto* upBtn = new QToolButton;
    upBtn->setText("↑");
    QFont upFont = upBtn->font();
    upFont.setPointSize(upFont.pointSize() + 3);
    upBtn->setFont(upFont);
    upBtn->setFixedSize(30, 30);
    upBtn->setToolTip("Go up  [Backspace]");
    bcRow->addWidget(upBtn);

    m_breadcrumbWidget  = new QWidget;
    m_breadcrumbLayout  = new QHBoxLayout(m_breadcrumbWidget);
    m_breadcrumbLayout->setContentsMargins(4, 0, 0, 0);
    m_breadcrumbLayout->setSpacing(0);
    bcRow->addWidget(m_breadcrumbWidget, 1);

    root->addLayout(bcRow);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);

    m_bookmarks = new QListWidget;
    m_bookmarks->setFixedWidth(175);
    m_bookmarks->setFrameStyle(QFrame::NoFrame);
    m_bookmarks->setFocusPolicy(Qt::NoFocus);
    m_bookmarks->setSpacing(1);

    m_fileList = new QTreeWidget;
    m_fileList->setHeaderLabels({"Name", "Size", "Modified"});
    m_fileList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileList->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_fileList->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_fileList->header()->resizeSection(1, 72);
    m_fileList->header()->resizeSection(2, 148);
    m_fileList->setRootIsDecorated(false);
    m_fileList->setFrameStyle(QFrame::NoFrame);
    m_fileList->setAlternatingRowColors(false);
    m_fileList->setSortingEnabled(false);
    m_fileList->setIconSize(QSize(18, 18));
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);

    splitter->addWidget(m_bookmarks);
    splitter->addWidget(m_fileList);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    auto* bottomRow = new QHBoxLayout;

    if (mode == SaveFile) {
        bottomRow->addWidget(new QLabel("Name:"));
        m_nameEdit = new QLineEdit;
        m_nameEdit->setPlaceholderText("Enter filename...");
        bottomRow->addWidget(m_nameEdit, 1);
    } else {
        m_nameEdit = nullptr;
        bottomRow->addStretch(1);
    }

    if (mode != OpenDirectory && !m_filterSections.isEmpty()) {
        m_filterCombo = new QComboBox;
        for (const QString& s : m_filterSections)
            m_filterCombo->addItem(s);
        m_filterCombo->setFixedWidth(230);
        bottomRow->addWidget(m_filterCombo);
    } else {
        m_filterCombo = nullptr;
    }

    m_acceptBtn = new QPushButton(
        mode == SaveFile      ? "Save"          :
        mode == OpenDirectory ? "Select Folder" : "Open");
    auto* cancelBtn = new QPushButton("Cancel");
    m_acceptBtn->setFixedWidth(110);
    cancelBtn->setFixedWidth(80);
    m_acceptBtn->setEnabled(mode == OpenDirectory);

    bottomRow->addWidget(m_acceptBtn);
    bottomRow->addWidget(cancelBtn);
    root->addLayout(bottomRow);

    auto addSep = [this](const QString& label) {
        auto* it = new QListWidgetItem(label);
        it->setFlags(Qt::NoItemFlags);
        QFont f = it->font();
        f.setPointSize(f.pointSize() - 1);
        f.setBold(false);
        it->setFont(f);
        it->setForeground(QColor(m_subTextColor));
        m_bookmarks->addItem(it);
    };

    addSep("  PLACES");
    addBookmark("Home",  QDir::homePath());
    addBookmark("Root",  "/");

    for (const QString& sub : {"Desktop","Documents","Downloads","PROJECTS"}) {
        QString p = QDir::homePath() + "/" + sub;
        if (QDir(p).exists()) addBookmark(sub, p);
    }

    QSettings s("CakeCFD", "CakeStudio");
    QStringList recent = s.value("recentDirs").toStringList();
    QStringList validRecent;
    for (const QString& r : recent)
        if (QDir(r).exists()) validRecent << r;
    if (!validRecent.isEmpty()) {
        addSep("  RECENT");
        for (const QString& r : validRecent)
            addBookmark(QFileInfo(r).fileName(), r);
    }

    connect(upBtn,       &QToolButton::clicked,                this, &FileBrowserDialog::goUp);
    connect(m_fileList,  &QTreeWidget::itemActivated,          this, &FileBrowserDialog::onItemActivated);
    connect(m_fileList,  &QTreeWidget::itemClicked,            this, &FileBrowserDialog::onItemClicked);
    connect(m_fileList,  &QTreeWidget::customContextMenuRequested,
                                                               this, &FileBrowserDialog::showContextMenu);
    connect(m_bookmarks, &QListWidget::itemClicked,            this, &FileBrowserDialog::onBookmarkClicked);
    connect(m_acceptBtn, &QPushButton::clicked,                this, &FileBrowserDialog::onAccept);
    connect(cancelBtn,   &QPushButton::clicked,                this, &QDialog::reject);

    if (m_nameEdit)
        connect(m_nameEdit, &QLineEdit::returnPressed, this, &FileBrowserDialog::onAccept);
    if (m_filterCombo)
        connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &FileBrowserDialog::onFilterChanged);

    QString start = (!startDir.isEmpty() && QDir(startDir).exists())
                    ? startDir : QDir::homePath();
    navigateTo(start);
}

void FileBrowserDialog::addBookmark(const QString& label, const QString& path)
{
    auto* it = new QListWidgetItem(
        style()->standardIcon(QStyle::SP_DirIcon), "  " + label);
    it->setData(Qt::UserRole, path);
    it->setToolTip(path);
    m_bookmarks->addItem(it);
}

void FileBrowserDialog::navigateTo(const QString& path)
{
    QDir dir(path);
    if (!dir.exists()) return;
    m_currentDir = dir.absolutePath();
    m_selected.clear();
    m_acceptBtn->setEnabled(m_mode == OpenDirectory);
    buildBreadcrumb(m_currentDir);
    populateList();
    addRecentDir(m_currentDir);
}

void FileBrowserDialog::buildBreadcrumb(const QString& path)
{
    while (QLayoutItem* item = m_breadcrumbLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    QStringList parts;
    QDir d(path);
    while (true) {
        parts.prepend(d.dirName().isEmpty() ? "/" : d.dirName());
        QString parent = d.absolutePath();
        if (!d.cdUp() || d.absolutePath() == parent) break;
    }

    QStringList paths;
    {
        QString cur;
        for (const QString& p : parts) {
            cur = (cur == "/" || cur.isEmpty()) ? "/" + (p == "/" ? "" : p)
                                                : cur + "/" + p;
            if (p == "/") cur = "/";
            paths << QDir::cleanPath(cur);
        }
    }

    for (int i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            auto* sep = new QLabel("›");
            sep->setStyleSheet(
                QString("color: %1; padding: 0 2px;").arg(m_subTextColor));
            m_breadcrumbLayout->addWidget(sep);
        }
        const QString segPath = paths[i];
        const QString segName = parts[i];

        if (i == parts.size() - 1) {
            auto* lbl = new QLabel(segName);
            lbl->setStyleSheet(
                QString("font-weight: bold; color: %1; padding: 0 4px;").arg(m_textColor));
            m_breadcrumbLayout->addWidget(lbl);
        } else {
            auto* btn = new QToolButton;
            btn->setText(segName);
            btn->setAutoRaise(true);
            btn->setStyleSheet(
                QString("color: %1; padding: 0 4px;").arg(m_accentColor));
            connect(btn, &QToolButton::clicked, this, [this, segPath]{ navigateTo(segPath); });
            m_breadcrumbLayout->addWidget(btn);
        }
    }
    m_breadcrumbLayout->addStretch(1);
}

void FileBrowserDialog::populateList()
{
    m_fileList->clear();

    QDir dir(m_currentDir);
    dir.setFilter(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
    dir.setSorting(QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    QFileIconProvider icons;

    for (const QFileInfo& fi : dir.entryInfoList()) {
        if (fi.isDir()) {
            auto* it = new QTreeWidgetItem;
            it->setIcon(0, icons.icon(QFileIconProvider::Folder));
            it->setText(0, fi.fileName());
            it->setText(2, fi.lastModified().toString("yyyy-MM-dd  hh:mm"));
            it->setData(0, Qt::UserRole,     fi.absoluteFilePath());
            it->setData(0, Qt::UserRole + 1, true);
            it->setForeground(2, QColor(m_subTextColor));
            it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            m_fileList->addTopLevelItem(it);
        } else {
            if (!m_extensions.isEmpty()) {
                bool match = false;
                for (const QString& ext : m_extensions)
                    if (fi.suffix().compare(ext, Qt::CaseInsensitive) == 0) { match = true; break; }
                if (!match) continue;
            }
            auto* it = new QTreeWidgetItem;
            it->setIcon(0, icons.icon(fi));
            it->setText(0, fi.fileName());
            it->setText(1, fmtSize(fi.size()));
            it->setText(2, fi.lastModified().toString("yyyy-MM-dd  hh:mm"));
            it->setData(0, Qt::UserRole,     fi.absoluteFilePath());
            it->setData(0, Qt::UserRole + 1, false);
            it->setForeground(1, QColor(m_subTextColor));
            it->setForeground(2, QColor(m_subTextColor));
            it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            m_fileList->addTopLevelItem(it);
        }
    }
}

void FileBrowserDialog::goUp()
{
    QDir d(m_currentDir);
    if (d.cdUp()) navigateTo(d.absolutePath());
}

void FileBrowserDialog::onItemClicked(QTreeWidgetItem* item, int)
{
    if (!item) return;
    bool isDir = item->data(0, Qt::UserRole + 1).toBool();
    QString path = item->data(0, Qt::UserRole).toString();

    if (m_mode == OpenFile && !isDir) {
        if (m_nameEdit) m_nameEdit->setText(item->text(0));
        m_acceptBtn->setEnabled(true);
        m_selected = path;
    } else if (m_mode == SaveFile && !isDir) {
        if (m_nameEdit) m_nameEdit->setText(item->text(0));
    } else if (m_mode == OpenDirectory && isDir) {
        m_selected = path;
        m_acceptBtn->setEnabled(true);
    }
}

void FileBrowserDialog::onItemActivated(QTreeWidgetItem* item, int)
{
    if (!item) return;
    bool isDir = item->data(0, Qt::UserRole + 1).toBool();
    QString path = item->data(0, Qt::UserRole).toString();

    if (isDir) {
        navigateTo(path);
    } else {
        m_selected = path;
        if (m_nameEdit) m_nameEdit->setText(item->text(0));
        onAccept();
    }
}

void FileBrowserDialog::onBookmarkClicked(QListWidgetItem* item)
{
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) navigateTo(path);
}

void FileBrowserDialog::onFilterChanged(int index)
{
    if (index < 0 || index >= m_filterSections.size()) return;
    m_extensions = parseExtensions(m_filterSections[index]);
    populateList();
}

void FileBrowserDialog::onAccept()
{
    if (m_mode == OpenDirectory) {
        auto* sel = m_fileList->currentItem();
        if (sel && sel->data(0, Qt::UserRole + 1).toBool())
            m_selected = sel->data(0, Qt::UserRole).toString();
        else
            m_selected = m_currentDir;
        accept();
        return;
    }

    if (m_mode == SaveFile) {
        QString name = m_nameEdit ? m_nameEdit->text().trimmed() : QString();
        if (name.isEmpty()) return;
        if (!m_extensions.isEmpty() && !name.contains('.'))
            name += "." + m_extensions.first();
        const QString target = m_currentDir + "/" + name;
        QFileInfo fi(target);
        if (fi.exists()) {
            if (fi.isDir()) { navigateTo(target); return; }
            if (QMessageBox::question(this, "Confirm Overwrite",
                    QString("\"%1\" already exists.\nDo you want to replace it?").arg(name),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
        }
        m_selected = target;
        accept();
        return;
    }

    if (m_selected.isEmpty()) {
        auto* sel = m_fileList->currentItem();
        if (sel && !sel->data(0, Qt::UserRole + 1).toBool())
            m_selected = sel->data(0, Qt::UserRole).toString();
    }
    if (!m_selected.isEmpty()) accept();
}

void FileBrowserDialog::showContextMenu(const QPoint& pos)
{
    auto* item = m_fileList->itemAt(pos);
    if (!item) return;
    bool isDir = item->data(0, Qt::UserRole + 1).toBool();
    if (isDir) return;

    QMenu menu(this);
    auto* delAct = menu.addAction("Delete");
    delAct->setShortcut(QKeySequence::Delete);

    if (!m_trashStack.isEmpty()) {
        auto* undoAct = menu.addAction(
            QString("Undo delete '%1'").arg(
                QFileInfo(m_trashStack.last().first).fileName()));
        connect(undoAct, &QAction::triggered, this, &FileBrowserDialog::undoDelete);
    }

    connect(delAct, &QAction::triggered, this, &FileBrowserDialog::deleteSelected);
    menu.exec(m_fileList->viewport()->mapToGlobal(pos));
}

void FileBrowserDialog::deleteSelected()
{
    auto* item = m_fileList->currentItem();
    if (!item) return;
    bool isDir = item->data(0, Qt::UserRole + 1).toBool();
    if (isDir) return;

    QString filePath = item->data(0, Qt::UserRole).toString();
    QString fileName = QFileInfo(filePath).fileName();

    QMessageBox confirm(this);
    confirm.setWindowTitle("Confirm Delete");
    confirm.setText(QString("Delete <b>%1</b>?").arg(fileName));
    confirm.setInformativeText("This can be undone during this session.");
    confirm.setIcon(QMessageBox::Warning);
    auto* delBtn    = confirm.addButton("Delete", QMessageBox::DestructiveRole);
    auto* cancelBtn = confirm.addButton("Cancel", QMessageBox::RejectRole);
    confirm.setDefaultButton(cancelBtn);
    confirm.exec();

    if (confirm.clickedButton() != delBtn) return;

    QString trashDir = QDir::tempPath() + "/.cakecfd_trash";
    QDir().mkpath(trashDir);

    QString trashName = fileName + "_" +
        QString::number(QDateTime::currentMSecsSinceEpoch());
    QString trashPath = trashDir + "/" + trashName;

    if (!QFile::rename(filePath, trashPath)) {
        QMessageBox::critical(this, "Delete Failed",
            QString("Could not move '%1' to trash.\nCheck file permissions.").arg(fileName));
        return;
    }

    m_trashStack.append({filePath, trashPath});
    populateList();
}

void FileBrowserDialog::undoDelete()
{
    if (m_trashStack.isEmpty()) return;

    auto [origPath, trashPath] = m_trashStack.last();
    QString fileName = QFileInfo(origPath).fileName();

    QDir destDir = QFileInfo(origPath).dir();
    if (!destDir.exists()) {
        QMessageBox::warning(this, "Undo Failed",
            QString("Original directory no longer exists:\n%1").arg(destDir.absolutePath()));
        return;
    }

    if (QFile::exists(origPath)) {
        QMessageBox::warning(this, "Undo Failed",
            QString("A file named '%1' already exists at the original location.").arg(fileName));
        return;
    }

    if (!QFile::rename(trashPath, origPath)) {
        QMessageBox::critical(this, "Undo Failed",
            QString("Could not restore '%1'.").arg(fileName));
        return;
    }

    m_trashStack.removeLast();
    if (QFileInfo(origPath).dir().absolutePath() == m_currentDir)
        populateList();
}

void FileBrowserDialog::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Backspace) {
        goUp();
        return;
    }
    if (e->key() == Qt::Key_Delete) {
        deleteSelected();
        return;
    }
    if (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_Z) {
        undoDelete();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (m_nameEdit && m_nameEdit->hasFocus()) return;
        auto* sel = m_fileList->currentItem();
        if (sel) onItemActivated(sel, 0);
        return;
    }
    QDialog::keyPressEvent(e);
}

QStringList FileBrowserDialog::parseExtensions(const QString& section) const
{
    QRegularExpression re(R"(\*\.(\w+))");
    auto it = re.globalMatch(section);
    QStringList exts;
    while (it.hasNext()) exts << it.next().captured(1);
    return exts;
}

void FileBrowserDialog::addRecentDir(const QString& path)
{
    QSettings s("CakeCFD", "CakeStudio");
    QStringList recents = s.value("recentDirs").toStringList();
    recents.removeAll(path);
    recents.prepend(path);
    while (recents.size() > 8) recents.removeLast();
    s.setValue("recentDirs", recents);
}

QString FileBrowserDialog::getOpenFileName(QWidget* parent, const QString& title,
                                           const QString& dir, const QString& filter)
{
    FileBrowserDialog dlg(OpenFile, dir, filter, parent);
    dlg.setWindowTitle(title);
    return dlg.exec() == QDialog::Accepted ? dlg.selectedPath() : QString();
}

QString FileBrowserDialog::getSaveFileName(QWidget* parent, const QString& title,
                                           const QString& dir, const QString& filter)
{
    FileBrowserDialog dlg(SaveFile, dir, filter, parent);
    dlg.setWindowTitle(title);
    return dlg.exec() == QDialog::Accepted ? dlg.selectedPath() : QString();
}

QString FileBrowserDialog::getExistingDirectory(QWidget* parent, const QString& title,
                                                const QString& dir)
{
    FileBrowserDialog dlg(OpenDirectory, dir, QString(), parent);
    dlg.setWindowTitle(title);
    return dlg.exec() == QDialog::Accepted ? dlg.selectedPath() : QString();
}
