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
#include <memory>

class TopoDS_Shape;
class vtkPolyData;
template<class T> class vtkSmartPointer;

namespace CFD {

struct TessellationParams {
    double linearDeflection  = 0.1;   // max chord error (mm)
    double angularDeflection = 0.5;   // max angle between normals (degrees)
    bool   relativeDeflection = true; // scale linear deflection by bounding box
};

class CADTessellator {
public:
    // Triangulate shape, merge all faces into one vtkPolyData
    static vtkSmartPointer<vtkPolyData> tessellate(
        const TopoDS_Shape& shape,
        const TessellationParams& params = {});
};

} // namespace CFD
