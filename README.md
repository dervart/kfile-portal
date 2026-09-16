# xdg-desktop-portal-kfile

A KDE `KFileWidget` / `KFileCustomDialog` backend for the
`org.freedesktop.impl.portal.FileChooser` interface of `xdg-desktop-portal`,
intended for Niri on Arch Linux.

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

## Requirements

Arch Linux packages:

```text
python
python-dbus-next
xdg-desktop-portal
kio
qt6-base
cmake
gcc
```

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
