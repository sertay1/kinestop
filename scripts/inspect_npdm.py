import struct

with open(r"C:\Users\serta\.gemini\antigravity\scratch\kinestop\sysmodule\main.npdm", "rb") as f:
    data = f.read()

print(f"Total size: {len(data)}")
print("NPDM META Header:")
# META Header (0x00 - 0x80):
# 0x00: "META"
# 0x04 - 0x0C: reserved
# 0x0C: mmu_flags (1 byte)
# 0x0D: reserved (1 byte)
# 0x0E: main_thread_prio (1 byte)
# 0x0F: default_cpu_id (1 byte)
# 0x10: reserved (4 bytes)
# 0x14: process_category (4 bytes)
# 0x18: main_thread_stack_size (4 bytes)
# 0x1C: name (16 bytes)
# 0x2C: product_code (16 bytes)
# 0x3C: reserved (48 bytes)
# 0x6C: aci0_offset (4 bytes)
# 0x70: aci0_size (4 bytes)
# 0x74: acid_offset (4 bytes)
# 0x78: acid_size (4 bytes)

aci0_offset, aci0_size, acid_offset, acid_size = struct.unpack("<IIII", data[0x6C:0x7C])
print(f"aci0_offset: 0x{aci0_offset:X}, aci0_size: 0x{aci0_size:X}")
print(f"acid_offset: 0x{acid_offset:X}, acid_size: 0x{acid_size:X}")

print(f"\nACI0 at 0x{aci0_offset:X}:")
aci0_magic = data[aci0_offset:aci0_offset+4]
print(f"Magic: {aci0_magic}")
# In ACI0 (offset 0x200 / aci0_offset):
# 0x00: "ACI0"
# 0x04 - 0x20: reserved
# 0x20: title_id (8 bytes)
# 0x28: reserved (8 bytes)
# 0x30: fah_offset (4 bytes)
# 0x34: fah_size (4 bytes)
# 0x38: sac_offset (4 bytes)
# 0x3C: sac_size (4 bytes)
# 0x40: kac_offset (4 bytes)
# 0x44: kac_size (4 bytes)
title_id = struct.unpack("<Q", data[aci0_offset+0x20:aci0_offset+0x28])[0]
fah_off, fah_sz, sac_off, sac_sz, kac_off, kac_sz = struct.unpack("<IIIIII", data[aci0_offset+0x30:aci0_offset+0x48])
print(f"Title ID: 0x{title_id:016X}")
print(f"FAH: offset 0x{fah_off:X}, size {fah_sz}")
print(f"SAC: offset 0x{sac_off:X}, size {sac_sz}")
print(f"KAC: offset 0x{kac_off:X}, size {kac_sz}")

print(f"\nACID at 0x{acid_offset:X}:")
acid_magic = data[acid_offset:acid_offset+4]
print(f"Magic: {acid_magic}")
# ACID has RSA sig (0x100), then ACID header at +0x100
acid_hdr = acid_offset + 0x100
acid_magic2 = data[acid_hdr:acid_hdr+4]
print(f"ACID header at 0x{acid_hdr:X}: {acid_magic2}")
if acid_magic2 == b"ACID":
    a_title_id = struct.unpack("<Q", data[acid_hdr+0x20:acid_hdr+0x28])[0]
    a_fah_off, a_fah_sz, a_sac_off, a_sac_sz, a_kac_off, a_kac_sz = struct.unpack("<IIIIII", data[acid_hdr+0x30:acid_hdr+0x48])
    print(f"ACID Title ID: 0x{a_title_id:016X}")
    print(f"ACID SAC: offset 0x{a_sac_off:X}, size {a_sac_sz}")
    print(f"ACID KAC: offset 0x{a_kac_off:X}, size {a_kac_sz}")

    # SAC content in ACID
    acid_sac = data[acid_hdr + a_sac_off : acid_hdr + a_sac_off + a_sac_sz]
    print(f"ACID SAC raw hex: {acid_sac.hex()}")
    idx = 0
    while idx < len(acid_sac):
        cb = acid_sac[idx]
        if cb == 0: break
        is_h = bool(cb & 0x80)
        nlen = (cb & 0x7F) + 1
        idx += 1
        name = acid_sac[idx:idx+nlen].decode("ascii", errors="ignore")
        print(f"  ACID SAC entry: [{'HOST' if is_h else 'CLIENT'}] {name}")
        idx += nlen
