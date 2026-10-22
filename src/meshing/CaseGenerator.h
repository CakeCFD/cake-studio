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
#include <QVector>
#include <memory>

namespace CFD { class CADGeometry; }

namespace CFD {

enum class TurbulenceModel {
    // RANS
    Laminar, SpalartAllmaras, KOmegaSST, KEpsilon,
    // LES / SGS
    Smagorinsky, DynamicKEqn, WALE, DynamicLagrangian
};
enum class SolverType { IcoFoam, SimpleFoam, PimpleFoam, RhoSimpleFoam };

inline bool isCompressible(SolverType s) {
    return s == SolverType::RhoSimpleFoam;
}

inline bool isTransient(SolverType s) {
    return s == SolverType::IcoFoam || s == SolverType::PimpleFoam;
}

inline bool isLES(TurbulenceModel m) {
    return m == TurbulenceModel::Smagorinsky
        || m == TurbulenceModel::DynamicKEqn
        || m == TurbulenceModel::WALE
        || m == TurbulenceModel::DynamicLagrangian;
}

// BC type for manual per-patch boundary condition assignment
enum class BCType {
    Wall,           // noSlip for U, zeroGradient for p/scalars
    VelocityInlet,  // fixedValue U, zeroGradient p
    PressureOutlet, // zeroGradient U, fixedValue p=0
    Symmetry,       // symmetry on all fields
    Slip,           // slip for U, zeroGradient for p/scalars
    ZeroGradient    // zeroGradient on all fields (freestream / far-field)
};

struct PatchBC {
    QString name;
    BCType  type     = BCType::Wall;
    double  velocity = 0.0; // VelocityInlet: x-component (m/s)
};

struct CaseParams {
    QString         caseName         = "cfdCase";
    QString         caseDir;
    SolverType      solver           = SolverType::SimpleFoam;
    TurbulenceModel turbulence       = TurbulenceModel::KOmegaSST;
    QString         divScheme        = "linearUpwind"; // "linearUpwind" | "teno" | "teno6"
    double          inflowVelocity   = 1.0;    // m/s along flow axis; used if patchBCs is empty
    double          aoaDeg           = 0.0;    // angle of attack, degrees (pitch about Y, +ve = nose up)
    // Flow direction. BlockMeshWriter puts the inlet on the X-max face, so the
    // flow must run in -X or the inlet velocity points out of the domain.
    QString         flowAxis         = "-X";   // flow direction: +X/-X/+Y/-Y/+Z/-Z
    double          nu               = 1.5e-5; // kinematic viscosity m^2/s (air)
    double          meshPadding      = 0.5;    // fraction of bbox on each side
    int             bgMeshCells      = 40;     // background mesh cells per direction
    int             nBoundaryLayers  = 3;
    double          surfaceRefinementLinear = 0.5; // % of bbox diagonal

    // snappyHexMesh controls (defaults match SnappyParams)
    int             maxGlobalCells       = 2000000;
    int             maxLocalCells        = 100000;
    int             surfaceRefinementMin = 2;
    int             surfaceRefinementMax = 3;
    int             resolveFeatureAngle  = 30;
    double          layerExpansionRatio  = 1.2;
    double          firstLayerThickness  = 0.3;
    double          minLayerThickness    = 0.1;

    // Per-patch BCs set from the GUI table.
    // When empty, writeBoundaryConditions falls back to the auto mode using inflowVelocity.
    QVector<PatchBC> patchBCs;
};

// Generates a complete OpenFOAM case directory ready to run
class CaseGenerator {
public:
    struct Result {
        bool success = false;
        QString errorMsg;
        QString caseDir;
        QString runScript; // shell command to execute the full pipeline
    };

    static Result generate(const CADGeometry& geo, const CaseParams& params);

    // Write all dict files only (caller handles STL + blockMeshDict externally)
    static bool generate_dicts_only(const CaseParams& params,
                                     const std::array<double,6>& geoBounds);

private:
    static bool writeFvSchemes(const QString& systemDir, bool les, bool compressible,
                              const QString& divScheme = "linearUpwind",
                              bool transient = false);
    static bool writeFvSolution(const QString& systemDir, SolverType solver);
    static bool writeControlDict(const QString& systemDir, SolverType solver,
                                  const QString& caseName,
                                  const QString& divScheme = "linearUpwind");
    static bool writeDecomposeParDict(const QString& systemDir, int nProcs = 4);
    static bool writeBoundaryConditions(const QString& dir0, const CaseParams& p);
    static bool writeTransportProperties(const QString& constDir, double nu);
    static bool writeThermophysicalProperties(const QString& constDir, double T0 = 300.0);
    static bool writeTurbulenceProperties(const QString& constDir,
                                           TurbulenceModel model);
    static QString solverName(SolverType s);
    static QString turbModelName(TurbulenceModel t);
};

} // namespace CFD
