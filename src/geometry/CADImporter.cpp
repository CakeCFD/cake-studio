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

#include "CADImporter.h"

#include <TopoDS_Shape.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <ShapeFix_Shape.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <QFileInfo>
#include <iostream>

namespace CFD {

CADFormat CADImporter::detectFormat(const QString& path)
{
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "step" || ext == "stp") return CADFormat::STEP;
    if (ext == "iges" || ext == "igs") return CADFormat::IGES;
    if (ext == "brep" || ext == "brp") return CADFormat::BREP;
    return CADFormat::STEP;
}

CADImportResult CADImporter::load(const QString& filePath, CADFormat format)
{
    if (format == CADFormat::AUTO)
        format = detectFormat(filePath);

    CADImportResult res;
    switch (format) {
    case CADFormat::STEP: res = loadSTEP(filePath); break;
    case CADFormat::IGES: res = loadIGES(filePath); break;
    case CADFormat::BREP: res = loadBREP(filePath); break;  // no repair for BREP
    default: res.errorMsg = "Unknown format"; break;
    }

    if (res.success && res.shape) {
        // Repair only STEP/IGES: BREP is already topologically valid
        if (false) // repair disabled for now: too slow on dev hardware
            repairShape(*res.shape);
        computeStats(res);
    }
    return res;
}

CADImportResult CADImporter::loadSTEP(const QString& path)
{
    CADImportResult res;
    STEPControl_Reader reader;
    if (reader.ReadFile(path.toStdString().c_str()) != IFSelect_RetDone) {
        res.errorMsg = "STEP reader failed.";
        return res;
    }
    reader.TransferRoots();
    if (reader.NbShapes() == 0) {
        res.errorMsg = "STEP file contained no transferable shapes.";
        return res;
    }
    res.shape   = std::make_shared<TopoDS_Shape>(reader.OneShape());
    res.success = true;
    return res;
}

CADImportResult CADImporter::loadIGES(const QString& path)
{
    CADImportResult res;
    IGESControl_Reader reader;
    if (reader.ReadFile(path.toStdString().c_str()) != IFSelect_RetDone) {
        res.errorMsg = "IGES reader failed.";
        return res;
    }
    reader.TransferRoots();
    if (reader.NbShapes() == 0) {
        res.errorMsg = "IGES file contained no transferable shapes.";
        return res;
    }
    res.shape   = std::make_shared<TopoDS_Shape>(reader.OneShape());
    res.success = true;
    return res;
}

CADImportResult CADImporter::loadBREP(const QString& path)
{
    CADImportResult res;
    BRep_Builder builder;
    TopoDS_Shape s;
    std::cerr << "[IMPORT] calling BRepTools::Read\n" << std::flush;
    if (!BRepTools::Read(s, path.toStdString().c_str(), builder)) {
        res.errorMsg = "BREP read failed.";
        return res;
    }
    std::cerr << "[IMPORT] BRepTools::Read done\n" << std::flush;
    res.shape   = std::make_shared<TopoDS_Shape>(s);
    res.success = true;
    return res;
}

void CADImporter::repairShape(TopoDS_Shape& shape)
{
    // Only called for STEP/IGES: fixes gaps and bad tolerances from CAD translators.
    // Keep it minimal: just face/wire fixes, no domain unification.
    ShapeFix_Shape fixer(shape);
    fixer.SetPrecision(1e-6);
    fixer.SetMaxTolerance(1e-3);
    fixer.FixFaceTool()->FixOrientationMode() = 1;
    fixer.Perform();
    shape = fixer.Shape();
}

void CADImporter::computeStats(CADImportResult& res)
{
    std::cerr << "[IMPORT] computeStats start\n" << std::flush;
    if (!res.shape) return;
    const TopoDS_Shape& s = *res.shape;

    for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) ++res.numSolids;
    for (TopExp_Explorer ex(s, TopAbs_SHELL); ex.More(); ex.Next()) ++res.numShells;
    for (TopExp_Explorer ex(s, TopAbs_FACE);  ex.More(); ex.Next()) ++res.numFaces;
    for (TopExp_Explorer ex(s, TopAbs_EDGE);  ex.More(); ex.Next()) ++res.numEdges;

    std::cerr << "[IMPORT] explorers done solids=" << res.numSolids << "\n" << std::flush;
    // Volume via GProp: only if shape has solids (avoids crash on shell-only imports)
    if (res.numSolids > 0) {
        GProp_GProps vp;
            std::cerr << "[IMPORT] VolumeProperties start\n" << std::flush;
        BRepGProp::VolumeProperties(s, vp, true);
        std::cerr << "[IMPORT] VolumeProperties done\n" << std::flush;
        std::cerr << "[IMPORT] VolumeProperties done\n" << std::flush; // useTriangulation=true: much faster
        res.volume = vp.Mass();
    }

    std::cerr << "[IMPORT] computeStats done\n" << std::flush;
    // Surface area skipped for now: BRepGProp::SurfaceProperties is slow
}

} // namespace CFD
