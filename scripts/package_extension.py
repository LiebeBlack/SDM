#!/usr/bin/env python3
# ============================================================================
#  S-DOWNLOAD MANAGER (SDM)
#  scripts/package_extension.py
#
#  Empaqueta la extension del navegador MV3 en dist/sdm-extension-<ver>.zip
#  listo para subir a un Release de GitHub (Chrome Web Store / AMO aceptan
#  este zip directamente).  Genera antes los iconos con stdlib pura.
#
#  Uso:  python scripts/package_extension.py [--out DIR]
# ============================================================================
from __future__ import annotations

import json
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXT_DIR = ROOT / "extension"
TOOLS_DIR = ROOT / "tools"

EXCLUDED_NAMES = {".DS_Store", "Thumbs.db"}
EXCLUDED_DIRS = {"__pycache__", ".git"}


def ensure_icons() -> None:
    sys.path.insert(0, str(TOOLS_DIR))
    try:
        import generate_icons  # type: ignore
    except ImportError:
        print("aviso: no se pudo importar generate_icons.py; se empaqueta sin iconos")
        return
    generate_icons.generate()
    generate_icons.patch_manifest()


def main() -> int:
    out_dir = ROOT / "dist"
    if "--out" in sys.argv:
        idx = sys.argv.index("--out")
        out_dir = Path(sys.argv[idx + 1])

    ensure_icons()

    manifest = json.loads((EXT_DIR / "manifest.json").read_text(encoding="utf-8"))
    version = manifest.get("version", "0.0.0")
    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / f"sdm-extension-{version}.zip"

    count = 0
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path in sorted(EXT_DIR.rglob("*")):
            if not path.is_file():
                continue
            rel = path.relative_to(EXT_DIR)
            if path.name in EXCLUDED_NAMES:
                continue
            if any(part in EXCLUDED_DIRS for part in rel.parts):
                continue
            zf.write(path, rel.as_posix())  # manifest.json en la raiz del zip
            count += 1

    print(f"extension empaquetada: {zip_path.relative_to(ROOT)} ({count} ficheros)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
