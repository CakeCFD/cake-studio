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
#include "surfaceInterpolationScheme.H"
#include "volFields.H"
#include "fvMesh.H"
#include "scalarField.H"

namespace Foam
{

// TENO scheme using the TenoStencilCache (LSQ-based, cache-friendly).
// Implements the two-level TENO reconstruction of Fu et al. 2016:
//   1. Try wide central stencil S_K (smooth → optimal accuracy).
//   2. Fall back to per-component TENO blend of S_0 / S_1 (near discontinuities).
// Smoothness indicators computed per field component (fixes vector-field bug).
// Adaptive C_T scales the cutoff threshold with local flow smoothness (Fu 2018).
template<class Type>
class tenoScheme
:
    public surfaceInterpolationScheme<Type>
{
    scalar C_T_;   // base TENO threshold
    scalar eps_;   // regularisation for smoothness indicators
    int    q_;     // exponent in scale-separation formula

    tenoScheme(const tenoScheme&) = delete;
    void operator=(const tenoScheme&) = delete;

    // Per-component smoothness indicator (fixes magSqr bug for vector fields)
    scalar betaIndicatorCmpt(
        const GeometricField<Type, fvPatchField, volMesh>& vf,
        direction cmpt,
        const labelList& stencil,
        const scalarField& lsq,
        const vector& faceC) const;

    // Per-component face reconstruction
    scalar reconstructFaceCmpt(
        const GeometricField<Type, fvPatchField, volMesh>& vf,
        direction cmpt,
        const labelList& stencil,
        const scalarField& lsq,
        const vector& faceC) const;

public:

    TypeName("teno");

    tenoScheme(const fvMesh& mesh, Istream& is)
    :   surfaceInterpolationScheme<Type>(mesh),
        C_T_(1e-6), eps_(1e-12), q_(6)
    {}

    tenoScheme(const fvMesh& mesh, const surfaceScalarField& faceFlux, Istream& is)
    :   surfaceInterpolationScheme<Type>(mesh),
        C_T_(1e-6), eps_(1e-12), q_(6)
    {}

    virtual tmp<surfaceScalarField> weights(
        const GeometricField<Type, fvPatchField, volMesh>&) const
    {
        return this->mesh().surfaceInterpolation::weights();
    }

    virtual bool corrected() const { return true; }

    virtual tmp<GeometricField<Type, fvsPatchField, surfaceMesh>>
    correction(const GeometricField<Type, fvPatchField, volMesh>& vf) const;
};

} // namespace Foam
