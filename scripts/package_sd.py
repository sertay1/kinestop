#!/usr/bin/env python3
"""
Kinestop SD card packager.

Kinestop is a modification of the original Kinestop by SertAy
(https://github.com/sertay1/Kinestop): a single overlay that runs in
Ultrahand's default 4 MB, with the settings panel and the dot HUD as two
modes of the same kinestop.ovl.

Lays the built overlay out as an SD-card-ready tree:

  sdcard/
    switch/.overlays/kinestop.ovl   <- overlay/kinestop.ovl

Deliberately NOT packaged:
  * config/kinestop/config.ini
      Shipping a config.ini would overwrite the user's settings (and their
      calibration) every time they copy a new release over their SD card.
      The overlay creates sane defaults when the file is missing, and keys
      missing from an older file keep their defaults.
  * atmosphere/contents/4200000000001042/
      The sysmodule of the earlier 2.0.0 test builds is gone; testers delete
      that folder by hand (see README).

Usage:
  scripts/package_sd.py [BASE_DIR] [--zip] [--out DIR]

  --out DIR must be new, empty or an earlier output of this script (it is
  deleted and recreated); point it at a folder, not at the SD card itself.
"""
import argparse
import os
import shutil
import struct
import sys
import zipfile

ZIP_NAME = "Kinestop.zip"
HEAP_SIZE = 4 * 1024 * 1024  # nx-ovlloader / Ultrahand default overlay memory


class PackageError(Exception):
    pass


def require_file(path, what, hint):
    if not os.path.isfile(path):
        raise PackageError(f"{what} not found: {path}\n    {hint}")
    if os.path.getsize(path) == 0:
        raise PackageError(f"{what} is empty: {path}\n    {hint}")


def ovl_info(path):
    """Returns (image_size, version): the NRO's text+ro+data+bss size (what
    nx-ovlloader carves out of its heap before the overlay's own malloc heap)
    and the display version from the NACP in the asset section (or None)."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 0x40 or data[0x10:0x14] != b"NRO0":
        raise PackageError(f"{path} is not an NRO (.ovl) file")
    nro_size = struct.unpack_from("<I", data, 0x18)[0]
    data_off, data_size = struct.unpack_from("<II", data, 0x30)
    bss_size = struct.unpack_from("<I", data, 0x38)[0]
    image = data_off + data_size + bss_size

    version = None
    try:
        if data[nro_size:nro_size + 4] == b"ASET":
            nacp_off, nacp_size = struct.unpack_from("<QQ", data, nro_size + 0x18)
            if nacp_size >= 0x3070:
                nacp = data[nro_size + nacp_off:nro_size + nacp_off + nacp_size]
                version = nacp[0x3060:0x3070].split(b"\0", 1)[0].decode("ascii", "replace") or None
    except struct.error:
        pass
    return image, version


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


def is_package_tree(path):
    """True when path holds nothing but an earlier package's files."""
    for root, _, files in os.walk(path):
        for name in files:
            rel = os.path.relpath(os.path.join(root, name), path).replace(os.sep, "/")
            if rel != "switch/.overlays/kinestop.ovl":
                return False
    return True


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
    ovl = os.path.join(base_dir, "overlay", "kinestop.ovl")

    print("==================================================")
    print("          Kinestop SD Card Packager")
    print("==================================================")

    # Validate everything before touching the output directory.
    require_file(ovl, "Overlay", "Run 'make' first.")
    image, version = ovl_info(ovl)

    # overlay/kinestop.ovl is tracked in git (upstream prebuilt), so it exists
    # even when the overlay was never rebuilt. Warn about a stale binary.
    src_dirs = [os.path.join(base_dir, d) for d in ("overlay/source", "common", "overlay/lib/libultrahand")]
    if newest_mtime(src_dirs, (".cpp", ".hpp", ".h", ".c")) > os.path.getmtime(ovl):
        print("[!] overlay/kinestop.ovl is older than its sources - "
              "it may be the stale prebuilt. Run 'make'.")

    # Start from an empty tree so files from older packages (a sample
    # config.ini, the 2.0.0 test sysmodule) can never leak into a new release.
    # A --out directory is only cleared when it holds nothing but an earlier
    # package, so pointing it at an SD card cannot wipe the card.
    if os.path.exists(sd_root):
        if out_dir and not is_package_tree(sd_root):
            raise PackageError(f"{sd_root} already exists and is not an earlier Kinestop package; "
                               "refusing to delete it.\n    Use a new or empty --out directory.")
        shutil.rmtree(sd_root)
    overlays_dir = os.path.join(sd_root, "switch", ".overlays")
    os.makedirs(overlays_dir)

    shutil.copy2(ovl, os.path.join(overlays_dir, "kinestop.ovl"))
    print("[+] Packaged: switch/.overlays/kinestop.ovl" + (f" (version {version})" if version else ""))
    print(f"[i] Image + bss: {image} bytes ({image / 1024:.0f} KiB) of the {HEAP_SIZE // (1024 * 1024)} MiB "
          f"overlay heap; {HEAP_SIZE - image} bytes left for the overlay's own heap")
    print("[i] Not packaged: config/kinestop/config.ini (created on first run; keeps user settings on update)")

    print_tree(sd_root)

    if want_zip:
        make_zip(sd_root, os.path.join(base_dir, ZIP_NAME))

    print(f"\n[SUCCESS] Copy the contents of {os.path.relpath(sd_root, os.getcwd())}/ to the root of your SD card.")


def main():
    ap = argparse.ArgumentParser(description="Lay out the Kinestop overlay as an SD card tree.")
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
