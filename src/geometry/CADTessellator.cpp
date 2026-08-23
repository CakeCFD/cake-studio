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

#include "CADTessellator.h"

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>
#include <TopLoc_Location.hxx>
#include <TopAbs.hxx>

#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>

#include <cmath>
#include <iostream>

namespace CFD {

// Transform a point by a TopLoc_Location
static gp_Pnt applyLoc(const gp_Pnt& p, const TopLoc_Location& loc)
{
    if (loc.IsIdentity()) return p;
    // TopLoc_Location implicitly converts to const gp_Trsf&
    gp_Pnt result = p;
    result.Transform(loc);
    return result;
}

vtkSmartPointer<vtkPolyData> CADTessellator::tessellate(
    const TopoDS_Shape& shape,
    const TessellationParams& params)
{
    double linDef = params.linearDeflection;

    if (params.relativeDeflection) {
        Bnd_Box box;
        BRepBndLib::Add(shape, box);
        if (!box.IsVoid()) {
            double xmin,ymin,zmin,xmax,ymax,zmax;
            box.Get(xmin,ymin,zmin,xmax,ymax,zmax);
            double diag = std::sqrt(
                (xmax-xmin)*(xmax-xmin) +
                (ymax-ymin)*(ymax-ymin) +
                (zmax-zmin)*(zmax-zmin));
            linDef = diag * params.linearDeflection; // fraction of bbox diagonal: coarse enough for snappyHexMesh STL
        }
    }

    double angDef = params.angularDeflection * M_PI / 180.0;

    std::cerr << "[TESS] BRepMesh start linDef=" << linDef << "\n" << std::flush;
    BRepMesh_IncrementalMesh mesher(shape, linDef, false, angDef, false);
    mesher.Perform();
    std::cerr << "[TESS] BRepMesh done\n" << std::flush;

    auto pts   = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();

    vtkIdType ptOffset = 0;

    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face& face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;

        bool reversed = (face.Orientation() == TopAbs_REVERSED);

        int nNodes = tri->NbNodes();
        for (int i = 1; i <= nNodes; ++i) {
            gp_Pnt p = applyLoc(tri->Node(i), loc);
            pts->InsertNextPoint(p.X(), p.Y(), p.Z());
        }

        int nTri = tri->NbTriangles();
        for (int i = 1; i <= nTri; ++i) {
            int n1, n2, n3;
            tri->Triangle(i).Get(n1, n2, n3);
            if (reversed) std::swap(n2, n3);
            vtkIdType ids[3] = {
                ptOffset + n1 - 1,
                ptOffset + n2 - 1,
                ptOffset + n3 - 1
            };
            cells->InsertNextCell(3, ids);
        }
        ptOffset += nNodes;
    }

    auto poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(pts);
    poly->SetPolys(cells);
    return poly;
}

} // namespace CFD
