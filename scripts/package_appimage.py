#!/usr/bin/env python3
"""
Builds dist/Sectio-x86_64.AppImage: a single portable file that runs on
(almost) any x86_64 Linux desktop with no install step.

This reuses package_linux.py's self-contained dist/linux-x86 bundle (Qt,
VTK, OpenCASCADE, QML modules, Qt plugins, all RPATH-patched and stripped)
as the payload -- it's always (re-)run first, so the AppImage never goes
stale relative to the current build. This script's own job is just to lay
that bundle out as an AppDir (AppRun, .desktop, icon) and hand it to
appimagetool.

Usage:
    scripts/package_appimage.py [--build-dir build] [--icon scripts/sectio.png]

Requires: everything package_linux.py requires, plus curl (or wget) the
first time, to fetch appimagetool if it isn't already cached.
"""
import argparse
import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path

APP_NAME = "Sectio"
APP_ID = "sectio"
APPIMAGETOOL_URL = (
    "https://github.com/AppImage/appimagetool/releases/download/"
    "continuous/appimagetool-x86_64.AppImage"
)

DESKTOP_ENTRY = f"""[Desktop Entry]
Type=Application
Name={APP_NAME}
Comment=STL/STEP viewer built on Qt Quick and VTK, with a live section-view
Exec={APP_NAME} %f
Icon={APP_ID}
Categories=Graphics;Engineering;3DGraphics;
Keywords=CAD;3D;STL;STEP;STP;Mesh;Model;Viewer;OpenCASCADE;VTK;Engineering;
MimeType=model/stl;application/sla;model/step;application/vnd.step;
Terminal=false
"""

# model/stl is already in most distros' base shared-mime-info database, but
# model/step generally is NOT (verified on this machine: it only resolved
# via `xdg-mime query` because of an unrelated third-party app's Flatpak
# export, /var/lib/flatpak/exports/share/mime/model/step.xml -- on a
# machine without that app installed, .step/.stp files wouldn't have a
# glob-recognized mimetype at all). Bundling both here means "Open With"
# and double-click association work regardless of what else is installed,
# once the AppImage is desktop-integrated (e.g. via appimaged).
MIME_XML = """<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="model/stl">
    <comment>STL 3D model</comment>
    <glob pattern="*.stl"/>
  </mime-type>
  <mime-type type="model/step">
    <comment>STEP 3D model</comment>
    <glob pattern="*.step"/>
    <glob pattern="*.stp"/>
  </mime-type>
</mime-info>
"""


def find_or_fetch_appimagetool(cache_dir: Path) -> Path:
    cached = cache_dir / "appimagetool-x86_64.AppImage"
    if cached.is_file() and os.access(cached, os.X_OK):
        return cached

    on_path = shutil.which("appimagetool")
    if on_path:
        return Path(on_path)

    cache_dir.mkdir(parents=True, exist_ok=True)
    print(f"==> appimagetool not found -- downloading from {APPIMAGETOOL_URL}")
    downloader = shutil.which("curl") or shutil.which("wget")
    if not downloader:
        sys.exit("error: need curl or wget to fetch appimagetool (or install it and put it on PATH)")

    tmp_path = cached.with_suffix(".tmp")
    if "curl" in downloader:
        subprocess.run([downloader, "-L", "-o", str(tmp_path), APPIMAGETOOL_URL], check=True)
    else:
        subprocess.run([downloader, "-O", str(tmp_path), APPIMAGETOOL_URL], check=True)
    tmp_path.rename(cached)
    cached.chmod(cached.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
    return cached


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", default=None, help="CMake build directory (default: <repo>/build)")
    ap.add_argument("--icon", default=None, help="Path to a square PNG icon (default: scripts/sectio.png)")
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    scripts_dir = repo_root / "scripts"
    dist_linux = repo_root / "dist" / "linux-x86"
    appdir = repo_root / "dist" / "appimage" / f"{APP_NAME}.AppDir"
    icon_path = Path(args.icon).resolve() if args.icon else scripts_dir / "sectio.png"

    if not icon_path.is_file():
        sys.exit(f"error: icon not found at {icon_path} (pass --icon to point at one)")

    for tool in ("ldd", "patchelf"):
        if not shutil.which(tool):
            sys.exit(f"error: required tool '{tool}' not found on PATH")

    # ---- 1. Always refresh dist/linux-x86 first, so the AppImage reflects the current build ----
    package_linux_args = [sys.executable, str(scripts_dir / "package_linux.py")]
    if args.build_dir:
        package_linux_args += ["--build-dir", args.build_dir]
    print("==> Refreshing dist/linux-x86 (via package_linux.py)")
    subprocess.run(package_linux_args, check=True)

    if not (dist_linux / APP_NAME).is_file():
        sys.exit(f"error: {dist_linux / APP_NAME} missing after package_linux.py ran -- see its output above")

    # ---- 2. Lay out the AppDir ----
    # dist/linux-x86's internal layout (binary at root, lib/qml/plugins as
    # immediate siblings) is copied in as-is: every .so in it already has an
    # $ORIGIN-relative RPATH computed for exactly that layout, so preserving
    # it here means nothing needs to be re-patched.
    print(f"==> Building AppDir at {appdir}")
    if appdir.exists():
        shutil.rmtree(appdir)
    shutil.copytree(dist_linux, appdir)

    # AppRun is the AppImage convention's entry point; it's just our
    # existing "Sectio" wrapper (which already sets QML2_IMPORT_PATH/
    # QT_PLUGIN_PATH relative to its own location and execs Sectio.bin)
    # under the name appimagetool/the AppImage runtime looks for.
    apprun = appdir / "AppRun"
    if apprun.exists() or apprun.is_symlink():
        apprun.unlink()
    apprun.symlink_to(APP_NAME)

    (appdir / f"{APP_NAME}.desktop").write_text(DESKTOP_ENTRY)
    shutil.copy2(icon_path, appdir / f"{APP_ID}.png")

    # Conventional location shared-mime-info-aware desktop integration
    # tools (e.g. appimaged) look for and install into
    # ~/.local/share/mime/packages/ when the AppImage is integrated.
    mime_dir = appdir / "usr" / "share" / "mime" / "packages"
    mime_dir.mkdir(parents=True, exist_ok=True)
    (mime_dir / f"{APP_ID}.xml").write_text(MIME_XML)

    # ---- 3. Build the AppImage ----
    appimagetool = find_or_fetch_appimagetool(scripts_dir / ".cache")
    output_dir = repo_root / "dist"
    output_path = output_dir / f"{APP_NAME}-x86_64.AppImage"
    output_dir.mkdir(parents=True, exist_ok=True)
    if output_path.exists():
        output_path.unlink()

    print("==> Running appimagetool")
    env = dict(os.environ)
    env["ARCH"] = "x86_64"
    # Avoids requiring a working FUSE mount (appimagetool is itself
    # distributed as an AppImage) -- it self-extracts to a temp dir and
    # runs from there instead, which works in more environments
    # (containers, CI, sandboxes) than a FUSE mount would.
    env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    subprocess.run([str(appimagetool), str(appdir), str(output_path)], check=True, env=env)
    output_path.chmod(output_path.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)

    print(f"==> Done: {output_path} ({output_path.stat().st_size / (1024**2):.0f} MiB)")
    print(f"==> Run: {output_path}")


if __name__ == "__main__":
    main()
