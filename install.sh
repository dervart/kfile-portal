#!/bin/bash
set -euo pipefail

# ============================================================
# Configuration
# ============================================================

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

BACKEND="/usr/local/bin/xdg-desktop-portal-kfile"
HELPER="/usr/local/bin/kfile-helper"

PORTAL="/usr/share/xdg-desktop-portal/portals/kfile.portal"
DBUS_SERVICE="/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kfile.service"

USER_CONFIG_DIR="${HOME}/.config/xdg-desktop-portal"
USER_CONFIG="${USER_CONFIG_DIR}/niri-portals.conf"
USER_BACKUP="${USER_CONFIG}.kfile-backup"

STALE_KDIALOG="${HOME}/.local/share/xdg-desktop-portal/portals/kdialog.portal"

# ============================================================
# Helpers
# ============================================================

die() {
    echo "Error: $*" >&2
    exit 1
}

check_package() {
    local package="$1"

    if ! pacman -Q "${package}" >/dev/null 2>&1; then
        die "Required package is not installed: ${package}"
    fi
}

# ============================================================
# Checks
# ============================================================

if [[ "${EUID}" -eq 0 ]]; then
    die "Do not run this installer with sudo. Run: ./install.sh"
fi

if ! command -v sudo >/dev/null 2>&1; then
    die "sudo is required. Please install sudo and run ./install.sh again."
fi

echo "Requesting administrator privileges..."
sudo -v

echo "Checking required packages..."

for package in \
    python \
    python-dbus-next \
    xdg-desktop-portal \
    kio \
    qt6-base \
    cmake \
    gcc
do
    check_package "${package}"
done

echo "All required packages are installed."

# ============================================================
# Build
# ============================================================

BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "${BUILD_DIR}"' EXIT

echo "Building kfile-helper..."

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" --parallel

[[ -x "${BUILD_DIR}/kfile-helper" ]] ||
    die "kfile-helper was not built successfully"

# ============================================================
# Install system files
# ============================================================

echo "Installing kfile-helper..."
sudo install -Dm755 \
    "${BUILD_DIR}/kfile-helper" \
    "${HELPER}"

echo "Installing D-Bus backend..."
sudo install -Dm755 \
    "${SCRIPT_DIR}/xdg-desktop-portal-kfile" \
    "${BACKEND}"

echo "Installing portal definition..."
sudo install -Dm644 \
    "${SCRIPT_DIR}/kfile.portal" \
    "${PORTAL}"

echo "Installing D-Bus service..."
sudo install -Dm644 \
    "${SCRIPT_DIR}/org.freedesktop.impl.portal.desktop.kfile.service" \
    "${DBUS_SERVICE}"

# ============================================================
# Niri portal configuration
# ============================================================

mkdir -p "${USER_CONFIG_DIR}"

if [[ -f "${USER_CONFIG}" && ! -f "${USER_BACKUP}" ]]; then
    echo "Backing up existing Niri portal configuration..."
    cp -a "${USER_CONFIG}" "${USER_BACKUP}"
fi

echo "Installing Niri portal configuration..."
install -Dm644 \
    "${SCRIPT_DIR}/niri-portals.conf" \
    "${USER_CONFIG}"

# ============================================================
# Remove obsolete local KDialog portal definition
# ============================================================

if [[ -f "${STALE_KDIALOG}" ]]; then
    echo "Removing obsolete user-local KDialog portal definition..."
    rm -f "${STALE_KDIALOG}"
fi

# ============================================================
# Restart portal
# ============================================================

echo "Restarting xdg-desktop-portal..."

if systemctl --user restart xdg-desktop-portal.service; then
    echo "xdg-desktop-portal restarted."
else
    echo "Warning: could not restart xdg-desktop-portal."
    echo "Restart it manually from the graphical user session."
fi

echo
echo "Installation completed successfully."
echo
echo "Installed:"
echo "  ${BACKEND}"
echo "  ${HELPER}"
echo "  ${PORTAL}"
echo "  ${DBUS_SERVICE}"
echo "  ${USER_CONFIG}"
echo
echo "The kfile backend is activated automatically by D-Bus."
