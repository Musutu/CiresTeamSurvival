"""Before/after captures of the Medieval Kingdom town (town-trim): the Play Bounds edge, the castle interior fill and the
pack's water. See Docs/CastleTown.md "Trim to Play Bounds".

    python Tools/RunTownShots.py --kinds edge --bounds auto            (after: trimmed)
    python Tools/RunTownShots.py --kinds edge --bounds auto --no-trim  (before: the whole town, same views)
    python Tools/RunTownShots.py --kinds castle,water                  (each shot twice: fills/water hidden, then shown)
    python Tools/RunTownShots.py --scan                                 (suggest castleInterior.anchors)

Runs the editor binaries in -game (-CireTown -CireTownShots=<kinds> -CireTownShotsExit), windowed 1600x900. Pictures land
in Saved/TownShots/<stamp>-trim|full/. Only child processes created by this runner are terminated.
"""
from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from RunTownPerf import ENV, GAME, ROOT, bounds_file, kill_tree  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--kinds", default="edge,castle,water")
    p.add_argument("--bounds", default="", help="Play Bounds polygon file or 'auto' (RunTownPerf.py --bounds)")
    p.add_argument("--no-trim", action="store_true")
    p.add_argument("--scan", action="store_true", help="-CireCastleLightScan: write Saved/TownShots/castle_anchors.json")
    p.add_argument("--timeout", type=int, default=1200)
    p.add_argument("--extra", action="append", default=[], help="extra engine argument (repeatable, use --extra=-Flag)")
    args = p.parse_args()
    folder = ROOT / "Saved/TownShotRuns" / datetime.now().strftime("%Y%m%d-%H%M%S")
    folder.mkdir(parents=True, exist_ok=True)
    log = folder / "run.log"
    command = [str(GAME), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-windowed", "-ResX=1600", "-ResY=900",
               "-CireTown", "-unattended", "-nosplash", "-nop4", "-NoLiveCoding", "-CireNoReplay", f"-CireTownShots={args.kinds}", "-CireTownShotsExit", f"-abslog={log}"]
    if args.bounds:
        command.append(f"-CireTownTrimBounds={bounds_file(args.bounds, folder)}")
    if args.no_trim:
        command.append("-CireNoTownTrim")
    if args.scan:
        command.append("-CireCastleLightScan")
    command += args.extra
    started = time.monotonic()
    child = subprocess.Popen(command, cwd=ROOT, env=ENV)
    try:
        code = child.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        kill_tree(child)
        code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    for line in text.splitlines():
        if re.search(r"CIRE_TOWN_SHOT|CIRE_TOWN_TRIM|CIRE_CASTLE_SCAN|CIRE_TOWN_WATER|CIRE_TOWN_LIGHTING|CIRE_TOWN_REALMS_LOADED", line):
            print("  " + line.split("Display: ", 1)[-1])
    done = "CIRE_TOWN_SHOTS_DONE" in text
    print(f"exit={code} {time.monotonic() - started:.0f} s done={done} log={log}")
    return 0 if done else 1


if __name__ == "__main__":
    raise SystemExit(main())
