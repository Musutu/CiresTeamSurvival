"""Cast every ability one by one on a clean stage and capture its key frames (ability-vfx).

Launches one bounded offscreen game process with -CireAbilityVFXGallery, which casts each
champion skill (actives, ultimates, passives, basic attacks) and each monster race ability in
isolation against a target dummy through the real gameplay paths, and screenshots six key
frames: aim/telegraph, cast, travel, impact, lingering, end. Stops only its own child.

Contact sheets (needs Pillow; created automatically in Saved/PyTools/venv when missing):
  <capture dir>/sheets/<index>_<id>.jpg      one ability, 3x2 frames with labels
  <capture dir>/sheets/overview_NN.jpg        four abilities per page, one row each
  --compare BEFORE_DIR AFTER_DIR               before/after rows per ability -> Saved/AbilityVFX/compare-<stamp>

Examples:
  python Tools/RunAbilityVFXGallery.py --set champion --tag before
  python Tools/RunAbilityVFXGallery.py --set monster --tag after
  python Tools/RunAbilityVFXGallery.py --only frost_bind,grave_line --tag after
  python Tools/RunAbilityVFXGallery.py --compare Saved/AbilityVFX/before-... Saved/AbilityVFX/after-...
A technical pass is not an art rating: every sheet still needs a visual review.
"""
from __future__ import annotations

from datetime import datetime, timezone
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
FRAME_TITLES = {"1_aim": "AIM", "1_telegraph": "TELEGRAPH", "2_cast": "CAST", "2_telegraph_late": "TELEGRAPH (LATE)",
                "3_travel": "TRAVEL", "3_release": "RELEASE", "4_impact": "IMPACT", "5_linger": "LINGER", "6_end": "END"}


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
    """Build per-ability and overview sheets inside a capture directory (runs under Pillow)."""
    from PIL import Image, ImageDraw, ImageFont
    manifest = [json.loads(line) for line in (directory / "manifest.jsonl").read_text(encoding="utf-8").splitlines() if line.strip()]
    sheets = directory / "sheets"
    sheets.mkdir(exist_ok=True)
    try:
        font = ImageFont.truetype("arial.ttf", 20)
        small = ImageFont.truetype("arial.ttf", 15)
    except OSError:
        font = small = ImageFont.load_default()
    written = []
    rows = []
    for entry in manifest:
        frames = []
        for name in entry["frames"]:
            path = directory / f"{entry['index']:03d}_{entry['id']}_{name}.png"
            frames.append((name, Image.open(path).convert("RGB") if path.exists() else None))
        tile_w, tile_h = 640, 360
        sheet = Image.new("RGB", (tile_w * 3, tile_h * 2 + 44), (18, 16, 20))
        draw = ImageDraw.Draw(sheet)
        title = f"{entry['label']}  [{entry['id']}]  {entry['group']} / {entry['shape']} / {entry['school']}  caster: {entry['caster']}"
        if entry.get("refused"):
            title += "   ** CAST REFUSED **"
        draw.text((12, 10), title, fill=(236, 214, 170), font=font)
        for i, (name, image) in enumerate(frames[:6]):
            x, y = (i % 3) * tile_w, 44 + (i // 3) * tile_h
            if image is not None:
                sheet.paste(image.resize((tile_w, tile_h)), (x, y))
            draw.rectangle([x, y, x + tile_w - 1, y + tile_h - 1], outline=(60, 54, 48))
            draw.text((x + 8, y + 6), FRAME_TITLES.get(name, name), fill=(255, 240, 200), font=small)
        out = sheets / f"{entry['index']:03d}_{entry['id']}.jpg"
        sheet.save(out, quality=88)
        written.append(str(out))
        row = Image.new("RGB", (1920, 180 + 26), (18, 16, 20))
        rd = ImageDraw.Draw(row)
        rd.text((8, 4), title, fill=(236, 214, 170), font=small)
        for i, (name, image) in enumerate(frames[:6]):
            if image is not None:
                row.paste(image.resize((320, 180)), (i * 320, 26))
        rows.append(row)
    for page in range(0, len(rows), 4):
        chunk = rows[page:page + 4]
        overview = Image.new("RGB", (1920, sum(r.height for r in chunk)), (18, 16, 20))
        y = 0
        for r in chunk:
            overview.paste(r, (0, y))
            y += r.height
        out = sheets / f"overview_{page // 4 + 1:02d}.jpg"
        overview.save(out, quality=86)
        written.append(str(out))
    return written


def compare(before: Path, after: Path) -> Path:
    from PIL import Image, ImageDraw, ImageFont
    load = lambda d: {e["id"]: e for e in (json.loads(l) for l in (d / "manifest.jsonl").read_text(encoding="utf-8").splitlines() if l.strip())}
    a, b = load(before), load(after)
    out = ROOT / "Saved/AbilityVFX" / f"compare-{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}"
    out.mkdir(parents=True)
    try:
        font = ImageFont.truetype("arial.ttf", 18)
    except OSError:
        font = ImageFont.load_default()
    for ability, entry in b.items():
        old = a.get(ability)
        sheet = Image.new("RGB", (1920, 2 * 206 + 30), (18, 16, 20))
        draw = ImageDraw.Draw(sheet)
        draw.text((8, 6), f"{entry['label']} [{ability}]  {entry['shape']} / {entry['school']}   top: BEFORE   bottom: AFTER", fill=(236, 214, 170), font=font)
        for r, (d, e) in enumerate(((before, old), (after, entry))):
            if not e:
                continue
            for i, name in enumerate(e["frames"][:6]):
                path = d / f"{e['index']:03d}_{e['id']}_{name}.png"
                if path.exists():
                    sheet.paste(Image.open(path).convert("RGB").resize((320, 180)), (i * 320, 30 + r * 206))
                draw.text((i * 320 + 6, 30 + r * 206 + 182), FRAME_TITLES.get(name, name), fill=(200, 190, 170), font=font)
        sheet.save(out / f"{entry['index']:03d}_{ability}.jpg", quality=86)
    print(f"CIRE_ABILITY_VFX_COMPARE {out}")
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--set", default="champion", help="champion | monster | all | <race id>")
    parser.add_argument("--only", default="", help="comma-separated ability ids")
    parser.add_argument("--tag", default="capture")
    parser.add_argument("--res", default="1280x720")
    parser.add_argument("--timeout", type=int, default=2700)
    parser.add_argument("--compose", type=Path, help="only (re)build sheets for an existing capture directory")
    parser.add_argument("--compare", nargs=2, type=Path, metavar=("BEFORE", "AFTER"))
    parser.add_argument("--db", type=Path, help="Ability Database JSON override (e.g. void zones on a ground skill)")
    parser.add_argument("--legacy", action="store_true", help="capture the previous presentation (-CireLegacyVFX) for before/after")
    parser.add_argument("--in-pillow", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.compare or args.compose:
        python = pillow()
        if not args.in_pillow and python != sys.executable:
            return subprocess.call([python, __file__, *sys.argv[1:], "--in-pillow"])
        if args.compare:
            compare(args.compare[0].resolve(), args.compare[1].resolve())
        else:
            print("\n".join(compose(args.compose.resolve())))
        return 0
    width, height = (int(v) for v in args.res.lower().split("x"))
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder = ROOT / "Saved/AbilityVFXChecks" / stamp
    folder.mkdir(parents=True)
    log, console = folder / "gallery.log", folder / "console.log"
    command = [str(EDITOR), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireAbilityVFXGallery",
               f"-CireVFXSet={args.set}", f"-CireVFXTag={args.tag}", "-CireTripoChampions", "-RenderOffscreen", "-ForceRes",
               f"-ResX={width}", f"-ResY={height}", "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding",
               "-ExecCmds=t.MaxFPS 30,r.AntiAliasingMethod 1", f"-abslog={log}"]
    if args.db:
        command.append(f"-CireVFXDb={args.db.resolve()}")
    if args.legacy:
        command.append("-CireLegacyVFX")
    if args.only:
        command.append(f"-CireVFXOnly={args.only}")
    started = time.monotonic()
    failure = ""
    with console.open("wb") as output:
        child = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            code = child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            child.terminate()
            try:
                code = child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                code = child.wait(timeout=5)
            failure = f"Gallery exceeded {args.timeout}-second bound"
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    match = re.search(r"CIRE_ABILITY_VFX_GALLERY_PASS abilities=(\d+) refused=(\d+) captures=(\d+) directory=(.+)", text)
    errors = [l for l in text.splitlines() if re.search(r"CIRE_\S*(?:FAIL|ERROR|BLOCKER)|Fatal error:|Assertion failed:|Ensure condition failed:", l)]
    refused = [l.split("CIRE_ABILITY_VFX_REFUSED", 1)[1].strip() for l in text.splitlines() if "CIRE_ABILITY_VFX_REFUSED" in l]
    passed = code == 0 and match is not None and not errors and not failure
    report = dict(passed=passed, exitCode=code, seconds=round(time.monotonic() - started, 1), failure=failure, errors=errors[:40],
                  refused=refused, log=str(log), visualReviewAccepted=False)
    if match:
        directory = Path(match.group(4).strip())
        report.update(abilities=int(match.group(1)), captures=int(match.group(3)), directory=str(directory))
        python = pillow()
        result = subprocess.run([python, __file__, "--compose", str(directory), "--in-pillow"], capture_output=True, text=True)
        report["sheets"] = [line for line in result.stdout.splitlines() if line.endswith(".jpg")]
        report["sheetErrors"] = result.stderr[-2000:]
    (folder / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "sheets"}, indent=2))
    print(f"Report: {folder / 'report.json'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
