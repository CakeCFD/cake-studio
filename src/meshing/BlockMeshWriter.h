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

struct BlockMeshParams {
    std::array<double,6> bounds; // xmin xmax ymin ymax zmin zmax (from geo bbox)
    double padding = 0.5;        // fraction of bbox extent to add on each side
    int cellsX = 40, cellsY = 40, cellsZ = 40;
};

class BlockMeshWriter {
public:
    static bool write(const QString& caseDir, const BlockMeshParams& p);
    static BlockMeshParams defaultParams(const std::array<double,6>& geoBounds,
                                         double padding = 0.5,
                                         int cells = 40);
};

} // namespace CFD
