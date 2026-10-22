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
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QSet>
#include <functional>

struct Residual {
    int iteration;
    QString field;
    double initial;
    double final_;
    int iterations;
};

class SolverRunner : public QObject {
    Q_OBJECT
public:
    explicit SolverRunner(QObject* parent = nullptr);
    ~SolverRunner();

    void run(const QString& solver, const QString& caseDir,
             const QStringList& args = {}, bool useGpu = false, double vramFraction = 0.80);
    void stop();
    bool isRunning() const;
    // State of the most recently finished run, valid inside solverFinished handlers
    bool lastRunStopped() const { return m_lastRunStopped; }
    bool lastRunCrashed() const { return m_lastRunCrashed; }

signals:
    void outputLine(const QString& line);
    void residualParsed(const Residual& r);
    void solverStarted();
    void solverFinished(int exitCode);
    void solverError(const QString& message);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onFinished(int exitCode, QProcess::ExitStatus status);

private:
    void parseLine(const QString& line);
    QProcess*    m_process      = nullptr;
    int          m_currentIter  = 0;
    QSet<QString> m_emittedThisStep;  // fields already plotted for current timestep
    bool         m_stopRequested  = false;
    bool         m_lastRunStopped = false;
    bool         m_lastRunCrashed = false;
};
