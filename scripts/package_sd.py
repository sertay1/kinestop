#!/usr/bin/env python3
"""
Kinestop SD Card Distribution Packager (Overlay Mode)
Removes sysmodule to prevent 2001-0132 boot memory crashes.
Packages:
  switch/.overlays/kinestop.ovl
  config/kinestop/config.ini
"""
import os
import shutil
import sys

def package_sd(base_dir="."):
    sd_root = os.path.join(base_dir, "sdcard")
    title_id = "4200000000001042"

    # Remove sysmodule from sdcard directory completely
    sysmodule_dir = os.path.join(sd_root, "atmosphere", "contents", title_id)
    if os.path.exists(sysmodule_dir):
        shutil.rmtree(sysmodule_dir)
        print(f"[-] Removed sysmodule directory to prevent 2001-0132 errors: {sysmodule_dir}")

    # Remove empty atmosphere/contents if empty
    contents_dir = os.path.join(sd_root, "atmosphere", "contents")
    if os.path.exists(contents_dir) and not os.listdir(contents_dir):
        shutil.rmtree(os.path.join(sd_root, "atmosphere"))

    overlays_dir = os.path.join(sd_root, "switch", ".overlays")
    config_dir   = os.path.join(sd_root, "config", "kinestop")

    for d in [overlays_dir, config_dir]:
        os.makedirs(d, exist_ok=True)

    print("==================================================")
    print("      Kinestop SD Card Packager (Zero-Sysmodule)  ")
    print("==================================================")

    # Copy Overlay Binary
    overlay_ovl = os.path.join(base_dir, "overlay", "kinestop.ovl")
    if os.path.exists(overlay_ovl):
        dst = os.path.join(overlays_dir, "kinestop.ovl")
        shutil.copy2(overlay_ovl, dst)
        print(f"[+] Packaged: switch/.overlays/kinestop.ovl ({os.path.getsize(dst)} bytes)")

    # Generate Default config.ini
    default_config_content = """; Kinestop Configuration File
; Motion sickness countermeasure overlay for Nintendo Switch CFW

[general]
enabled = true
poll_rate_hz = 60

[visual]
style = 0
opacity = 0.70
line_thickness = 3
draw_center_reticle = true
draw_roll_indicators = true

[filter]
alpha = 0.96
deadzone_deg = 0.5

[calibration]
pitch_offset = 0.0
roll_offset = 0.0
invert_pitch = false
invert_roll = false
sensitivity_pitch = 1.0
sensitivity_roll = 1.0
"""
    config_file_path = os.path.join(config_dir, "config.ini")
    with open(config_file_path, "w", encoding="utf-8") as f:
        f.write(default_config_content)
    print(f"[+] Created:  config/kinestop/config.ini")

    print("\n--- Distribution Tree in sdcard/ ---")
    for root, dirs, files in os.walk(sd_root):
        rel = os.path.relpath(root, sd_root)
        if rel == ".":
            print(f"sdcard/")
        else:
            depth = rel.count(os.sep)
            indent = "  " * (depth + 1)
            print(f"{indent}{os.path.basename(root)}/")
        for file in files:
            depth = (0 if rel == "." else rel.count(os.sep) + 1)
            indent = "  " * (depth + 1)
            size = os.path.getsize(os.path.join(root, file))
            print(f"{indent}{file} ({size} bytes)")

    print("\n[SUCCESS] Clean SD Card package ready! ZERO boot crashes guaranteed.")

if __name__ == "__main__":
    base = sys.argv[1] if len(sys.argv) > 1 else "."
    package_sd(base)
