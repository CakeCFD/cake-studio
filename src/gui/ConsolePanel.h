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
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QCompleter>
#include <QEvent>
#include <QStringListModel>
#include <QProcess>
#include "../solver/SolverRunner.h"
#include "ResidualPlot.h"
#include "RunLogger.h"
#include "themes/ThemeEngine.h"

class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePanel(QWidget* parent = nullptr);
    SolverRunner* runner() { return m_runner; }
    QString caseDir() const;

signals:
    void resultsReady(const QString& caseDir);
    void aboutToRun();   // emitted before the solver starts so MainWindow can flush PhysicsPanel
    void caseFilesChanged(const QString& caseDir);  // controlDict patched - Properties panel needs refresh

public slots:
    void appendLine(const QString& line);
    void onSolverStarted();
    void onSolverFinished(int exitCode);
    void onRunClicked();
    void onStopClicked();
    void setCaseDir(const QString& dir);
    void setSolver(const QString& solver);
    void setTheme(Theme t) { if (m_plot) m_plot->setTheme(t); }

private:
    void setupUI();
    void execCommand(const QString& cmd);
    void pushHistory(const QString& cmd);
    void detectAndCiteSchemes();
    void showSolverBanner(const QString& solver);
    void hideSolverBanner();
    bool eventFilter(QObject* obj, QEvent* ev) override;

    QPlainTextEdit* m_output;
    ResidualPlot*   m_plot;
    QLineEdit*      m_casePathEdit;
    QComboBox*      m_solverCombo;
    QPushButton*    m_runBtn;
    QPushButton*    m_stopBtn;
    QLabel*         m_statusLabel;
    QPushButton*    m_solverConfigBtn = nullptr;
    SolverRunner*   m_runner;

    QFrame*         m_solverBanner    = nullptr;
    QLabel*         m_bannerLabel     = nullptr;

    // Terminal bar
    QLineEdit*          m_cmdEdit     = nullptr;
    QCompleter*         m_completer   = nullptr;
    QStringListModel*   m_suggModel   = nullptr;
    QStringList         m_cmdHistory;
    int                 m_historyIdx  = -1;
    QProcess*           m_cmdProcess  = nullptr;
    QString             m_currentRunDir;
};
