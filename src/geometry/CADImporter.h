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
#include <memory>

class TopoDS_Shape;

namespace CFD {

enum class CADFormat { STEP, IGES, BREP, AUTO };

struct CADImportResult {
    bool success = false;
    QString errorMsg;
    std::shared_ptr<TopoDS_Shape> shape;
    int numSolids   = 0;
    int numShells   = 0;
    int numFaces    = 0;
    int numEdges    = 0;
    double volume   = 0.0;
    double surfArea = 0.0;
};

class CADImporter {
public:
    static CADImportResult load(const QString& filePath,
                                CADFormat format = CADFormat::AUTO);

private:
    static CADFormat detectFormat(const QString& path);
    static CADImportResult loadSTEP(const QString& path);
    static CADImportResult loadIGES(const QString& path);
    static CADImportResult loadBREP(const QString& path);
    static void            repairShape(TopoDS_Shape& shape);
    static void            computeStats(CADImportResult& result);
};

}
