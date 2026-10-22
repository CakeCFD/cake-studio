#!/usr/bin/env bash
# Packages build/ and libtenoScheme.so into cakestudio-<version>-ubuntu24.04-x86_64.tar.gz.
# Usage: package.sh <version> [output-dir]
set -euo pipefail

VERSION="${1:?usage: package.sh <version> [output-dir]}"
OUT="${2:-dist}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NAME="cakestudio-${VERSION}-ubuntu24.04-x86_64"
TENO_LIB="${FOAM_USER_LIBBIN:-}/libtenoScheme.so"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
pkg="$stage/$NAME"
mkdir -p "$pkg/bin" "$pkg/lib" "$pkg/share"

install -m 755 "$ROOT/build/src/gui/CFDStudio" "$pkg/bin/CFDStudio"
install -m 755 "$ROOT/build/src/cli/cfdstudio" "$pkg/bin/cfdstudio"
strip "$pkg/bin/CFDStudio" "$pkg/bin/cfdstudio"
if [ -f "$TENO_LIB" ]; then
    install -m 755 "$TENO_LIB" "$pkg/lib/libtenoScheme.so"
else
    echo "warning: $TENO_LIB not found, packaging without the TENO library" >&2
fi
install -m 644 "$ROOT/packaging/cakecfd.svg" "$pkg/share/cakestudio.svg"
install -m 644 "$ROOT/LICENSE" "$pkg/LICENSE"
install -m 644 "$ROOT/packaging/linux/install.sh" "$pkg/install.sh"
echo "$VERSION" > "$pkg/VERSION"

mkdir -p "$OUT"
tar -C "$stage" -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "$OUT/$NAME.tar.gz"
