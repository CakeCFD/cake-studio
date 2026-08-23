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
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QProcess>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>

class MeshQualityDialog : public QDialog {
    Q_OBJECT
public:
    explicit MeshQualityDialog(const QString& caseDir, QWidget* parent = nullptr);

private slots:
    void onRun();
    void onSave();
    void onReadOutput();
    void onFinished(int code);

private:
    struct Metric {
        QString name;
        QString value;
        QString guideline;
        enum Status { OK, WARN, ERROR } status;
    };

    void           parseAndDisplay(const QString& output);
    void           addRow(const Metric& m);
    static double  extractDouble(const QString& text, const QString& pattern);
    static QString extractString(const QString& text, const QString& pattern);

    QString         m_caseDir;
    QString         m_rawOutput;
    QPlainTextEdit* m_rawText  = nullptr;
    QTableWidget*   m_table    = nullptr;
    QLabel*         m_status   = nullptr;
    QProgressBar*   m_progress = nullptr;
    QPushButton*    m_btnRun   = nullptr;
    QPushButton*    m_btnSave  = nullptr;
    QProcess*       m_proc     = nullptr;
};
