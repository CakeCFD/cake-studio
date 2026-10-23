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

#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QString>
#include <QFileSystemWatcher>
#include <QTimer>

class ViewportWidget;

// Data roles stored on each QTreeWidgetItem
enum CaseItemRole {
    RolePath      = Qt::UserRole,       // absolute file path
    RoleType      = Qt::UserRole + 1,   // CaseItemType
    RoleStepIndex = Qt::UserRole + 2,   // index into m_timeSteps (result steps)
};

enum CaseItemType {
    TypeRoot, TypeSection,
    TypeGeoFile, TypeMeshInfo, TypeBCFile, TypeResultStep, TypeSysFile
};

class CaseTreePanel : public QWidget {
    Q_OBJECT
public:
    explicit CaseTreePanel(QWidget* parent = nullptr);
    void setViewport(ViewportWidget* vp) { m_viewport = vp; }

public slots:
    void setCaseDir(const QString& dir);
    void refresh();

signals:
    void itemSelected(int itemType, const QString& path, const QString& caseDir);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onItemClicked(QTreeWidgetItem* item, int col);
    void onItemDoubleClicked(QTreeWidgetItem* item, int col);
    void onContextMenu(const QPoint& pos);

private:
    void populate();
    QTreeWidgetItem* addSection(const QString& label, CaseItemType sectionType);
    void openFileEditor(const QString& path);
    void deleteItem(QTreeWidgetItem* item);
    void addGeometryFile();
    void addDictFile(const QString& dir, const QString& defaultName);

    QString              m_caseDir;
    QStringList          m_timeSteps;
    QTreeWidget*         m_tree;
    QLabel*              m_statusLabel;
    ViewportWidget*      m_viewport = nullptr;
    QFileSystemWatcher*  m_watcher  = nullptr;
    QTimer*              m_refreshTimer = nullptr;   // debounce rapid FS events
};
