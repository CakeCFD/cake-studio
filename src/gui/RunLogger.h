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
#include <QString>
#include <QList>
#include <QMap>
#include <QStringList>

struct RunInfo {
    int     index      = 0;
    QString path;
    QString solver;
    QString divScheme;     // "linearUpwind" | "teno5" | "teno6"
    QString flowDir;       // path to run's snapshotted flow data (for vtkOpenFOAMReader)
    QStringList timeSteps; // snapshotted time step dirs available in flowDir
    QString simType;
    QString turbModel;
    QString deltaModel;   // LES filter-width model (empty for RAS/laminar)
    QString nu;
    QString inletU;
    QString endTime;
    QString nCores;
    QString timestamp;
    bool    converged  = false;
    bool    stopped    = false;
    int     nSteps     = 0;
    double  finalTime  = 0.0;
    QMap<QString, double> finalResiduals;
    double  latestFx   = 0.0, latestFy = 0.0, latestFz = 0.0;
    bool    hasForces  = false;
    QStringList citedKeys;
    bool    hasFlow() const { return !flowDir.isEmpty() && !timeSteps.isEmpty(); }
    QString displayLabel() const {
        QString s = QString("run_%1").arg(index, 3, 10, QChar('0'));
        if (!divScheme.isEmpty()) s += " (" + divScheme + ")";
        return s;
    }
};

class RunLogger {
public:
    // Call just before solver starts: snapshots inputs, returns run directory path
    static QString        startRun(const QString& caseDir, const QString& solver);
    // Call after solver exits: writes results CSV, metadata.json, run_report.md.
    // stopped marks a run the user interrupted (kept, but flagged in metadata).
    static void           finishRun(const QString& runDir, const QString& caseDir, bool converged,
                                    bool stopped = false);
    // Returns all runs found in <caseDir>/runs/, sorted by index
    static QList<RunInfo> listRuns(const QString& caseDir);
    // Reads a RunInfo from a run directory's metadata.json
    static RunInfo        readRunInfo(const QString& runDir);
    // Writes a full markdown report for a RunInfo to outputPath
    static bool           generateReport(const RunInfo& info, const QString& outputPath);

private:
    static int    nextIndex(const QString& caseDir);
    static void   copyInputs(const QString& caseDir, const QString& inputsDir);
    static QString detectSolver(const QString& caseDir, const QString& preferred = {});
    static QString caseDirForRun(const QString& runDir);
    static QString readDictValue(const QString& filePath, const QString& key);
    static QString readDivScheme(const QString& fvSchemesPath);
    static bool    copyDir(const QString& src, const QString& dst);

    struct ResidualRow {
        int step; double time;
        QMap<QString, double> initResiduals;
    };
    struct ForceRow {
        double time, fx, fy, fz, mx, my, mz;
    };

    static QList<ResidualRow> parseResiduals(const QString& logPath);
    static QList<ForceRow>    parseForces(const QString& caseDir);
};
