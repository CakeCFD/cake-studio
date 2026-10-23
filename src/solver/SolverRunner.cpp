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

#include "SolverRunner.h"
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <signal.h>

SolverRunner::SolverRunner(QObject* parent) : QObject(parent) {}

SolverRunner::~SolverRunner() {
    stop();
}

void SolverRunner::run(const QString& solver, const QString& caseDir,
                       const QStringList& args, bool useGpu, double vramFraction) {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        emit solverError("Solver already running");
        return;
    }

    delete m_process;
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(caseDir);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    if (useGpu) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("CAKEFOAM_USE_GPU", "1");
        env.insert("CAKEFOAM_GPU_VRAM_FRACTION", QString::number(vramFraction, 'f', 2));
        m_process->setProcessEnvironment(env);
    }

    connect(m_process, &QProcess::readyReadStandardOutput, this, &SolverRunner::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,  this, &SolverRunner::onReadyReadStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SolverRunner::onFinished);

    QString argStr;
    for (const QString& a : args) {
        QString q = a;
        q.replace("'", "'\\''");
        argStr += " '" + q + "'";
    }

    QString cmd = QString(
        "set -o pipefail; source /usr/lib/openfoam/openfoam2412/etc/bashrc 2>/dev/null && %1%2 2>&1 | tee log.%1")
        .arg(solver, argStr);
    QStringList setsidArgs = { "bash", "-c", cmd };
    m_currentIter = 0;
    m_emittedThisStep.clear();
    m_stopRequested = false;
    m_process->start("setsid", setsidArgs);

    if (!m_process->waitForStarted(3000)) {
        emit solverError("Failed to start solver: " + solver);
        return;
    }

    emit solverStarted();
}

void SolverRunner::stop() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_stopRequested = true;
        // setsid execs bash without forking because QProcess children are never group
        // leaders, so the pid is also the process group id
        const qint64 pgid = m_process->processId();
        if (pgid > 0) ::kill(-static_cast<pid_t>(pgid), SIGTERM);
        else          m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            if (pgid > 0) ::kill(-static_cast<pid_t>(pgid), SIGKILL);
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
}

bool SolverRunner::isRunning() const {
    return m_process && m_process->state() == QProcess::Running;
}

void SolverRunner::onReadyReadStdout() {
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        emit outputLine(line);
        parseLine(line);
    }
}

void SolverRunner::onReadyReadStderr() {
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        emit outputLine("[ERR] " + line);
    }
}

void SolverRunner::onFinished(int exitCode, QProcess::ExitStatus status) {
    m_lastRunStopped = m_stopRequested;
    m_lastRunCrashed = (status == QProcess::CrashExit);
    m_stopRequested = false;
    emit solverFinished(exitCode);
}

void SolverRunner::parseLine(const QString& line) {
    static QRegularExpression timeRe(R"(^Time = ([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*s?$)");
    auto timeMatch = timeRe.match(line);
    if (timeMatch.hasMatch()) {
        ++m_currentIter;
        m_emittedThisStep.clear();
        return;
    }

    static QRegularExpression re(
        R"(Solving for (\w+),\s*Initial residual = ([0-9eE+\-.]+),\s*Final residual = ([0-9eE+\-.]+),\s*No Iterations (\d+))"
    );
    auto match = re.match(line);
    if (!match.hasMatch()) return;

    Residual r;
    r.field      = match.captured(1);
    r.initial    = match.captured(2).toDouble();
    r.final_     = match.captured(3).toDouble();
    r.iterations = match.captured(4).toInt();
    r.iteration  = m_currentIter;

    if (m_emittedThisStep.contains(r.field)) return;
    m_emittedThisStep.insert(r.field);
    emit residualParsed(r);
}
