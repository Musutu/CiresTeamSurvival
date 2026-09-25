"""Build the shared ability icon set and import it into /Game/UI/Abilities.

Runs Tools/BuildAbilityIcons.py (Pillow) then Tools/ImportDraftPortraits.py in
UnrealEditor-Cmd with the ability destination/prefix. Only the child processes
started here are ever stopped. See Docs/Roster.md "Ability icons".

Painted overrides: Art/Icons/ChatGPT/Abilities/<id>.png (256x256 icons generated for Eric via
ChatGPT, sliced by Tools/SliceIconSheet.py) replace the procedural render of that id before import;
pass --procedural to ignore them.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--project", type=Path, default=ROOT / "CiresTeamSurvival.uproject")
    parser.add_argument("--pylib", type=Path, help="Directory containing Pillow, if not installed")
    parser.add_argument("--ids", help="Comma-separated subset")
    parser.add_argument("--procedural", action="store_true", help="Ignore the painted overrides")
    args = parser.parse_args()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = ROOT / "Saved/AbilityIcons" / stamp
    build = [sys.executable, str(ROOT / "Tools/BuildAbilityIcons.py"), "--out", str(out), "--sheet", str(out / "contact-sheet.png"),
             "--data", str(ROOT / "Content/Data/AbilityIcons.json")]
    if args.pylib:
        build += ["--pylib", str(args.pylib)]
    if args.ids:
        build += ["--ids", args.ids]
    subprocess.run(build, check=True, cwd=ROOT)
    (out / "contact-sheet.png").rename(out.parent / f"{stamp}-contact-sheet.png")
    painted = 0
    if not args.procedural:
        for override in (ROOT / "Art/Icons/ChatGPT/Abilities").glob("*.png"):
            target = out / override.name
            if target.is_file() or not args.ids:  # painted-only ids (status_*) too, unless a subset was asked
                target.write_bytes(override.read_bytes())
                painted += 1
    log = out.parent / f"{stamp}-import.log"
    for existing in (ROOT / "Content/UI/Abilities").glob("*.uasset"):
        os.chmod(existing, 0o666)  # LFS-lockable assets check out read-only
    env = dict(os.environ, CIRE_DRAFT_PORTRAIT_DIR=str(out), CIRE_UI_TEXTURE_DEST="/Game/UI/Abilities", CIRE_UI_TEXTURE_PREFIX="T_")
    code = subprocess.run([str(args.editor), str(args.project.resolve()), "-run=pythonscript", f"-script={ROOT / 'Tools/ImportDraftPortraits.py'}",
                           "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-stdout", f"-abslog={log}"],
                          cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900,
                          creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)).returncode
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    match = re.search(r"CIRE_DRAFT_PORTRAIT_IMPORT_PASS imported=(\d+)", text)
    expected = len(list(out.glob("*.png")))
    missing = [p.stem for p in out.glob("*.png") if not (ROOT / "Content/UI/Abilities" / f"T_{p.stem}.uasset").is_file()]
    passed = code == 0 and match is not None and int(match.group(1)) == expected and not missing
    print(json.dumps(dict(passed=passed, exitCode=code, icons=expected, painted=painted, imported=int(match.group(1)) if match else 0,
                          missing=missing, directory=str(out), log=str(log)), indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
