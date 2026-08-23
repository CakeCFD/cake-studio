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
#include <QLineEdit>
#include <QLabel>
#include <QString>

class OpenCaseDialog : public QDialog {
    Q_OBJECT
public:
    explicit OpenCaseDialog(QWidget* parent = nullptr);

    QString selectedDir() const;

private slots:
    void onBrowse();
    void onDirChanged(const QString& path);

private:
    QLineEdit* m_pathEdit;
    QLabel*    m_stlInfo;
    QLabel*    m_meshInfo;
    QLabel*    m_resultsInfo;
    QPushButton* m_loadBtn;

    void scanCase(const QString& dir);
};
