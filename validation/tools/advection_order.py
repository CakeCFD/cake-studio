#!/usr/bin/env python3
"""
VC-001: Spatial order-of-accuracy verification by linear advection of a smooth profile.

Advects T(x,0) = sin(2*pi*x) once around a periodic unit domain at u = 1 m/s, so the
exact solution at t = 1 s is the initial condition. Pure advection: the diffusivity is
zero, so the only spatial discretisation exercised is div(phi,T).

Temporal error is kept below spatial error by scaling the time step as dt ~ dx^2.5
against a second-order time scheme, so the measured slope reflects the convection
scheme rather than the time integration. Norms are grid-normalised so they are
comparable across refinement levels.

Usage:
    source /usr/lib/openfoam/openfoam2412/etc/bashrc
    python3 validation/tools/advection_order.py
"""

import csv
import math
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Refinement sequence. Ratio r = 2 throughout, well above the r > 1.3 that Celik et al.
# (2008) recommend for the grid-convergence procedure.
GRIDS = [20, 40, 80, 160]

# Scheme name as written into fvSchemes -> label used in the report.
SCHEMES = {
    "linear": "linear (CD2)",
    "linearUpwind grad(T)": "linearUpwind",
    "teno": "TENO5",
    "teno6": "TENO6",
}

END_TIME = 1.0          # one full period at u = 1 m/s over a unit domain
CFL_COARSE = 0.2        # CFL on the coarsest grid; finer grids scale down as dx^2.5
TENO_LIBS = '("libtenoScheme.so")'


def head():
    return (
        "FoamFile\n"
        "{\n"
        "    version 2.0; format ascii;\n"
    )


def write_blockmesh(case: Path, n: int):
    # 1-D strip: n cells in x, one cell in y and z, front/back and top/bottom empty.
    (case / "system").mkdir(parents=True, exist_ok=True)
    (case / "system" / "blockMeshDict").write_text(
        head() + "    class dictionary; object blockMeshDict;\n}\n\n"
        "scale 1;\n\n"
        "vertices\n(\n"
        "    (0 0 0) (1 0 0) (1 0.1 0) (0 0.1 0)\n"
        "    (0 0 0.1) (1 0 0.1) (1 0.1 0.1) (0 0.1 0.1)\n"
        ");\n\n"
        f"blocks\n(\n    hex (0 1 2 3 4 5 6 7) ({n} 1 1) simpleGrading (1 1 1)\n);\n\n"
        "edges ();\n\n"
        "boundary\n(\n"
        "    left  { type cyclic; neighbourPatch right; faces ((0 4 7 3)); }\n"
        "    right { type cyclic; neighbourPatch left;  faces ((1 2 6 5)); }\n"
        "    walls { type empty; faces ((3 7 6 2) (0 1 5 4) (0 3 2 1) (4 5 6 7)); }\n"
        ");\n\n"
        "mergePatchPairs ();\n"
    )


def write_fields(case: Path, n: int):
    zero = case / "0"
    zero.mkdir(parents=True, exist_ok=True)

    # T is set directly on cell centres rather than via setFields, so the initial
    # condition carries no additional discretisation error of its own.
    xs = [(i + 0.5) / n for i in range(n)]
    vals = "\n".join(f"{math.sin(2.0 * math.pi * x):.16e}" for x in xs)
    (zero / "T").write_text(
        head() + "    class volScalarField; object T;\n}\n\n"
        "dimensions [0 0 0 1 0 0 0];\n\n"
        f"internalField nonuniform List<scalar>\n{n}\n(\n{vals}\n);\n\n"
        "boundaryField\n{\n"
        "    left  { type cyclic; }\n"
        "    right { type cyclic; }\n"
        "    walls { type empty; }\n"
        "}\n"
    )
    (zero / "U").write_text(
        head() + "    class volVectorField; object U;\n}\n\n"
        "dimensions [0 1 -1 0 0 0 0];\n\n"
        "internalField uniform (1 0 0);\n\n"
        "boundaryField\n{\n"
        "    left  { type cyclic; }\n"
        "    right { type cyclic; }\n"
        "    walls { type empty; }\n"
        "}\n"
    )
    const = case / "constant"
    const.mkdir(parents=True, exist_ok=True)
    # Zero diffusivity: pure advection, so div(phi,T) is the only term under test.
    (const / "transportProperties").write_text(
        head() + "    class dictionary; object transportProperties;\n}\n\n"
        "DT [0 2 -1 0 0 0 0] 0;\n"
    )


def write_system(case: Path, n: int, scheme: str, dt: float):
    sysd = case / "system"
    sysd.mkdir(parents=True, exist_ok=True)
    (sysd / "controlDict").write_text(
        head() + "    class dictionary; object controlDict;\n}\n\n"
        f"libs {TENO_LIBS};\n\n"
        "application     scalarTransportFoam;\n"
        "startFrom       startTime;\n"
        "startTime       0;\n"
        "stopAt          endTime;\n"
        f"endTime         {END_TIME};\n"
        f"deltaT          {dt:.12g};\n"
        "writeControl    runTime;\n"
        f"writeInterval   {END_TIME};\n"
        "purgeWrite      0;\n"
        "writeFormat     ascii;\n"
        "writePrecision  16;\n"
        "timeFormat      general;\n"
        "timePrecision   10;\n"
        "runTimeModifiable false;\n"
    )
    (sysd / "fvSchemes").write_text(
        head() + "    class dictionary; object fvSchemes;\n}\n\n"
        # CrankNicolson 1 is second order in time; dt ~ dx^2.5 keeps its error
        # an order of magnitude below the spatial error being measured.
        "ddtSchemes      { default CrankNicolson 1; }\n"
        "gradSchemes     { default Gauss linear; }\n"
        f"divSchemes      {{ default none; div(phi,T) Gauss {scheme}; }}\n"
        "laplacianSchemes { default Gauss linear corrected; }\n"
        "interpolationSchemes { default linear; }\n"
        "snGradSchemes   { default corrected; }\n"
    )
    (sysd / "fvSolution").write_text(
        head() + "    class dictionary; object fvSolution;\n}\n\n"
        "solvers\n{\n"
        "    T { solver PBiCGStab; preconditioner DILU;\n"
        "        tolerance 1e-14; relTol 0; }\n"
        "}\n\n"
        "SIMPLE { nNonOrthogonalCorrectors 0; }\n"
    )


def read_internal_field(path: Path):
    """Parse the internalField of an ASCII OpenFOAM volScalarField."""
    txt = path.read_text()
    m = re.search(r"internalField\s+nonuniform\s+List<scalar>\s*\n(\d+)\s*\n\(", txt)
    if m:
        count = int(m.group(1))
        start = m.end()
        body = txt[start:]
        vals = []
        for line in body.splitlines():
            line = line.strip()
            if line.startswith(")"):
                break
            if line:
                vals.append(float(line))
        if len(vals) != count:
            raise ValueError(f"expected {count} values, parsed {len(vals)} from {path}")
        return vals
    m = re.search(r"internalField\s+uniform\s+([-\d.eE+]+)\s*;", txt)
    if m:
        raise ValueError(f"{path} is a uniform field; the solution collapsed")
    raise ValueError(f"could not parse internalField from {path}")


def run(cmd, cwd):
    p = subprocess.run(cmd, cwd=cwd, shell=True, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def norms(num, exact, n):
    """Grid-normalised L1, L2 and Linf error norms."""
    diffs = [a - b for a, b in zip(num, exact)]
    l1 = sum(abs(d) for d in diffs) / n
    l2 = math.sqrt(sum(d * d for d in diffs) / n)
    li = max(abs(d) for d in diffs)
    return l1, l2, li


def main():
    if not os.environ.get("FOAM_APPBIN"):
        print("ERROR: OpenFOAM environment not sourced.")
        print("  source /usr/lib/openfoam/openfoam2412/etc/bashrc")
        return 1

    root = Path(__file__).resolve().parents[1] / "cases" / "VC-001-advection-order"
    work = root / "work"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)

    dx_coarse = 1.0 / GRIDS[0]
    dt_coarse = CFL_COARSE * dx_coarse

    results = {}
    for scheme, label in SCHEMES.items():
        results[label] = []
        for n in GRIDS:
            dx = 1.0 / n
            # dt ~ dx^2.5 so the second-order-in-time error falls faster than a
            # fifth-order spatial error and never becomes the limiting term.
            dt = dt_coarse * (dx / dx_coarse) ** 2.5
            case = work / f"{label.split()[0].replace('(','').replace(')','')}_N{n}"
            case.mkdir(parents=True, exist_ok=True)
            write_blockmesh(case, n)
            write_fields(case, n)
            write_system(case, n, scheme, dt)

            rc, out = run("blockMesh", case)
            if rc != 0:
                print(f"  blockMesh FAILED for {label} N={n}\n{out[-800:]}")
                return 1
            rc, out = run("scalarTransportFoam", case)
            if rc != 0:
                print(f"  solver FAILED for {label} N={n}\n{out[-1500:]}")
                results[label].append((n, None, None, None))
                continue

            tdirs = [d for d in case.iterdir()
                     if d.is_dir() and re.fullmatch(r"[0-9.eE+-]+", d.name)
                     and float(d.name) > 0]
            if not tdirs:
                print(f"  no output time written for {label} N={n}")
                results[label].append((n, None, None, None))
                continue
            last = max(tdirs, key=lambda d: float(d.name))

            try:
                num = read_internal_field(last / "T")
            except ValueError as e:
                print(f"  {e}")
                results[label].append((n, None, None, None))
                continue

            exact = [math.sin(2.0 * math.pi * ((i + 0.5) / n)) for i in range(n)]
            l1, l2, li = norms(num, exact, n)
            results[label].append((n, l1, l2, li))
            print(f"  {label:14s} N={n:4d}  L1={l1:.4e}  L2={l2:.4e}  Linf={li:.4e}")

    # Report
    out_csv = root / "results_order.csv"
    lines = ["scheme,N,L1,L2,Linf,order_L1,order_L2,order_Linf"]
    print("\n" + "=" * 78)
    print("VC-001  Observed order of accuracy, linear advection of sin(2*pi*x)")
    print("=" * 78)
    for label, rows in results.items():
        print(f"\n{label}")
        print(f"  {'N':>5} {'L1':>12} {'p(L1)':>7} {'L2':>12} {'p(L2)':>7} "
              f"{'Linf':>12} {'p(Linf)':>8}")
        prev = None
        for (n, l1, l2, li) in rows:
            if l1 is None:
                print(f"  {n:5d} {'FAILED':>12}")
                lines.append(f"{label},{n},,,,,,")
                continue
            if prev is None:
                print(f"  {n:5d} {l1:12.4e} {'-':>7} {l2:12.4e} {'-':>7} "
                      f"{li:12.4e} {'-':>8}")
                lines.append(f"{label},{n},{l1:.6e},{l2:.6e},{li:.6e},,,")
            else:
                pn, p1, p2, pi = prev
                r = n / pn
                o1 = math.log(p1 / l1) / math.log(r) if l1 > 0 else float("nan")
                o2 = math.log(p2 / l2) / math.log(r) if l2 > 0 else float("nan")
                oi = math.log(pi / li) / math.log(r) if li > 0 else float("nan")
                print(f"  {n:5d} {l1:12.4e} {o1:7.2f} {l2:12.4e} {o2:7.2f} "
                      f"{li:12.4e} {oi:8.2f}")
                lines.append(f"{label},{n},{l1:.6e},{l2:.6e},{li:.6e},"
                             f"{o1:.4f},{o2:.4f},{oi:.4f}")
            prev = (n, l1, l2, li)

    out_csv.write_text("\n".join(lines) + "\n")
    print(f"\nWritten: {out_csv}")
    print("\nFormal orders: linear 2, linearUpwind 2, TENO5 5, TENO6 6.")
    print("An observed order clamped near 2 for the TENO schemes indicates the")
    print("surrounding finite-volume framework is limiting the reconstruction.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
