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

#include "RunConfig.h"
#include "CADGeometry.h"
#include "CaseGenerator.h"

#include <QCoreApplication>
#include <QString>

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <chrono>
#include <sstream>

// tiny JSON helpers
static std::string jstr(const std::string& s) {
    return "\"" + s + "\"";
}
static std::string jkv(const std::string& k, const std::string& v, bool last=false){
    return "  " + jstr(k) + ": " + jstr(v) + (last?"":",");
}
static std::string jkvn(const std::string& k, double v, bool last=false){
    std::ostringstream ss; ss << v;
    return "  " + jstr(k) + ": " + ss.str() + (last?"":",");
}

static long ms() {
    static auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
}

static void log(const std::string& msg, bool jsonMode) {
    if (!jsonMode) std::cout << msg << "\n" << std::flush;
}

// Wrap s in single quotes for bash; an embedded ' becomes '\''
static std::string shQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

static int shell(const std::string& cmd, bool jsonMode) {
    log("  $ " + cmd, jsonMode);
    int r = std::system(("bash -c " + shQuote(cmd)).c_str());
    log(r == 0 ? "  [OK]" : "  [FAILED]", jsonMode);
    return r;
}

static CFD::SolverType parseSolver(const std::string& s) {
    if (s == "icoFoam")       return CFD::SolverType::IcoFoam;
    if (s == "pimpleFoam")    return CFD::SolverType::PimpleFoam;
    if (s == "rhoSimpleFoam") return CFD::SolverType::RhoSimpleFoam;
    return CFD::SolverType::SimpleFoam;
}

static CFD::TurbulenceModel parseTurb(const std::string& s) {
    if (s == "laminar")          return CFD::TurbulenceModel::Laminar;
    if (s == "SpalartAllmaras")  return CFD::TurbulenceModel::SpalartAllmaras;
    if (s == "kEpsilon")         return CFD::TurbulenceModel::KEpsilon;
    return CFD::TurbulenceModel::KOmegaSST;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    bool jsonMode = false;
    bool printHelp = false;
    std::string configPath;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--json")   { jsonMode = true; continue; }
        if (a == "--help" || a == "-h") { printHelp = true; break; }
        if (!a.empty() && a[0] != '-') { configPath = a; }
    }

    if (printHelp || configPath.empty()) {
        std::cout <<
            "Cake CFD Studio CLI\n"
            "Usage: cfdstudio <config.yaml> [--json]\n\n"
            "Sections in config.yaml:\n"
            "  geometry:  file\n"
            "  case:      name, output_dir\n"
            "  solver:    type, turbulence, inflow_velocity, nu\n"
            "  mesh:      padding, background_cells, max_global_cells,\n"
            "             max_local_cells, surface_refinement_min/max,\n"
            "             resolve_feature_angle, boundary_layers,\n"
            "             layer_expansion_ratio, first_layer_thickness, min_thickness\n"
            "  run:       mesh (bool), solver (bool), cores\n\n"
            "Solver types:     icoFoam simpleFoam pimpleFoam rhoSimpleFoam\n"
            "Turbulence types: laminar SpalartAllmaras kOmegaSST kEpsilon\n";
        return 0;
    }

    // Load config
    CFD::CLI::RunConfig cfg;
    try { cfg = CFD::CLI::loadYAML(configPath); }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    log("[cfdstudio] Loaded: " + configPath, jsonMode);

    // Load geometry
    log("[" + std::to_string(ms()) + "ms] Loading geometry: " + cfg.geometryFile, jsonMode);
    auto geo = CFD::CADGeometry::fromFile(QString::fromStdString(cfg.geometryFile));
    if (!geo->isValid()) {
        std::string err = geo->importResult().errorMsg.toStdString();
        if (jsonMode)
            std::cout << "{\n" << jkv("status","error") << "\n"
                      << jkv("error", err, true) << "\n}\n";
        else std::cerr << "Geometry error: " << err << "\n";
        return 1;
    }

    auto bounds = geo->bounds();
    log("[" + std::to_string(ms()) + "ms] Geometry loaded: "
        + std::to_string(geo->importResult().numSolids) + " solid(s), "
        + std::to_string(geo->polyData()->GetNumberOfPoints()) + " VTK points", jsonMode);

    // Build CaseParams
    CFD::CaseParams cp;
    cp.caseName        = QString::fromStdString(cfg.caseName);
    cp.caseDir         = QString::fromStdString(cfg.outputDir + "/" + cfg.caseName);
    cp.solver          = parseSolver(cfg.solver);
    cp.turbulence      = parseTurb(cfg.turbulence);
    cp.inflowVelocity  = cfg.inflowVelocity;
    cp.nu              = cfg.nu;
    cp.meshPadding     = cfg.meshPadding;
    cp.bgMeshCells     = cfg.bgMeshCells;
    cp.nBoundaryLayers = cfg.nBoundaryLayers;
    // BlockMeshWriter puts the inlet on X-max, so the flow runs in -X
    cp.flowAxis        = "-X";

    // snappyHexMesh controls from the mesh section
    cp.maxGlobalCells       = cfg.maxGlobalCells;
    cp.maxLocalCells        = cfg.maxLocalCells;
    cp.surfaceRefinementMin = cfg.surfaceRefinementMin;
    cp.surfaceRefinementMax = cfg.surfaceRefinementMax;
    cp.resolveFeatureAngle  = cfg.resolveFeatureAngle;
    cp.layerExpansionRatio  = cfg.layerExpansionRatio;
    cp.firstLayerThickness  = cfg.firstLayerThickness;
    cp.minLayerThickness    = cfg.minThickness;

    // Generate case
    log("[" + std::to_string(ms()) + "ms] Generating OpenFOAM case...", jsonMode);
    auto result = CFD::CaseGenerator::generate(*geo, cp);
    if (!result.success) {
        std::string err = result.errorMsg.toStdString();
        if (jsonMode)
            std::cout << "{\n" << jkv("status","error") << "\n"
                      << jkv("error", err, true) << "\n}\n";
        else std::cerr << "Case generation error: " << err << "\n";
        return 1;
    }

    std::string caseDir = result.caseDir.toStdString();
    log("[" + std::to_string(ms()) + "ms] Case written: " + caseDir, jsonMode);

    // Run OF pipeline
    int meshRC = 0;
    int solverRC = 0;
    if (cfg.runMesh) {
        std::string of  = "source /usr/lib/openfoam/openfoam2412/etc/bashrc 2>/dev/null";
        std::string cdc = "cd " + shQuote(caseDir);
        std::string base = of + " && " + cdc + " && ";

        log("[" + std::to_string(ms()) + "ms] surfaceFeatureExtract...", jsonMode);
        if (shell(base + "surfaceFeatureExtract > log.featureExtract 2>&1", jsonMode) != 0)
            { meshRC = 1; goto done; }

        log("[" + std::to_string(ms()) + "ms] blockMesh...", jsonMode);
        if (shell(base + "blockMesh > log.blockMesh 2>&1", jsonMode) != 0)
            { meshRC = 1; goto done; }

        log("[" + std::to_string(ms()) + "ms] snappyHexMesh...", jsonMode);
        if (shell(base + "snappyHexMesh -overwrite > log.snappyHexMesh 2>&1", jsonMode) != 0)
            { meshRC = 1; goto done; }

        log("[" + std::to_string(ms()) + "ms] checkMesh...", jsonMode);
        shell(base + "checkMesh > log.checkMesh 2>&1", jsonMode);
    }

    if (cfg.runSolver && meshRC == 0) {
        std::string of  = "source /usr/lib/openfoam/openfoam2412/etc/bashrc 2>/dev/null";
        std::string cdc = "cd " + shQuote(caseDir);
        std::string base = of + " && " + cdc + " && ";
        std::string solver = cfg.solver;
        log("[" + std::to_string(ms()) + "ms] Running solver: " + solver, jsonMode);
        if (shell(base + solver + " > log." + solver + " 2>&1", jsonMode) != 0) {
            solverRC = 2;
            if (!jsonMode)
                std::cerr << "Solver " << solver << " failed; see "
                          << caseDir << "/log." << solver << "\n";
        }
    }

done:
    // Output
    if (jsonMode) {
        std::cout << "{\n"
            << jkv("status", meshRC != 0   ? "mesh_failed"
                           : solverRC != 0 ? "solver_failed" : "ok") << "\n"
            << jkv("case_dir", caseDir) << "\n"
            << jkvn("bounds_xmin", bounds[0]) << "\n"
            << jkvn("bounds_xmax", bounds[1]) << "\n"
            << jkvn("bounds_ymin", bounds[2]) << "\n"
            << jkvn("bounds_ymax", bounds[3]) << "\n"
            << jkvn("bounds_zmin", bounds[4]) << "\n"
            << jkvn("bounds_zmax", bounds[5]) << "\n"
            << jkvn("volume", geo->importResult().volume) << "\n"
            << jkvn("vtk_points",
                    (double)geo->polyData()->GetNumberOfPoints()) << "\n"
            << jkvn("elapsed_ms", (double)ms(), true) << "\n"
            << "}\n";
    } else {
        std::cout << "\n=== Done in " << ms() << "ms ===\n";
        std::cout << "Case: " << caseDir << "\n";
    }

    return meshRC != 0 ? meshRC : solverRC;
}
