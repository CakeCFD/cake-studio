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

#include "RunConfig.h"
#include <yaml-cpp/yaml.h>
#include <stdexcept>

namespace CFD::CLI {

static std::string str(const YAML::Node& n, const std::string& key,
                       const std::string& def = "")
{
    return n[key] ? n[key].as<std::string>() : def;
}
static double dbl(const YAML::Node& n, const std::string& key, double def = 0.0)
{
    return n[key] ? n[key].as<double>() : def;
}
static int intv(const YAML::Node& n, const std::string& key, int def = 0)
{
    return n[key] ? n[key].as<int>() : def;
}
static bool boolv(const YAML::Node& n, const std::string& key, bool def = false)
{
    return n[key] ? n[key].as<bool>() : def;
}

RunConfig loadYAML(const std::string& path)
{
    YAML::Node root;
    try { root = YAML::LoadFile(path); }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("YAML parse error: ") + e.what());
    }

    RunConfig c;

    if (root["geometry"])
        c.geometryFile = str(root["geometry"], "file");

    if (root["case"]) {
        auto n = root["case"];
        c.caseName  = str(n, "name",       c.caseName);
        c.outputDir = str(n, "output_dir", c.outputDir);
    }

    if (root["solver"]) {
        auto n = root["solver"];
        c.solver          = str(n, "type",           c.solver);
        c.turbulence      = str(n, "turbulence",     c.turbulence);
        c.inflowVelocity  = dbl(n, "inflow_velocity", c.inflowVelocity);
        c.nu              = dbl(n, "nu",              c.nu);
    }

    if (root["mesh"]) {
        auto n = root["mesh"];
        c.meshPadding             = dbl(n,  "padding",                c.meshPadding);
        c.bgMeshCells             = intv(n, "background_cells",       c.bgMeshCells);
        c.maxGlobalCells          = intv(n, "max_global_cells",       c.maxGlobalCells);
        c.maxLocalCells           = intv(n, "max_local_cells",        c.maxLocalCells);
        c.surfaceRefinementMin    = intv(n, "surface_refinement_min", c.surfaceRefinementMin);
        c.surfaceRefinementMax    = intv(n, "surface_refinement_max", c.surfaceRefinementMax);
        c.resolveFeatureAngle     = intv(n, "resolve_feature_angle",  c.resolveFeatureAngle);
        c.nBoundaryLayers         = intv(n, "boundary_layers",        c.nBoundaryLayers);
        c.layerExpansionRatio     = dbl(n,  "layer_expansion_ratio",  c.layerExpansionRatio);
        c.firstLayerThickness     = dbl(n,  "first_layer_thickness",  c.firstLayerThickness);
        c.minThickness            = dbl(n,  "min_thickness",          c.minThickness);
    }

    if (root["run"]) {
        auto n = root["run"];
        c.runMesh   = boolv(n, "mesh",   c.runMesh);
        c.runSolver = boolv(n, "solver", c.runSolver);
        c.nCores    = intv(n,  "cores",  c.nCores);
    }

    return c;
}

} // namespace CFD::CLI
