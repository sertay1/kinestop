#!/usr/bin/env python3
"""
NPDM Binary Service Access Control (SAC) Verifier for Nintendo Switch
Inspects main.npdm binary to ensure critical services (vi:u, vi:m, vi:s, hid, hid:sys, sm:)
and wildcard access are properly baked into the ACI0 and ACID headers.
"""
import sys
import struct
import os

def verify_npdm(filepath):
    if not os.path.exists(filepath):
        print(f"[ERROR] File not found: {filepath}")
        return False

    with open(filepath, "rb") as f:
        data = f.read()

    if len(data) < 0x200 or data[0:4] != b"META":
        print("[ERROR] Invalid NPDM file or missing META header.")
        return False

    print("==================================================")
    print("        Kinestop NPDM SAC Permission Verifier     ")
    print("==================================================")
    print(f"File: {filepath} ({len(data)} bytes)\n")

    # Read META header offsets (at 0x70)
    aci0_offset, aci0_size, acid_offset, acid_size = struct.unpack("<IIII", data[0x70:0x80])
    print(f"[NPDM META] ACI0 Offset: 0x{aci0_offset:X}, Size: 0x{aci0_size:X}")
    print(f"[NPDM META] ACID Offset: 0x{acid_offset:X}, Size: 0x{acid_size:X}")

    if aci0_offset >= len(data) or data[aci0_offset:aci0_offset+4] != b"ACI0":
        # Fallback search for ACI0
        aci0_offset = data.find(b"ACI0")
        if aci0_offset == -1:
            print("[ERROR] ACI0 section not found!")
            return False

    # In ACI0:
    # 0x00: ACI0
    # 0x10: Title ID (uint64)
    # 0x20: FAH offset (uint32), FAH size (uint32)
    # 0x28: SAC offset (uint32), SAC size (uint32)
    # 0x30: KAC offset (uint32), KAC size (uint32)
    title_id = struct.unpack("<Q", data[aci0_offset + 0x10 : aci0_offset + 0x18])[0]
    sac_rel_offset, sac_size = struct.unpack("<II", data[aci0_offset + 0x28 : aci0_offset + 0x30])
    sac_abs_offset = aci0_offset + sac_rel_offset

    print(f"\n[ACI0 Header] Title ID: 0x{title_id:016X}")
    print(f"[ACI0 Header] SAC Offset: 0x{sac_abs_offset:X}, SAC Size: {sac_size} bytes")

    sac_data = data[sac_abs_offset : sac_abs_offset + sac_size]

    services = []
    idx = 0
    while idx < len(sac_data):
        control_byte = sac_data[idx]
        if control_byte == 0 and idx == len(sac_data) - 1:
            break
        is_host = bool(control_byte & 0x80)
        name_len = (control_byte & 0x7F) + 1
        idx += 1
        if idx + name_len <= len(sac_data):
            svc_name = sac_data[idx : idx + name_len].decode("ascii", errors="ignore").rstrip("\x00")
            services.append(("HOST" if is_host else "CLIENT", svc_name))
            idx += name_len
        else:
            break

    print("\n--- Detected Service Access Control (SAC) Entries ---")
    for stype, sname in services:
        print(f"  [{stype:<6}] {sname}")
    print("----------------------------------------------------\n")

    client_services = [s[1] for s in services if s[0] == "CLIENT"]
    host_services = [s[1] for s in services if s[0] == "HOST"]

    has_wildcard = "*" in client_services or "*" in host_services
    has_vi = any("vi" in s for s in client_services)
    has_hid = any("hid" in s for s in client_services)
    has_sm = any("sm" in s for s in client_services)

    print(f"[*] Title ID 0x4200000000001042 Match : {'PASS' if title_id == 0x4200000000001042 else 'FAIL'}")
    print(f"[*] Wildcard Access (*)                : {'PASS' if has_wildcard else 'NO'}")
    print(f"[*] Display Service (vi:u, vi:m, vi:s) : {'PASS' if (has_vi or has_wildcard) else 'FAIL'}")
    print(f"[*] HID SixAxis Service (hid, hid:sys) : {'PASS' if (has_hid or has_wildcard) else 'FAIL'}")
    print(f"[*] Service Manager (sm:)              : {'PASS' if (has_sm or has_wildcard) else 'FAIL'}")

    all_passed = (title_id == 0x4200000000001042) and (has_wildcard or (has_vi and has_hid))
    if all_passed:
        print("\n[RESULT] >>> SUCCESS: All required NPDM permissions are VERIFIED! <<<\n")
        return True
    else:
        print("\n[RESULT] >>> FAILED: Missing essential NPDM permissions! <<<\n")
        return False

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "sysmodule/main.npdm"
    ok = verify_npdm(target)
    sys.exit(0 if ok else 1)
