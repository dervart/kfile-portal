#!/bin/bash

set -euo pipefail

# ============================================================
# Configuration
# ============================================================

BACKEND="/usr/local/bin/xdg-desktop-portal-kfile"
HELPER="/usr/local/bin/kfile-helper"

PORTAL="/usr/share/xdg-desktop-portal/portals/kfile.portal"
DBUS_SERVICE="/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kfile.service"

BUS_NAME="org.freedesktop.impl.portal.desktop.kfile"

USER_CONFIG_DIR="${HOME}/.config/xdg-desktop-portal"
USER_CONFIG="${USER_CONFIG_DIR}/niri-portals.conf"

USER_BACKUP="${USER_CONFIG}.kfile-backup"

STALE_KDIALOG="${HOME}/.local/share/xdg-desktop-portal/portals/kdialog.portal"


# ============================================================
# Helpers
# ============================================================

remove_file()
{
    local file="$1"

    if [[ -e "${file}" || -L "${file}" ]]; then
        echo "Removing:"
        echo "  ${file}"

        sudo rm -f -- "${file}"
    else
        echo "Not found:"
        echo "  ${file}"
    fi
}


# ============================================================
# Basic checks
# ============================================================

if [[ "${EUID}" -eq 0 ]]; then
    echo "Error: Do not run this script with sudo."
    echo
    echo "Run it as your normal user:"
    echo
    echo "  ./uninstall.sh"
    exit 1
fi

if ! command -v sudo >/dev/null 2>&1; then
    echo "Error: sudo is required."
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "Error: python3 is required."
    exit 1
fi

if ! command -v busctl >/dev/null 2>&1; then
    echo "Error: busctl is required."
    exit 1
fi

echo "Requesting administrator privileges..."
sudo -v


# ============================================================
# Confirmation
# ============================================================

echo
echo "============================================================"
echo "Uninstall xdg-desktop-portal-kfile"
echo "============================================================"
echo

echo "The following files will be removed:"
echo

echo "  ${BACKEND}"
echo "  ${HELPER}"
echo "  ${PORTAL}"
echo "  ${DBUS_SERVICE}"

echo
echo "The following Niri portal configuration will be cleaned:"
echo

echo "  ${USER_CONFIG}"

echo
echo "System packages such as xdg-desktop-portal, KIO,"
echo "kio-fuse, Qt, Dolphin, etc. will NOT be removed."
echo

read -r -p "Continue? [y/N] " answer

case "${answer}" in
    y|Y|yes|YES|Yes)
        ;;
    *)
        echo
        echo "Uninstallation cancelled."
        exit 0
        ;;
esac


# ============================================================
# Detect running backend
#
# D-Bus activation starts the backend on demand. If an instance
# is currently running, remember its PID so it can be terminated
# after the activation service is removed.
# ============================================================

echo
echo "Checking for a running kfile backend..."

KFILE_PID=""

if busctl --user status "${BUS_NAME}" >/dev/null 2>&1; then
    KFILE_PID="$(
        busctl --user status "${BUS_NAME}" 2>/dev/null |
            awk '/^[[:space:]]*PID:/ { print $2; exit }'
    )"

    if [[ "${KFILE_PID}" =~ ^[0-9]+$ ]]; then
        echo "Running backend found:"
        echo "  Bus name: ${BUS_NAME}"
        echo "  PID:      ${KFILE_PID}"
    else
        echo "The kfile backend is running, but its PID could not be determined."
        KFILE_PID=""
    fi
else
    echo "kfile backend is not currently running."
fi


# ============================================================
# Remove system files
#
# Remove the D-Bus activation file before terminating an already
# running backend. This prevents a new instance from being
# automatically started while the uninstall is in progress.
# ============================================================

echo
echo "Removing installed kfile files..."

remove_file "${DBUS_SERVICE}"
remove_file "${BACKEND}"
remove_file "${HELPER}"
remove_file "${PORTAL}"


# ============================================================
# Stop running backend
# ============================================================

if [[ -n "${KFILE_PID}" ]]; then
    echo
    echo "Stopping running kfile backend..."

    if kill -TERM "${KFILE_PID}" 2>/dev/null; then
        echo "Sent SIGTERM to PID ${KFILE_PID}."
    else
        echo "The backend process has already exited."
    fi

    for _ in {1..20}; do
        if ! kill -0 "${KFILE_PID}" 2>/dev/null; then
            break
        fi

        sleep 0.1
    done

    if kill -0 "${KFILE_PID}" 2>/dev/null; then
        echo "Backend did not exit after SIGTERM."
        echo "Sending SIGKILL to PID ${KFILE_PID}..."

        kill -KILL "${KFILE_PID}" 2>/dev/null || true
    fi
fi


# ============================================================
# Niri portal configuration
#
# Only our FileChooser entry is removed.
# All unrelated portal configuration is preserved.
# ============================================================

echo
echo "Cleaning Niri portal configuration..."

if [[ -f "${USER_CONFIG}" ]]; then

    TMP_CONFIG="$(mktemp)"

    cleanup_config()
    {
        rm -f "${TMP_CONFIG}"
    }

    trap cleanup_config EXIT

    python3 - "${USER_CONFIG}" "${TMP_CONFIG}" <<'PY'
import sys

source = sys.argv[1]
destination = sys.argv[2]

with open(source, "r", encoding="utf-8") as f:
    lines = f.readlines()

result = []

for line in lines:
    stripped = line.strip()

    if (
        not stripped
        or stripped.startswith("#")
        or stripped.startswith(";")
    ):
        result.append(line)
        continue

    if "=" not in stripped:
        result.append(line)
        continue

    key, value = stripped.split("=", 1)

    key = key.strip()
    value = value.strip()

    if (
        key == "org.freedesktop.impl.portal.FileChooser"
        and value.rstrip(";").strip() == "kfile"
    ):
        continue

    result.append(line)

with open(destination, "w", encoding="utf-8") as f:
    f.writelines(result)
PY

    if cmp -s "${USER_CONFIG}" "${TMP_CONFIG}"; then
        echo "No kfile entry found in:"
        echo "  ${USER_CONFIG}"
    else
        install -Dm644 \
            "${TMP_CONFIG}" \
            "${USER_CONFIG}"

        echo "Removed kfile FileChooser entry from:"
        echo "  ${USER_CONFIG}"
    fi

    rm -f "${TMP_CONFIG}"
    trap - EXIT

else
    echo "Niri portal configuration does not exist:"
    echo "  ${USER_CONFIG}"
fi


# ============================================================
# Installer backup
#
# Do not automatically restore or delete the backup.
#
# The user may have changed niri-portals.conf after installation,
# so automatically restoring the old file could overwrite valid
# user changes.
# ============================================================

if [[ -f "${USER_BACKUP}" ]]; then
    echo
    echo "An installer backup exists:"
    echo "  ${USER_BACKUP}"
    echo
    echo "It has NOT been restored or deleted."
fi


# ============================================================
# Remove obsolete user-local KDialog portal
#
# This was used by an older version of the project and is not
# part of the current kfile backend.
# ============================================================

if [[ -f "${STALE_KDIALOG}" ]]; then
    echo
    echo "Removing obsolete user-local KDialog portal..."

    rm -f -- "${STALE_KDIALOG}"

    echo "Removed:"
    echo "  ${STALE_KDIALOG}"
else
    echo
    echo "No obsolete user-local KDialog portal found."
fi

STALE_KDIALOG_DIR="$(dirname "${STALE_KDIALOG}")"

if [[ -d "${STALE_KDIALOG_DIR}" ]]; then
    rmdir "${STALE_KDIALOG_DIR}" 2>/dev/null || true
fi


# ============================================================
# Restart xdg-desktop-portal
# ============================================================

echo
echo "Restarting xdg-desktop-portal..."

if systemctl --user restart xdg-desktop-portal.service; then
    echo "xdg-desktop-portal restarted successfully."
else
    echo
    echo "Warning: could not restart xdg-desktop-portal automatically."
    echo "Please restart it manually with:"
    echo
    echo "  systemctl --user restart xdg-desktop-portal.service"
fi


# ============================================================
# Final verification
# ============================================================

echo
echo "============================================================"
echo "Uninstallation completed successfully."
echo "============================================================"
echo

echo "The kfile backend has been removed."

echo
echo "The following external packages were intentionally left installed:"
echo "  xdg-desktop-portal"
echo "  KIO"
echo "  kio-fuse"
echo "  Qt"
echo "  KDE Frameworks"
echo

echo "If you want to remove unused packages, do that separately."
echo
