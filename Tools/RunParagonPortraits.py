"""Draft portraits for the Paragon champions, captured LOCALLY from their own meshes.

Step 1 runs Tools/RunDraftPortraits.py --skip-import for the installed Paragon ids (the same native fixture as every
other champion: live preview hero on the draft stage, bust framing, 512x512 PNG in Saved/DraftPortraits/<stamp>).
Step 2 imports them with Tools/ImportDraftPortraits.py into /Game/ParagonDerived/Portraits/T_Portrait_<id>
(CIRE_UI_TEXTURE_DEST override). Content/ParagonDerived matches the Content/Paragon* gitignore rule: the portraits are
renders of Epic-licensed characters and are never committed. The draft screen falls back to them
(CireParagonChampions::Portrait) when /Game/UI/Draft/Portraits has no committed portrait for the id.

Usage: python Tools/RunParagonPortraits.py [--ids pg_greystone,pg_kwang]
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ids", default="")
    args = parser.parse_args()
    data = json.loads((ROOT / "Content/Data/ParagonChampions.json").read_text(encoding="utf-8"))
    ids = args.ids.split(",") if args.ids else [h["id"] for h in data["heroes"]
                                                  if (ROOT / "Content" / h["pack"]).is_dir()]
    if not ids:
        print("No installed Paragon packs.")
        return 1
    render = subprocess.run([sys.executable, str(ROOT / "Tools/RunDraftPortraits.py"), "--skip-import", "--rendered", "--ids", ",".join(ids)],
                            cwd=ROOT, capture_output=True, text=True)
    print(render.stdout[-3000:])
    match = re.search(r'"directory": "([^"]+)"', render.stdout)
    if render.returncode != 0 or not match:
        print("Portrait render failed", render.stderr[-2000:])
        return 1
    directory = Path(match.group(1).replace("\\\\", "\\"))
    log = ROOT / "Saved/Logs/ParagonPortraitImport.log"
    env = dict(os.environ, CIRE_DRAFT_PORTRAIT_DIR=str(directory), CIRE_UI_TEXTURE_DEST="/Game/ParagonDerived/Portraits",
               CIRE_UI_TEXTURE_PREFIX="T_Portrait_", UE_SKIP_UBT_SDK_SETUP="1")
    code = subprocess.run([str(EDITOR), str(ROOT / "CiresTeamSurvival.uproject"), "-run=pythonscript",
                           f"-script={ROOT / 'Tools/ImportDraftPortraits.py'}", "-unattended", "-nosplash", "-nosound", "-nop4",
                           "-NoLiveCoding", f"-abslog={log}"], cwd=ROOT, env=env, timeout=900).returncode
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    ok = bool(re.search(r"CIRE_DRAFT_PORTRAIT_IMPORT_PASS imported=(\d+)", text))
    missing = [i for i in ids if not (ROOT / "Content/ParagonDerived/Portraits" / f"T_Portrait_{i}.uasset").is_file()]
    print(f"PARAGON PORTRAITS {'PASS' if ok and not missing else 'FAIL'} imported_to=/Game/ParagonDerived/Portraits missing={missing} exit={code}")
    return 0 if ok and not missing else 1


if __name__ == "__main__":
    raise SystemExit(main())
