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

#include "BlockMeshWriter.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace CFD {

BlockMeshParams BlockMeshWriter::defaultParams(
    const std::array<double,6>& b, double padding, int cells)
{
    double dx = b[1]-b[0], dy = b[3]-b[2], dz = b[5]-b[4];
    // A planar or line-like STL has a zero extent in some direction, which would
    // give a zero-volume block. Treat any degenerate extent as a fraction of the
    // largest one (or 1 m if the whole bbox is degenerate) before padding.
    {
        const double maxExt = std::max({dx, dy, dz});
        const double minExt = (maxExt > 0.0) ? 0.1 * maxExt : 1.0;
        auto fix = [&](double& d) { if (!(d > 1e-9 * std::max(maxExt, 1.0))) d = minExt; };
        fix(dx); fix(dy); fix(dz);
    }
    // Centre of the (possibly widened) bbox, so the geometry stays in the middle
    const double cx = 0.5*(b[0]+b[1]), cy = 0.5*(b[2]+b[3]), cz = 0.5*(b[4]+b[5]);
    BlockMeshParams p;
    const double hx = (0.5 + padding)*dx, hy = (0.5 + padding)*dy, hz = (0.5 + padding)*dz;
    p.bounds = {
        cx - hx, cx + hx,
        cy - hy, cy + hy,
        cz - hz, cz + hz
    };
    p.padding  = padding;
    // Scale cell counts to keep roughly cubic cells
    double maxD = std::max({dx, dy, dz});
    p.cellsX = std::max(4, int(cells * dx / maxD));
    p.cellsY = std::max(4, int(cells * dy / maxD));
    p.cellsZ = std::max(4, int(cells * dz / maxD));
    return p;
}

bool BlockMeshWriter::write(const QString& caseDir, const BlockMeshParams& p)
{
    QDir dir(caseDir);
    dir.mkpath("system");
    QFile f(dir.filePath("system/blockMeshDict"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);

    const auto& b = p.bounds;
    out << "FoamFile\n{\n"
        << "    version     2.0;\n"
        << "    format      ascii;\n"
        << "    class       dictionary;\n"
        << "    location    \"system\";\n"
        << "    object      blockMeshDict;\n}\n\n"
        << "scale   1;\n\n"
        << "vertices\n(\n"
        << "    (" << b[0] << " " << b[2] << " " << b[4] << ")\n"
        << "    (" << b[1] << " " << b[2] << " " << b[4] << ")\n"
        << "    (" << b[1] << " " << b[3] << " " << b[4] << ")\n"
        << "    (" << b[0] << " " << b[3] << " " << b[4] << ")\n"
        << "    (" << b[0] << " " << b[2] << " " << b[5] << ")\n"
        << "    (" << b[1] << " " << b[2] << " " << b[5] << ")\n"
        << "    (" << b[1] << " " << b[3] << " " << b[5] << ")\n"
        << "    (" << b[0] << " " << b[3] << " " << b[5] << ")\n"
        << ");\n\n"
        << "blocks\n(\n"
        << "    hex (0 1 2 3 4 5 6 7) ("
        << p.cellsX << " " << p.cellsY << " " << p.cellsZ
        << ") simpleGrading (1 1 1)\n);\n\n"
        << "edges ();\n\n"
        << "boundary\n(\n"
        // V1 flow is -X: inlet must be the X-max face (1 2 6 5) so the inlet
        // velocity (-U 0 0) points INTO the domain. Keep in sync with
        // DomainSetupDialog's -X mapping, or phi leaves through the inlet and
        // the compressible solvers abort on negative temperature.
        << "    inlet  { type patch; faces ((1 2 6 5)); }\n"
        << "    outlet { type patch; faces ((0 4 7 3)); }\n"
        << "    top    { type symmetry; faces ((3 7 6 2)); }\n"
        << "    bottom { type symmetry; faces ((0 1 5 4)); }\n"
        << "    front  { type symmetry; faces ((4 5 6 7)); }\n"
        << "    back   { type symmetry; faces ((0 3 2 1)); }\n"
        << ");\n\n"
        << "mergePatchPairs ();\n";
    return true;
}

} // namespace CFD
