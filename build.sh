set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"
LOG="$BUILD_DIR/build.log"
TENO_DIR="$ROOT/src/solver/schemes/TENO"

BUILD_TYPE="Release"
JOBS="$(nproc 2>/dev/null || echo 4)"
DO_CLEAN=0
DO_APP=1
DO_TENO=1
VERBOSE=0

if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
    BOLD=$'\033[1m'; DIM=$'\033[2m'; RED=$'\033[31m'
    GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; RESET=$'\033[0m'
else
    BOLD=""; DIM=""; RED=""; GREEN=""; YELLOW=""; BLUE=""; RESET=""
fi

step()  { printf '%s==>%s %s%s%s\n' "$BLUE" "$RESET" "$BOLD" "$1" "$RESET"; }
ok()    { printf '  %s+%s %s\n' "$GREEN" "$RESET" "$1"; }
warn()  { printf '  %s!%s %s\n' "$YELLOW" "$RESET" "$1"; }
fail()  { printf '  %sx%s %s\n' "$RED" "$RESET" "$1"; }
note()  { printf '    %s%s%s\n' "$DIM" "$1" "$RESET"; }

die() {
    fail "$1"
    [[ -s "$LOG" ]] && note "last lines of $LOG:" && tail -n 15 "$LOG" | sed 's/^/    /'
    exit 1
}

run() {
    if (( VERBOSE )); then
        "$@" 2>&1 | tee -a "$LOG"
        return "${PIPESTATUS[0]}"
    fi
    "$@" >>"$LOG" 2>&1
}

usage() {
    cat <<'USAGE'
Builds the desktop application and the TENO scheme library.

  ./build.sh                 build everything
  ./build.sh --clean         remove build artefacts first
  ./build.sh --debug         build with debug symbols
  ./build.sh --app-only      skip the TENO library
  ./build.sh --teno-only     skip the application
  ./build.sh --verbose       show full compiler output
  ./build.sh --jobs N        parallel jobs (default all cores)
USAGE
    exit 0
}

while (( $# )); do
    case "$1" in
        --clean)     DO_CLEAN=1 ;;
        --debug)     BUILD_TYPE="Debug" ;;
        --app-only)  DO_TENO=0 ;;
        --teno-only) DO_APP=0 ;;
        --verbose|-v) VERBOSE=1 ;;
        --jobs|-j)   JOBS="${2:?--jobs needs a number}"; shift ;;
        --help|-h)   usage ;;
        *) fail "unknown option: $1"; echo "try --help"; exit 2 ;;
    esac
    shift
done

banner() {
    local cols=0
    [[ -t 1 ]] && cols="$(tput cols 2>/dev/null || echo 0)"

    if [[ ! -t 1 || "$cols" -lt 32 ]]; then
        printf '\nCakeCFD build (%s, %s jobs)\n\n' "$BUILD_TYPE" "$JOBS"
        return
    fi

    printf '\n%s' "$BLUE$BOLD"
    cat <<'ART'
 █▀▀ ▄▀▄ █ █ █▀▀ █▀▀ █▀▀ █▀▄
 █   █▀█ █▀▄ █▀▀ █   █▀▀ █ █
 ▀▀▀ ▀ ▀ ▀ ▀ ▀▀▀ ▀▀▀ ▀   ▀▀
ART
    printf '%s\n' "$RESET"
}

banner

step "Checking environment"

MISSING=()
for tool in cmake g++ make; do
    command -v "$tool" >/dev/null 2>&1 || MISSING+=("$tool")
done
if (( ${#MISSING[@]} )); then
    fail "missing build tools: ${MISSING[*]}"
    note "sudo apt install build-essential cmake"
    exit 1
fi
ok "build tools present"

if (( DO_TENO )); then
    if [[ -z "${FOAM_USER_LIBBIN:-}" ]]; then
        FOAM_BASHRC="/usr/lib/openfoam/openfoam2412/etc/bashrc"
        if [[ -f "$FOAM_BASHRC" ]]; then
            warn "OpenFOAM environment not sourced; sourcing it for this build"
            set +u
            # shellcheck disable=SC1090
            source "$FOAM_BASHRC" >/dev/null 2>&1
            set -u
        fi
    fi
    if [[ -z "${FOAM_USER_LIBBIN:-}" ]]; then
        fail "OpenFOAM environment not found"
        note "source /usr/lib/openfoam/openfoam2412/etc/bashrc"
        note "or build without the scheme library:  ./build.sh --app-only"
        exit 1
    fi
    ok "OpenFOAM ${WM_PROJECT_VERSION:-} environment active"
fi

mkdir -p "$BUILD_DIR"
: >"$LOG"

if (( DO_CLEAN )); then
    step "Cleaning"
    rm -rf "${BUILD_DIR:?}"/*
    : >"$LOG"
    if (( DO_TENO )) && [[ -d "$TENO_DIR" ]]; then
        ( cd "$TENO_DIR" && run wclean ) || true
    fi
    ok "build artefacts removed"
fi

if (( DO_APP )); then
    step "Configuring"
    run cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        || die "cmake configure failed (Qt5, VTK 9 or OpenCASCADE may be missing)"
    ok "configured"

    step "Building application"
    run cmake --build "$BUILD_DIR" -j "$JOBS" \
        || die "application build failed"
    ok "CFDStudio built"
fi

if (( DO_TENO )); then
    step "Building TENO scheme library"
    if [[ ! -d "$TENO_DIR" ]]; then
        die "scheme sources not found at $TENO_DIR"
    fi
    ( cd "$TENO_DIR" && run wmake libso ) \
        || die "TENO library build failed"

    LIB="$FOAM_USER_LIBBIN/libtenoScheme.so"
    [[ -f "$LIB" ]] || die "expected library not produced: $LIB"

    REGISTERED="$(strings "$LIB" 2>/dev/null | grep -xE 'teno|teno6' | sort -u | tr '\n' ' ')"
    if [[ -n "$REGISTERED" ]]; then
        ok "libtenoScheme.so built, registers: ${REGISTERED% }"
    else
        warn "libtenoScheme.so built but no scheme names found in it"
    fi
fi

printf '\n%sDone.%s\n' "$GREEN$BOLD" "$RESET"
if (( DO_APP )); then
    printf '  run:  %s%s/run.sh%s\n' "$BOLD" "$ROOT" "$RESET"
fi
if (( DO_TENO )); then
    printf '  use:  %sdiv(phi,U) Gauss teno;%s  in system/fvSchemes\n' "$BOLD" "$RESET"
fi
printf '  log:  %s%s\n\n' "$DIM$LOG" "$RESET"
