#include <iostream>
#include <cstdlib>
#include <cmath>
#include <string>
#include <chrono>

#include <QCoreApplication>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopLoc_Location.hxx>

#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkSTLWriter.h>

#include "CaseGenerator.h"
#include "BlockMeshWriter.h"
#include "SnappyConfigWriter.h"
#include <QDir>

using Clock = std::chrono::steady_clock;
static auto t0 = Clock::now();

static void log(const std::string& msg) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - t0).count();
    std::cout << "  [" << ms << "ms] " << msg << std::endl;
}

static int shell(const std::string& cmd) {
    std::cout << "\n  $ " << cmd << "\n" << std::flush;
    int r = std::system(("bash -c '" + cmd + "'").c_str());
    std::cout << (r == 0 ? "  [OK]\n" : "  [FAILED]\n") << std::flush;
    return r;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    using namespace CFD;

    // Resolve test dir relative to the binary location (build/tests/mesh_pipeline_test/)
    const std::string testDir = std::string(argv[0]).substr(
        0, std::string(argv[0]).rfind('/')) + "/../../../tests/mesh_pipeline_test";

    // Step 1 creates the BREP
    std::cout << "\n=== Step 1: Create sphere BREP (r=0.5m) ===\n" << std::flush;
    t0 = Clock::now();
    const std::string brepPath = testDir + "/sphere.brep";
    {
        log("BRepPrimAPI_MakeSphere...");
        TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(0.5).Shape();
        log("BRepTools::Write...");
        BRepTools::Write(sphere, brepPath.c_str());
        log("done");
    }

    // Step 2 loads the BREP manually, bypassing CADImporter for now
    std::cout << "\n=== Step 2: Load BREP + tessellate ===\n" << std::flush;
    t0 = Clock::now();

    log("BRepTools::Read...");
    BRep_Builder builder;
    TopoDS_Shape shape;
    BRepTools::Read(shape, brepPath.c_str(), builder);
    log("read done");

    log("TopExp_Explorer: counting topology...");
    int nSolids=0, nFaces=0, nEdges=0;
    for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next()) ++nSolids;
    for (TopExp_Explorer e(shape, TopAbs_FACE);  e.More(); e.Next()) ++nFaces;
    for (TopExp_Explorer e(shape, TopAbs_EDGE);  e.More(); e.Next()) ++nEdges;
    std::cout << "  Solids=" << nSolids
              << "  Faces=" << nFaces
              << "  Edges=" << nEdges << "\n" << std::flush;

    log("BRepGProp::VolumeProperties...");
    GProp_GProps vp;
    BRepGProp::VolumeProperties(shape, vp, true);
    double vol = vp.Mass();
    double expected = 4.0/3.0 * M_PI * 0.125;
    std::cout << "  Volume=" << vol << "  expected=" << expected
              << "  err=" << std::abs(vol-expected)/expected*100.0 << "%\n"
              << std::flush;

    log("BRepBndLib::Add (bounding box)...");
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    double xmn,ymn,zmn,xmx,ymx,zmx;
    box.Get(xmn,ymn,zmn,xmx,ymx,zmx);
    double diag = std::sqrt((xmx-xmn)*(xmx-xmn)+(ymx-ymn)*(ymx-ymn)+(zmx-zmn)*(zmx-zmn));
    std::cout << "  Bounds: [" << xmn << "," << xmx << "] x ["
              << ymn << "," << ymx << "] x ["
              << zmn << "," << zmx << "]  diag=" << diag << "\n" << std::flush;

    double linDef = diag * 0.1 * 0.1; // 10% of 10% = 1% (coarse)
    double angDef = 20.0 * M_PI / 180.0;
    log("BRepMesh_IncrementalMesh linDef=" + std::to_string(linDef) + "...");
    BRepMesh_IncrementalMesh mesher(shape, linDef, false, angDef, true);
    mesher.Perform();
    log("mesh done");

    log("Building vtkPolyData...");
    auto pts   = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    vtkIdType offset = 0;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face& face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        auto tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;
        bool rev = (face.Orientation() == TopAbs_REVERSED);
        for (int i = 1; i <= tri->NbNodes(); ++i) {
            gp_Pnt p = tri->Node(i);
            if (!loc.IsIdentity()) p.Transform(loc);
            pts->InsertNextPoint(p.X(), p.Y(), p.Z());
        }
        for (int i = 1; i <= tri->NbTriangles(); ++i) {
            int n1,n2,n3; tri->Triangle(i).Get(n1,n2,n3);
            if (rev) std::swap(n2,n3);
            vtkIdType ids[3] = {offset+n1-1, offset+n2-1, offset+n3-1};
            cells->InsertNextCell(3, ids);
        }
        offset += tri->NbNodes();
    }
    auto poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(pts); poly->SetPolys(cells);
    std::cout << "  VTK points=" << poly->GetNumberOfPoints()
              << "  tris=" << poly->GetNumberOfCells() << "\n" << std::flush;

    // Step 3 writes the STL and generates the case
    std::cout << "\n=== Step 3: Generate OpenFOAM case ===\n" << std::flush;
    t0 = Clock::now();

    log("Writing STL...");
    std::string caseDir = testDir + "/of_case";
    std::string triDir  = caseDir + "/constant/triSurface";
    std::system(("mkdir -p " + triDir).c_str());
    auto writer = vtkSmartPointer<vtkSTLWriter>::New();
    writer->SetFileName((triDir + "/sphere.stl").c_str());
    writer->SetInputData(poly);
    writer->SetFileTypeToASCII();
    writer->Write();
    log("STL written");

    // Use CaseGenerator for all the dict files (skip geo import, pass nullptr-safe path)
    // Re-use our bounding box for BlockMeshWriter directly
    log("Writing OF dictionaries...");
    std::array<double,6> bounds = {xmn,xmx,ymn,ymx,zmn,zmx};
    auto bmParams = CFD::BlockMeshWriter::defaultParams(bounds, 3.0, 20);
    CFD::BlockMeshWriter::write(QString::fromStdString(caseDir), bmParams);

    CFD::SnappyParams snp;
    snp.surfaceName = "sphere";
    snp.nSurfaceLayers = 2;
    // Point inside domain bbox but outside sphere - use 80% of padded extent
    double domXmin = xmn - 3.0*(xmx-xmn);
    snp.locationInMesh = { domXmin * 0.8, (ymn+ymx)*0.5, (zmn+zmx)*0.5 };
    CFD::SnappyConfigWriter::writeFeatureExtract(
        QString::fromStdString(caseDir), "sphere");
    CFD::SnappyConfigWriter::write(QString::fromStdString(caseDir), snp);

    // Write fvSchemes/fvSolution/controlDict/BCs manually via CaseGenerator helpers
    CFD::CaseParams cp;
    cp.caseName = "sphere"; cp.caseDir = QString::fromStdString(caseDir);
    cp.solver = CFD::SolverType::SimpleFoam;
    cp.turbulence = CFD::TurbulenceModel::KOmegaSST;
    cp.inflowVelocity = 10.0; cp.nu = 1.5e-5;

    // Write remaining dicts (fvSchemes etc) - access via static helpers
    QString sysDir = QString::fromStdString(caseDir + "/system");
    QString constDir = QString::fromStdString(caseDir + "/constant");
    QString dir0 = QString::fromStdString(caseDir + "/0");
    QDir().mkpath(sysDir); QDir().mkpath(constDir); QDir().mkpath(dir0);

    // Use the full generator just for the dict-writing part
    auto res = CFD::CaseGenerator::generate_dicts_only(cp, bounds);
    log(res ? "dicts written" : "dict write failed");

    // Step 4 runs OpenFOAM
    std::cout << "\n=== Step 4: OpenFOAM pipeline ===\n" << std::flush;
    std::string of = "source /usr/lib/openfoam/openfoam2412/etc/bashrc 2>/dev/null";
    std::string cd = "cd " + caseDir;
    std::string base = of + " && " + cd + " && ";

    if (shell(base + "surfaceFeatureExtract > log.featureExtract 2>&1") != 0) return 1;
    if (shell(base + "blockMesh > log.blockMesh 2>&1") != 0) return 1;
    if (shell(base + "snappyHexMesh -overwrite > log.snappyHexMesh 2>&1") != 0) return 1;
    if (shell(base + "checkMesh > log.checkMesh 2>&1") != 0) return 1;

    std::cout << "\n=== Mesh quality ===\n";
    std::system(("bash -c '" + base + "grep -A 20 \"Mesh stats\" log.checkMesh'").c_str());
    std::system(("bash -c '" + base +
        "grep -E \"Max.*skewness|Max.*non-ortho|FAILED|No errors\" log.checkMesh'").c_str());

    std::cout << "\n=== Done! ===\n";
    return 0;
}
