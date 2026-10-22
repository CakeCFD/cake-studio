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

#include "tenoScheme.H"
#include "TenoStencilCache.H"
#include "fvMesh.H"
#include "volFields.H"
#include "surfaceFields.H"
#include "surfaceInterpolationScheme.H"

namespace Foam
{

// Per-component smoothness indicator
// Fixes the original magSqr() bug, so vector/tensor fields now correctly
// operate on each scalar component independently.
template<class Type>
scalar tenoScheme<Type>::betaIndicatorCmpt(
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
        scalar uj = component(vf[stencil[j]], cmpt);   // per-component extraction
        bias += lsq[0*n + j] * uj;
        gx   += lsq[1*n + j] * uj;
        gy   += lsq[2*n + j] * uj;
        gz   += lsq[3*n + j] * uj;
    }

    // First-order smoothness |∇u|²
    scalar beta1 = gx*gx + gy*gy + gz*gz;

    // Second-order smoothness from the residual variance of the linear fit
    const vectorField& cc = this->mesh().cellCentres();
    scalar beta2 = 0.0;
    for (label j = 0; j < n; ++j)
    {
        scalar uj = component(vf[stencil[j]], cmpt);
        vector d  = cc[stencil[j]] - faceC;
        scalar predicted = bias + gx*d.x() + gy*d.y() + gz*d.z();
        scalar res = uj - predicted;
        beta2 += res*res;
    }

    return beta1 + beta2 / max(n - 1, 1);
}

// Per-component face reconstruction
template<class Type>
scalar tenoScheme<Type>::reconstructFaceCmpt(
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

// TENO correction (per-component, adaptive C_T)
template<class Type>
tmp<GeometricField<Type, fvsPatchField, surfaceMesh>>
tenoScheme<Type>::correction(
    const GeometricField<Type, fvPatchField, volMesh>& vf) const
{
    const fvMesh& mesh = this->mesh();
    const TenoStencilCache& cache = TenoStencilCache::New(mesh);

    auto tlinear = surfaceInterpolationScheme<Type>::interpolate(
        vf, mesh.surfaceInterpolation::weights());

    auto tcorr = tmp<GeometricField<Type, fvsPatchField, surfaceMesh>>::New(
        IOobject("teno::correction("+vf.name()+")", mesh.time().timeName(), mesh),
        mesh, dimensioned<Type>(vf.dimensions(), pTraits<Type>::zero));

    auto& corr = tcorr.ref();

    forAll(mesh.owner(), facei)
    {
        const FaceStencilData& fd = cache.faceData(facei);
        const vector& fC = fd.faceCentre;

        Type tv = pTraits<Type>::zero;

        // Per-component TENO
        for (direction cmpt = 0; cmpt < pTraits<Type>::nComponents; ++cmpt)
        {
            const scalar bK = betaIndicatorCmpt(vf, cmpt, fd.stencilK, fd.lsqK, fC);
            const scalar b0 = betaIndicatorCmpt(vf, cmpt, fd.stencil0, fd.lsq0, fC);
            const scalar b1 = betaIndicatorCmpt(vf, cmpt, fd.stencil1, fd.lsq1, fC);

            // Global smoothness discriminator τ = max pairwise beta spread (proxy for Fu τ5)
            // Max spread is used because stencil ordering isn't fixed in 3D.
            const scalar tau = max(mag(bK - b0), max(mag(b0 - b1), mag(bK - b1))) + eps_;

            // Scale separation (Fu 2016 Eq.23) χ_k = (1 + τ/(β_k+ε))^q
            // 1+τ/β guarantees χ_k ≥ 1, which prevents all-zero collapse
            // that would set face values to 0 and blow up pressure-velocity coupling.
            const scalar gK = Foam::pow(1.0 + tau/(eps_ + bK), scalar(q_));
            const scalar g0 = Foam::pow(1.0 + tau/(eps_ + b0), scalar(q_));
            const scalar g1 = Foam::pow(1.0 + tau/(eps_ + b1), scalar(q_));
            const scalar gS = gK + g0 + g1 + SMALL;

            // Adaptive C_T (Fu 2018 TENO-A)
            const scalar betaMin  = min(bK, min(b0, b1));
            const scalar sigma    = tau / (eps_ + betaMin);
            const scalar C_T_loc  = C_T_ * (1.0 + 10.0 * sigma / (1.0 + sigma));

            // Hard cutoff δ_k (Fu 2016 Eq.24-25)
            const scalar deltaK = (gK/gS > C_T_loc) ? 1.0 : 0.0;
            const scalar delta0 = (g0/gS > C_T_loc) ? 1.0 : 0.0;
            const scalar delta1 = (g1/gS > C_T_loc) ? 1.0 : 0.0;

            // Two-level reconstruction (Fu 2016 Eq.26-28)
            //   Wide stencil K smooth → fall back to conservative linear (zero correction)
            //     so the SIMPLE pressure-velocity coupling stays stable over many iterations.
            //     LSQ(K) ≠ mesh-weighted-linear even in smooth regions, and the accumulated
            //     non-conservative correction diverges in steady-state SIMPLE.
            //   K non-smooth → TENO blend of sub-stencils, clamped to owner-neighbour range.
            //   All sub-stencils excluded → owner-side reconstruction, clamped.
            scalar tvCmpt;
            if (deltaK > 0.5)
            {
                // Smooth flow uses the mesh-weighted linear value with zero correction,
                // scheme is conservative, SIMPLE stays stable.
                tvCmpt = component(tlinear.ref()[facei], cmpt);
            }
            else
            {
                const scalar r0 = reconstructFaceCmpt(vf, cmpt, fd.stencil0, fd.lsq0, fC);
                const scalar r1 = reconstructFaceCmpt(vf, cmpt, fd.stencil1, fd.lsq1, fC);
                const scalar dS = delta0 + delta1;
                scalar raw = (dS < SMALL) ? r0 : (delta0/dS)*r0 + (delta1/dS)*r1;

                // Clamp to the owner-neighbour range to prevent LSQ extrapolation blow-up.
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

makeSurfaceInterpolationScheme(tenoScheme)

} // namespace Foam
