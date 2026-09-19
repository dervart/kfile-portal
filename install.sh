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

die()
{
    echo "Error: $*" >&2
    exit 1
}


# ============================================================
# Package manager detection
#
# Arch + Niri is the tested configuration.
#
# Other package managers are retained as experimental support.
# Their package names and installation commands have not been
# tested by the project.
# ============================================================

PKG_MANAGER=""

if command -v pacman >/dev/null 2>&1; then
    PKG_MANAGER="pacman"
elif command -v apt-get >/dev/null 2>&1; then
    PKG_MANAGER="apt"
elif command -v dnf >/dev/null 2>&1; then
    PKG_MANAGER="dnf"
elif command -v zypper >/dev/null 2>&1; then
    PKG_MANAGER="zypper"
fi


# ============================================================
# Package lists
#
# Arch is the tested configuration.
#
# The other lists are best-effort package name guesses and have
# not been tested.
# ============================================================

PACMAN_PACKAGES=(
    python
    python-dbus-next
    xdg-desktop-portal
    kio
    kio-fuse
    qt6-base
    cmake
    gcc
)

APT_PACKAGES=(
    python3
    python3-dbus-next
    xdg-desktop-portal
    libkf6kio-dev
    kio-fuse
    qt6-base-dev
    cmake
    g++
)

DNF_PACKAGES=(
    python3
    python3-dbus-next
    xdg-desktop-portal
    kf6-kio-devel
    kio-fuse
    qt6-qtbase-devel
    cmake
    gcc-c++
)

ZYPPER_PACKAGES=(
    python3
    python3-dbus-next
    xdg-desktop-portal
    kf6-kio-devel
    kio-fuse
    qt6-base-devel
    cmake
    gcc-c++
)


check_package()
{
    local package="$1"

    case "${PKG_MANAGER}" in
        pacman)
            pacman -Q "${package}" >/dev/null 2>&1
            ;;
        apt)
            dpkg -s "${package}" >/dev/null 2>&1
            ;;
        dnf|zypper)
            rpm -q "${package}" >/dev/null 2>&1
            ;;
        *)
            return 1
            ;;
    esac
}


install_package()
{
    local package="$1"

    echo "Installing missing package: ${package}"

    case "${PKG_MANAGER}" in
        pacman)
            sudo pacman -S --needed --noconfirm -- "${package}"
            ;;
        apt)
            sudo apt-get install -y -- "${package}"
            ;;
        dnf)
            sudo dnf install -y -- "${package}"
            ;;
        zypper)
            sudo zypper --non-interactive install -- "${package}"
            ;;
        *)
            die "Cannot install ${package}: no supported package manager found."
            ;;
    esac
}


# ============================================================
# Basic checks
# ============================================================

if [[ "${EUID}" -eq 0 ]]; then
    echo "Error: Do not run this installer with sudo."
    echo
    echo "Run it as your normal user:"
    echo "  ./install.sh"
    exit 1
fi

if ! command -v sudo >/dev/null 2>&1; then
    die "sudo is required. Please install sudo and run ./install.sh again."
fi

if ! command -v python3 >/dev/null 2>&1; then
    die "python3 is required."
fi

echo "Requesting administrator privileges..."
sudo -v


# ============================================================
# Package checks
# ============================================================

echo
echo "Checking required packages..."

case "${PKG_MANAGER}" in
    pacman)
        REQUIRED_PACKAGES=("${PACMAN_PACKAGES[@]}")
        ;;
    apt)
        REQUIRED_PACKAGES=("${APT_PACKAGES[@]}")
        ;;
    dnf)
        REQUIRED_PACKAGES=("${DNF_PACKAGES[@]}")
        ;;
    zypper)
        REQUIRED_PACKAGES=("${ZYPPER_PACKAGES[@]}")
        ;;
    *)
        echo "Warning: no supported package manager was detected"
        echo "(pacman, apt, dnf, zypper)."
        echo
        echo "Skipping automatic package checks."
        echo "Make sure the following are installed manually:"
        echo
        echo "  Python 3"
        echo "  python-dbus-next"
        echo "  xdg-desktop-portal"
        echo "  KDE Frameworks 6 KIO"
        echo "  kio-fuse"
        echo "  Qt6 base"
        echo "  CMake"
        echo "  A C++ compiler"
        echo
        echo "Note: Arch + Niri is the only tested configuration."
        REQUIRED_PACKAGES=()
        ;;
esac

MISSING_PACKAGES=()

for package in "${REQUIRED_PACKAGES[@]}"; do
    if ! check_package "${package}"; then
        MISSING_PACKAGES+=("${package}")
    fi
done

if [[ ${#MISSING_PACKAGES[@]} -gt 0 ]]; then
    echo "Missing packages:"

    for package in "${MISSING_PACKAGES[@]}"; do
        echo "  ${package}"
    done

    echo

    read -r -p "Install missing packages now? [y/N] " answer

    case "${answer}" in
        y|Y|yes|YES|Yes)
            for package in "${MISSING_PACKAGES[@]}"; do
                install_package "${package}"
            done
            ;;
        *)
            die "Required packages are missing. Install them and run ./install.sh again."
            ;;
    esac
elif [[ ${#REQUIRED_PACKAGES[@]} -gt 0 ]]; then
    echo "All required packages are installed."
fi


# ============================================================
# Source files
# ============================================================

[[ -f "${SCRIPT_DIR}/kfile-helper.cpp" ]] ||
    die "Missing source file: ${SCRIPT_DIR}/kfile-helper.cpp"

[[ -f "${SCRIPT_DIR}/CMakeLists.txt" ]] ||
    die "Missing source file: ${SCRIPT_DIR}/CMakeLists.txt"

[[ -f "${SCRIPT_DIR}/xdg-desktop-portal-kfile" ]] ||
    die "Missing backend: ${SCRIPT_DIR}/xdg-desktop-portal-kfile"

[[ -f "${SCRIPT_DIR}/kfile.portal" ]] ||
    die "Missing portal definition: ${SCRIPT_DIR}/kfile.portal"

[[ -f "${SCRIPT_DIR}/org.freedesktop.impl.portal.desktop.kfile.service" ]] ||
    die "Missing D-Bus service file: ${SCRIPT_DIR}/org.freedesktop.impl.portal.desktop.kfile.service"


# ============================================================
# Build
# ============================================================

BUILD_DIR="$(mktemp -d)"
TMP_CONFIG="$(mktemp)"

cleanup()
{
    rm -rf "${BUILD_DIR}"
    rm -f "${TMP_CONFIG}"
}

trap cleanup EXIT

echo
echo "Building kfile-helper..."

cmake \
    -S "${SCRIPT_DIR}" \
    -B "${BUILD_DIR}"

cmake \
    --build "${BUILD_DIR}" \
    --parallel

[[ -x "${BUILD_DIR}/kfile-helper" ]] ||
    die "kfile-helper was not built successfully."


# ============================================================
# Install system files
# ============================================================

echo
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
#
# Only the FileChooser entry is changed.
# All other existing portal settings are preserved.
# ============================================================

echo
echo "Updating Niri portal configuration..."

mkdir -p "${USER_CONFIG_DIR}"

if [[ ! -f "${USER_CONFIG}" ]]; then

    cat > "${USER_CONFIG}" <<'EOF'
[preferred]
org.freedesktop.impl.portal.FileChooser=kfile;
EOF

    chmod 644 "${USER_CONFIG}"

    echo "Created:"
    echo "  ${USER_CONFIG}"

else

    # Create a backup before modifying an existing configuration.
    if [[ ! -f "${USER_BACKUP}" ]]; then
        echo "Creating backup of existing Niri portal configuration..."

        cp -a \
            "${USER_CONFIG}" \
            "${USER_BACKUP}"

        echo "Backup:"
        echo "  ${USER_BACKUP}"
    else
        echo "Backup already exists:"
        echo "  ${USER_BACKUP}"
    fi

    # Use Python for robust modification of portals.conf.
    #
    # Only the FileChooser entry is changed.
    # Existing unrelated settings are preserved.

    python3 - "${USER_CONFIG}" "${TMP_CONFIG}" <<'PY'
import sys

source = sys.argv[1]
destination = sys.argv[2]

with open(source, "r", encoding="utf-8") as f:
    lines = f.readlines()

preferred_start = None
preferred_end = None

for index, line in enumerate(lines):
    stripped = line.strip()

    if stripped.startswith("[") and stripped.endswith("]"):
        section = stripped[1:-1].strip()

        if section == "preferred":
            preferred_start = index
            continue

        if preferred_start is not None and preferred_end is None:
            preferred_end = index
            break

if preferred_start is not None and preferred_end is None:
    preferred_end = len(lines)

filechooser_key = "org.freedesktop.impl.portal.FileChooser"
replacement = f"{filechooser_key}=kfile;\n"

if preferred_start is not None:
    found = False

    for index in range(preferred_start + 1, preferred_end):
        stripped = lines[index].strip()

        if (
            not stripped
            or stripped.startswith("#")
            or stripped.startswith(";")
        ):
            continue

        if "=" not in stripped:
            continue

        key = stripped.split("=", 1)[0].strip()

        if key == filechooser_key:
            lines[index] = replacement
            found = True

    if not found:
        insert_at = preferred_start + 1

        while insert_at < preferred_end:
            stripped = lines[insert_at].strip()

            if (
                stripped == ""
                or stripped.startswith("#")
                or stripped.startswith(";")
            ):
                insert_at += 1
                continue

            break

        lines.insert(insert_at, replacement)

else:
    if lines and not lines[-1].endswith("\n"):
        lines[-1] += "\n"

    if lines and lines[-1].strip():
        lines.append("\n")

    lines.append("[preferred]\n")
    lines.append(replacement)

with open(destination, "w", encoding="utf-8") as f:
    f.writelines(lines)
PY

    if cmp -s "${USER_CONFIG}" "${TMP_CONFIG}"; then
        echo "Niri portal configuration is already correct."
        rm -f "${TMP_CONFIG}"
    else
        install -Dm644 \
            "${TMP_CONFIG}" \
            "${USER_CONFIG}"

        echo "Updated:"
        echo "  ${USER_CONFIG}"
        echo
        echo "Only the FileChooser portal entry was changed."
    fi
fi


# ============================================================
# Remove obsolete local KDialog portal
# ============================================================

if [[ -f "${STALE_KDIALOG}" ]]; then
    echo
    echo "Removing obsolete user-local KDialog portal definition..."

    rm -f "${STALE_KDIALOG}"

    echo "Removed:"
    echo "  ${STALE_KDIALOG}"
fi

STALE_KDIALOG_DIR="$(dirname "${STALE_KDIALOG}")"

if [[ -d "${STALE_KDIALOG_DIR}" ]]; then
    rmdir "${STALE_KDIALOG_DIR}" 2>/dev/null || true
fi


# ============================================================
# Restart portal
# ============================================================

echo
echo "Restarting xdg-desktop-portal..."

if systemctl --user restart xdg-desktop-portal.service; then
    echo "xdg-desktop-portal restarted successfully."
else
    echo
    echo "Warning: could not restart xdg-desktop-portal automatically."
    echo "Please restart it manually:"
    echo
    echo "  systemctl --user restart xdg-desktop-portal.service"
fi


# ============================================================
# Installation summary
# ============================================================

echo
echo "============================================================"
echo "Installation completed successfully."
echo "============================================================"
echo

echo "Installed:"
echo "  ${BACKEND}"
echo "  ${HELPER}"
echo "  ${PORTAL}"
echo "  ${DBUS_SERVICE}"

echo
echo "Required runtime dependency:"
echo "  kio-fuse"

echo
echo "Niri configuration:"
echo "  ${USER_CONFIG}"

echo
echo "FileChooser backend:"
echo "  org.freedesktop.impl.portal.FileChooser=kfile;"

echo
echo "The kfile backend is activated automatically by D-Bus."

echo
echo "Verify the backend with:"
echo
echo "  busctl --user status org.freedesktop.impl.portal.desktop.kfile"

echo
echo "For diagnostic logging, use:"
echo
echo "  KFILE_PORTAL_DEBUG=1"
echo
