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
#include <array>

namespace CFD {

struct SnappyParams {
    QString surfaceName;
    int     maxGlobalCells   = 2000000;
    int     maxLocalCells    = 100000;
    int     minRefinementCells = 10;
    int     surfaceRefinementMin = 2;
    int     surfaceRefinementMax = 3;
    int     resolveFeatureAngle  = 30;
    int     nSmoothPatch   = 3;
    double  snapTolerance  = 2.0;
    int     nSolveIter     = 30;
    int     nRelaxIter     = 5;
    int     nSurfaceLayers = 3;
    double  layerExpansionRatio = 1.2;
    double  finalLayerThickness = 0.3;
    double  minThickness        = 0.1;
    std::array<double,3> locationInMesh = {0.0, 0.0, 0.0};
};

class SnappyConfigWriter {
public:
    static bool write(const QString& caseDir, const SnappyParams& p);
    static bool writeFeatureExtract(const QString& caseDir,
                                     const QString& surfaceName);
};

}
