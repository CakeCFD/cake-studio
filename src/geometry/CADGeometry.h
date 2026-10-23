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
#include "CADImporter.h"
#include "CADTessellator.h"
#include <QString>
#include <array>
#include <memory>

#include <vtkSmartPointer.h>
#include <vtkPolyData.h>

namespace CFD {

class CADGeometry {
public:
    static std::unique_ptr<CADGeometry> fromFile(
        const QString& path,
        const TessellationParams& tessParams = {});

    bool isValid() const { return m_result.success; }
    const CADImportResult& importResult() const { return m_result; }

    vtkSmartPointer<vtkPolyData> polyData() const { return m_poly; }

    // Axis-aligned bounding box: {xmin,xmax,ymin,ymax,zmin,zmax}
    std::array<double,6> bounds() const;

    void retessellate(const TessellationParams& params);

private:
    CADImportResult              m_result;
    vtkSmartPointer<vtkPolyData> m_poly;
};

} // namespace CFD
