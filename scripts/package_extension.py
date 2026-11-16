#!/usr/bin/env python3
# ============================================================================
#  S-DOWNLOAD MANAGER (SDM)
#  scripts/package_extension.py
#
#  Empaqueta la extension del navegador en dist/:
#    - sdm-extension-<ver>.zip           -> Chromium MV3 (Chrome, Edge, Brave,
#                                          Vivaldi, Opera, Chromium)
#    - sdm-extension-firefox-<ver>.zip   -> Firefox MV3 (event page + id fijo
#                                          sdm@sdm.local, sin tocar nada a mano)
#
#  El id fijo de Firefox es lo que permite que el puente funcione sin
#  configuracion: SDM registra `allowed_extensions` con ese valor.
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

FIREFOX_EXTENSION_ID = "sdm@sdm.local"

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


def firefox_manifest(manifest: dict) -> dict:
    """Adapta el manifiesto MV3 de Chromium a Firefox.

    Firefox no soporta `background.service_worker`: usa una event page
    (`background.scripts`). Tampoco entiende `minimum_chrome_version`.
    """
    result = json.loads(json.dumps(manifest))
    result.pop("minimum_chrome_version", None)
    background = result.get("background") or {}
    worker = background.get("service_worker")
    if worker:
        result["background"] = {"scripts": [worker]}
    result["browser_specific_settings"] = {
        "gecko": {
            "id": FIREFOX_EXTENSION_ID,
            "strict_min_version": "115.0",
        }
    }
    return result


def write_zip(zip_path: Path, manifest_override: dict | None) -> int:
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
            if manifest_override is not None and rel.as_posix() == "manifest.json":
                zf.writestr(
                    "manifest.json",
                    json.dumps(manifest_override, indent=2, ensure_ascii=False) + "\n",
                )
            else:
                zf.write(path, rel.as_posix())  # manifest.json en la raiz del zip
            count += 1
    return count


def main() -> int:
    out_dir = ROOT / "dist"
    if "--out" in sys.argv:
        idx = sys.argv.index("--out")
        out_dir = Path(sys.argv[idx + 1])

    ensure_icons()

    manifest = json.loads((EXT_DIR / "manifest.json").read_text(encoding="utf-8"))
    version = manifest.get("version", "0.0.0")
    out_dir.mkdir(parents=True, exist_ok=True)

    chromium_zip = out_dir / f"sdm-extension-{version}.zip"
    chromium_count = write_zip(chromium_zip, None)
    print(f"extension Chromium: {chromium_zip.relative_to(ROOT)} ({chromium_count} ficheros)")

    firefox_zip = out_dir / f"sdm-extension-firefox-{version}.zip"
    firefox_count = write_zip(firefox_zip, firefox_manifest(manifest))
    print(f"extension Firefox:  {firefox_zip.relative_to(ROOT)} ({firefox_count} ficheros, "
          f"id {FIREFOX_EXTENSION_ID})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
