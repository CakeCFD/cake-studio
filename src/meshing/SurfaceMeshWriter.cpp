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

#include "SurfaceMeshWriter.h"
#include "../geometry/CADGeometry.h"

#include <vtkPolyData.h>
#include <vtkSTLWriter.h>
#include <vtkSmartPointer.h>
#include <vtkTriangleFilter.h>
#include <vtkCleanPolyData.h>
#include <vtkPolyDataNormals.h>

#include <QDir>
#include <QFileInfo>

namespace CFD {

QString SurfaceMeshWriter::write(const CADGeometry& geo,
                                  const QString& caseDir,
                                  const QString& surfaceName)
{
    if (!geo.isValid() || !geo.polyData()) return {};

    QDir dir(caseDir);
    dir.mkpath("constant/triSurface");
    QString outPath = dir.filePath("constant/triSurface/" + surfaceName + ".stl");

    auto clean = vtkSmartPointer<vtkCleanPolyData>::New();
    clean->SetInputData(geo.polyData());
    clean->Update();

    auto triFilter = vtkSmartPointer<vtkTriangleFilter>::New();
    triFilter->SetInputConnection(clean->GetOutputPort());
    triFilter->Update();

    auto normFilter = vtkSmartPointer<vtkPolyDataNormals>::New();
    normFilter->SetInputConnection(triFilter->GetOutputPort());
    normFilter->ComputePointNormalsOff();
    normFilter->ComputeCellNormalsOn();
    normFilter->ConsistencyOn();
    normFilter->AutoOrientNormalsOn();
    normFilter->Update();

    auto writer = vtkSmartPointer<vtkSTLWriter>::New();
    writer->SetFileName(outPath.toStdString().c_str());
    writer->SetInputConnection(normFilter->GetOutputPort());
    writer->SetFileTypeToASCII();
    if (writer->Write() != 1 || !QFileInfo::exists(outPath)) return {};

    return outPath;
}

}
