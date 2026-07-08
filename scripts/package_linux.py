#!/usr/bin/env python3
"""
Bundles Sectio and every non-system shared library, Qt plugin, and QML
module it needs into dist/linux-x86, so the only thing still required from
the target machine is glibc and the host's own graphics/windowing stack
(X11/Wayland/GL/driver libraries) -- see EXCLUDE_PATTERNS for why those two
categories are deliberately left out.

Usage:
    scripts/package_linux.py [--build-dir build]

Requires: patchelf, ldd, qtpaths6 (or qtpaths) on PATH.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

APP_NAME = "Sectio"

# Libraries assumed present -- and required to match the host -- on every
# target Linux machine:
#   - core glibc: ABI-stable and tied to the running kernel; vendoring it
#     is unnecessary and can even break NSS/dlopen-based lookups.
#   - X11/Wayland/GL/driver stack: must match the host compositor and GPU
#     driver. Bundling a different version than what's actually installed
#     is a classic way to turn "portable" into "crashes on other machines".
EXCLUDE_PATTERNS = [
    r"^ld-linux.*\.so",
    r"^libc\.so", r"^libm\.so", r"^libdl\.so", r"^libpthread\.so",
    r"^librt\.so", r"^libresolv\.so", r"^libutil\.so", r"^libnsl\.so",
    r"^libcrypt\.so",
    r"^libGL(X|dispatch)?\.so", r"^libEGL\.so", r"^libOpenGL\.so",
    r"^libgbm\.so", r"^libdrm.*\.so", r"^libvulkan\.so",
    r"^libX11.*\.so", r"^libxcb.*\.so", r"^libXext\.so", r"^libXrender\.so",
    r"^libXi\.so", r"^libXrandr\.so", r"^libXfixes\.so", r"^libXcursor\.so",
    r"^libXcomposite\.so", r"^libXdamage\.so", r"^libXtst\.so", r"^libXinerama\.so",
    r"^libwayland-.*\.so", r"^libxkbcommon.*\.so",
    r"^libnvidia.*\.so", r"^libglapi\.so",
]
EXCLUDE_RE = re.compile("|".join(EXCLUDE_PATTERNS))

# Qt plugin categories worth bundling for a Quick app: platform backends
# (both X11 and Wayland, since we don't know the target's session type),
# their sub-integrations, image/icon support, and TLS (pulled in
# transitively via QtNetwork). Any that don't exist in this Qt install are
# silently skipped.
#
# Deliberately excludes "platformthemes": Qt auto-selects a native theme
# integration plugin based on the desktop environment (e.g. libqgtk3 under
# GNOME), and that single plugin drags in an entire second UI toolkit --
# GTK3, pango, cairo, atk, atspi, ~80 libraries -- that this app (Material
# style, no native dialogs) never needs.
PLUGIN_CATEGORIES = [
    "platforms", "xcbglintegrations", "wayland-graphics-integration-client",
    "wayland-decoration-client", "wayland-shell-integration",
    "platforminputcontexts", "imageformats",
    "iconengines", "styles", "tls", "generic",
]


def qt_query(key: str) -> str:
    for tool in ("qtpaths6", "qtpaths"):
        if shutil.which(tool):
            out = subprocess.run([tool, "--query", key], capture_output=True, text=True, check=True)
            return out.stdout.strip()
    sys.exit("error: neither qtpaths6 nor qtpaths found on PATH")


def find_qmlimportscanner(qt_libexec: str) -> str:
    # On some distros /usr/bin/qmlimportscanner is a qtchooser dispatcher
    # that fails outright even when a perfectly good Qt6 is installed; the
    # real binary lives under Qt's libexec dir. Try that first, verified by
    # actually running it, then fall back to whatever's on PATH.
    candidates = [os.path.join(qt_libexec, "qmlimportscanner"), shutil.which("qmlimportscanner")]
    for candidate in candidates:
        if not candidate or not os.path.isfile(candidate):
            continue
        probe = subprocess.run([candidate, "-rootPath", "."], capture_output=True, text=True)
        if probe.returncode == 0:
            return candidate
    sys.exit(f"error: could not find a working qmlimportscanner (tried: {candidates})")


def ldd_deps(path: str):
    """Yields (needed_name, resolved_path) for each direct shared-library
    dependency of `path`, per `ldd`'s output. needed_name is the exact
    NEEDED entry (what the dynamic linker actually looks up at runtime) --
    NOT the basename of resolved_path, which can differ across symlinks."""
    result = subprocess.run(["ldd", path], capture_output=True, text=True)
    if result.returncode != 0:
        return
    for line in result.stdout.splitlines():
        line = line.strip()
        m = re.match(r"^(\S+) => (\S+) \(0x", line)
        if m:
            needed, resolved = m.group(1), m.group(2)
            if resolved != "not":
                yield needed, resolved
            continue
        # Lines with no "=>", e.g. "/lib64/ld-linux-x86-64.so.2 (0x...)".
        # linux-vdso.so.1 has no real path and won't match this either --
        # correctly skipped, there's nothing to bundle for it.
        m2 = re.match(r"^(/\S+) \(0x", line)
        if m2:
            yield os.path.basename(m2.group(1)), m2.group(1)


def set_rpath(target: Path, rpath: str) -> None:
    # --force-rpath writes the legacy DT_RPATH tag instead of the modern
    # DT_RUNPATH. This matters: DT_RUNPATH is overridden by LD_LIBRARY_PATH,
    # so on a target machine with its own (possibly incompatible) VTK/Qt on
    # LD_LIBRARY_PATH, a RUNPATH-based bundle could silently load those
    # instead of our bundled, version-matched copies. DT_RPATH always wins,
    # which is what "self-contained" is supposed to mean here.
    subprocess.run(["patchelf", "--set-rpath", rpath, "--force-rpath", str(target)], check=True)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", default=None, help="CMake build directory (default: <repo>/build)")
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = Path(args.build_dir).resolve() if args.build_dir else repo_root / "build"
    dist_dir = repo_root / "dist" / "linux-x86"
    qml_src_dir = repo_root / "qml"

    binary = build_dir / APP_NAME
    if not binary.is_file() or not os.access(binary, os.X_OK):
        sys.exit(f"error: {binary} not found or not executable -- build the project first "
                  f"(cmake --build {build_dir})")

    for tool in ("patchelf", "ldd"):
        if not shutil.which(tool):
            sys.exit(f"error: required tool '{tool}' not found on PATH")

    print(f"==> Packaging {APP_NAME} from {build_dir} into {dist_dir}")

    if dist_dir.exists():
        shutil.rmtree(dist_dir)
    lib_dir = dist_dir / "lib"
    qml_dir = dist_dir / "qml"
    plugins_dir = dist_dir / "plugins"
    for d in (dist_dir, lib_dir, qml_dir, plugins_dir):
        d.mkdir(parents=True, exist_ok=True)

    # The real executable is named "Sectio.bin"; "Sectio" itself becomes a
    # thin wrapper script (added at the end) that sets QML2_IMPORT_PATH/
    # QT_PLUGIN_PATH before exec'ing it. Without that, Qt falls back to
    # whatever qml/plugin paths were compiled into libQt6Core at build time
    # -- verified via strace to be the *system* Qt's paths, silently
    # defeating the entire point of bundling our own qml/ and plugins/ (it
    # only "worked" on this dev machine because system Qt happened to still
    # be installed).
    dest_binary = dist_dir / f"{APP_NAME}.bin"
    shutil.copy2(binary, dest_binary)
    os.chmod(dest_binary, 0o755)

    qt_qml = qt_query("QT_INSTALL_QML")
    qt_plugins = qt_query("QT_INSTALL_PLUGINS")
    qt_libexec = qt_query("QT_INSTALL_LIBEXECS")

    # ---- 1. QML modules actually used by qml/main.qml (transitively) ----
    scanner = find_qmlimportscanner(qt_libexec)
    scan = subprocess.run(
        [scanner, "-rootPath", str(qml_src_dir), "-importPath", qt_qml],
        capture_output=True, text=True, check=True,
    )
    modules = json.loads(scan.stdout)

    all_plugin_sos = []  # seeds the dependency closure below, alongside the main binary

    print("==> Copying QML modules")
    for mod in modules:
        src_path = mod.get("path")
        rel_path = mod.get("relativePath")
        if not src_path or not rel_path or not os.path.isdir(src_path):
            # Either a style variant unavailable on this platform (Windows/
            # macOS/iOS/FluentWinUI3) or our own C++-registered SceneApp
            # module, which has no plugin directory to copy.
            continue
        dest = qml_dir / rel_path
        shutil.copytree(src_path, dest, dirs_exist_ok=True)
        all_plugin_sos.extend(str(p) for p in dest.rglob("*.so"))
        print(f"    {mod.get('name')} -> qml/{rel_path}")

    # ---- 2. Qt plugins needed at runtime (platform backend, image formats, etc.) ----
    print("==> Copying Qt plugins")
    for category in PLUGIN_CATEGORIES:
        src = os.path.join(qt_plugins, category)
        if not os.path.isdir(src):
            continue
        dest = plugins_dir / category
        shutil.copytree(src, dest, dirs_exist_ok=True)
        all_plugin_sos.extend(str(p) for p in dest.rglob("*.so"))
        print(f"    {category}/")

    # ---- 3. Recursively resolve every shared-library dependency ----
    print("==> Resolving shared library dependencies")
    seen_libs = {}  # NEEDED name -> dest path, to dedupe
    queue = [str(dest_binary)] + all_plugin_sos
    processed = set()
    while queue:
        current = queue.pop()
        if current in processed:
            continue
        processed.add(current)
        for needed, resolved_path in ldd_deps(current):
            if EXCLUDE_RE.match(needed) or needed in seen_libs:
                continue
            if not os.path.isfile(resolved_path):
                continue
            dest = lib_dir / needed
            shutil.copy2(resolved_path, dest)
            seen_libs[needed] = str(dest)
            queue.append(resolved_path)  # chase its dependencies too
            print(f"    {needed}")

    # ---- 4. Set RPATH so the dynamic linker finds everything bundled above ----
    print("==> Setting RPATH")
    set_rpath(dest_binary, "$ORIGIN/lib")

    for so in lib_dir.glob("*.so*"):
        if so.is_file() and not so.is_symlink():
            set_rpath(so, "$ORIGIN")

    for so in list(qml_dir.rglob("*.so")) + list(plugins_dir.rglob("*.so")):
        depth = len(so.relative_to(dist_dir).parts) - 1  # directory levels below dist_dir
        rpath = "$ORIGIN/" + "/".join([".."] * depth) + "/lib"
        set_rpath(so, rpath)

    # ---- 5. Strip debug info ----
    # These are distro/from-source builds with full debug_info still
    # attached; libvtkCommonCore-9.6.so.1 alone is 461MB unstripped vs. 72MB
    # stripped (--strip-unneeded keeps the dynamic symbol table needed for
    # linking/dlopen, only drops debug info and local symbols -- safe for
    # shared libraries and executables alike).
    if shutil.which("strip"):
        print("==> Stripping debug symbols")
        targets = [dest_binary] + [p for p in lib_dir.glob("*.so*") if p.is_file() and not p.is_symlink()]
        targets += list(qml_dir.rglob("*.so")) + list(plugins_dir.rglob("*.so"))
        subprocess.run(["strip", "--strip-unneeded", *[str(t) for t in targets]], check=False)
    else:
        print("==> 'strip' not found on PATH -- skipping (package will be larger than necessary)")

    # ---- 6. Wrapper script (the actual entry point) ----
    wrapper_path = dist_dir / APP_NAME
    wrapper_path.write_text(
        "#!/bin/sh\n"
        "HERE=\"$(cd \"$(dirname \"$(readlink -f \"$0\")\")\" && pwd)\"\n"
        "export QML2_IMPORT_PATH=\"$HERE/qml${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}\"\n"
        "export QML_IMPORT_PATH=\"$HERE/qml${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}\"\n"
        "export QT_PLUGIN_PATH=\"$HERE/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}\"\n"
        f'exec "$HERE/{APP_NAME}.bin" "$@"\n'
    )
    wrapper_path.chmod(0o755)

    total_size = sum(f.stat().st_size for f in dist_dir.rglob("*") if f.is_file())
    print(f"==> Done: {len(seen_libs)} libraries bundled, {total_size / (1024**2):.0f} MiB total")
    print(f"==> Run: {wrapper_path}")


if __name__ == "__main__":
    main()
