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

// TENO6 - 6th-order Targeted ENO scheme (Fu et al. 2016, Table 3).
// Uses 4 candidate 3-point sub-stencils + 1 wide 6th-order stencil:
//   S_K : wide central stencil  (6th-order optimal)
//   S_0 : owner 1-ring          (left-biased)
//   S_1 : neighbour 1-ring      (right-biased)
//   S_2 : owner 2nd-ring        (far left, strongly upstream)
// Global discriminator: τ6 = |β_S2 - β_S1|
//   (leftmost vs rightmost sub-stencil, analogous to TENO5 τ5 = |β0 - β2|)
// Adaptive C_T scales with local smoothness (TENO-A, Fu 2018).
// Smoothness indicators computed per field component (vector-safe).
template<class Type>
class teno6Scheme
:
    public surfaceInterpolationScheme<Type>
{
    scalar C_T_;
    scalar eps_;
    int    q_;

    teno6Scheme(const teno6Scheme&) = delete;
    void operator=(const teno6Scheme&) = delete;

    scalar betaIndicatorCmpt(
        const GeometricField<Type, fvPatchField, volMesh>& vf,
        direction cmpt,
        const labelList& stencil,
        const scalarField& lsq,
        const vector& faceC) const;

    scalar reconstructFaceCmpt(
        const GeometricField<Type, fvPatchField, volMesh>& vf,
        direction cmpt,
        const labelList& stencil,
        const scalarField& lsq,
        const vector& faceC) const;

public:

    TypeName("teno6");

    teno6Scheme(const fvMesh& mesh, Istream& is)
    :   surfaceInterpolationScheme<Type>(mesh),
        C_T_(1e-7), eps_(1e-12), q_(6)
    {}

    teno6Scheme(const fvMesh& mesh, const surfaceScalarField& faceFlux, Istream& is)
    :   surfaceInterpolationScheme<Type>(mesh),
        C_T_(1e-7), eps_(1e-12), q_(6)
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
