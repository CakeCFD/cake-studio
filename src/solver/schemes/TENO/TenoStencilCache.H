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
#include "fvMesh.H"
#include "MeshObject.H"
#include "scalarField.H"
#include "vectorField.H"

namespace Foam
{

// Per-face stencil data for TENO schemes.
// Provides three directional sub-stencils (owner-biased, central, neighbour-biased)
// and a wide central stencil for high-order reconstruction.
// TENO5 uses stencil0/stencil1/stencilK (3 candidates).
// TENO6 additionally uses stencil2 for a 4th candidate.
struct FaceStencilData
{
    labelList stencilK;   // wide central stencil  (owner ∪ neighbour 1-ring)
    labelList stencil0;   // owner-side sub-stencil (owner 1-ring)
    labelList stencil1;   // neighbour-side sub-stencil (neighbour 1-ring)
    labelList stencil2;   // owner 2nd-ring sub-stencil (for TENO6 4th candidate)

    // Pre-computed least-squares pseudo-inverse (4 x n) row-major:
    //   u_face = sum_j lsq[0*n+j]*u[j]
    //   du/dx  = sum_j lsq[1*n+j]*u[j]   (evaluated at faceCentre)
    scalarField lsqK;
    scalarField lsq0;
    scalarField lsq1;
    scalarField lsq2;

    vector faceCentre;
};

class TenoStencilCache
:
    public MeshObject<fvMesh, UpdateableMeshObject, TenoStencilCache>
{
    List<FaceStencilData> faceData_;

    void buildStencils(const fvMesh& mesh);
    scalarField computeLSQ(
        const labelList& stencil,
        const vectorField& cellCentres,
        const vector& faceC) const;

public:
    TypeName("TenoStencilCache");

    explicit TenoStencilCache(const fvMesh& mesh);

    const FaceStencilData& faceData(label facei) const
    {
        return faceData_[facei];
    }

    label size() const { return faceData_.size(); }

    virtual bool movePoints() { buildStencils(this->mesh()); return true; }
    virtual void updateMesh(const mapPolyMesh&) { buildStencils(this->mesh()); }
};

} // namespace Foam
