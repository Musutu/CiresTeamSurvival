"""Render every purchased Fab VFX system alone on a stage and compose rated contact sheets (pack-usage).

Launches one bounded offscreen game with -CireFabVFXCatalog=<list.json> (CireFabVFXCatalog.cpp): each system from
Art/Fab/FabVFXInventory.json is spawned at scale 1 on a neutral floor and captured twice (early frame for bursts,
late frame for loops). Pillow then composes sheets of 24 systems (early | late tiles + label + current quality),
grouped by pack / sub-kit, so the quality rating in the inventory can be reviewed against the real look.

  python Tools/RunFabVFXCatalog.py                        # capture everything -> Saved/FabVFXCatalog/<stamp>
  python Tools/RunFabVFXCatalog.py --pack Shadow_Magic    # one pack
  python Tools/RunFabVFXCatalog.py --compose DIR          # only rebuild the sheets of a capture
After a run: python Tools/InventoryFabVFX.py --catalog Saved/FabVFXCatalog/<stamp>   (folds in reach + colour params)
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
INVENTORY = ROOT / "Art" / "Fab" / "FabVFXInventory.json"
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def pillow():
    try:
        import PIL  # noqa: F401
        return sys.executable
    except ImportError:
        pass
    venv = ROOT / "Saved/PyTools/venv"
    python = venv / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.exists():
        subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
        subprocess.run([str(python), "-m", "pip", "install", "--quiet", "pillow"], check=True)
    return str(python)


def compose(directory: Path) -> list[str]:
    from PIL import Image, ImageDraw, ImageFont
    items = json.loads((directory / "list.json").read_text(encoding="utf-8"))["systems"]
    inventory = {r["path"]: r for r in json.loads(INVENTORY.read_text(encoding="utf-8"))["systems"]} if INVENTORY.exists() else {}
    sheets = directory / "sheets"
    sheets.mkdir(exist_ok=True)
    try:
        font, small = ImageFont.truetype("arial.ttf", 17), ImageFont.truetype("arial.ttf", 14)
    except OSError:
        font = small = ImageFont.load_default()
    tile_w, tile_h, cols, rows_per = 300, 168, 3, 8
    written = []
    groups: dict[str, list] = {}
    for index, item in enumerate(items):
        groups.setdefault(item.get("group", "all"), []).append((index, item))
    for group, members in groups.items():
        for page in range(0, len(members), cols * rows_per):
            chunk = members[page:page + cols * rows_per]
            sheet = Image.new("RGB", (cols * (tile_w * 2 + 12), 30 + rows_per * (tile_h + 34)), (18, 16, 20))
            draw = ImageDraw.Draw(sheet)
            draw.text((10, 6), f"{group}   sheet {page // (cols * rows_per) + 1}   (left: early frame 0.3 s, right: late frame 1.15 s; scale 1)", fill=(236, 214, 170), font=font)
            for i, (index, item) in enumerate(chunk):
                x, y = (i % cols) * (tile_w * 2 + 12), 30 + (i // cols) * (tile_h + 34)
                for j, frame in enumerate(("early", "late")):
                    path = directory / f"{index:03d}_{frame}.png"
                    if path.exists():
                        sheet.paste(Image.open(path).convert("RGB").resize((tile_w, tile_h)), (x + j * tile_w, y))
                    draw.rectangle([x + j * tile_w, y, x + j * tile_w + tile_w - 1, y + tile_h - 1], outline=(60, 54, 48))
                row = inventory.get(item["path"], {})
                label = f"{index:03d} {item['label']}   [{row.get('quality') or '?'}]"
                draw.text((x + 6, y + tile_h + 4), label[:70], fill=(255, 240, 200), font=small)
                if row.get("look"):
                    draw.text((x + 6, y + tile_h + 19), row["look"][:74], fill=(190, 180, 160), font=small)
            out = sheets / f"{group}_{page // (cols * rows_per) + 1:02d}.jpg"
            sheet.save(out, quality=86)
            written.append(str(out))
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pack", default="", help="only this pack folder (default: every pack in the inventory)")
    parser.add_argument("--timeout", type=int, default=2700)
    parser.add_argument("--compose", type=Path)
    parser.add_argument("--in-pillow", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.compose:
        python = pillow()
        if not args.in_pillow and python != sys.executable:
            return subprocess.call([python, __file__, *sys.argv[1:], "--in-pillow"])
        print("\n".join(compose(args.compose.resolve())))
        return 0
    if not INVENTORY.exists():
        subprocess.run([sys.executable, str(ROOT / "Tools/InventoryFabVFX.py")], check=True)
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))["systems"]
    systems = [{"path": r["path"], "label": f"{r['subkit']}/{r['stem']}" if r["pack"] == "Big_Pack_Magic_VFX" else r["stem"],
                "group": f"{r['pack']}_{r['subkit']}" if r["pack"] == "Big_Pack_Magic_VFX" else r["pack"]}
               for r in inventory if not r.get("base") and (not args.pack or r["pack"] == args.pack)]
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    folder = ROOT / "Saved/FabVFXCatalog" / stamp
    folder.mkdir(parents=True)
    (folder / "list.json").write_text(json.dumps({"systems": systems}, indent=1), encoding="utf-8")
    log, console = folder / "gallery.log", folder / "console.log"
    command = [str(EDITOR), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", f"-CireFabVFXCatalog={folder / 'list.json'}",
               f"-CireFabVFXCatalogOut={folder}", "-RenderOffscreen", "-ForceRes", "-ResX=960", "-ResY=540", "-unattended", "-nosplash",
               "-nosound", "-nop4", "-NoLiveCoding", "-ExecCmds=t.MaxFPS 30,r.AntiAliasingMethod 1", f"-abslog={log}"]
    started = time.monotonic()
    failure = ""
    with console.open("wb") as output:
        child = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), env=EDITOR_ENV)
        try:
            code = child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            kill_tree(child)
            code = -1
            failure = f"catalogue exceeded {args.timeout}-second bound"
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    match = re.search(r"CIRE_FAB_CATALOG_PASS items=(\d+) captures=(\d+) missing=(\d+)", text)
    passed = code == 0 and match is not None and not failure
    report = dict(passed=passed, exitCode=code, seconds=round(time.monotonic() - started, 1), failure=failure, systems=len(systems),
                  captures=int(match.group(2)) if match else 0, missing=int(match.group(3)) if match else -1, log=str(log))
    (folder / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    print(json.dumps(report, indent=1))
    if passed:
        subprocess.call([pillow(), __file__, "--compose", str(folder), "--in-pillow"])
        subprocess.call([sys.executable, str(ROOT / "Tools/InventoryFabVFX.py"), "--catalog", str(folder)])
    print(f"CIRE_FAB_VFX_CATALOG_{'PASS' if passed else 'FAIL'}: {folder}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
