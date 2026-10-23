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

struct FaceStencilData
{
    labelList stencilK;
    labelList stencil0;
    labelList stencil1;
    labelList stencil2;

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

}
