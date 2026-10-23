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

namespace
{

void jacobiEigen3(scalar A[3][3], scalar V[3][3])
{
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            V[r][c] = (r == c) ? 1.0 : 0.0;

    for (int sweep = 0; sweep < 50; ++sweep)
    {
        const scalar off =
            std::abs(A[0][1]) + std::abs(A[0][2]) + std::abs(A[1][2]);
        const scalar dia =
            std::abs(A[0][0]) + std::abs(A[1][1]) + std::abs(A[2][2]);
        if (off <= 1e-15*dia || off < VSMALL) break;

        for (int p = 0; p < 2; ++p)
        {
            for (int q = p+1; q < 3; ++q)
            {
                if (std::abs(A[p][q]) < VSMALL) continue;

                const scalar theta = (A[q][q] - A[p][p])/(2.0*A[p][q]);
                const scalar t =
                    (theta >= 0 ? 1.0 : -1.0)
                   /(std::abs(theta) + std::sqrt(theta*theta + 1.0));
                const scalar c = 1.0/std::sqrt(t*t + 1.0);
                const scalar s = t*c;

                for (int k = 0; k < 3; ++k)
                {
                    const scalar akp = A[k][p], akq = A[k][q];
                    A[k][p] = c*akp - s*akq;
                    A[k][q] = s*akp + c*akq;
                }
                for (int k = 0; k < 3; ++k)
                {
                    const scalar apk = A[p][k], aqk = A[q][k];
                    A[p][k] = c*apk - s*aqk;
                    A[q][k] = s*apk + c*aqk;
                }
                for (int k = 0; k < 3; ++k)
                {
                    const scalar vkp = V[k][p], vkq = V[k][q];
                    V[k][p] = c*vkp - s*vkq;
                    V[k][q] = s*vkp + c*vkq;
                }
            }
        }
    }
}

}

scalarField TenoStencilCache::computeLSQ(
    const labelList& stencil,
    const vectorField& cc,
    const vector& faceC) const
{
    const label n = stencil.size();
    scalarField pinv(4*n, 0.0);
    if (n == 0) return pinv;

    vector dbar = Zero;
    for (label i = 0; i < n; ++i) dbar += cc[stencil[i]] - faceC;
    dbar /= scalar(n);

    scalar M[3][3] = {};
    for (label i = 0; i < n; ++i)
    {
        const vector e = cc[stencil[i]] - faceC - dbar;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                M[r][c] += e[r]*e[c];
    }

    scalar V[3][3];
    jacobiEigen3(M, V);

    const scalar lamMax =
        max(std::abs(M[0][0]), max(std::abs(M[1][1]), std::abs(M[2][2])));
    const scalar tol = 1e-12*lamMax;

    scalar Minv[3][3] = {};
    if (lamMax > VSMALL)
    {
        for (int k = 0; k < 3; ++k)
        {
            const scalar lam = M[k][k];
            if (lam <= tol) continue;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    Minv[r][c] += V[r][k]*V[c][k]/lam;
        }
    }

    for (label j = 0; j < n; ++j)
    {
        const vector e = cc[stencil[j]] - faceC - dbar;
        vector w = Zero;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                w[r] += Minv[r][c]*e[c];

        pinv[0*n + j] = 1.0/scalar(n) - (dbar & w);
        pinv[1*n + j] = w.x();
        pinv[2*n + j] = w.y();
        pinv[3*n + j] = w.z();
    }

    return pinv;
}

void TenoStencilCache::buildStencils(const fvMesh& mesh)
{
    const labelUList& own = mesh.owner();
    const labelUList& nei = mesh.neighbour();
    const vectorField& cc = mesh.cellCentres();
    const vectorField& fc = mesh.faceCentres();
    const labelListList& cn = mesh.cellCells();

    label nFaces = own.size();

    faceData_.clear();
    faceData_.resize(nFaces);

    forAll(own, facei)
    {
        label o = own[facei], n = nei[facei];
        vector fC = fc[facei];
        auto& fd = faceData_[facei];
        fd.faceCentre = fC;

        labelHashSet vK;
        auto addK = [&](label c){ if(vK.insert(c)) fd.stencilK.append(c); };
        addK(o); addK(n);
        for (label nb : cn[o]) addK(nb);
        for (label nb : cn[n]) addK(nb);

        labelHashSet v0;
        auto add0 = [&](label c){ if(v0.insert(c)) fd.stencil0.append(c); };
        add0(o);
        for (label nb : cn[o]) add0(nb);

        labelHashSet v1;
        auto add1 = [&](label c){ if(v1.insert(c)) fd.stencil1.append(c); };
        add1(n);
        for (label nb : cn[n]) add1(nb);

        labelHashSet v2(v0);
        auto add2 = [&](label c){ if(v2.insert(c)) fd.stencil2.append(c); };
        for (label c : fd.stencil0) fd.stencil2.append(c);
        for (label c0 : fd.stencil0)
            for (label nb : cn[c0])
                add2(nb);

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

}
