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
