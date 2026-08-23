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

#include "RunLogger.h"
#include "CitationManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTextCodec>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>

// Public API

QString RunLogger::startRun(const QString& caseDir, const QString& solver)
{
    // Clear stale time directories from previous runs so finishRun() snapshots
    // only THIS run's output, not old diverged results.
    static const QRegularExpression reNum(R"(^\d+\.?\d*$)");
    for (const QFileInfo& fi : QDir(caseDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (reNum.match(fi.fileName()).hasMatch() && fi.fileName() != "0")
            QDir(fi.filePath()).removeRecursively();
    }
    // Clear postProcessing so force.dat never mixes runs
    QDir(caseDir + "/postProcessing").removeRecursively();

    int idx = nextIndex(caseDir);
    QString runDir = caseDir + QString("/results/runs/run_%1").arg(idx, 3, 10, QChar('0'));
    QDir().mkpath(runDir + "/inputs");
    QDir().mkpath(runDir + "/results");
    copyInputs(caseDir, runDir + "/inputs");

    QJsonObject meta;
    meta["index"]     = idx;
    meta["solver"]    = solver;
    meta["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    meta["converged"] = false;
    meta["n_steps"]   = 0;

    // Physics snapshot: try both OF naming conventions
    QString mtPath = caseDir + "/constant/momentumTransport";
    if (!QFile::exists(mtPath))
        mtPath = caseDir + "/constant/turbulenceProperties";
    QString simType = readDictValue(mtPath, "simulationType");
    meta["sim_type"] = simType;
    // nu: incompressible → transportProperties; compressible → thermophysicalProperties (mu/Pr); OF10+ → physicalProperties
    QString nu = readDictValue(caseDir + "/constant/transportProperties", "nu");
    if (nu.isEmpty()) nu = readDictValue(caseDir + "/constant/physicalProperties", "nu");
    if (nu.isEmpty()) nu = readDictValue(caseDir + "/constant/thermophysicalProperties", "mu");
    meta["nu"] = nu;
    meta["end_time"] = readDictValue(caseDir + "/system/controlDict", "endTime");
    meta["n_cores"]  = readDictValue(caseDir + "/system/decomposeParDict", "numberOfSubdomains");

    auto firstOf = [&](const QStringList& keys) -> QString {
        for (const QString& k : keys) {
            QString v = readDictValue(mtPath, k);
            if (!v.isEmpty()) return v;
        }
        return {};
    };
    if (simType == "LES") {
        meta["turb_model"]  = firstOf({"LESModel", "subgridStressModel", "model"});
        meta["delta_model"] = readDictValue(mtPath, "delta");
    } else if (simType == "RAS") {
        meta["turb_model"]  = firstOf({"RASModel", "model"});
    } else {
        meta["turb_model"]  = firstOf({"model"});
    }

    QFile uf(caseDir + "/0/U");
    if (uf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString txt = uf.readAll();
        QRegularExpression re(R"(uniform\s*\(\s*([\d.eE+\-]+\s+[\d.eE+\-]+\s+[\d.eE+\-]+)\s*\))");
        auto m = re.match(txt);
        if (m.hasMatch()) meta["inlet_U"] = "(" + m.captured(1) + ")";
    }

    // Div scheme: read from fvSchemes
    meta["div_scheme"] = readDivScheme(caseDir + "/system/fvSchemes");

    QFile mf(runDir + "/metadata.json");
    if (mf.open(QIODevice::WriteOnly))
        mf.write(QJsonDocument(meta).toJson());

    return runDir;
}

void RunLogger::finishRun(const QString& runDir, const QString& caseDir, bool converged)
{
    if (runDir.isEmpty() || !QDir(runDir).exists()) return;

    // Copy solver log
    QString solver = detectSolver(caseDir);
    if (!solver.isEmpty()) {
        QString src = caseDir + "/log." + solver;
        QString dst = runDir  + "/results/log." + solver;
        if (QFile::exists(src)) { QFile::remove(dst); QFile::copy(src, dst); }
    }

    // Parse results
    auto residuals = parseResiduals(caseDir + "/log." + solver);
    auto forces    = parseForces(caseDir);

    // Residuals CSV
    if (!residuals.isEmpty()) {
        QFile rf(runDir + "/results/residuals.csv");
        if (rf.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream o(&rf);
            QStringList fields;
            for (const auto& r : residuals)
                for (const QString& k : r.initResiduals.keys())
                    if (!fields.contains(k)) fields << k;
            o << "step,time," << fields.join(',') << "\n";
            for (const auto& r : residuals) {
                o << r.step << "," << r.time;
                for (const QString& k : fields)
                    o << "," << (r.initResiduals.contains(k)
                                 ? QString::number(r.initResiduals[k], 'e', 6) : "");
                o << "\n";
            }
        }
    }

    // Forces CSV
    if (!forces.isEmpty()) {
        QFile ff(runDir + "/results/forces.csv");
        if (ff.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream o(&ff);
            o << "time,Fx,Fy,Fz,Mx,My,Mz\n";
            for (const auto& r : forces)
                o << r.time << "," << r.fx << "," << r.fy << "," << r.fz
                  << "," << r.mx << "," << r.my << "," << r.mz << "\n";
        }
    }

    // Update metadata.json
    QJsonObject meta;
    {
        QFile mf(runDir + "/metadata.json");
        if (mf.open(QIODevice::ReadOnly))
            meta = QJsonDocument::fromJson(mf.readAll()).object();
    }
    meta["converged"]  = converged;
    meta["n_steps"]    = residuals.size();
    meta["final_time"] = residuals.isEmpty() ? 0.0 : residuals.last().time;

    QJsonArray citeArr;
    for (const QString& k : CitationManager::instance().allCitedKeys()) citeArr.append(k);
    meta["cited_keys"] = citeArr;

    if (!residuals.isEmpty()) {
        QJsonObject res;
        for (auto it = residuals.last().initResiduals.begin();
             it != residuals.last().initResiduals.end(); ++it)
            res[it.key()] = it.value();
        meta["final_residuals"] = res;
    }
    if (!forces.isEmpty()) {
        const auto& fr = forces.last();
        QJsonObject fobj;
        fobj["Fx"] = fr.fx; fobj["Fy"] = fr.fy; fobj["Fz"] = fr.fz;
        fobj["Mx"] = fr.mx; fobj["My"] = fr.my; fobj["Mz"] = fr.mz;
        fobj["time"] = fr.time;
        meta["forces_latest"] = fobj;
    }
    {
        QFile mf(runDir + "/metadata.json");
        if (mf.open(QIODevice::WriteOnly))
            mf.write(QJsonDocument(meta).toJson());
    }

    // Snapshot the final time step into run_XXX/results/flow/ so each run has
    // independent flow data. Multiple runs in the same case dir otherwise overwrite
    // each other's time directories.
    {
        static const QRegularExpression reNum(R"(^\d+\.?\d*$)");
        QStringList timeSteps;
        for (const QFileInfo& fi : QDir(caseDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (reNum.match(fi.fileName()).hasMatch() && fi.fileName() != "0")
                timeSteps << fi.fileName();
        }
        std::sort(timeSteps.begin(), timeSteps.end(),
                  [](const QString& a, const QString& b) { return a.toDouble() < b.toDouble(); });

        QString flowDir = runDir + "/results/flow";
        QDir().mkpath(flowDir);

        // Empty case.foam marker required by vtkOpenFOAMReader
        { QFile f(flowDir + "/case.foam"); f.open(QIODevice::WriteOnly); }

        // Copy constant/polyMesh: real files, not symlinks.
        // Symlinks can cause silent vtkOpenFOAMReader failures on some VTK/WSL
        // builds. Each run snapshot is self-contained (Blender-style).
        copyDir(caseDir + "/constant/polyMesh", flowDir + "/constant/polyMesh");

        // Copy small constant text files needed by the reader
        QDir().mkpath(flowDir + "/constant");
        for (const QString& cf : {"transportProperties", "momentumTransport",
                                   "turbulenceProperties", "thermophysicalProperties",
                                   "physicalProperties"}) {
            QString src = caseDir + "/constant/" + cf;
            if (QFile::exists(src))
                QFile::copy(src, flowDir + "/constant/" + cf);
        }

        // Minimal system dir: controlDict is all vtkOpenFOAMReader needs
        QDir().mkpath(flowDir + "/system");
        QFile::copy(caseDir + "/system/controlDict",
                    flowDir + "/system/controlDict");

        // Copy only the final time step: this is all vtkOpenFOAMReader needs
        QStringList snapshotSteps;
        if (!timeSteps.isEmpty()) {
            QString lastTs = timeSteps.last();
            if (copyDir(caseDir + "/" + lastTs, flowDir + "/" + lastTs))
                snapshotSteps << lastTs;
        }

        QJsonObject cfg;
        cfg["case_dir"]          = flowDir;   // snapshot dir, NOT the live case dir
        cfg["original_case_dir"] = caseDir;
        cfg["div_scheme"]        = meta["div_scheme"].toString();
        cfg["solver"]            = meta["solver"].toString();
        QJsonArray tsArr;
        for (const QString& ts : snapshotSteps) tsArr.append(ts);
        cfg["time_steps"] = tsArr;
        QFile cf(runDir + "/flow_config.json");
        if (cf.open(QIODevice::WriteOnly))
            cf.write(QJsonDocument(cfg).toJson());
    }

    // Auto-generate per-run report
    RunInfo info = readRunInfo(runDir);
    generateReport(info, runDir + "/run_report.md");
}

QList<RunInfo> RunLogger::listRuns(const QString& caseDir)
{
    QList<RunInfo> result;
    // Check new location first, then legacy location for backward compat
    for (const QString& runsPath : {caseDir + "/results/runs", caseDir + "/runs"}) {
        QDir runsDir(runsPath);
        if (!runsDir.exists()) continue;
        for (const QFileInfo& fi : runsDir.entryInfoList({"run_*"}, QDir::Dirs, QDir::Name)) {
            RunInfo info = readRunInfo(fi.filePath());
            if (info.index > 0) result.append(info);
        }
        if (!result.isEmpty()) break;  // found runs in this location, don't double-count
    }
    return result;
}

RunInfo RunLogger::readRunInfo(const QString& runDir)
{
    RunInfo info;
    info.path = runDir;

    QFile mf(runDir + "/metadata.json");
    if (!mf.open(QIODevice::ReadOnly)) return info;
    QJsonObject meta = QJsonDocument::fromJson(mf.readAll()).object();

    info.index      = meta["index"].toInt();
    info.solver     = meta["solver"].toString();
    info.divScheme  = meta["div_scheme"].toString();
    info.simType    = meta["sim_type"].toString();
    info.turbModel  = meta["turb_model"].toString();
    info.deltaModel = meta["delta_model"].toString();
    info.nu         = meta["nu"].toString();
    info.inletU     = meta["inlet_U"].toString();
    info.endTime    = meta["end_time"].toString();
    info.nCores     = meta["n_cores"].toString();
    info.timestamp  = meta["timestamp"].toString();
    info.converged  = meta["converged"].toBool();
    info.nSteps     = meta["n_steps"].toInt();
    info.finalTime  = meta["final_time"].toDouble();

    // Flow config: prefer explicit file; fall back to reconstructed case path
    QFile cf(runDir + "/flow_config.json");
    if (cf.open(QIODevice::ReadOnly)) {
        QJsonObject cfg = QJsonDocument::fromJson(cf.readAll()).object();
        info.flowDir = cfg["case_dir"].toString();
        for (const QJsonValue& v : cfg["time_steps"].toArray())
            info.timeSteps << v.toString();
        if (info.divScheme.isEmpty())
            info.divScheme = cfg["div_scheme"].toString();
    } else {
        // Old run (pre flow_config.json): reconstruct case dir from path
        // runDir = <case>/results/runs/run_XXX  → go up 3 levels
        QString reconstructed = QFileInfo(runDir).dir()   // runs/
                                    .absolutePath();
        reconstructed = QFileInfo(reconstructed).dir()    // results/
                            .absolutePath();
        reconstructed = QFileInfo(reconstructed).dir()    // case/
                            .absolutePath();
        if (QDir(reconstructed).exists()) {
            info.flowDir = reconstructed;
            static const QRegularExpression reNum(R"(^\d+\.?\d*$)");
            for (const QFileInfo& fi : QDir(reconstructed)
                     .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (reNum.match(fi.fileName()).hasMatch() && fi.fileName() != "0")
                    info.timeSteps << fi.fileName();
            }
        }
    }

    QJsonObject res = meta["final_residuals"].toObject();
    for (const QString& k : res.keys())
        info.finalResiduals[k] = res[k].toDouble();

    if (meta.contains("forces_latest")) {
        QJsonObject f = meta["forces_latest"].toObject();
        info.latestFx  = f["Fx"].toDouble();
        info.latestFy  = f["Fy"].toDouble();
        info.latestFz  = f["Fz"].toDouble();
        info.hasForces = true;
    }
    for (const QJsonValue& v : meta["cited_keys"].toArray())
        info.citedKeys << v.toString();
    return info;
}

bool RunLogger::generateReport(const RunInfo& info, const QString& outputPath)
{
    QFile f(outputPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);
    o.setCodec(QTextCodec::codecForName("UTF-8"));

    // header
    o << "# Cake Studio - Simulation Report\n\n";
    o << "| | |\n|---|---|\n";
    o << "| **Run** | " << info.index << " |\n";
    o << "| **Case** | " << QFileInfo(info.path).dir().dirName() << " |\n";
    o << "| **Date** | " << info.timestamp << " |\n";
    o << "| **Solver** | " << (info.solver.isEmpty() ? "(not detected)" : info.solver) << " |\n\n";

    // inputs
    QString st = info.simType.isEmpty() ? "laminar" : info.simType;
    QString tm = info.turbModel.isEmpty() ? "(not specified)" : info.turbModel;
    o << "## Simulation Inputs\n\n";
    o << "| Parameter | Value |\n|---|---|\n";
    o << "| Simulation type | " << st << " |\n";
    if (st == "LES") {
        o << "| SGS model | " << tm << " |\n";
        if (!info.deltaModel.isEmpty())
            o << "| Delta (filter-width) model | " << info.deltaModel << " |\n";
    } else if (st == "RAS") {
        o << "| Turbulence model | " << tm << " |\n";
    } else {
        o << "| Turbulence | laminar (no model) |\n";
    }
    o << "| Inlet velocity U | " << (info.inletU.isEmpty()  ? "(not specified)" : info.inletU  + " m/s")   << " |\n";
    o << "| Kinematic viscosity nu | " << (info.nu.isEmpty() ? "(not specified)" : info.nu     + " m^2/s") << " |\n";
    if (!info.endTime.isEmpty()) o << "| End time / max iterations | " << info.endTime << " |\n";
    if (!info.nCores.isEmpty())  o << "| CPU cores | " << info.nCores << " |\n";
    o << "\n";

    // mesh stats
    QString caseDir = QFileInfo(info.path).dir().absolutePath();
    QMap<QString,QString> meshStats;
    for (const QString& name : {"log.checkMesh", "checkMesh.log"}) {
        QFile mf(caseDir + "/" + name);
        if (!mf.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString txt = mf.readAll();
        QRegularExpression reCells(R"(\bcells:\s+(\d+))");   auto m = reCells.match(txt);
        if (m.hasMatch()) meshStats["cells"] = m.captured(1);
        QRegularExpression reNO(R"(non-orthogonality Max:\s+([\d.]+))"); m = reNO.match(txt);
        if (m.hasMatch()) meshStats["max_non_ortho"] = m.captured(1);
        QRegularExpression reSk(R"([Mm]ax skewness\s*=\s*([\d.]+))"); m = reSk.match(txt);
        if (m.hasMatch()) meshStats["max_skewness"] = m.captured(1);
        QRegularExpression reBb(R"(Overall domain bounding box\s*\(([^)]+)\)\s*\(([^)]+)\))");
        m = reBb.match(txt);
        if (m.hasMatch()) meshStats["bbox"] = "(" + m.captured(1) + ") (" + m.captured(2) + ")";
        meshStats["ok"] = txt.contains("Mesh OK") ? "true" : "false";
        break;
    }
    if (!meshStats.isEmpty()) {
        o << "## Mesh\n\n";
        o << "| Metric | Value |\n|---|---|\n";
        if (meshStats.contains("cells"))
            o << "| Total cells | " << meshStats["cells"] << " |\n";
        if (meshStats.contains("max_non_ortho"))
            o << "| Max non-orthogonality | " << meshStats["max_non_ortho"] << " deg |\n";
        if (meshStats.contains("max_skewness"))
            o << "| Max skewness | " << meshStats["max_skewness"] << " |\n";
        if (meshStats.contains("bbox"))
            o << "| Domain bounding box | " << meshStats["bbox"] << " |\n";
        o << "| checkMesh | " << (meshStats.value("ok") == "true" ? "OK" : "FAILED") << " |\n\n";
    }

    // convergence
    o << "## Convergence\n\n";
    if (info.finalResiduals.isEmpty()) {
        o << "_No residual data._\n\n";
    } else {
        o << "| Total steps | " << info.nSteps << " |\n";
        o << "|---|---|\n";
        o << "| Final time | " << info.finalTime << " s |\n";
        o << "| Status | " << (info.converged ? "**Converged**" : "**Did not converge**") << " |\n\n";
        o << "| Field | Final residual |\n|---|---|\n";
        for (auto it = info.finalResiduals.begin(); it != info.finalResiduals.end(); ++it)
            o << "| " << it.key() << " | " << QString::number(it.value(), 'e', 4) << " |\n";
        o << "\n";
    }

    // forces
    if (info.hasForces) {
        o << "## Aerodynamic Forces (final timestep)\n\n";
        o << "| Component | Fx [N] | Fy [N] | Fz [N] |\n|---|---|---|---|\n";
        o << "| Force | " << info.latestFx << " | " << info.latestFy << " | " << info.latestFz << " |\n\n";
        o << "> Cd = Fx / (0.5 * rho * |U|^2 * A_ref)  -- supply rho and A_ref to compute.\n\n";
    }

    // citations
    o << "## References\n\n";

    CitationManager& cm = CitationManager::instance();

    // Build ordered list: OpenFOAM → TENO5 → turbulence model → dataset citations
    QStringList citeKeys;
    auto addKey = [&](const QString& k) {
        if (!k.isEmpty() && !citeKeys.contains(k)) citeKeys << k;
    };
    addKey("openfoam");
    addKey("teno5");
    addKey(CitationManager::turbModelKey(info.turbModel));
    for (const QString& k : info.citedKeys) addKey(k);

    int n = 0;
    for (const QString& key : citeKeys) {
        QString ref = cm.markdownRef(key);
        if (!ref.isEmpty())
            o << ++n << ". " << ref << "\n";
    }
    if (n == 0) o << "_No citation data available._\n";

    o << "\n---\n*Generated by Cake Studio*\n";
    return true;
}

// Private helpers

int RunLogger::nextIndex(const QString& caseDir)
{
    int maxIdx = 0;
    for (const QString& runsPath : {caseDir + "/results/runs", caseDir + "/runs"}) {
        QDir runsDir(runsPath);
        if (!runsDir.exists()) continue;
        for (const QFileInfo& fi : runsDir.entryInfoList({"run_*"}, QDir::Dirs)) {
            int idx = fi.fileName().mid(4).toInt();
            if (idx > maxIdx) maxIdx = idx;
        }
    }
    return maxIdx + 1;
}

void RunLogger::copyInputs(const QString& caseDir, const QString& inputsDir)
{
    const QStringList toCopy = {
        "/system/fvSchemes",
        "/system/fvSolution",
        "/system/controlDict",
        "/constant/momentumTransport",
        "/constant/turbulenceProperties",
        "/constant/transportProperties",
    };
    for (const QString& rel : toCopy) {
        QString src = caseDir + rel;
        if (QFile::exists(src))
            QFile::copy(src, inputsDir + "/" + QFileInfo(rel).fileName());
    }
    // Inlet BC
    if (QFile::exists(caseDir + "/0/U"))
        QFile::copy(caseDir + "/0/U", inputsDir + "/U");
}

QString RunLogger::detectSolver(const QString& caseDir)
{
    // Ordered by frequency: first match wins
    static const QStringList knownSolvers = {
        // Incompressible steady/transient
        "simpleFoam", "pimpleFoam", "pisoFoam", "icoFoam",
        "SRFSimpleFoam", "SRFPimpleFoam", "nonNewtonianIcoFoam",
        "boundaryFoam", "shallowWaterFoam",
        // Compressible
        "rhoSimpleFoam", "rhoPimpleFoam", "rhoCentralFoam",
        "sonicFoam", "transonicFoam", "rhoFoam",
        // Buoyancy / heat transfer
        "buoyantSimpleFoam", "buoyantPimpleFoam",
        "buoyantBoussinesqSimpleFoam", "buoyantBoussinesqPimpleFoam",
        // Multiphase
        "interFoam", "interIsoFoam", "interMixingFoam",
        "multiphaseInterFoam", "twoPhaseEulerFoam", "reactingMultiphaseEulerFoam",
        // Combustion / reacting
        "reactingFoam", "fireFoam", "XiFoam",
        // Lagrangian / particle
        "coalChemistryFoam", "sprayFoam",
        // Potential / Laplacian
        "potentialFoam", "laplacianFoam",
        // Electromagnetics
        "magneticFoam",
    };
    for (const QString& s : knownSolvers) {
        if (QFile::exists(caseDir + "/log." + s)) return s;
    }
    // Fallback: pick any log.* file
    for (const QFileInfo& fi : QDir(caseDir).entryInfoList({"log.*"}, QDir::Files)) {
        int dot = fi.fileName().indexOf('.');
        if (dot >= 0) return fi.fileName().mid(dot + 1);
    }
    return {};
}

QString RunLogger::readDictValue(const QString& filePath, const QString& key)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString txt = f.readAll();
    QRegularExpression re(key + R"(\s+([^\s;]+)\s*;)");
    auto m = re.match(txt);
    return m.hasMatch() ? m.captured(1) : QString{};
}

QString RunLogger::readDivScheme(const QString& fvSchemesPath)
{
    QFile f(fvSchemesPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString txt = f.readAll();
    // Match the div(phi,U) line: capture everything up to the semicolon
    QRegularExpression re(R"(div\(\s*phi\s*,\s*[Uu]\s*\)\s+([^;]+);)");
    auto m = re.match(txt);
    if (!m.hasMatch()) return {};
    QString entry = m.captured(1).toLower().trimmed();
    if (entry.contains("teno6"))       return "teno6";
    if (entry.contains("teno5"))       return "teno5";
    if (entry.contains("linearupwind")) return "linearUpwind";
    if (entry.contains("linear"))      return "linear";
    if (entry.contains("upwind"))      return "upwind";
    return entry.section(' ', -1);  // last token as fallback
}

QList<RunLogger::ResidualRow> RunLogger::parseResiduals(const QString& logPath)
{
    if (logPath.isEmpty() || !QFile::exists(logPath)) return {};
    QFile f(logPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};

    QList<ResidualRow> rows;
    ResidualRow current; current.step = 0; current.time = 0;
    bool inStep = false;

    static const QRegularExpression reTime(R"(^Time = ([\d.eE+\-]+))");
    static const QRegularExpression reRes(R"(Solving for (\w+),\s+Initial residual = ([\d.eE+\-]+))");

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine();
        auto mTime = reTime.match(line);
        if (mTime.hasMatch()) {
            if (inStep && !current.initResiduals.isEmpty()) rows.append(current);
            current.step++;
            current.time = mTime.captured(1).toDouble();
            current.initResiduals.clear();
            inStep = true;
            continue;
        }
        auto mRes = reRes.match(line);
        if (mRes.hasMatch() && inStep) {
            QString field = mRes.captured(1);
            if (!current.initResiduals.contains(field))
                current.initResiduals[field] = mRes.captured(2).toDouble();
        }
    }
    if (inStep && !current.initResiduals.isEmpty()) rows.append(current);
    return rows;
}

// OF 2412 force.dat/moment.dat: col[0]=time, col[1-3]=total, col[4-6]=pressure, col[7-9]=viscous
static QStringList splitForceLine(const QString& raw)
{
    QString clean = raw;
    clean.replace('(', ' ').replace(')', ' ');
    return clean.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
}

QList<RunLogger::ForceRow> RunLogger::parseForces(const QString& caseDir)
{
    QDir ppDir(caseDir + "/postProcessing/forces");
    if (!ppDir.exists()) return {};

    QString timeDir;
    for (const QFileInfo& td : ppDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        timeDir = td.filePath(); break;
    }
    if (timeDir.isEmpty()) return {};

    QString forcePath = timeDir + "/forces.dat";
    if (!QFile::exists(forcePath)) forcePath = timeDir + "/force.dat";
    if (!QFile::exists(forcePath)) return {};

    QString momentPath = timeDir + "/moments.dat";
    if (!QFile::exists(momentPath)) momentPath = timeDir + "/moment.dat";
    bool hasMoments = QFile::exists(momentPath);

    QMap<double, ForceRow> byTime;
    {
        QFile f(forcePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith('#') || line.isEmpty()) continue;
            QStringList p = splitForceLine(line);
            if (p.size() < 4) continue;
            bool ok; ForceRow r;
            r.time = p[0].toDouble(&ok); if (!ok) continue;
            // col[1-3] = total force
            r.fx = p[1].toDouble(); r.fy = p[2].toDouble(); r.fz = p[3].toDouble();
            byTime[r.time] = r;
        }
    }

    if (hasMoments) {
        QFile mf(momentPath);
        if (mf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&mf);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith('#') || line.isEmpty()) continue;
                QStringList p = splitForceLine(line);
                if (p.size() < 4) continue;
                bool ok; double t = p[0].toDouble(&ok); if (!ok) continue;
                if (!byTime.contains(t)) continue;
                // col[1-3] = total moment
                byTime[t].mx = p[1].toDouble(); byTime[t].my = p[2].toDouble(); byTime[t].mz = p[3].toDouble();
            }
        }
    }

    return byTime.values();
}

bool RunLogger::copyDir(const QString& src, const QString& dst)
{
    QDir srcDir(src);
    if (!srcDir.exists()) return false;
    QDir().mkpath(dst);
    for (const QFileInfo& fi : srcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (!QFile::copy(fi.filePath(), dst + "/" + fi.fileName())) return false;
    }
    for (const QFileInfo& fi : srcDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!copyDir(fi.filePath(), dst + "/" + fi.fileName())) return false;
    }
    return true;
}
