# Sectio

*Sectio* (Latin: a cutting, a division) — a Qt Quick + VTK viewer for STL and STEP
files, with a live section-view (cutting plane) feature.

## What it does

- Imports **STL** (`vtkSTLReader`) and **STEP** (`OcctStepSource`, backed by
  OpenCASCADE) files, rendered as VTK actors in a Qt Quick viewport.
- Object list sidebar: per-object visibility toggle, delete, click-to-select
  (also selectable by clicking the actor in the 3D view).
- Global **opacity** slider (all actors; newly imported objects pick up the
  current value).
- **Section view**: a single shared cutting plane, toggleable, aligned to
  X/Y/Z with an adjustable rotation and distance, applied to every actor at
  once with proper capped cross-sections (`vtkClipClosedSurface`).
- Dark/light theme (Qt Quick Controls Material style), toggled from a
  Settings dialog and persisted across runs.


## Build

Requires Qt 6 (Quick, QuickControls2), VTK built with `GUISupportQtQuick` (VTK >= 9.2,
built against Qt6), and OpenCASCADE (with the DataExchange module, for STEP support).
If these are installed under a non-system prefix (e.g. `~/.local`), CMake picks that
up automatically via `CMAKE_PREFIX_PATH` in `CMakeLists.txt` — adjust that line if
yours live elsewhere.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/Sectio
```

## Packaging a standalone Linux build

`scripts/package_linux.py` bundles the built binary together with every Qt,
VTK, and OpenCASCADE shared library, QML module, and Qt plugin it needs into
`dist/linux-x86/` (RPATH-patched via `patchelf`, debug symbols stripped).
The result runs standalone: the only things still required from the target
machine are glibc and its own graphics/windowing stack (X11/Wayland/GL/GPU
driver) -- those two categories are deliberately never bundled, since they
must match the host, not travel with the app. Requires `patchelf`, `ldd`,
and `qtpaths6` on `PATH`.

The real executable ends up at `dist/linux-x86/Sectio.bin`; `dist/linux-x86/Sectio`
is a small wrapper script that points `QML2_IMPORT_PATH`/`QT_PLUGIN_PATH` at
the bundled `qml/`/`plugins/` dirs before exec'ing it -- without that, Qt
falls back to whatever qml/plugin paths were compiled into `libQt6Core` at
build time (verified via `strace`: the *system* Qt's paths), which would
silently defeat the bundling on a machine without system Qt installed. Run
the wrapper, not the `.bin` directly.

```sh
cmake --build build -j
python3 scripts/package_linux.py
dist/linux-x86/Sectio
```

## Building an AppImage

`scripts/package_appimage.py` wraps the above into a single portable
`dist/Sectio-x86_64.AppImage` file. It always re-runs `package_linux.py`
first (so the AppImage can't go stale), lays the result out as an AppDir
(reusing the same wrapper as `AppRun`, plus a `.desktop` entry and the icon
at `scripts/sectio.png`), and hands it to `appimagetool` -- downloaded
automatically into `scripts/.cache/` on first use if not already on `PATH`.

```sh
cmake --build build -j
python3 scripts/package_appimage.py
dist/Sectio-x86_64.AppImage
```
