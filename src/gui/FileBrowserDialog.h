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
#include <QDialog>
#include <QTreeWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>

class QPushButton;

class FileBrowserDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { OpenFile, SaveFile, OpenDirectory };

    explicit FileBrowserDialog(Mode mode,
                               const QString& startDir = QString(),
                               const QString& filter   = QString(),
                               QWidget* parent = nullptr);

    QString selectedPath() const { return m_selected; }

    static QString getOpenFileName(QWidget* parent,
                                   const QString& title,
                                   const QString& dir    = QString(),
                                   const QString& filter = QString());

    static QString getSaveFileName(QWidget* parent,
                                   const QString& title,
                                   const QString& dir    = QString(),
                                   const QString& filter = QString());

    static QString getExistingDirectory(QWidget* parent,
                                        const QString& title,
                                        const QString& dir = QString());

private slots:
    void navigateTo(const QString& path);
    void onItemActivated(QTreeWidgetItem* item, int col);
    void onItemClicked(QTreeWidgetItem* item, int col);
    void onBookmarkClicked(QListWidgetItem* item);
    void onFilterChanged(int index);
    void onAccept();
    void goUp();
    void deleteSelected();
    void undoDelete();
    void showContextMenu(const QPoint& pos);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    void buildBreadcrumb(const QString& path);
    void populateList();
    void addBookmark(const QString& label, const QString& path);
    void addRecentDir(const QString& path);
    QStringList parseExtensions(const QString& filterSection) const;

    Mode        m_mode;
    QString     m_currentDir;
    QString     m_selected;
    QStringList m_extensions;

    QWidget*     m_breadcrumbWidget;
    QHBoxLayout* m_breadcrumbLayout;
    QTreeWidget* m_fileList;
    QListWidget* m_bookmarks;
    QLineEdit*   m_nameEdit;
    QComboBox*   m_filterCombo;
    QPushButton* m_acceptBtn;

    QStringList m_filterSections;

    QVector<QPair<QString,QString>> m_trashStack;

    QString m_subTextColor;
    QString m_accentColor;
    QString m_textColor;
    QString m_dimTextColor;
};
