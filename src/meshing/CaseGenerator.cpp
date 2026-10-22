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

#include "CaseGenerator.h"
#include "SurfaceMeshWriter.h"
#include "BlockMeshWriter.h"
#include "SnappyConfigWriter.h"
#include "../geometry/CADGeometry.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace CFD {

QString CaseGenerator::solverName(SolverType s) {
    switch (s) {
    case SolverType::IcoFoam:      return "icoFoam";
    case SolverType::SimpleFoam:   return "simpleFoam";
    case SolverType::PimpleFoam:   return "pimpleFoam";
    case SolverType::RhoSimpleFoam:return "rhoSimpleFoam";
    default: return "simpleFoam";
    }
}

QString CaseGenerator::turbModelName(TurbulenceModel t) {
    switch (t) {
    case TurbulenceModel::Laminar:            return "laminar";
    case TurbulenceModel::SpalartAllmaras:    return "SpalartAllmaras";
    case TurbulenceModel::KOmegaSST:          return "kOmegaSST";
    case TurbulenceModel::KEpsilon:           return "kEpsilon";
    case TurbulenceModel::Smagorinsky:        return "Smagorinsky";
    case TurbulenceModel::DynamicKEqn:        return "dynamicKEqn";
    case TurbulenceModel::WALE:               return "WALE";
    case TurbulenceModel::DynamicLagrangian:  return "dynamicLagrangian";
    default: return "kOmegaSST";
    }
}

CaseGenerator::Result CaseGenerator::generate(
    const CADGeometry& geo, const CaseParams& params)
{
    Result res;
    if (!geo.isValid()) {
        res.errorMsg = "Invalid geometry";
        return res;
    }

    QDir root(params.caseDir);
    if (!root.mkpath(".")) {
        res.errorMsg = "Cannot create case directory: " + params.caseDir;
        return res;
    }

    // Remove numeric time directories (solver output) so they don't bleed into
    // the new run. Leave everything else the user may have put there untouched.
    for (const QString& entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        entry.toDouble(&ok);
        if (ok) QDir(root.filePath(entry)).removeRecursively();
    }

    QString systemDir = root.filePath("system");
    QString constDir  = root.filePath("constant");
    QString dir0      = root.filePath("0");
    root.mkpath(systemDir);
    root.mkpath(constDir);
    root.mkpath(dir0);

    // 1. Write surface STL
    QString stlPath = SurfaceMeshWriter::write(geo, params.caseDir, params.caseName);
    if (stlPath.isEmpty()) {
        res.errorMsg = "Failed to write surface STL";
        return res;
    }

    // 2. blockMeshDict auto-sized from the geometry bbox plus padding
    auto bounds = geo.bounds();
    auto bmParams = BlockMeshWriter::defaultParams(bounds, params.meshPadding,
                                                    params.bgMeshCells);
    if (!BlockMeshWriter::write(params.caseDir, bmParams)) {
        res.errorMsg = "Failed to write blockMeshDict";
        return res;
    }

    // 3. snappyHexMeshDict + surfaceFeatureExtractDict
    SnappyParams snp;
    snp.surfaceName          = params.caseName;
    snp.nSurfaceLayers       = params.nBoundaryLayers;
    snp.maxGlobalCells       = params.maxGlobalCells;
    snp.maxLocalCells        = params.maxLocalCells;
    snp.surfaceRefinementMin = params.surfaceRefinementMin;
    snp.surfaceRefinementMax = params.surfaceRefinementMax;
    snp.resolveFeatureAngle  = params.resolveFeatureAngle;
    snp.layerExpansionRatio  = params.layerExpansionRatio;
    snp.finalLayerThickness  = params.firstLayerThickness;
    snp.minThickness         = params.minLayerThickness;
    // locationInMesh is the midpoint between the inlet wall and the geometry front face.
    // -X flow puts the inlet on X-max, so seed on that side (matches BlockMeshWriter).
    // Y/Z are nudged off the domain centre by a fraction of a cell because with an even
    // cell count the exact centre lies on a cell face and snappy rejects it.
    const auto& bm = bmParams;
    const double cellY = (bm.bounds[3] - bm.bounds[2]) / bm.cellsY;
    const double cellZ = (bm.bounds[5] - bm.bounds[4]) / bm.cellsZ;
    snp.locationInMesh = {
        (bm.bounds[1] + bounds[1]) * 0.5,  // halfway between inlet and geometry X-max
        (bm.bounds[2] + bm.bounds[3]) * 0.5 + 0.137 * cellY,
        (bm.bounds[4] + bm.bounds[5]) * 0.5 + 0.137 * cellZ
    };
    SnappyConfigWriter::writeFeatureExtract(params.caseDir, params.caseName);
    if (!SnappyConfigWriter::write(params.caseDir, snp)) {
        res.errorMsg = "Failed to write snappyHexMeshDict";
        return res;
    }

    // 4. fvSchemes / fvSolution / controlDict
    bool les  = isLES(params.turbulence);
    bool comp = isCompressible(params.solver);
    if (!writeFvSchemes(systemDir, les, comp, params.divScheme, isTransient(params.solver))
     || !writeFvSolution(systemDir, params.solver)
     || !writeControlDict(systemDir, params.solver, params.caseName, params.divScheme)
     || !writeDecomposeParDict(systemDir)) {
        res.errorMsg = "Failed to write system dictionaries";
        return res;
    }

    // 5. Boundary conditions (0/)
    if (!writeBoundaryConditions(dir0, params)) {
        res.errorMsg = "Failed to write boundary conditions";
        return res;
    }

    // 6. Physical properties
    bool propOk = comp
        ? writeThermophysicalProperties(constDir)
        : writeTransportProperties(constDir, params.nu);
    if (!propOk || !writeTurbulenceProperties(constDir, params.turbulence)) {
        res.errorMsg = "Failed to write physical properties";
        return res;
    }

    // 7. Generate shell run script
    // Single-quote the case dir so spaces and shell metacharacters survive;
    // an embedded ' is closed, escaped and reopened.
    QString quotedDir = params.caseDir;
    quotedDir.replace("'", "'\\''");
    QString runScript =
        "#!/bin/bash\n"
        "source /usr/lib/openfoam/openfoam2412/etc/bashrc\n"
        "cd '" + quotedDir + "' || exit 1\n"
        "surfaceFeatureExtract\n"
        "blockMesh\n"
        "snappyHexMesh -overwrite\n"
        + solverName(params.solver) + "\n";

    QFile script(root.filePath("Allrun"));
    if (script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(&script) << runScript;
        script.setPermissions(
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
            QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }

    res.success  = true;
    res.caseDir  = params.caseDir;
    res.runScript = runScript;
    return res;
}

bool CaseGenerator::writeFvSchemes(const QString& systemDir, bool les, bool compressible,
                                    const QString& divScheme, bool transient)
{
    QFile f(systemDir + "/fvSchemes");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);

    static const char header[] =
        "FoamFile\n{ version 2.0; format ascii; class dictionary; object fvSchemes; }\n\n";

    // Build the div(phi,U) token from the scheme name
    QString divUScheme;
    if (divScheme == "teno6")
        divUScheme = "Gauss teno6";
    else if (divScheme == "teno" || divScheme == "teno5")
        divUScheme = "Gauss teno";
    else
        divUScheme = "Gauss linearUpwind grad(U)";

    const char* ddt = transient ? "Euler" : "steadyState";

    if (compressible) {
        // For compressible, secondary scalar transports always use linearUpwind
        o << header
          << "ddtSchemes      { default " << ddt << "; }\n\n"
          << "gradSchemes     { default Gauss linear; }\n\n"
          << "divSchemes\n{\n"
          << "    default                               none;\n"
          << "    div(phi,U)                            " << divUScheme << ";\n"
          << "    div(phi,h)                            Gauss linearUpwind grad(h);\n"
          << "    div(phi,K)                            Gauss linearUpwind grad(K);\n"
          << "    div(phiv,p)                           Gauss linearUpwind grad(p);\n"
          << "    turbulence                            bounded Gauss upwind;\n"
          << "    div(phi,k)                            $turbulence;\n"
          << "    div(phi,omega)                        $turbulence;\n"
          << "    div(phi,epsilon)                      $turbulence;\n"
          << "    div(phi,nuTilda)                      $turbulence;\n"
          << "    div(((rho*nuEff)*dev2(T(grad(U)))))   Gauss linear;\n"
          << "}\n\n"
          << "laplacianSchemes  { default Gauss linear corrected; }\n\n"
          << "interpolationSchemes { default linear; }\n\n"
          << "snGradSchemes     { default corrected; }\n\n"
          << "fluxRequired      { default no; p ; }\n\n"
          << "wallDist          { method meshWave; }\n";
    } else if (les) {
        // For LES, TENO replaces the momentum div while viscous stress terms stay linear
        QString lesDiv = (divScheme == "teno6" || divScheme == "teno" || divScheme == "teno5")
            ? divUScheme : "Gauss linear";
        o << header
          << "ddtSchemes      { default backward; }\n\n"
          << "gradSchemes     { default Gauss linear; }\n\n"
          << "divSchemes\n{\n"
          << "    default                               none;\n"
          << "    div(phi,U)                            " << lesDiv << ";\n"
          << "    div((nuEff*dev(T(grad(U)))))          Gauss linear;\n"
          << "    div(B)                                Gauss linear;\n"
          << "    div((nuEff*dev2(T(grad(U)))))         Gauss linear;\n"
          << "}\n\n"
          << "laplacianSchemes  { default Gauss linear corrected; }\n\n"
          << "interpolationSchemes { default linear; }\n\n"
          << "snGradSchemes     { default corrected; }\n\n"
          << "fluxRequired      { default no; p ; }\n\n"
          << "wallDist          { method meshWave; }\n";
    } else {
        o << header
          << "ddtSchemes      { default " << ddt << "; }\n\n"
          << "gradSchemes     { default Gauss linear; }\n\n"
          << "divSchemes\n{\n"
          << "    default          none;\n"
          << "    div(phi,U)       " << divUScheme << ";\n"
          << "    turbulence       bounded Gauss upwind;\n"
          << "    div(phi,k)       $turbulence;\n"
          << "    div(phi,omega)   $turbulence;\n"
          << "    div(phi,epsilon) $turbulence;\n"
          << "    div(phi,nuTilda) $turbulence;\n"
          << "    div((nuEff*dev2(T(grad(U))))) Gauss linear;\n"
          << "}\n\n"
          << "laplacianSchemes  { default Gauss linear corrected; }\n\n"
          << "interpolationSchemes { default linear; }\n\n"
          << "snGradSchemes     { default corrected; }\n\n"
          << "fluxRequired      { default no; p ; }\n\n"
          << "wallDist          { method meshWave; }\n";
    }
    return true;
}

bool CaseGenerator::writeFvSolution(const QString& systemDir, SolverType solver)
{
    QFile f(systemDir + "/fvSolution");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);
    // U uses smoothSolver + symGaussSeidel for steady SIMPLE, PBiCGStab + DILU for transient
    const char* uSolver = isTransient(solver)
        ? "solver PBiCGStab; preconditioner DILU;"
        : "solver smoothSolver; smoother symGaussSeidel;";
    o << "FoamFile\n{ version 2.0; format ascii; class dictionary;"
      << " object fvSolution; }\n\n"
      << "solvers\n{\n"
      << "    p       { solver GAMG; smoother GaussSeidel; tolerance 1e-7;"
      <<               " relTol 0.01; }\n"
      << "    pFinal  { $p; relTol 0; }\n"
      << "    U       { " << uSolver << " tolerance 1e-8;"
      <<               " relTol 0.1; }\n"
      << "    UFinal  { $U; relTol 0; }\n"
      << "    \"(k|omega|epsilon|nuTilda|kFinal|omegaFinal|epsilonFinal|nuTildaFinal)\""
              <<               " { solver smoothSolver; smoother GaussSeidel;"
              <<               " tolerance 1e-8; relTol 0.1; }\n";

    if (isCompressible(solver)) {
        o << "    h       { solver smoothSolver; smoother GaussSeidel; tolerance 1e-8;"
          <<               " relTol 0.1; }\n"
          << "    hFinal  { $h; relTol 0; }\n"
          << "    \"rho.*\" { solver diagonal; }\n";
    }
    o << "}\n\n";

    if (solver == SolverType::SimpleFoam) {
        o << "SIMPLE\n{\n"
          << "    nNonOrthogonalCorrectors 3;\n"
          << "    residualControl { p 1e-5; U 1e-5; }\n"
          << "}\n\n"
          << "relaxationFactors\n{\n"
          << "    fields  { p 0.3; }\n"
          << "    equations { U 0.5; k 0.5; omega 0.5; epsilon 0.5; }\n}\n";
    } else if (solver == SolverType::RhoSimpleFoam) {
        o << "SIMPLE\n{\n"
          << "    nNonOrthogonalCorrectors 1;\n"
          << "    consistent yes;\n"
          << "    residualControl { p 1e-4; U 1e-4; h 1e-4; }\n"
          << "}\n\n"
          << "relaxationFactors\n{\n"
          << "    fields  { p 0.3; rho 0.05; }\n"
          << "    equations { U 0.5; h 0.5; k 0.5; omega 0.5; epsilon 0.5; }\n}\n";
    } else if (solver == SolverType::IcoFoam) {
        // icoFoam reads a PISO dict
        o << "PISO\n{\n"
          << "    nCorrectors 2;\n"
          << "    nNonOrthogonalCorrectors 1;\n}\n";
    } else {
        o << "PIMPLE\n{\n"
          << "    nOuterCorrectors 2;\n"
          << "    nCorrectors 2;\n"
          << "    nNonOrthogonalCorrectors 1;\n}\n\n"
          // Transient runs skip under-relaxation (full update per step)
          << "relaxationFactors\n{\n"
          << "    equations { \".*\" 1; }\n}\n";
    }
    return true;
}

bool CaseGenerator::writeControlDict(
    const QString& systemDir, SolverType solver, const QString& caseName,
    const QString& divScheme)
{
    QFile f(systemDir + "/controlDict");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);
    bool steady = (solver == SolverType::SimpleFoam ||
                   solver == SolverType::RhoSimpleFoam);
    bool teno   = (divScheme == "teno" || divScheme == "teno5" || divScheme == "teno6");

    QString deltaT, endTime, writeControl, writeInterval, adjustTS, maxCo, maxDeltaT;
    if (steady) {
        deltaT = "1"; endTime = "1000"; adjustTS = "no"; maxCo = "0.5";
        maxDeltaT = "1"; writeControl = "timeStep"; writeInterval = "100";
    } else if (teno) {
        deltaT = "1e-5"; endTime = "0.5"; adjustTS = "yes"; maxCo = "0.4";
        maxDeltaT = "5e-5"; writeControl = "adjustableRunTime"; writeInterval = "0.05";
    } else {
        deltaT = "1e-4"; endTime = "0.5"; adjustTS = "yes"; maxCo = "0.5";
        maxDeltaT = "2e-4"; writeControl = "adjustableRunTime"; writeInterval = "0.05";
    }

    o << "FoamFile\n{ version 2.0; format ascii; class dictionary;"
      << " object controlDict; }\n\n"
      << "application     " << solverName(solver) << ";\n"
      << "startFrom       startTime;\n"
      << "startTime       0;\n"
      << "stopAt          endTime;\n"
      << "endTime         " << endTime << ";\n"
      << "deltaT          " << deltaT  << ";\n"
      << "adjustTimeStep  " << adjustTS << ";\n"
      << "maxCo           " << maxCo   << ";\n"
      << "maxDeltaT       " << maxDeltaT << ";\n"
      << "writeControl    " << writeControl  << ";\n"
      << "writeInterval   " << writeInterval << ";\n"
      << "purgeWrite      3;\n"
      << "writeFormat     ascii;\n"
      << "writePrecision  8;\n"
      << "runTimeModifiable true;\n\n";
    // libtenoScheme.so registers the teno and teno6 div schemes
    if (teno)
        o << "libs (\"libtenoScheme.so\");\n\n";
    o << "functions\n{\n"
      << "    forces\n    {\n"
      << "        type            forces;\n"
      << "        libs            (forces);\n"
      << "        rho             rhoInf;\n"
      << "        patches         (" << caseName << ");\n"
      << "        rhoInf          1.225;\n"
      << "        CofR            (0 0 0);\n"
      << "        writeControl    timeStep;\n"
      << "        writeInterval   10;\n"
      << "    }\n}\n";
    return true;
}

bool CaseGenerator::writeDecomposeParDict(const QString& systemDir, int nProcs)
{
    QFile f(systemDir + "/decomposeParDict");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);
    o << "FoamFile\n{ version 2.0; format ascii; class dictionary;"
      << " object decomposeParDict; }\n\n"
      << "numberOfSubdomains " << nProcs << ";\n"
      << "method scotch;\n";
    return true;
}

bool CaseGenerator::writeBoundaryConditions(
    const QString& dir0, const CaseParams& p)
{
    QDir d(dir0);
    bool sa   = (p.turbulence == TurbulenceModel::SpalartAllmaras);
    bool ke   = (p.turbulence == TurbulenceModel::KEpsilon);
    bool kw   = (p.turbulence == TurbulenceModel::KOmegaSST);
    bool les  = isLES(p.turbulence);
    bool lesk = (p.turbulence == TurbulenceModel::DynamicKEqn);
    bool comp = isCompressible(p.solver);

    // Use first VelocityInlet patch velocity when patchBCs is active
    double U = p.inflowVelocity;
    for (const auto& bc : p.patchBCs)
        if (bc.type == BCType::VelocityInlet) { U = bc.velocity; break; }

    const double _aoa  = p.aoaDeg * M_PI / 180.0;
    const double _sign = (p.flowAxis == "-X" || p.flowAxis == "-Y" || p.flowAxis == "-Z") ? -1.0 : 1.0;
    auto uVec = [&](double mag) -> QString {
        double u0 = _sign * mag * std::cos(_aoa); // along flow axis
        double u1 = mag * std::sin(_aoa);          // perpendicular (AoA lift component)
        if (p.flowAxis == "+X" || p.flowAxis == "-X")
            return QString("(%1 0 %2)").arg(u0, 0,'g',6).arg(u1, 0,'g',6);
        if (p.flowAxis == "+Y" || p.flowAxis == "-Y")
            return QString("(%2 %1 0)").arg(u0, 0,'g',6).arg(u1, 0,'g',6);
        // +Z / -Z
        return QString("(0 %2 %1)").arg(u0, 0,'g',6).arg(u1, 0,'g',6);
    };
    double I = 0.05;
    // Floor k so a zero inflow velocity does not give k = omega = 0
    double k_val = std::max(1.5 * (U*I) * (U*I), 1e-10);
    // omega = k / (TVR * nu) with turbulent viscosity ratio TVR = 10
    double omega_val = std::max(k_val / (10.0 * std::max(p.nu, 1e-12)), 1e-6);
    double eps_val   = 0.09 * k_val * omega_val;
    double nut_val   = p.nu * 10.0;
    double nuTilda   = 5.0 * p.nu;

    // nutkWallFunction needs k, which Spalart-Allmaras does not solve
    const QString nutWall = sa
        ? "type nutUSpaldingWallFunction; value uniform 0;"
        : "type nutkWallFunction; value uniform 0;";
    const bool dynLag = (p.turbulence == TurbulenceModel::DynamicLagrangian);

    // Per-patch BC mode (GUI table)
    if (!p.patchBCs.isEmpty()) {

        // Write one OpenFOAM field file with the per-patch boundary field.
        // entryFor(bc) returns the content between the curly braces for that patch.
        auto writeF = [&](const QString& fname,
                           const char* foamClass,
                           const char* dims,
                           const QString& internalVal,
                           auto entryFor) -> bool {
            QFile f(d.filePath(fname));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
            QTextStream o(&f);
            o << "FoamFile\n{ version 2.0; format ascii; class " << foamClass
              << "; object " << fname << "; }\n\n"
              << "dimensions " << dims << ";\n"
              << "internalField " << internalVal << ";\n"
              << "boundaryField\n{\n";
            for (const auto& bc : p.patchBCs)
                o << "    " << bc.name << " { " << entryFor(bc) << " }\n";
            o << "}\n";
            return true;
        };

        auto symOrZero = [](const PatchBC& bc) -> QString {
            return (bc.type == BCType::Symmetry) ? "type symmetry;" : "type zeroGradient;";
        };

        // U
        if (!writeF("U", "volVectorField", "[0 1 -1 0 0 0 0]",
                "uniform " + uVec(U),
                [&](const PatchBC& bc) -> QString {
                    switch (bc.type) {
                    case BCType::VelocityInlet:
                        return QString("type fixedValue; value uniform %1;").arg(uVec(bc.velocity));
                    case BCType::PressureOutlet: return "type zeroGradient;";
                    case BCType::Wall:           return "type noSlip;";
                    case BCType::Symmetry:       return "type symmetry;";
                    case BCType::Slip:           return "type slip;";
                    default:                     return "type zeroGradient;";
                    }
                })) return false;

        // p
        if (!writeF("p",
                comp ? "volScalarField" : "volScalarField",
                comp ? "[1 -1 -2 0 0 0 0]" : "[0 2 -2 0 0 0 0]",
                comp ? "uniform 101325" : "uniform 0",
                [&](const PatchBC& bc) -> QString {
                    if (comp) {
                        // waveTransmissive (advective) needs a time derivative and
                        // aborts under steadyState, so steady solvers fix p at the outlet
                        const bool steady = !isTransient(p.solver);
                        switch (bc.type) {
                        case BCType::VelocityInlet:
                            return steady ? "type zeroGradient;"
                                          : "type fixedValue; value uniform 101325;";
                        case BCType::PressureOutlet:
                            return steady ? "type fixedValue; value uniform 101325;"
                                          : "type waveTransmissive; gamma 1.4; fieldInf 101325; lInf 1; value uniform 101325;";
                        case BCType::Symmetry: return "type symmetry;";
                        default: return "type zeroGradient;";
                        }
                    } else {
                        switch (bc.type) {
                        case BCType::VelocityInlet:  return "type zeroGradient;";
                        case BCType::PressureOutlet: return "type fixedValue; value uniform 0;";
                        case BCType::Symmetry:       return "type symmetry;";
                        default:                     return "type zeroGradient;";
                        }
                    }
                })) return false;

        // T (compressible only)
        if (comp && !writeF("T", "volScalarField", "[0 0 0 1 0 0 0]", "uniform 300",
                [&](const PatchBC& bc) -> QString {
                    switch (bc.type) {
                    case BCType::VelocityInlet: return "type fixedValue; value uniform 300;";
                    case BCType::Symmetry:      return "type symmetry;";
                    default:                    return "type zeroGradient;";
                    }
                })) return false;

        // k (kOmegaSST or kEpsilon)
        if ((kw || ke) && !writeF("k", "volScalarField", "[0 2 -2 0 0 0 0]",
                "uniform " + QString::number(k_val),
                [&](const PatchBC& bc) -> QString {
                    switch (bc.type) {
                    case BCType::VelocityInlet:
                        return QString("type fixedValue; value uniform %1;").arg(k_val);
                    case BCType::Wall:
                        return QString("type kqRWallFunction; value uniform %1;").arg(k_val);
                    case BCType::Symmetry: return "type symmetry;";
                    default: return "type zeroGradient;";
                    }
                })) return false;

        // omega (kOmegaSST)
        if (kw && !writeF("omega", "volScalarField", "[0 0 -1 0 0 0 0]",
                "uniform " + QString::number(omega_val),
                [&](const PatchBC& bc) -> QString {
                    switch (bc.type) {
                    case BCType::VelocityInlet:
                        return QString("type fixedValue; value uniform %1;").arg(omega_val);
                    case BCType::Wall:
                        return QString("type omegaWallFunction; value uniform %1;").arg(omega_val);
                    case BCType::Symmetry: return "type symmetry;";
                    default: return "type zeroGradient;";
                    }
                })) return false;

        // nut (RANS models)
        if ((kw || ke || sa) && !writeF("nut", "volScalarField", "[0 2 -1 0 0 0 0]",
                "uniform " + QString::number(nut_val),
                [&](const PatchBC& bc) -> QString {
                    switch (bc.type) {
                    case BCType::Wall:     return nutWall;
                    case BCType::Symmetry: return "type symmetry;";
                    default: return QString("type calculated; value uniform %1;").arg(nut_val);
                    }
                })) return false;

        // epsilon (kEpsilon)
        if (ke && !writeF("epsilon", "volScalarField", "[0 2 -3 0 0 0 0]",
                "uniform " + QString::number(eps_val),
                [&](const PatchBC& bc) -> QString {
                    switch (bc.type) {
                    case BCType::VelocityInlet:
                        return QString("type fixedValue; value uniform %1;").arg(eps_val);
                    case BCType::Wall:
                        return QString("type epsilonWallFunction; value uniform %1;").arg(eps_val);
                    case BCType::Symmetry: return "type symmetry;";
                    default: return "type zeroGradient;";
                    }
                })) return false;

        // k is the DynamicKEqn LES SGS kinetic energy
        if (lesk && !writeF("k", "volScalarField", "[0 2 -2 0 0 0 0]",
                "uniform " + QString::number(k_val),
                [&](const PatchBC& bc) -> QString {
                    switch (bc.type) {
                    case BCType::VelocityInlet:
                        return QString("type fixedValue; value uniform %1;").arg(k_val);
                    case BCType::Symmetry: return "type symmetry;";
                    default: return "type zeroGradient;";
                    }
                })) return false;

        // nut for all LES models, which v2412 reads as nut
        if (les && !writeF("nut", "volScalarField", "[0 2 -1 0 0 0 0]",
                "uniform 0", symOrZero)) return false;

        // flm / fmm (dynamicLagrangian Lagrangian-averaged coefficients)
        if (dynLag && (!writeF("flm", "volScalarField", "[0 4 -4 0 0 0 0]",
                               "uniform 0", symOrZero)
                    || !writeF("fmm", "volScalarField", "[0 4 -4 0 0 0 0]",
                               "uniform 1e-15", symOrZero))) return false;

        // alphat (compressible + turbulence)
        if (comp && p.turbulence != TurbulenceModel::Laminar
                && !writeF("alphat", "volScalarField", "[1 -1 -1 0 0 0 0]", "uniform 0",
                [&](const PatchBC& bc) -> QString {
                    switch (bc.type) {
                    case BCType::Wall:
                        return "type compressible::alphatWallFunction; Prt 0.85; value uniform 0;";
                    case BCType::Symmetry: return "type symmetry;";
                    default: return "type calculated; value uniform 0;";
                    }
                })) return false;

        // nuTilda (SpalartAllmaras)
        if (sa && !writeF("nuTilda", "volScalarField", "[0 2 -1 0 0 0 0]",
                "uniform " + QString::number(nuTilda),
                [&](const PatchBC& bc) -> QString {
                    switch (bc.type) {
                    case BCType::VelocityInlet:
                        return QString("type fixedValue; value uniform %1;").arg(nuTilda);
                    case BCType::Wall:      return "type fixedValue; value uniform 0;";
                    case BCType::Symmetry:  return "type symmetry;";
                    default:               return "type zeroGradient;";
                    }
                })) return false;

        return true;
    }
    // End per-patch mode

    // U
    {
        QFile f(d.filePath("U"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{ version 2.0; format ascii; class volVectorField; object U; }\n\n"
          << "dimensions [0 1 -1 0 0 0 0];\n"
          << "internalField uniform " << uVec(U) << ";\n"
          << "boundaryField\n{\n"
          << "    inlet   { type fixedValue; value uniform " << uVec(U) << "; }\n"
          << "    outlet  { type zeroGradient; }\n"
          << "    " << p.caseName << " { type noSlip; }\n"
          << "    top     { type symmetry; }\n"
          << "    bottom  { type symmetry; }\n"
          << "    front   { type symmetry; }\n"
          << "    back    { type symmetry; }\n}\n";
    }
    // p is kinematic (m²/s²) when incompressible and thermodynamic (Pa) when compressible
    {
        QFile f(d.filePath("p"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object p; }\n\n";
        if (comp) {
            const bool steady = !isTransient(p.solver);
            o << "dimensions [1 -1 -2 0 0 0 0];\n"
              << "internalField uniform 101325;\n"
              << "boundaryField\n{\n";
            if (steady)
                o << "    inlet   { type zeroGradient; }\n"
                  << "    outlet  { type fixedValue; value uniform 101325; }\n";
            else
                o << "    inlet   { type fixedValue; value uniform 101325; }\n"
                  << "    outlet  { type waveTransmissive; gamma 1.4;"
                  <<               " fieldInf 101325; lInf 1; value uniform 101325; }\n";
            o
              << "    " << p.caseName << " { type zeroGradient; }\n"
              << "    top     { type symmetry; }\n"
              << "    bottom  { type symmetry; }\n"
              << "    front   { type symmetry; }\n"
              << "    back    { type symmetry; }\n}\n";
        } else {
            o << "dimensions [0 2 -2 0 0 0 0];\n"
              << "internalField uniform 0;\n"
              << "boundaryField\n{\n"
              << "    inlet   { type zeroGradient; }\n"
              << "    outlet  { type fixedValue; value uniform 0; }\n"
              << "    " << p.caseName << " { type zeroGradient; }\n"
              << "    top     { type symmetry; }\n"
              << "    bottom  { type symmetry; }\n"
              << "    front   { type symmetry; }\n"
              << "    back    { type symmetry; }\n}\n";
        }
    }
    // T for compressible only
    if (comp) {
        QFile f(d.filePath("T"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object T; }\n\n"
          << "dimensions [0 0 0 1 0 0 0];\n"
          << "internalField uniform 300;\n"
          << "boundaryField\n{\n"
          << "    inlet   { type fixedValue; value uniform 300; }\n"
          << "    outlet  { type zeroGradient; }\n"
          << "    " << p.caseName << " { type zeroGradient; }\n"
          << "    top     { type symmetry; }\n"
          << "    bottom  { type symmetry; }\n"
          << "    front   { type symmetry; }\n"
          << "    back    { type symmetry; }\n}\n";
    }
    // k (if needed)
    if (kw || ke) {
        QFile f(d.filePath("k"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object k; }\n\n"
          << "dimensions [0 2 -2 0 0 0 0];\n"
          << "internalField uniform " << k_val << ";\n"
          << "boundaryField\n{\n"
          << "    inlet   { type fixedValue; value uniform " << k_val << "; }\n"
          << "    outlet  { type zeroGradient; }\n"
          << "    " << p.caseName << " { type kqRWallFunction; value uniform " << k_val << "; }\n"
          << "    top     { type symmetry; }\n"
          << "    bottom  { type symmetry; }\n"
          << "    front   { type symmetry; }\n"
          << "    back    { type symmetry; }\n}\n";
    }
    // omega (kOmegaSST)
    if (kw) {
        QFile f(d.filePath("omega"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object omega; }\n\n"
          << "dimensions [0 0 -1 0 0 0 0];\n"
          << "internalField uniform " << omega_val << ";\n"
          << "boundaryField\n{\n"
          << "    inlet   { type fixedValue; value uniform " << omega_val << "; }\n"
          << "    outlet  { type zeroGradient; }\n"
          << "    " << p.caseName << " { type omegaWallFunction; value uniform " << omega_val << "; }\n"
          << "    top     { type symmetry; }\n"
          << "    bottom  { type symmetry; }\n"
          << "    front   { type symmetry; }\n"
          << "    back    { type symmetry; }\n}\n";
    }
    // k for dynamicKEqn (LES SGS kinetic energy)
    if (lesk) {
        QFile f(d.filePath("k"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object k; }\n\n"
          << "dimensions [0 2 -2 0 0 0 0];\n"
          << "internalField uniform " << k_val << ";\n"
          << "boundaryField\n{\n"
          << "    inlet   { type fixedValue; value uniform " << k_val << "; }\n"
          << "    outlet  { type zeroGradient; }\n"
          << "    " << p.caseName << " { type zeroGradient; }\n"
          << "    top     { type symmetry; }\n"
          << "    bottom  { type symmetry; }\n"
          << "    front   { type symmetry; }\n"
          << "    back    { type symmetry; }\n}\n";
    }
    // nut is required by all LES models in v2412.
    // dynamicLagrangian also reads flm and fmm; fmm starts at a tiny positive
    // value because the initial nut = (flm/fmm)*delta^2*|S| divides by it.
    if (les) {
        struct LesField { const char* name; const char* dims; const char* init; };
        QVector<LesField> fields = { { "nut", "[0 2 -1 0 0 0 0]", "0" } };
        if (dynLag) {
            fields.append({ "flm", "[0 4 -4 0 0 0 0]", "0" });
            fields.append({ "fmm", "[0 4 -4 0 0 0 0]", "1e-15" });
        }
        for (const auto& lf : fields) {
            QFile f(d.filePath(lf.name));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
            QTextStream o(&f);
            o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object "
              << lf.name << "; }\n\n"
              << "dimensions " << lf.dims << ";\n"
              << "internalField uniform " << lf.init << ";\n"
              << "boundaryField\n{\n"
              << "    inlet   { type zeroGradient; }\n"
              << "    outlet  { type zeroGradient; }\n"
              << "    " << p.caseName << " { type zeroGradient; }\n"
              << "    top     { type symmetry; }\n"
              << "    bottom  { type symmetry; }\n"
              << "    front   { type symmetry; }\n"
              << "    back    { type symmetry; }\n}\n";
        }
    }
    // alphat for compressible solvers with a turbulence model
    if (comp && p.turbulence != TurbulenceModel::Laminar) {
        QFile f(d.filePath("alphat"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object alphat; }\n\n"
          << "dimensions [1 -1 -1 0 0 0 0];\n"
          << "internalField uniform 0;\n"
          << "boundaryField\n{\n"
          << "    inlet   { type calculated; value uniform 0; }\n"
          << "    outlet  { type calculated; value uniform 0; }\n"
          << "    " << p.caseName << " { type compressible::alphatWallFunction; Prt 0.85; value uniform 0; }\n"
          << "    top     { type symmetry; }\n"
          << "    bottom  { type symmetry; }\n"
          << "    front   { type symmetry; }\n"
          << "    back    { type symmetry; }\n}\n";
    }
    // nut is required by all RAS models
    if (kw || ke || sa) {
        QFile f(d.filePath("nut"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{\n    version     2.0;\n    format      ascii;\n    class       volScalarField;\n    object      nut;\n}\n\n"
          << "dimensions [0 2 -1 0 0 0 0];\n"
          << "internalField uniform " << nut_val << ";\n"
          << "boundaryField\n{\n"
          << "    inlet   { type calculated; value uniform " << nut_val << "; }\n"
          << "    outlet  { type calculated; value uniform " << nut_val << "; }\n"
          << "    " << p.caseName << " { " << nutWall << " }\n"
          << "    top     { type symmetry; }\n"
          << "    bottom  { type symmetry; }\n"
          << "    front   { type symmetry; }\n"
          << "    back    { type symmetry; }\n}\n";
    }
    // epsilon (kEpsilon only)
    if (ke) {
        QFile f(d.filePath("epsilon"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{\n    version     2.0;\n    format      ascii;\n    class       volScalarField;\n    object      epsilon;\n}\n\n"
          << "dimensions [0 2 -3 0 0 0 0];\n"
          << "internalField uniform " << eps_val << ";\n"
          << "boundaryField\n{\n"
          << "    inlet   { type fixedValue; value uniform " << eps_val << "; }\n"
          << "    outlet  { type zeroGradient; }\n"
          << "    " << p.caseName << " { type epsilonWallFunction; value uniform " << eps_val << "; }\n"
          << "    top     { type symmetry; }\n"
          << "    bottom  { type symmetry; }\n"
          << "    front   { type symmetry; }\n"
          << "    back    { type symmetry; }\n}\n";
    }
    // nuTilda (Spalart-Allmaras)
    if (sa) {
        QFile f(d.filePath("nuTilda"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream o(&f);
        o << "FoamFile\n{ version 2.0; format ascii; class volScalarField; object nuTilda; }\n\n"
          << "dimensions [0 2 -1 0 0 0 0];\n"
          << "internalField uniform " << nuTilda << ";\n"
          << "boundaryField\n{\n"
          << "    inlet   { type fixedValue; value uniform " << nuTilda << "; }\n"
          << "    outlet  { type zeroGradient; }\n"
          << "    " << p.caseName << " { type fixedValue; value uniform 0; }\n"
          << "    top     { type symmetry; }\n"
          << "    bottom  { type symmetry; }\n"
          << "    front   { type symmetry; }\n"
          << "    back    { type symmetry; }\n}\n";
    }
    return true;
}

bool CaseGenerator::writeTransportProperties(
    const QString& constDir, double nu)
{
    QFile f(constDir + "/transportProperties");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);
    o << "FoamFile\n{ version 2.0; format ascii; class dictionary;"
      << " object transportProperties; }\n\n"
      << "transportModel Newtonian;\n"
      << "nu             " << nu << ";\n";
    return true;
}

bool CaseGenerator::writeThermophysicalProperties(const QString& constDir, double T0)
{
    QFile f(constDir + "/thermophysicalProperties");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);
    o << "FoamFile\n{ version 2.0; format ascii; class dictionary;"
      << " object thermophysicalProperties; }\n\n"
      << "thermoType\n{\n"
      << "    type            heRhoThermo;\n"
      << "    mixture         pureMixture;\n"
      << "    transport       const;\n"
      << "    thermo          hConst;\n"
      << "    equationOfState perfectGas;\n"
      << "    specie          specie;\n"
      << "    energy          sensibleEnthalpy;\n"
      << "}\n\n"
      << "mixture\n{\n"
      << "    specie         { nMoles 1; molWeight 28.96; }\n"
      << "    thermodynamics { Cp 1005; Hf 0; }\n"
      << "    transport      { mu 1.8e-5; Pr 0.713; }\n"
      << "}\n";
    return true;
}

bool CaseGenerator::writeTurbulenceProperties(
    const QString& constDir, TurbulenceModel model)
{
    QString body;
    QTextStream o(&body);
    if (model == TurbulenceModel::Laminar) {
        o << "simulationType laminar;\n";
    } else if (isLES(model)) {
        o << "simulationType LES;\n\n"
          << "LES\n{\n"
          << "    LESModel     " << turbModelName(model) << ";\n"
          << "    turbulence   on;\n"
          << "    printCoeffs  on;\n"
          << "    delta        cubeRootVol;\n"
          << "    cubeRootVolCoeffs { deltaCoeff 1; }\n";
        // The dynamic models construct an LESfilter from their coeffs dict
        if (model == TurbulenceModel::DynamicKEqn || model == TurbulenceModel::DynamicLagrangian)
            o << "    " << turbModelName(model) << "Coeffs { filter simple; }\n";
        o << "}\n";
    } else {
        o << "simulationType RAS;\n\n"
          << "RAS\n{\n"
          << "    RASModel     " << turbModelName(model) << ";\n"
          << "    turbulence   on;\n"
          << "    printCoeffs  on;\n}\n";
    }
    o.flush();

    // Write both names. v2412 reads turbulenceProperties, while momentumTransport
    // keeps the case readable by the other tools that look for it.
    for (const QString& name : { QString("turbulenceProperties"), QString("momentumTransport") }) {
        QFile f(constDir + "/" + name);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream(&f) << "FoamFile\n{ version 2.0; format ascii; class dictionary;"
                        << " object " << name << "; }\n\n" << body;
    }
    return true;
}

} // namespace CFD

bool CFD::CaseGenerator::generate_dicts_only(
    const CFD::CaseParams& params, const std::array<double,6>&)
{
    QDir root(params.caseDir);
    QString systemDir = root.filePath("system");
    QString constDir  = root.filePath("constant");
    QString dir0      = root.filePath("0");
    root.mkpath(systemDir);
    root.mkpath(constDir);
    root.mkpath(dir0);

    bool comp = isCompressible(params.solver);
    bool propOk = comp
        ? writeThermophysicalProperties(constDir)
        : writeTransportProperties(constDir, params.nu);
    return writeFvSchemes(systemDir, isLES(params.turbulence), comp, params.divScheme,
                          isTransient(params.solver))
        && writeFvSolution(systemDir, params.solver)
        && writeControlDict(systemDir, params.solver, params.caseName, params.divScheme)
        && writeDecomposeParDict(systemDir)
        && writeBoundaryConditions(dir0, params)
        && propOk
        && writeTurbulenceProperties(constDir, params.turbulence);
}
