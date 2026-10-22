set -uo pipefail

REPO="CakeCFD/cake-studio"
OF_VERSION="2412"
OF_ROOT="/usr/lib/openfoam/openfoam${OF_VERSION}"
APP_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/cakestudio"
BIN_DIR="$HOME/.local/bin"

MODE="all"
VERSION=""
TARBALL=""
WITH_TENO=1

usage() {
    cat <<'USAGE'
Usage: install.sh [--deps | --app | --uninstall] [--version X.Y.Z] [--tarball PATH] [--no-teno]
  (no mode)      install packages (with sudo) and CakeStudio
  --deps         system packages only, run as root
  --app          CakeStudio only, run as your user
  --uninstall    remove CakeStudio, keep packages
  --version      release to install (default: latest)
  --tarball      install from a local package
  --no-teno      skip the TENO scheme library
USAGE
}

step()     { echo "@@STEP $*"; }
progress() { echo "@@PROGRESS $1"; }
fail()     { echo "@@ERROR $*" >&2; echo "Error: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --deps)      MODE="deps" ;;
        --app)       MODE="app" ;;
        --uninstall) MODE="uninstall" ;;
        --version)   VERSION="${2:-}"; shift ;;
        --tarball)   TARBALL="${2:-}"; shift ;;
        --no-teno)   WITH_TENO=0 ;;
        -h|--help)   usage; exit 0 ;;
        *)           fail "Unknown option: $1" ;;
    esac
    shift
done

is_wsl() { grep -qi microsoft /proc/version 2>/dev/null; }

check_platform() {
    [ "$(uname -m)" = "x86_64" ] || fail "CakeStudio needs a 64-bit x86 machine (found $(uname -m))."
    [ -r /etc/os-release ] || fail "Cannot read /etc/os-release."
    # Subshell because os-release defines VERSION, which would clobber ours
    local os; os="$(. /etc/os-release; echo "${ID:-}|${VERSION_ID:-}|${PRETTY_NAME:-unknown}")"
    if [ "${os%%|*}" != "ubuntu" ] || [ "$(echo "$os" | cut -d'|' -f2)" != "24.04" ]; then
        fail "The prebuilt package needs Ubuntu 24.04 (found ${os##*|}). Build from source instead: https://github.com/${REPO}"
    fi
}

# Runtime libraries of the prebuilt binaries, plus OpenFOAM to run the solvers
PACKAGES=(
    curl ca-certificates
    libqt5core5t64 libqt5gui5t64 libqt5widgets5t64 libqt5network5t64
    libvtk9.1t64 libvtk9.1t64-qt
    libocct-foundation-7.6t64 libocct-modeling-data-7.6t64
    libocct-modeling-algorithms-7.6t64 libocct-data-exchange-7.6t64
    libjsoncpp25 libyaml-cpp0.8 libglew2.2 libgl1 libgl1-mesa-dri
    "openfoam${OF_VERSION}-default"
)

install_deps() {
    local SUDO=""
    if [ "$(id -u)" -ne 0 ]; then
        command -v sudo >/dev/null || fail "Installing packages needs root or sudo."
        SUDO="sudo"
    fi
    export DEBIAN_FRONTEND=noninteractive

    step "Preparing the package manager"
    progress 5
    $SUDO apt-get update -q || fail "apt-get update failed. Check your internet connection."
    $SUDO apt-get install -y -q curl ca-certificates || fail "Could not install curl."

    if ! apt-cache policy "openfoam${OF_VERSION}-default" 2>/dev/null | grep -q 'Candidate: [0-9]'; then
        step "Adding the OpenFOAM package repository"
        progress 10
        curl -fsSL https://dl.openfoam.com/add-debian-repo.sh | $SUDO bash \
            || fail "Could not add the OpenFOAM repository (dl.openfoam.com)."
    fi

    step "Installing OpenFOAM v${OF_VERSION} and libraries (this can take a while)"
    progress 15
    $SUDO apt-get install -y -q --no-install-recommends --no-upgrade "${PACKAGES[@]}" \
        || fail "Package installation failed."
    progress 60
}

latest_version() {
    curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
        | grep -m1 '"tag_name"' | sed -E 's/.*"v?([^"]+)".*/\1/'
}

install_app() {
    [ "$(id -u)" -ne 0 ] || fail "Install CakeStudio as your normal user, not root."
    # Global because the EXIT trap runs after this function has returned
    TMP_DIR="$(mktemp -d)"
    trap 'rm -rf "$TMP_DIR"' EXIT
    local tmp="$TMP_DIR"

    if [ -z "$TARBALL" ]; then
        step "Finding the latest CakeStudio release"
        progress 62
        [ -n "$VERSION" ] || VERSION="$(latest_version)"
        [ -n "$VERSION" ] || fail "Could not find a CakeStudio release on GitHub."
        local name="cakestudio-${VERSION}-ubuntu24.04-x86_64.tar.gz"
        step "Downloading CakeStudio ${VERSION}"
        progress 65
        curl -fL --retry 3 -o "$tmp/$name" \
            "https://github.com/${REPO}/releases/download/v${VERSION}/${name}" \
            || fail "Download failed: ${name}"
        TARBALL="$tmp/$name"
    fi
    [ -f "$TARBALL" ] || fail "Package not found: $TARBALL"

    step "Unpacking CakeStudio"
    progress 80
    local stage="$tmp/stage"
    mkdir -p "$stage"
    tar -xzf "$TARBALL" -C "$stage" || fail "The package is damaged."
    local pkg; pkg="$(find "$stage" -mindepth 1 -maxdepth 1 -type d | head -1)"
    [ -x "$pkg/bin/CFDStudio" ] || fail "The package does not contain CFDStudio."
    [ -n "$VERSION" ] || VERSION="$(cat "$pkg/VERSION" 2>/dev/null || echo local)"

    mkdir -p "$APP_ROOT" "$BIN_DIR"
    rm -rf "${APP_ROOT:?}/$VERSION"
    mv "$pkg" "$APP_ROOT/$VERSION"
    ln -sfn "$VERSION" "$APP_ROOT/current"
    ln -sfn "$APP_ROOT/current/bin/CFDStudio" "$BIN_DIR/cakestudio"
    ln -sfn "$APP_ROOT/current/bin/cfdstudio" "$BIN_DIR/cfdstudio"

    if [ "$WITH_TENO" -eq 1 ]; then
    step "Installing the TENO scheme library"
    progress 88
    local libbin="$HOME/OpenFOAM/${USER:-$(id -un)}-v${OF_VERSION}/platforms/linux64GccDPInt32Opt/lib"
    if [ -f "$APP_ROOT/current/lib/libtenoScheme.so" ]; then
        mkdir -p "$libbin"
        cp -f "$APP_ROOT/current/lib/libtenoScheme.so" "$libbin/"
    fi
    fi

    # WSLg already lists .desktop apps in the Windows Start menu, and the Windows
    # setup makes its own shortcut, so only add a launcher entry on native Linux
    if ! is_wsl; then
        step "Adding CakeStudio to the applications menu"
        local apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
        local icons="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
        mkdir -p "$apps" "$icons"
        cp -f "$APP_ROOT/current/share/cakestudio.svg" "$icons/cakestudio.svg" 2>/dev/null || true
        cat > "$apps/cakestudio.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=CakeStudio
Comment=CFD studio for OpenFOAM
Exec=$APP_ROOT/current/bin/CFDStudio
Icon=cakestudio
Terminal=false
Categories=Science;Engineering;
DESKTOP
    fi
    progress 95
}

uninstall_app() {
    step "Removing CakeStudio"
    rm -rf "$APP_ROOT"
    rm -f "$BIN_DIR/cakestudio" "$BIN_DIR/cfdstudio"
    rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/applications/cakestudio.desktop"
    rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps/cakestudio.svg"
    echo "CakeStudio removed. OpenFOAM and system packages were left installed."
}

case "$MODE" in
    deps)      check_platform; install_deps ;;
    app)       check_platform; install_app ;;
    uninstall) uninstall_app ;;
    all)       check_platform; install_deps; install_app ;;
esac

progress 100
echo "@@DONE"
case "$MODE" in app|all) echo "CakeStudio is installed. Start it with: cakestudio" ;; esac
