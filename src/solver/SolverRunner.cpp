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

    // Source OF environment, run solver, tee output to log.<solver> for RunLogger
    QString cmd = QString(
        "set -o pipefail; source /usr/lib/openfoam/openfoam2412/etc/bashrc 2>/dev/null && %1 2>&1 | tee log.%1").arg(solver);
    QStringList bashArgs = { "-c", cmd };
    m_currentIter = 0;
    m_process->start("bash", bashArgs);

    if (!m_process->waitForStarted(3000)) {
        emit solverError("Failed to start solver: " + solver);
        return;
    }

    emit solverStarted();
}

void SolverRunner::stop() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000))
            m_process->kill();
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

void SolverRunner::onFinished(int exitCode, QProcess::ExitStatus) {
    emit solverFinished(exitCode);
}

// Parses OpenFOAM residual lines like:
// smoothSolver:  Solving for Ux, Initial residual = 0.1, Final residual = 1e-5, No Iterations 3
void SolverRunner::parseLine(const QString& line) {
    // Track time steps: steady cases emit integer Time (use as x-axis directly),
    // transient cases emit fractional Time (0.001, 0.0242...) - in that case
    // just increment a counter so m_maxIter stays > 0 and the plot renders.
    static QRegularExpression timeRe(R"(^Time = (\d+(?:\.\d+)?))");
    auto timeMatch = timeRe.match(line);
    if (timeMatch.hasMatch()) {
        double t = timeMatch.captured(1).toDouble();
        const int prev = m_currentIter;
        m_currentIter = (t >= 1.0 && t == static_cast<int>(t)) ? static_cast<int>(t) : m_currentIter + 1;
        if (m_currentIter != prev) m_emittedThisStep.clear();
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

    // Only emit the first residual per field per timestep - subsequent inner
    // PIMPLE/PISO iterations are filtered out to keep the plot clean.
    if (m_emittedThisStep.contains(r.field)) return;
    m_emittedThisStep.insert(r.field);
    emit residualParsed(r);
}
