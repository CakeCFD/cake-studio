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

#include "SnappyConfigWriter.h"
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace CFD {

bool SnappyConfigWriter::writeFeatureExtract(
    const QString& caseDir, const QString& surfaceName)
{
    QDir dir(caseDir);
    dir.mkpath("system");
    QFile f(dir.filePath("system/surfaceFeatureExtractDict"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);
    o << "FoamFile\n{\n"
      << "    version 2.0; format ascii;\n"
      << "    class dictionary; object surfaceFeatureExtractDict;\n}\n\n"
      << surfaceName << ".stl\n{\n"
      << "    extractionMethod    extractFromSurface;\n"
      << "    extractFromSurfaceCoeffs { includedAngle 150; }\n"
      << "    writeObj yes;\n}\n";
    return true;
}

bool SnappyConfigWriter::write(const QString& caseDir, const SnappyParams& p)
{
    QDir dir(caseDir);
    dir.mkpath("system");
    QFile f(dir.filePath("system/snappyHexMeshDict"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream o(&f);

    o << "FoamFile\n{ version 2.0; format ascii; class dictionary;"
      << " object snappyHexMeshDict; }\n\n"
      << "castellatedMesh true;\nsnap true;\naddLayers true;\n\n"

      << "geometry\n{\n"
      << "    " << p.surfaceName << "\n    {\n"
      << "        type triSurfaceMesh;\n"
      << "        file \"" << p.surfaceName << ".stl\";\n    }\n};\n\n"

      << "castellatedMeshControls\n{\n"
      << "    maxGlobalCells       " << p.maxGlobalCells << ";\n"
      << "    maxLocalCells        " << p.maxLocalCells  << ";\n"
      << "    minRefinementCells   " << p.minRefinementCells << ";\n"
      << "    nCellsBetweenLevels  3;\n"
      << "    resolveFeatureAngle  " << p.resolveFeatureAngle << ";\n"
      << "    features\n    (\n"
      << "        { file \"" << p.surfaceName << ".eMesh\"; level 2; }\n"
      << "    );\n"
      << "    refinementSurfaces\n    {\n"
      << "        " << p.surfaceName << "\n        {\n"
      << "            level (" << p.surfaceRefinementMin
      <<                   " " << p.surfaceRefinementMax << ");\n"
      << "            patchInfo { type wall; name " << p.surfaceName << "; }\n        }\n    }\n"
      << "    refinementRegions {}\n"
      << "    locationInMesh ("
      <<      p.locationInMesh[0] << " "
      <<      p.locationInMesh[1] << " "
      <<      p.locationInMesh[2] << ");\n"
      << "    allowFreeStandingZoneFaces true;\n}\n\n"

      << "snapControls\n{\n"
      << "    nSmoothPatch  " << p.nSmoothPatch << ";\n"
      << "    tolerance     " << p.snapTolerance << ";\n"
      << "    nSolveIter    " << p.nSolveIter << ";\n"
      << "    nRelaxIter    " << p.nRelaxIter  << ";\n"
      << "    nFeatureSnapIter 10;\n"
      << "    implicitFeatureSnap false;\n"
      << "    explicitFeatureSnap true;\n"
      << "    multiRegionFeatureSnap false;\n}\n\n"

      << "addLayersControls\n{\n"
      << "    relativeSizes true;\n"
      << "    layers\n    {\n"
      << "        " << p.surfaceName
      <<        " { nSurfaceLayers " << p.nSurfaceLayers << "; }\n"
      << "    }\n"
      << "    expansionRatio          " << p.layerExpansionRatio << ";\n"
      << "    firstLayerThickness     " << p.finalLayerThickness << ";\n"
      << "    minThickness            " << p.minThickness << ";\n"
      << "    nGrow                   0;\n"
      << "    featureAngle            60;\n"
      << "    slipFeatureAngle        30;\n"
      << "    nRelaxIter              5;\n"
      << "    nRelaxedIter            10;\n"
      << "    nSmoothSurfaceNormals   10;\n"
      << "    nSmoothNormals          3;\n"
      << "    nSmoothThickness        10;\n"
      << "    maxFaceThicknessRatio   0.5;\n"
      << "    maxThicknessToMedialRatio 0.3;\n"
      << "    minMedialAxisAngle      90;\n"
      << "    nMedialAxisIter         10;\n"
      << "    nBufferCellsNoExtrude   0;\n"
      << "    nLayerIter              50;\n}\n\n"

      << "meshQualityControls\n{\n"
      << "    maxNonOrtho 65;\n"
      << "    maxBoundarySkewness 20;\n"
      << "    maxInternalSkewness 4;\n"
      << "    maxConcave 80;\n"
      << "    minVol 1e-13;\n"
      << "    minTetQuality 1e-15;\n"
      << "    minArea -1;\n"
      << "    minTwist 0.02;\n"
      << "    minDeterminant 0.001;\n"
      << "    minFaceWeight 0.05;\n"
      << "    minVolRatio 0.01;\n"
      << "    minTriangleTwist -1;\n"
      << "    nSmoothScale 4;\n"
      << "    errorReduction 0.75;\n"
      << "    relaxed { maxNonOrtho 75; }\n}\n\n"
      << "debug 0;\n"
      << "mergeTolerance 1e-6;\n";
    return true;
}

}
