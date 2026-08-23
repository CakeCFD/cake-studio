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

#include "teno6Scheme.H"
#include "TenoStencilCache.H"
#include "fvMesh.H"
#include "volFields.H"
#include "surfaceFields.H"
#include "surfaceInterpolationScheme.H"

namespace Foam
{

// Per-component smoothness indicator
template<class Type>
scalar teno6Scheme<Type>::betaIndicatorCmpt(
    const GeometricField<Type, fvPatchField, volMesh>& vf,
    direction cmpt,
    const labelList& stencil,
    const scalarField& lsq,
    const vector& faceC) const
{
    label n = stencil.size();
    if (n < 2) return 0.0;

    scalar gx = 0, gy = 0, gz = 0, bias = 0;
    for (label j = 0; j < n; ++j)
    {
        scalar uj = component(vf[stencil[j]], cmpt);
        bias += lsq[0*n + j] * uj;
        gx   += lsq[1*n + j] * uj;
        gy   += lsq[2*n + j] * uj;
        gz   += lsq[3*n + j] * uj;
    }

    scalar beta1 = gx*gx + gy*gy + gz*gz;

    const vectorField& cc = this->mesh().cellCentres();
    scalar beta2 = 0.0;
    for (label j = 0; j < n; ++j)
    {
        scalar uj = component(vf[stencil[j]], cmpt);
        vector d  = cc[stencil[j]] - faceC;
        scalar res = uj - (bias + gx*d.x() + gy*d.y() + gz*d.z());
        beta2 += res*res;
    }

    return beta1 + beta2 / max(n - 1, 1);
}

// Per-component face reconstruction
template<class Type>
scalar teno6Scheme<Type>::reconstructFaceCmpt(
    const GeometricField<Type, fvPatchField, volMesh>& vf,
    direction cmpt,
    const labelList& stencil,
    const scalarField& lsq,
    const vector& /*faceC*/) const
{
    label n = stencil.size();
    if (n == 0) return 0.0;
    if (n == 1) return component(vf[stencil[0]], cmpt);

    scalar result = 0.0;
    for (label j = 0; j < n; ++j)
        result += lsq[0*n + j] * component(vf[stencil[j]], cmpt);
    return result;
}

// TENO6 correction
template<class Type>
tmp<GeometricField<Type, fvsPatchField, surfaceMesh>>
teno6Scheme<Type>::correction(
    const GeometricField<Type, fvPatchField, volMesh>& vf) const
{
    const fvMesh& mesh = this->mesh();
    const TenoStencilCache& cache = TenoStencilCache::New(mesh);

    auto tlinear = surfaceInterpolationScheme<Type>::interpolate(
        vf, mesh.surfaceInterpolation::weights());

    auto tcorr = tmp<GeometricField<Type, fvsPatchField, surfaceMesh>>::New(
        IOobject("teno6::correction("+vf.name()+")", mesh.time().timeName(), mesh),
        mesh, dimensioned<Type>(vf.dimensions(), pTraits<Type>::zero));

    auto& corr = tcorr.ref();

    forAll(mesh.owner(), facei)
    {
        const FaceStencilData& fd = cache.faceData(facei);
        const vector& fC = fd.faceCentre;

        Type tv = pTraits<Type>::zero;

        for (direction cmpt = 0; cmpt < pTraits<Type>::nComponents; ++cmpt)
        {
            // Four candidate sub-stencils
            const scalar bK = betaIndicatorCmpt(vf, cmpt, fd.stencilK, fd.lsqK, fC);
            const scalar b0 = betaIndicatorCmpt(vf, cmpt, fd.stencil0, fd.lsq0, fC);
            const scalar b1 = betaIndicatorCmpt(vf, cmpt, fd.stencil1, fd.lsq1, fC);
            const scalar b2 = betaIndicatorCmpt(vf, cmpt, fd.stencil2, fd.lsq2, fC);

            // Global smoothness discriminator: max pairwise beta spread across all stencils.
            // Stencil2 is a 2nd-ring (wide area) - its beta can be 100x larger than
            // the 1-ring betas, making |b2-b1| blow up tau6 and overflow (1+tau/b)^q
            // to Inf. Max-pairwise keeps tau in proportion to the actual local spread.
            const scalar tau6 =
                Foam::max(Foam::max(Foam::max(Foam::max(Foam::max(
                    Foam::mag(bK-b0),
                    Foam::mag(bK-b1)),
                    Foam::mag(bK-b2)),
                    Foam::mag(b0-b1)),
                    Foam::mag(b0-b2)),
                    Foam::mag(b1-b2)) + eps_;

            // Cap ratio before exponentiation: (1+1e20)^6 ~ 1e120, within double range.
            // Without cap, stencil2's large beta → tau/b_k → Inf → g = Inf → NaN comparisons
            // → deltaK always 0 → smooth-flow guard never fires → divergence.
            auto safePow = [&](scalar b) -> scalar {
                return Foam::pow(1.0 + min(tau6/(eps_ + b), scalar(1e20)), scalar(q_));
            };
            const scalar gK = safePow(bK);
            const scalar g0 = safePow(b0);
            const scalar g1 = safePow(b1);
            const scalar g2 = safePow(b2);
            const scalar gS = gK + g0 + g1 + g2 + SMALL;

            // Adaptive C_T: raise threshold near discontinuities
            const scalar betaMin = min(bK, min(b0, min(b1, b2)));
            const scalar betaMax = max(bK, max(b0, max(b1, b2)));
            const scalar sigma   = (betaMax - betaMin) / (eps_ + betaMin);
            const scalar C_T_loc = C_T_ * (1.0 + 10.0 * sigma / (1.0 + sigma));

            // Binary cutoffs - isfinite guard catches any residual NaN from overflow.
            // Note: g2 / gS informs tau but stencil2 is NOT used for reconstruction
            // (see below) - its 2nd-ring LSQ is too wide for stable corrections in SIMPLE.
            auto cutoff = [&](scalar g) -> scalar {
                scalar r = g/gS;
                return (std::isfinite(r) && r > C_T_loc) ? 1.0 : 0.0;
            };
            const scalar deltaK = cutoff(gK);
            const scalar delta0 = cutoff(g0);
            const scalar delta1 = cutoff(g1);

            // Reconstruction - stencil2 used ONLY for tau (wider context for shock detection),
            // never for the actual face value. Its 2nd-ring LSQ produces non-conservative
            // corrections that accumulate in steady-state SIMPLE and cause divergence.
            // Blend is S_0 / S_1 only (same as tenoScheme), but with 4-stencil tau.
            scalar tvCmpt;
            if (deltaK > 0.5)
            {
                tvCmpt = component(tlinear.ref()[facei], cmpt);
            }
            else
            {
                const scalar r0 = reconstructFaceCmpt(vf, cmpt, fd.stencil0, fd.lsq0, fC);
                const scalar r1 = reconstructFaceCmpt(vf, cmpt, fd.stencil1, fd.lsq1, fC);
                const scalar dS = delta0 + delta1;
                scalar raw = (dS < SMALL) ? r0
                           : (delta0/dS)*r0 + (delta1/dS)*r1;

                // Clamp to owner-neighbour range: prevents LSQ extrapolation blow-up.
                const label o = mesh.owner()[facei];
                const label n = mesh.neighbour()[facei];
                const scalar vO = component(vf[o], cmpt);
                const scalar vN = component(vf[n], cmpt);
                tvCmpt = max(min(vO, vN), min(max(vO, vN), raw));
            }

            setComponent(tv, cmpt) = tvCmpt;
        }

        corr[facei] = tv - tlinear.ref()[facei];
    }

    return tcorr;
}

makeSurfaceInterpolationScheme(teno6Scheme)

} // namespace Foam
