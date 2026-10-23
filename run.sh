#!/usr/bin/env bash
#
# Launch Cake Studio.
#
# Mesa resolves its Gallium driver while libGL is being loaded, which happens
# before main() runs, so the driver cannot be selected from inside the program.
# It has to be in the environment before exec. Without this WSLg falls back to
# llvmpipe, a CPU rasteriser that reports "Accelerated: no" and makes the whole
# interface stutter, not only the 3D viewport.
#
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="$ROOT/build/src/gui/CFDStudio"

if [[ ! -x "$APP" ]]; then
    echo "Cake Studio is not built yet. Run ./build.sh first." >&2
    exit 1
fi

# Only pin the driver on WSL, where /dev/dxg exposes the host GPU, and only when
# the user has not chosen a driver themselves.
if [[ -e /dev/dxg && -z "${GALLIUM_DRIVER:-}" ]]; then
    export GALLIUM_DRIVER=d3d12
fi

exec "$APP" "$@"
