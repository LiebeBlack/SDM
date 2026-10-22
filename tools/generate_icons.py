#!/usr/bin/env python3
# ============================================================================
#  S-DOWNLOAD MANAGER (SDM)
#  tools/generate_icons.py
#
#  Genera los iconos PNG de la extension del navegador (16/32/48/128/256)
#  usando SOLO la libreria estandar de Python (sin Pillow), dibujando una
#  flecha de descarga blanca sobre un cuadrado redondeado verde Fluent
#  (#107C10 -> #0C630E) con antialias por supersampling 4x.
#
#  Tambien inyecta las entradas "icons" y "action.default_icon" en
#  extension/manifest.json si no existen, para que Chrome no muestre
#  avisos de icono ausente.
#
#  Uso:  python tools/generate_icons.py [--no-manifest]
# ============================================================================
from __future__ import annotations

import json
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXT_DIR = ROOT / "extension"
ICONS_DIR = EXT_DIR / "icons"
SIZES = (16, 32, 48, 128, 256)
SUPERSAMPLE = 4


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_png(path: Path, size: int, rows: list[bytes]) -> None:
    """rows: `size` filas de `size * 4` bytes (RGBA)."""
    raw = b"".join(b"\x00" + row for row in rows)
    payload = (
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
        + _png_chunk(b"IDAT", zlib.compress(raw, 9))
        + _png_chunk(b"IEND", b"")
    )
    path.write_bytes(payload)


def _inside_rounded_rect(x: float, y: float, size: float, radius: float) -> bool:
    half = size / 2.0
    dx = max(abs(x - half) - (half - radius), 0.0)
    dy = max(abs(y - half) - (half - radius), 0.0)
    return dx * dx + dy * dy <= radius * radius


def _inside_arrow(x: float, y: float, size: float) -> bool:
    cx = size / 2.0
    # Vástago de la flecha.
    if abs(x - cx) <= 0.08 * size and 0.18 * size <= y <= 0.58 * size:
        return True
    # Punta triangular (anchura decreciente hasta la punta en y = 0.80).
    if 0.50 * size <= y <= 0.80 * size:
        t = (0.80 * size - y) / (0.30 * size)
        return abs(x - cx) <= 0.28 * size * t
    return False


def _lerp(a: float, b: float, t: float) -> int:
    return int(round(a + (b - a) * t))


def render_icon(size: int) -> list[bytes]:
    big = size * SUPERSAMPLE
    radius = big * 0.22
    top = (0x12, 0x84, 0x12)   # verde Fluent acento (claro)
    bottom = (0x0C, 0x63, 0x0E)  # sombra del degradado
    px = bytearray(big * big * 4)

    for y in range(big):
        t = y / max(big - 1, 1)
        g_top = _lerp(top[0], bottom[0], t), _lerp(top[1], bottom[1], t), _lerp(top[2], bottom[2], t)
        for x in range(big):
            if not _inside_rounded_rect(x + 0.5, y + 0.5, big, radius):
                continue
            if _inside_arrow(x + 0.5, y + 0.5, big):
                r, g, b, a = 255, 255, 255, 255
            else:
                r, g, b, a = g_top[0], g_top[1], g_top[2], 255
            i = (y * big + x) * 4
            px[i] = r
            px[i + 1] = g
            px[i + 2] = b
            px[i + 3] = a

    # Box-filter 4x -> tamaño final.
    ss = SUPERSAMPLE
    rows: list[bytes] = []
    for oy in range(size):
        row = bytearray(size * 4)
        for ox in range(size):
            sr = sg = sb = sa = 0
            for dy in range(ss):
                base = ((oy * ss + dy) * big + ox * ss) * 4
                for dx in range(ss):
                    i = base + dx * 4
                    if px[i + 3]:
                        sr += px[i]
                        sg += px[i + 1]
                        sb += px[i + 2]
                        sa += 255
                    # Fuera del rectángulo: RGBA 0,0,0,0 (índice i ya es 0).
            n = ss * ss
            cnt = sa // 255  # muestras opacas (RGB en alpha straight)
            j = ox * 4
            if cnt:
                row[j] = min(255, sr // cnt)
                row[j + 1] = min(255, sg // cnt)
                row[j + 2] = min(255, sb // cnt)
            row[j + 3] = sa // n
        rows.append(bytes(row))
    return rows


def generate() -> list[Path]:
    ICONS_DIR.mkdir(parents=True, exist_ok=True)
    produced: list[Path] = []
    for size in SIZES:
        out = ICONS_DIR / f"icon{size}.png"
        write_png(out, size, render_icon(size))
        produced.append(out)
    return produced


def patch_manifest() -> bool:
    manifest_path = EXT_DIR / "manifest.json"
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    changed = False

    icons = {str(s): f"icons/icon{s}.png" for s in SIZES}
    if "icons" not in data:
        data["icons"] = icons
        changed = True

    action = data.setdefault("action", {})
    if "default_icon" not in action:
        action["default_icon"] = icons
        changed = True

    if changed:
        manifest_path.write_text(
            json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
    return changed


def main() -> int:
    files = generate()
    for f in files:
        print(f"generado: {f.relative_to(ROOT)}")
    if "--no-manifest" not in sys.argv:
        if patch_manifest():
            print("manifest.json actualizado con los iconos")
        else:
            print("manifest.json ya contiene los iconos")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
