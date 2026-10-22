# CakeCFD

## About CakeCFD

CakeCFD is a general purpose computational fluid dynamics platform combining an
OpenFOAM based solver backend with a native C++ Qt desktop application. It provides
geometry import, automated mesh generation, solver configuration, run control and
post processing from a single interface, together with high order numerical schemes
that are not part of a stock OpenFOAM installation.

The application is developed and supported on Linux. On Windows it runs under WSL2,
where the graphical interface is presented on the Windows desktop by WSLg without
requiring a separate X server.

## Copyright

CakeCFD is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version. See the file
LICENSE in this directory, or <https://www.gnu.org/licenses/>, for a description of
the GNU General Public License terms under which you can copy the files.

## Features

### Solver

OpenFOAM 2412 backend with custom numerical schemes. TENO (Targeted Essentially
Non-Oscillatory) convection schemes are provided as a wmake library and are not
available in stock OpenFOAM.

Verification status: the schemes as shipped measure second order on smooth linear
advection, not the fifth and sixth order of the reference formulation. The
reconstruction currently reduces to central differencing in smooth regions, which
is deliberate for pressure-velocity stability but caps the achievable order. The
measurements are reproducible with validation/tools/advection_order.py (case
VC-001). Work toward the full design order is in progress on the
teno-kexact-wip branch, where a k-exact upwind-biased reconstruction reaches
approximately fifth order on the same test.

Supported solvers: simpleFoam, icoFoam, pimpleFoam, pisoFoam, rhoSimpleFoam,
rhoPimpleFoam, rhoCentralFoam, buoyantSimpleFoam and buoyantPimpleFoam.

Supported turbulence models: k-omega SST, k-epsilon and Spalart-Allmaras.

### Geometry

Native BREP and STEP import through OpenCASCADE (OCCT 7.6), with automatic
tessellation and STL export for meshing.

### Meshing

Automated blockMesh and snappyHexMesh pipeline driven by a mesh setup wizard,
covering surface feature extraction, boundary layers and refinement regions.

### Graphical interface

A native C++ Qt5 application providing dark and light themes, a 3D VTK viewport for
velocity, pressure and turbulence fields, isocontours and streamline tracing, a live
residual plot during solver runs, and a solver console with autocompletion, command
history and filtered output. A per solver configuration dialog writes the required
OpenFOAM case files, including fvSchemes, fvSolution and initial fields, from
values entered in the interface.

### Dataset manager

The Johns Hopkins Turbulence Database is integrated as the default data source,
allowing direct numerical simulation turbulence data to be queried from the
interface and exported to an OpenFOAM timeVaryingMappedFixedValue boundary
condition. A configurable endpoint slot accepts any compatible API, with a live test
editor for defining the response format.

## Software stack

| Component        | Technology                              |
|------------------|-----------------------------------------|
| Interface        | Qt5 and VTK 9                           |
| Geometry         | OpenCASCADE (OCCT 7.6)                  |
| Solver backend   | OpenFOAM 2412                           |
| Custom schemes   | C++ wmake library (libtenoScheme.so)  |
| Visualisation    | VTK 9                                   |
| Build system     | CMake 3.16 or later, and wmake          |

## System requirements

- Linux (Ubuntu 22.04 or later recommended), or WSL2 on Windows
- OpenFOAM 2412
- Qt5 (Widgets, OpenGL, Network)
- VTK 9
- OpenCASCADE 7.6
- CMake 3.16 or later
- GCC 11 or later with C++17 support

On Windows, install WSL2 by running wsl --install in an administrator terminal,
then carry out the build steps below inside the WSL shell.

## Building CakeCFD

One command builds the application and the TENO scheme library:

```bash
git clone https://github.com/Trigodil/cake-studio.git
cd cake-studio
./build.sh
```

The script checks for the required tools, sources the OpenFOAM environment if it
is not already active, and reports which schemes the resulting library
registers. Full compiler output goes to build/build.log.

Options:

| Option | Effect |
|-------------------|-----------------------------------------------|
| --clean | Remove build artefacts first |
| --debug | Build with debug symbols |
| --app-only | Skip the TENO library (no OpenFOAM needed) |
| --teno-only | Skip the application |
| --verbose, -v | Show full compiler output |
| --jobs N, -j N| Parallel jobs, defaults to all cores |

To build by hand instead, the application uses CMake in the usual way, and the
scheme library is built with wmake libso from src/solver/schemes/TENO with
an OpenFOAM environment active.

## Running CakeCFD

```bash
./build/src/gui/CFDStudio
```

A project file may be supplied on the command line:

```bash
./build/src/gui/CFDStudio /path/to/project.cake
```

## Turbulence database access

The dataset manager connects to the
[Johns Hopkins Turbulence Database](http://turbulence.pha.jhu.edu/) for direct
numerical simulation data. To enable it:

1. Register for an access token at <http://turbulence.pha.jhu.edu/>.
2. Open the dataset manager panel in the application.
3. Enter the token in the API token field.

The token is stored locally in the application settings and is not written to the
repository.

## Reporting bugs

Please report defects through the GitHub issue tracker for this repository. Include
the solver, the case configuration and the relevant portion of the solver log.
