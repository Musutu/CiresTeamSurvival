"""progression-shop: contact sheet of every generated item icon (review aid).
Usage: python Tools/ItemIconSheet.py OUT.png"""
from pathlib import Path
import json, struct, sys, zlib
ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "Content/UI/Items/src"

def read(path):
    d = path.read_bytes(); pos = 8; w = h = 0; idat = b""
    while pos < len(d):
        n = struct.unpack(">I", d[pos:pos + 4])[0]; t = d[pos + 4:pos + 8]; c = d[pos + 8:pos + 8 + n]; pos += 12 + n
        if t == b"IHDR": w, h = struct.unpack(">II", c[:8])
        elif t == b"IDAT": idat += c
    raw = zlib.decompress(idat); s = w * 4
    return w, h, [raw[y * (s + 1) + 1:(y + 1) * (s + 1)] for y in range(h)]

def main(out):
    ids = [i["id"] for i in json.loads((ROOT / "Content/Data/Items.json").read_text(encoding="utf-8"))["items"]]
    cols, cell = 8, 132
    rows = (len(ids) + cols - 1) // cols
    W, H = cols * cell, rows * cell
    canvas = [bytearray(b"\x10\x10\x14\xff" * W) for _ in range(H)]
    for n, item in enumerate(ids):
        w, h, px = read(SRC / f"T_Item_{item}.png")
        ox, oy = (n % cols) * cell + 2, (n // cols) * cell + 2
        for y in range(h):
            canvas[oy + y][ox * 4:(ox + w) * 4] = px[y]
    raw = b"".join(b"\x00" + bytes(r) for r in canvas)
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    Path(out).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    print(out, len(ids))

if __name__ == "__main__":
    main(sys.argv[1])
