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

#include "TenoStencilCache.H"
#include "addToRunTimeSelectionTable.H"

namespace Foam
{

defineTypeNameAndDebug(TenoStencilCache, 0);

// Least-squares pseudo-inverse
// Returns (4 x n) flat matrix so that:
//   u(faceC) ≈ sum_j pinv[0*n+j]*u[j]
//   ∂u/∂x    ≈ sum_j pinv[1*n+j]*u[j]   etc.
scalarField TenoStencilCache::computeLSQ(
    const labelList& stencil,
    const vectorField& cc,
    const vector& faceC) const
{
    label n = stencil.size();
    scalarField AT(4*n, 0.0);
    scalar AtA[4][4] = {};

    for (label i = 0; i < n; ++i)
    {
        vector d = cc[stencil[i]] - faceC;
        scalar row[4] = {1.0, d.x(), d.y(), d.z()};
        for (int r = 0; r < 4; ++r) AT[r*n + i] = row[r];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                AtA[r][c] += row[r]*row[c];
    }

    // Invert AtA via Gaussian elimination with partial pivoting
    scalar inv[4][4] = {};
    for (int i = 0; i < 4; ++i) inv[i][i] = 1.0;
    scalar mat[4][4];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            mat[r][c] = AtA[r][c];

    for (int col = 0; col < 4; ++col)
    {
        int pivot = col;
        for (int r = col+1; r < 4; ++r)
            if (std::abs(mat[r][col]) > std::abs(mat[pivot][col])) pivot = r;
        for (int c = 0; c < 4; ++c)
        {
            std::swap(mat[col][c], mat[pivot][c]);
            std::swap(inv[col][c], inv[pivot][c]);
        }
        scalar diag = mat[col][col];
        if (std::abs(diag) < 1e-30) {
            // Near-singular column: zero out this row so it can't corrupt the back-substitution
            // of subsequent columns, and drop this polynomial term from the fit.
            for (int c = 0; c < 4; ++c) { mat[col][c] = 0.0; inv[col][c] = 0.0; }
            continue;
        }
        for (int c = 0; c < 4; ++c) { mat[col][c] /= diag; inv[col][c] /= diag; }
        for (int r = 0; r < 4; ++r)
        {
            if (r == col) continue;
            scalar f = mat[r][col];
            for (int c = 0; c < 4; ++c)
            { mat[r][c] -= f*mat[col][c]; inv[r][c] -= f*inv[col][c]; }
        }
    }

    // pinv = inv(AtA) * A^T  (4 x n)
    scalarField pinv(4*n, 0.0);
    for (int r = 0; r < 4; ++r)
        for (label j = 0; j < n; ++j)
            for (int k = 0; k < 4; ++k)
                pinv[r*n + j] += inv[r][k] * AT[k*n + j];

    return pinv;
}

// Build stencils for every internal face
void TenoStencilCache::buildStencils(const fvMesh& mesh)
{
    const labelUList& own = mesh.owner();
    const labelUList& nei = mesh.neighbour();
    const vectorField& cc = mesh.cellCentres();
    const vectorField& fc = mesh.faceCentres();
    const labelListList& cn = mesh.cellCells();

    label nFaces = own.size();
    faceData_.resize(nFaces);

    forAll(own, facei)
    {
        label o = own[facei], n = nei[facei];
        vector fC = fc[facei];
        auto& fd = faceData_[facei];
        fd.faceCentre = fC;

        // S_K: wide central stencil (1-ring around both owner and neighbour)
        labelHashSet vK;
        auto addK = [&](label c){ if(vK.insert(c)) fd.stencilK.append(c); };
        addK(o); addK(n);
        for (label nb : cn[o]) addK(nb);
        for (label nb : cn[n]) addK(nb);

        // S_0: owner-side (owner + owner's face-neighbours)
        labelHashSet v0;
        auto add0 = [&](label c){ if(v0.insert(c)) fd.stencil0.append(c); };
        add0(o);
        for (label nb : cn[o]) add0(nb);

        // S_1: neighbour-side (neighbour + neighbour's face-neighbours)
        labelHashSet v1;
        auto add1 = [&](label c){ if(v1.insert(c)) fd.stencil1.append(c); };
        add1(n);
        for (label nb : cn[n]) add1(nb);

        // S_2: owner 2nd-ring - used as 4th TENO6 candidate
        // Extends stencil0 by one more ring: neighbours-of-neighbours of owner.
        // This gives a strongly upstream-biased stencil distinct from S_0.
        labelHashSet v2(v0);  // start from S_0 cells
        auto add2 = [&](label c){ if(v2.insert(c)) fd.stencil2.append(c); };
        // Add already-in-stencil0 cells first (preserve ordering)
        for (label c : fd.stencil0) fd.stencil2.append(c);
        // Then extend one more ring
        for (label c0 : fd.stencil0)
            for (label nb : cn[c0])
                add2(nb);

        // LSQ matrices
        fd.lsqK = computeLSQ(fd.stencilK, cc, fC);
        fd.lsq0 = computeLSQ(fd.stencil0, cc, fC);
        fd.lsq1 = computeLSQ(fd.stencil1, cc, fC);
        fd.lsq2 = computeLSQ(fd.stencil2, cc, fC);
    }

    Info << "TenoStencilCache: built stencils for " << nFaces
         << " internal faces" << endl;
}

TenoStencilCache::TenoStencilCache(const fvMesh& mesh)
:
    MeshObject<fvMesh, UpdateableMeshObject, TenoStencilCache>(mesh)
{
    buildStencils(mesh);
}

} // namespace Foam
