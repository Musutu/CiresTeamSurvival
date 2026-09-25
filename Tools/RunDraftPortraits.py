"""Generate draft-screen portrait icons from the real champion meshes.

Step 1 launches the game offscreen with -CireDraftPortraits -CireTripoChampions:
the native fixture binds each roster profile to a preview hero on the draft
stage (same body, weapons, scale and idle pose as in play), frames a bust and
writes Saved/DraftPortraits/<stamp>/<profile_id>.png (512x512).
Step 2 runs Tools/ImportDraftPortraits.py in UnrealEditor-Cmd to import them as
/Game/UI/Draft/Portraits/T_Portrait_<profile_id>. Only child processes started
here are ever stopped. --ids limits the run to a comma-separated subset.

Painted overrides: Art/DraftPortraits/Painted/<profile_id>.png (512x512 portraits generated for Eric via
ChatGPT from the renders, for bodies whose renders read pale; see Content/UI/Draft/LICENSES.md) replace
the render of that id before import; pass --rendered to keep the raw renders.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent


def run(command: list[str], log_dir: Path, name: str, timeout: int, env=None) -> tuple[int, str]:
    with (log_dir / f"{name}-console.log").open("wb") as stream:
        child = subprocess.Popen(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, env=env,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            return child.wait(timeout=timeout), ""
        except subprocess.TimeoutExpired:
            child.terminate()
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill(); child.wait(timeout=10)
            return -1, f"{name} exceeded {timeout} seconds"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=ROOT / "CiresTeamSurvival.uproject")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--ids", help="Comma-separated profile ids (default: whole roster)")
    parser.add_argument("--skip-import", action="store_true")
    parser.add_argument("--import-dir", type=Path, help="Skip rendering; import an existing Saved/DraftPortraits/<stamp> folder")
    parser.add_argument("--rendered", action="store_true", help="Ignore the painted overrides in Art/DraftPortraits/Painted")
    args = parser.parse_args()
    folder = ROOT / "Saved/DraftPortraitChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    render_log = folder / "render.log"
    command = [str(args.editor), str(args.project.resolve()), "/Game/Maps/Citadel", "-game", "-CireDraftPortraits",
               "-CireTripoChampions", "-RenderOffscreen", "-ForceRes", "-ResX=1920", "-ResY=1080", "-unattended",
               "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", f"-abslog={render_log}"]
    if args.ids:
        command.append("-CireDraftPortraitIds=" + args.ids)
    started = time.monotonic()
    if args.import_dir:
        code, failure = 0, ""
        text = f"CIRE_DRAFT_PORTRAITS_PASS count=0 directory={args.import_dir.resolve()}"
    else:
        code, failure = run(command, folder, "render", 420)
        text = render_log.read_text(encoding="utf-8", errors="replace") if render_log.exists() else ""
    match = re.search(r"CIRE_DRAFT_PORTRAITS_PASS count=(\d+) directory=(.+)", text)
    errors = [line for line in text.splitlines() if "CIRE_DRAFT_PORTRAIT FAIL" in line or "Fatal error:" in line]
    portraits = []
    directory = Path(match.group(2).strip()) if match else None
    painted_dir = ROOT / "Art/DraftPortraits/Painted"
    if directory and not args.rendered and painted_dir.is_dir() and directory.resolve() != painted_dir.resolve():
        for painted in painted_dir.glob("*.png"):
            if (directory / painted.name).is_file():
                (directory / painted.name).write_bytes(painted.read_bytes())
    if directory:
        for png in sorted(directory.glob("*.png")):
            with png.open("rb") as stream:
                header = stream.read(24)
            size = struct.unpack(">II", header[16:24]) if header[:8] == b"\x89PNG\r\n\x1a\n" else (0, 0)
            if size != (512, 512):
                errors.append("Bad portrait size " + png.name)
            portraits.append(dict(profile=png.stem, path=str(png), bytes=png.stat().st_size))
    imported = None
    if directory and (directory / "Exposure.json").is_file() and not errors:
        # Merge the measured per-champion exposure trims for the live draft preview.
        target = ROOT / "Content/UI/Draft/Portraits/Exposure.json"
        merged = json.loads(target.read_text(encoding="utf-8")) if target.is_file() else {}
        merged.update(json.loads((directory / "Exposure.json").read_text(encoding="utf-8")))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(dict(sorted(merged.items())), indent=2) + "\n", encoding="utf-8")
    if directory and not errors and not args.skip_import:
        import_log = folder / "import.log"
        # LFS-lockable assets check out read-only; the editor must be able to replace them.
        for existing in (ROOT / "Content/UI/Draft/Portraits").glob("*.uasset"):
            os.chmod(existing, 0o666)
        env = dict(os.environ, CIRE_DRAFT_PORTRAIT_DIR=str(directory))
        icode, ifailure = run([str(args.editor), str(args.project.resolve()), "-run=pythonscript",
                               f"-script={ROOT / 'Tools/ImportDraftPortraits.py'}", "-unattended", "-nosplash", "-nosound",
                               "-nop4", "-NoLiveCoding", "-stdout", "-FullStdOutLogOutput", f"-abslog={import_log}"],
                              folder, "import", 600, env)
        itext = import_log.read_text(encoding="utf-8", errors="replace") if import_log.exists() else ""
        imported = bool(re.search(r"CIRE_DRAFT_PORTRAIT_IMPORT_PASS imported=(\d+)", itext))
        if not imported:
            errors.append(ifailure or f"Portrait import failed (exit {icode}); see {import_log}")
        for row in portraits:
            if not (ROOT / "Content/UI/Draft/Portraits" / f"T_Portrait_{row['profile']}.uasset").is_file():
                errors.append("Missing imported asset for " + row["profile"])
    passed = code == 0 and match is not None and not errors and not failure and (imported or args.skip_import)
    report = dict(passed=passed, exitCode=code, failure=failure, seconds=round(time.monotonic() - started, 2),
                  errors=errors, portraits=portraits, imported=imported, directory=str(directory) if directory else None,
                  visualReviewRequired=True)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "portraits"}, indent=2))
    print(f"{len(portraits)} portraits")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
