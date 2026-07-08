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
