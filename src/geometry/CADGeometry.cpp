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

#include "CADGeometry.h"
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkSTLReader.h>
#include <TopoDS_Shape.hxx>
#include <QFileInfo>

namespace CFD {

std::unique_ptr<CADGeometry> CADGeometry::fromFile(
    const QString& path,
    const TessellationParams& tessParams)
{
    auto geo = std::make_unique<CADGeometry>();

    // STL: load directly via VTK: no OCCT pipeline needed
    if (QFileInfo(path).suffix().toLower() == "stl") {
        auto reader = vtkSmartPointer<vtkSTLReader>::New();
        reader->SetFileName(path.toStdString().c_str());
        reader->Update();
        geo->m_poly = reader->GetOutput();
        if (!geo->m_poly || geo->m_poly->GetNumberOfPoints() == 0) {
            geo->m_result.errorMsg = "STL read failed or file is empty.";
            return geo;
        }
        geo->m_result.success  = true;
        geo->m_result.numFaces = static_cast<int>(geo->m_poly->GetNumberOfCells());
        return geo;
    }

    // CAD formats (STEP, IGES, BREP): go through OCCT + tessellator
    geo->m_result = CADImporter::load(path);
    if (!geo->m_result.success) return geo;

    geo->m_poly = CADTessellator::tessellate(*geo->m_result.shape, tessParams);
    return geo;
}

std::array<double,6> CADGeometry::bounds() const
{
    std::array<double,6> b{0,0,0,0,0,0};
    if (m_poly) m_poly->GetBounds(b.data());
    return b;
}

void CADGeometry::retessellate(const TessellationParams& params)
{
    if (!m_result.success || !m_result.shape) return;
    m_poly = CADTessellator::tessellate(*m_result.shape, params);
}

} // namespace CFD
