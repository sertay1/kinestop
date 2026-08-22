with open(r"C:\Users\serta\.gemini\antigravity\scratch\kinestop\sysmodule\main.npdm", "rb") as f:
    data = f.read()

import struct

print("Length:", len(data))
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    hex_str = " ".join(f"{b:02X}" for b in chunk)
    ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
    if any(b != 0 for b in chunk):
        print(f"{i:04X}:  {hex_str:<48}  {ascii_str}")
