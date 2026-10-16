#!/usr/bin/env python3
"""
Kinestop SD card packager.

Kinestop is a modification of the original Kinestop by SertAy
(https://github.com/sertay1/Kinestop): dot rendering moved into an on-demand
sysmodule; the overlay is a settings panel that starts/stops it.

Lays the built artifacts out as an SD-card-ready tree:

  sdcard/
    atmosphere/contents/4200000000001042/exefs.nsp     <- sysmodule/out/kinestop.nsp
    atmosphere/contents/4200000000001042/toolbox.json  <- sysmodule/toolbox.json
    switch/.overlays/kinestop.ovl                      <- overlay/kinestop.ovl

Deliberately NOT packaged:
  * atmosphere/contents/<tid>/flags/boot2.flag
      The overlay launches the sysmodule on demand (pmshellLaunchProgram).
      A boot2-launched sysmodule that hits the System memory pool limit aborts
      boot with 2001-0132 (svc::ResultLimitReached), so auto-start is an opt-in
      the user enables from the overlay, never a default of the package.
  * config/kinestop/config.ini
      Shipping a config.ini would overwrite the user's settings (and their
      calibration) every time they copy a new release over their SD card.
      Both binaries create sane defaults when the file is missing, and keys
      missing from an older file keep their defaults.

Usage:
  scripts/package_sd.py [BASE_DIR] [--zip] [--out DIR]
"""
import argparse
import json
import os
import re
import shutil
import struct
import sys
import zipfile

FALLBACK_TITLE_ID = "4200000000001042"
ZIP_NAME = "Kinestop.zip"


class PackageError(Exception):
    pass


def read_title_id(base_dir):
    """Single source of truth: KINESTOP_PROGRAM_ID_STR in common/kinestop_ipc.h."""
    header = os.path.join(base_dir, "common", "kinestop_ipc.h")
    try:
        with open(header, "r", encoding="utf-8") as f:
            m = re.search(r'#define\s+KINESTOP_PROGRAM_ID_STR\s+"([0-9A-Fa-f]{16})"', f.read())
        if m:
            return m.group(1).upper()
    except OSError:
        pass
    print(f"[!] Could not read the program id from {header}; using {FALLBACK_TITLE_ID}")
    return FALLBACK_TITLE_ID


def require_file(path, what, hint):
    if not os.path.isfile(path):
        raise PackageError(f"{what} not found: {path}\n    {hint}")
    if os.path.getsize(path) == 0:
        raise PackageError(f"{what} is empty: {path}\n    {hint}")


def check_nsp(path):
    """exefs.nsp is a PFS0 that must hold at least 'main' and 'main.npdm'."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 0x10 or data[0:4] != b"PFS0":
        raise PackageError(f"{path} is not a PFS0 (.nsp) image")
    count, strtab_size = struct.unpack_from("<II", data, 4)
    entries_end = 0x10 + count * 0x18
    if entries_end + strtab_size > len(data):
        raise PackageError(f"{path}: truncated PFS0 header")
    strtab = data[entries_end:entries_end + strtab_size]
    names = []
    for i in range(count):
        str_off = struct.unpack_from("<I", data, 0x10 + i * 0x18 + 0x10)[0]
        end = strtab.find(b"\0", str_off)
        names.append(strtab[str_off:end if end >= 0 else None].decode("ascii", "replace"))
    for needed in ("main", "main.npdm"):
        if needed not in names:
            raise PackageError(f"{path}: PFS0 has no '{needed}' (contains: {', '.join(names) or 'nothing'})")
    return names


def check_toolbox(path, title_id):
    """Validate the fields ovl-sysmodules requires (tid, name, requires_reboot)
    and the optional graceful-shutdown contract (shutdown_service/shutdown_cmd)."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            tb = json.load(f)
    except (OSError, ValueError) as e:
        raise PackageError(f"{path}: invalid JSON ({e})")
    if not isinstance(tb, dict):
        raise PackageError(f"{path}: expected a JSON object")
    tid = tb.get("tid")
    if not isinstance(tid, str) or tid.upper() != title_id:
        raise PackageError(f"{path}: \"tid\" is {tid!r}, expected \"{title_id}\"")
    if not isinstance(tb.get("name"), str):
        raise PackageError(f"{path}: missing string \"name\"")
    if not isinstance(tb.get("requires_reboot"), bool):
        raise PackageError(f"{path}: missing boolean \"requires_reboot\"")
    if tb["requires_reboot"]:
        print(f"[!] {path}: requires_reboot is true, but Kinestop is launched on demand")
    svc, cmd = tb.get("shutdown_service"), tb.get("shutdown_cmd")
    if (svc is None) != (cmd is None):
        print(f"[!] {path}: shutdown_service and shutdown_cmd must be set together; "
              "ovl-sysmodules ignores the contract otherwise")
    elif svc is not None:
        if not isinstance(svc, str) or not 0 < len(svc) <= 8:
            raise PackageError(f"{path}: shutdown_service must be a 1..8 character string")
        if not isinstance(cmd, int) or isinstance(cmd, bool) or cmd < 0:
            raise PackageError(f"{path}: shutdown_cmd must be a non-negative integer")
    return tb


def ovl_version(path):
    """Best effort: display version from the NACP in the NRO asset section."""
    try:
        with open(path, "rb") as f:
            data = f.read()
        if data[0x10:0x14] != b"NRO0":
            return None
        nro_size = struct.unpack_from("<I", data, 0x18)[0]
        if data[nro_size:nro_size + 4] != b"ASET":
            return None
        nacp_off, nacp_size = struct.unpack_from("<QQ", data, nro_size + 0x18)
        if nacp_size < 0x3070:
            return None
        nacp = data[nro_size + nacp_off:nro_size + nacp_off + nacp_size]
        return nacp[0x3060:0x3070].split(b"\0", 1)[0].decode("ascii", "replace") or None
    except (OSError, struct.error):
        return None


def newest_mtime(dirs, exts):
    newest = 0.0
    for d in dirs:
        for root, _, files in os.walk(d):
            for name in files:
                if name.endswith(exts):
                    newest = max(newest, os.path.getmtime(os.path.join(root, name)))
    return newest


def print_tree(sd_root):
    print("\n--- Distribution tree ---")
    for root, dirs, files in os.walk(sd_root):
        dirs.sort()
        rel = os.path.relpath(root, sd_root)
        depth = 0 if rel == "." else rel.count(os.sep) + 1
        print("  " * depth + (os.path.basename(sd_root) if rel == "." else os.path.basename(root)) + "/")
        for name in sorted(files):
            size = os.path.getsize(os.path.join(root, name))
            print("  " * (depth + 1) + f"{name} ({size} bytes)")


def make_zip(sd_root, zip_path):
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, dirs, files in os.walk(sd_root):
            dirs.sort()
            rel_root = os.path.relpath(root, sd_root)
            if rel_root != ".":
                # explicit directory entries so extractors recreate .overlays etc.
                zf.write(root, rel_root.replace(os.sep, "/") + "/")
            for name in sorted(files):
                full = os.path.join(root, name)
                zf.write(full, os.path.relpath(full, sd_root).replace(os.sep, "/"))
    print(f"[+] Created:  {zip_path} ({os.path.getsize(zip_path)} bytes)")


def package_sd(base_dir, out_dir=None, want_zip=False):
    base_dir = os.path.abspath(base_dir)
    sd_root = os.path.abspath(out_dir) if out_dir else os.path.join(base_dir, "sdcard")
    title_id = read_title_id(base_dir)

    nsp = os.path.join(base_dir, "sysmodule", "out", "kinestop.nsp")
    toolbox = os.path.join(base_dir, "sysmodule", "toolbox.json")
    ovl = os.path.join(base_dir, "overlay", "kinestop.ovl")

    print("==================================================")
    print("          Kinestop SD Card Packager")
    print("==================================================")

    # Validate everything before touching the output directory.
    require_file(nsp, "Sysmodule package", "Run 'make sysmodule' (or 'make') first.")
    require_file(toolbox, "Sysmodule toolbox.json", "It lives in sysmodule/toolbox.json; restore it from git.")
    require_file(ovl, "Overlay", "Run 'make overlay' (or 'make') first.")
    nsp_files = check_nsp(nsp)
    check_toolbox(toolbox, title_id)

    # overlay/kinestop.ovl is tracked in git (upstream prebuilt), so it exists
    # even when the overlay was never rebuilt. Warn about a stale binary.
    src_mtime = newest_mtime([os.path.join(base_dir, "overlay", "source"), os.path.join(base_dir, "common")],
                             (".cpp", ".hpp", ".h", ".c"))
    if src_mtime > os.path.getmtime(ovl):
        print("[!] overlay/kinestop.ovl is older than the overlay/common sources - "
              "it may be the stale prebuilt. Run 'make overlay'.")

    # Start from an empty tree so files from older packages (a sample
    # config.ini, a boot2.flag) can never leak into a new release.
    if os.path.exists(sd_root):
        shutil.rmtree(sd_root)
    contents_dir = os.path.join(sd_root, "atmosphere", "contents", title_id)
    overlays_dir = os.path.join(sd_root, "switch", ".overlays")
    os.makedirs(contents_dir)
    os.makedirs(overlays_dir)

    shutil.copy2(nsp, os.path.join(contents_dir, "exefs.nsp"))
    print(f"[+] Packaged: atmosphere/contents/{title_id}/exefs.nsp ({', '.join(nsp_files)})")
    shutil.copy2(toolbox, os.path.join(contents_dir, "toolbox.json"))
    print(f"[+] Packaged: atmosphere/contents/{title_id}/toolbox.json")
    shutil.copy2(ovl, os.path.join(overlays_dir, "kinestop.ovl"))
    ver = ovl_version(ovl)
    print(f"[+] Packaged: switch/.overlays/kinestop.ovl" + (f" (version {ver})" if ver else ""))
    print("[i] Not packaged: flags/boot2.flag (opt-in from the overlay), "
          "config/kinestop/config.ini (created on first run; keeps user settings on update)")

    print_tree(sd_root)

    if want_zip:
        make_zip(sd_root, os.path.join(base_dir, ZIP_NAME))

    print(f"\n[SUCCESS] Copy the contents of {os.path.relpath(sd_root, os.getcwd())}/ to the root of your SD card.")


def main():
    ap = argparse.ArgumentParser(description="Lay out Kinestop build artifacts as an SD card tree.")
    ap.add_argument("base_dir", nargs="?", default=".", help="repository root (default: .)")
    ap.add_argument("--out", help="output directory (default: BASE_DIR/sdcard)")
    ap.add_argument("--zip", action="store_true", help=f"also write BASE_DIR/{ZIP_NAME}")
    args = ap.parse_args()
    try:
        package_sd(args.base_dir, args.out, args.zip)
    except PackageError as e:
        sys.stdout.flush()
        print(f"[ERROR] {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
