# xdg-desktop-portal-kfile

A KDE `KFileWidget` / `KFileCustomDialog` backend for the
`org.freedesktop.impl.portal.FileChooser` interface of `xdg-desktop-portal`,
intended for Niri.

The project targets Linux distributions in general. `install.sh` currently
auto-detects `pacman` (Arch), `apt` (Debian/Ubuntu), `dnf` (Fedora) and
`zypper` (openSUSE) to check and install build dependencies. The core
programs (`kfile-helper` and the Python D-Bus backend) have no
distribution-specific code.

## How it works

```text
xdg-desktop-portal
        |
        v
xdg-desktop-portal-kfile  (Python / D-Bus)
        |
        v
kfile-helper              (C++ / Qt6 / KDE Frameworks 6)
        |
        v
KFileCustomDialog
        |
        v
KFileWidget / KIO
```

The backend provides:

- Open File
- Open Multiple Files
- Save File
- Save Multiple Files (`SaveFiles`)
- Select Directory
- Current folder
- Current filename
- Glob filters
- MIME filters
- Current filter
- KDE/KIO file chooser UI
- Request cancellation (`Close`)
- Safe handling of the requested save file name (no path traversal)
- Non-blocking backend: multiple concurrent dialog requests do not stall
  each other

## Requirements

`install.sh` installs these automatically when possible, matched to the
detected package manager:

| Dependency                     | Arch (`pacman`)      | Debian/Ubuntu (`apt`) | Fedora (`dnf`)      | openSUSE (`zypper`)  |
|---------------------------------|-----------------------|------------------------|-----------------------|-----------------------|
| Python 3                        | `python`             | `python3`              | `python3`             | `python3`             |
| `dbus-next` (Python)             | `python-dbus-next`   | `python3-dbus-next`    | `python3-dbus-next`   | `python3-dbus-next`   |
| xdg-desktop-portal               | `xdg-desktop-portal` | `xdg-desktop-portal`   | `xdg-desktop-portal`  | `xdg-desktop-portal`  |
| KDE Frameworks 6 KIO (dev files) | `kio`                | `libkf6kio-dev`        | `kf6-kio-devel`       | `kf6-kio-devel`       |
| Qt6 base (dev files)             | `qt6-base`           | `qt6-base-dev`         | `qt6-qtbase-devel`    | `qt6-base-devel`      |
| CMake                            | `cmake`              | `cmake`                | `cmake`               | `cmake`               |
| C++ compiler                     | `gcc`                | `g++`                  | `gcc-c++`             | `gcc-c++`             |

Exact package names may still differ between distribution versions and
third-party repositories. If a package is not found under these names,
install the equivalent development package for KDE Frameworks 6 KIO and Qt6
Widgets manually, then re-run `./install.sh`.

If no supported package manager is detected, `install.sh` skips automatic
package checks/installation and prints the list of dependencies to install
manually.

## Build

From the project directory:

```bash
cmake -S . -B build
cmake --build build --parallel
```

The resulting helper is:

```text
build/kfile-helper
```

## Installation

Install the helper:

```bash
sudo install -Dm755 build/kfile-helper \
    /usr/local/bin/kfile-helper
```

Install the Python D-Bus backend:

```bash
sudo install -Dm755 xdg-desktop-portal-kfile \
    /usr/local/bin/xdg-desktop-portal-kfile
```

Install the portal definition:

```bash
sudo install -Dm644 kfile.portal \
    /usr/share/xdg-desktop-portal/portals/kfile.portal
```

Install the D-Bus service:

```bash
sudo install -Dm644 \
    org.freedesktop.impl.portal.desktop.kfile.service \
    /usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kfile.service
```

For Niri, install:

```bash
mkdir -p ~/.config/xdg-desktop-portal

install -Dm644 niri-portals.conf \
    ~/.config/xdg-desktop-portal/niri-portals.conf
```

Then restart the user portal:

```bash
systemctl --user restart xdg-desktop-portal.service
```

The backend is activated automatically through D-Bus. Do not start
`xdg-desktop-portal-kfile` manually.

## Request cancellation

The backend implements `org.freedesktop.impl.portal.Request`. Every call to
`OpenFile`, `SaveFile` or `SaveFiles` exports a `Request` object at the
`handle` path supplied by the caller.

If the calling application invokes `Close()` on that object before the user
finishes the dialog, the backend terminates the running `kfile-helper`
process and the pending D-Bus call returns as cancelled. This matches the
behaviour expected by portal clients that cancel a request programmatically
(for example, when the requesting window is closed).

## Niri configuration

The supplied configuration is:

```ini
[preferred]
default=gtk;
org.freedesktop.impl.portal.FileChooser=kfile;
```

This selects `kfile` only for the FileChooser interface. Other portal
interfaces continue to use the normal Niri/system configuration.

## D-Bus

Bus name:

```text
org.freedesktop.impl.portal.desktop.kfile
```

Object path:

```text
/org/freedesktop/portal/desktop
```

Interface:

```text
org.freedesktop.impl.portal.FileChooser
```

## Installed files

```text
/usr/local/bin/xdg-desktop-portal-kfile
/usr/local/bin/kfile-helper
/usr/share/xdg-desktop-portal/portals/kfile.portal
/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kfile.service
~/.config/xdg-desktop-portal/niri-portals.conf
```

## Uninstallation

Remove the system files:

```bash
sudo rm -f \
    /usr/local/bin/xdg-desktop-portal-kfile \
    /usr/local/bin/kfile-helper \
    /usr/share/xdg-desktop-portal/portals/kfile.portal \
    /usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kfile.service
```

Then restore or remove the Niri-specific configuration as appropriate and
restart:

```bash
systemctl --user restart xdg-desktop-portal.service
```

## Notes

This project does not modify the existing KDialog backend.

The portal definition contains:

```ini
UseIn=niri;
```

so the backend is intended for Niri.

## Known limitations

- The dialog window is not marked as transient for the calling application
  window (`parent_window` is currently ignored). The dialog therefore is not
  guaranteed to stay above the calling application on all compositors.
- A single dialog request runs for as long as the user needs; an internal
  safety timeout (10 minutes) protects the backend from a crashed or
  wedged `kfile-helper` process, automatically failing the request instead
  of hanging the whole portal.
