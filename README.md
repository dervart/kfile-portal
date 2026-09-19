# xdg-desktop-portal-kfile

A KDE `KFileWidget` / `KFileCustomDialog` backend for the
`org.freedesktop.impl.portal.FileChooser` interface of
`xdg-desktop-portal`, intended for Niri.

The project provides the KDE file picker to applications using
`xdg-desktop-portal`, without installing the full
`xdg-desktop-portal-kde` package.

The project was developed and tested on **Arch Linux + Niri**.

> **Important:** Arch Linux + Niri is the only configuration currently
> tested by the author. Support for other Linux distributions was added
> experimentally and largely based on assumed package names and layouts.
> Debian/Ubuntu, Fedora and openSUSE installations have **not been tested**.
> Their package-manager support in `install.sh` should therefore be
> considered best-effort and may require manual adjustments.

## How it works

```text
Application
     |
     v
xdg-desktop-portal
     |
     v
xdg-desktop-portal-kfile
     |
     v
kfile-helper
     |
     v
KFileCustomDialog
     |
     v
KFileWidget / KIO
     |
     +----------------------+
     |                      |
     | local URL             | remote URL
     v                      v
file://...                 smb://...
                            nfs://...
                            sftp://...
                            ...
                               |
                               v
                            KIOFuse
                               |
                               v
                     /run/user/.../kio-fuse-...
                               |
                               v
                         file://...
```

The Python component implements the portal/D-Bus side.

`kfile-helper` is a small C++ application using Qt6 and KDE Frameworks 6.
It creates the actual KDE file dialog.

KIO/KFileWidget handles remote URLs directly.

**KIOFuse is used only after the user selects a remote URL.**

The initial file chooser navigation is therefore performed using the
original KIO URL. For example:

```text
smb://server/share/
```

is opened directly by KFileWidget.

After the user selects:

```text
smb://server/share/file.txt
```

the helper asks KIOFuse to mount the corresponding remote location and
returns a local `file://` URI to the portal client.

This allows applications that require local file paths to work with
remote KIO locations without forcing the file chooser itself to operate
through KIOFuse.

## Features

The backend currently supports:

* Open File
* Open Multiple Files
* Save File
* Save Multiple Files (`SaveFiles`)
* Select Directory
* Initial directory
* Initial filename
* Glob filters
* MIME filters
* Current filter
* KDE/KIO file chooser UI
* Local files
* Remote KIO URLs
* SMB
* NFS
* Other KIO-supported remote locations
* KIOFuse conversion of selected remote URLs
* Request cancellation (`Close`)
* Safe handling of requested save filenames
* Path traversal protection
* Multiple concurrent portal requests

## Remote files

Remote file handling is based on KDE KIO.

The file chooser does **not** convert the initial directory to a
KIOFuse mount.

For example:

```text
smb://192.168.70.1/obmen/
```

remains an SMB URL inside KFileWidget.

After selecting a file:

```text
smb://192.168.70.1/obmen/example.txt
```

the helper uses KIOFuse:

```text
smb://192.168.70.1/obmen/
        |
        v
KIOFuse
        |
        v
/run/user/1000/kio-fuse-.../smb/192.168.70.1/obmen/
```

and appends the selected filename.

The portal client then receives a local URI such as:

```text
file:///run/user/1000/kio-fuse-.../smb/192.168.70.1/obmen/example.txt
```

### Why KIOFuse is required

KFileWidget/KIO can browse and select remote URLs directly, but many
portal clients expect the returned URI to reference a local filesystem
path.

KIOFuse provides that bridge.

Therefore **`kio-fuse` is a runtime dependency of the current
implementation**.

The installer checks for it but does not use it for ordinary local
file selection.

## SaveFiles

The portal `SaveFiles` operation is supported.

The application provides a list of filenames and the user selects the
target directory.

The helper then returns local `file://` URIs for the requested filenames.

For remote directories, the selected directory is localized through
KIOFuse first.

Filenames are checked to prevent path traversal. Absolute paths,
directory separators and `.` / `..` are rejected.

## File filters

The backend supports both glob and MIME filters.

The portal request uses:

```json
{
  "name": "Image Files",
  "globs": ["*.png", "*.jpg"],
  "mimes": ["image/png", "image/jpeg"]
}
```

The selected filter is returned using the same structure:

```json
{
  "name": "Image Files",
  "globs": ["*.png", "*.jpg"],
  "mimes": ["image/png", "image/jpeg"]
}
```

This is important for applications that keep track of the selected
file filter between portal requests.

## Requirements

### Tested configuration

The tested environment is:

* Arch Linux
* Niri
* Wayland
* `xdg-desktop-portal`
* Qt6
* KDE Frameworks 6
* KIO
* KIOFuse
* Python 3
* `python-dbus-next`
* CMake
* GCC

On Arch Linux the required packages are:

```text
python
python-dbus-next
xdg-desktop-portal
kio
kio-fuse
qt6-base
cmake
gcc
```

### Other distributions

`install.sh` contains experimental package-manager support for:

* Debian/Ubuntu (`apt`)
* Fedora (`dnf`)
* openSUSE (`zypper`)

This support is **not tested**.

The package names in those sections are best-effort guesses based on
expected package naming conventions. They may be incomplete, incorrect,
or different on a particular distribution release.

If installation fails on another distribution, install the equivalent
packages manually and adjust the installer as necessary.

No claim of full cross-distribution compatibility is currently made.

## Installation

Clone the repository:

```bash
git clone https://github.com/dervart/kfile-portal.git
cd kfile-portal
```

Make the installer executable:

```bash
chmod +x install.sh
```

Run it as the normal user:

```bash
./install.sh
```

**Do not run `install.sh` with `sudo`.**

The script uses `sudo` only for operations that require root
permissions.

The installer:

1. Checks required dependencies.
2. Builds `kfile-helper`.
3. Installs the Python D-Bus backend.
4. Installs the portal definition.
5. Installs the D-Bus activation service.
6. Configures the Niri FileChooser portal.
7. Creates a backup of an existing `niri-portals.conf`.
8. Removes an obsolete user-local KDialog portal definition if one
   exists.
9. Restarts `xdg-desktop-portal`.

## Installed files

The installer installs:

```text
/usr/local/bin/xdg-desktop-portal-kfile
/usr/local/bin/kfile-helper
/usr/share/xdg-desktop-portal/portals/kfile.portal
/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kfile.service
```

and configures:

```text
~/.config/xdg-desktop-portal/niri-portals.conf
```

If an existing Niri portal configuration is modified, the installer
creates:

```text
~/.config/xdg-desktop-portal/niri-portals.conf.kfile-backup
```

The backup is not automatically overwritten on subsequent installations.

## Niri configuration

The installer adds:

```ini
[preferred]
org.freedesktop.impl.portal.FileChooser=kfile;
```

to the user's Niri portal configuration.

If the file already contains other portal settings, they are preserved.

For example, a configuration can contain:

```ini
[preferred]
default=gtk;
org.freedesktop.impl.portal.FileChooser=kfile;
```

This means that only the FileChooser interface uses the kfile backend.
Other portal interfaces continue to use their normal backends.

The supplied portal definition contains:

```ini
UseIn=niri;
```

so the backend is specifically intended for Niri.

## D-Bus

The backend is activated automatically through D-Bus.

It should **not** normally be started manually.

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

To check whether the backend is currently running:

```bash
busctl --user status org.freedesktop.impl.portal.desktop.kfile
```

It is normal for the service not to be running until an application
actually requests a FileChooser portal.

## Debugging

The helper normally produces only error messages.

For detailed diagnostic output, enable:

```bash
export KFILE_PORTAL_DEBUG=1
```

The debug output includes information such as:

```text
Selected URL: smb://server/share/file.txt
KIOFuse mount URL: smb://server/share
KIOFuse local path: /run/user/1000/kio-fuse-.../...
Localized URL: file:///run/user/1000/kio-fuse-.../...
```

This is useful for diagnosing remote file selection and localization
problems.

The variable can be unset again with:

```bash
unset KFILE_PORTAL_DEBUG
```

The helper is activated by D-Bus, so the environment variable must be
available to the D-Bus-activated service if debug logging is needed
during normal portal operation.

## KIOFuse timeout

Remote URL localization uses a synchronous D-Bus call to KIOFuse with a
30-second timeout.

The call happens after the file dialog has already been accepted and
closed, so it does not block the visible KFileWidget interface.

The timeout provides a bounded wait if KIOFuse or the remote filesystem
does not respond.

## Request cancellation

The Python backend implements:

```text
org.freedesktop.impl.portal.Request
```

For each portal request it exports the corresponding request object.

If the portal client calls `Close()` before the user finishes the
operation, the running `kfile-helper` process is terminated and the
request is returned as cancelled.

## Uninstallation

Run:

```bash
./uninstall.sh
```

Do not run it with `sudo`.

The uninstaller removes:

```text
/usr/local/bin/xdg-desktop-portal-kfile
/usr/local/bin/kfile-helper
/usr/share/xdg-desktop-portal/portals/kfile.portal
/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.kfile.service
```

It also removes only the project's:

```ini
org.freedesktop.impl.portal.FileChooser=kfile;
```

entry from:

```text
~/.config/xdg-desktop-portal/niri-portals.conf
```

Other portal configuration is preserved.

The installer backup is intentionally **not** restored automatically,
because the user may have modified `niri-portals.conf` after installation.

The uninstaller also does **not** remove system packages.

In particular, it leaves:

```text
xdg-desktop-portal
KIO
kio-fuse
Qt
KDE Frameworks
```

installed.

## Known limitations

* The dialog's `parent_window` is currently not used, so the dialog is
  not guaranteed to be transient for the calling application on every
  compositor.
* Remote URL localization depends on KIOFuse being available and able
  to access the selected remote resource.
* The current project is specifically tested with Arch Linux + Niri.
* Other distribution support is experimental and untested.

## Project status

This project is primarily intended for the author's Arch Linux + Niri
environment.

It is provided as-is.

The implementation deliberately avoids installing the full
`xdg-desktop-portal-kde` stack when only KDE's file picker is required.
