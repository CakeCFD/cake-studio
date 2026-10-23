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
#include "CaseGenerator.h"
#include "SnappyConfigWriter.h"
#include "BlockMeshWriter.h"
#include <string>

namespace CFD::CLI {

struct RunConfig {
    // Geometry
    std::string geometryFile;       // path to STEP/IGES/BREP/STL

    // Case
    std::string caseName   = "cfdCase";
    std::string outputDir  = "./cfd_output";

    // Solver
    std::string solver     = "simpleFoam";  // icoFoam simpleFoam pimpleFoam rhoSimpleFoam
    std::string turbulence = "kOmegaSST";   // laminar SA kOmegaSST kEpsilon
    double inflowVelocity  = 10.0;
    double nu              = 1.5e-5;

    // Background mesh
    double meshPadding = 0.5;
    int    bgMeshCells = 40;

    // snappyHexMesh
    int    maxGlobalCells         = 2000000;
    int    maxLocalCells          = 100000;
    int    surfaceRefinementMin   = 2;
    int    surfaceRefinementMax   = 3;
    int    resolveFeatureAngle    = 30;
    int    nBoundaryLayers        = 3;
    double layerExpansionRatio    = 1.2;
    double firstLayerThickness    = 0.3;
    double minThickness           = 0.1;

    // Run control
    bool runMesh   = true;   // execute blockMesh + snappyHexMesh
    bool runSolver = false;  // execute the solver after meshing
    int  nCores    = 4;
};

RunConfig loadYAML(const std::string& path);

} // namespace CFD::CLI
