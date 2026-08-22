import struct

with open(r"C:\Users\serta\.gemini\antigravity\scratch\kinestop\overlay\kinestop.ovl", "rb") as f:
    data = f.read()

print("File size:", len(data))
magic = data[0x10:0x14]
print("NRO Magic:", magic)
if magic == b"NRO0":
    nro_size = struct.unpack("<I", data[0x18:0x1C])[0]
    print(f"NRO Size: {nro_size} (0x{nro_size:X})")
    
    # Asset header is right after NRO
    if len(data) > nro_size:
        asset_hdr = data[nro_size:]
        asset_magic = asset_hdr[0:4]
        print(f"Asset Magic at 0x{nro_size:X}: {asset_magic}")
        if asset_magic == b"ASET":
            icon_off, icon_sz, nacp_off, nacp_sz, romfs_off, romfs_sz = struct.unpack("<QQQQQQ", asset_hdr[8:8+48])
            print(f"NACP Offset in ASET: 0x{nacp_off:X}, Size: 0x{nacp_sz:X}")
            if nacp_sz > 0:
                nacp_data = asset_hdr[nacp_off:nacp_off+nacp_sz]
                # NACP title string is at 0x00 (308 bytes)
                title = nacp_data[0:512].split(b'\x00')[0].decode('utf-8', errors='ignore')
                author = nacp_data[512:768].split(b'\x00')[0].decode('utf-8', errors='ignore')
                print(f"[SUCCESS] Embedded NACP Title: '{title}', Author: '{author}'")
