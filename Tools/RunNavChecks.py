"""Navigation checks (nav-paths): native navmesh/path/editor checks, the timed march + performance
probe, and optionally the rendered gallery (navmesh over the town, the path editor dragging a
waypoint, an arena navmesh). See Docs/Navigation.md.

Only child processes created by this runner are terminated. Nothing is built or imported.

    python Tools/RunNavChecks.py [--only native|probe|gallery|all]
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
FAILURE = re.compile(r"CIRE_\S*(?:FAIL|ERROR)|Fatal error:|Assertion failed:|Ensure condition failed:")
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")


# AutoSDK is off on this machine, so every editor boot otherwise runs "Build.bat -Mode=ValidatePlatforms"
# and blocks on Build.bat's machine-wide lock file while any other worktree compiles. Probes only target Win64.
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def run(name: str, args: list[str], folder: Path, marker: str, timeout: int) -> dict:
    log = folder / f"{name}.log"
    command = [str(EDITOR), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", *args,
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-CireNoReplay", f"-abslog={log}"]
    started = time.monotonic()
    failure = ""
    with (folder / f"{name}.console.log").open("wb") as output:
        child = subprocess.Popen(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0, env=EDITOR_ENV)
        try:
            code = child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            kill_tree(child)
            child.terminate()
            try:
                code = child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                code = child.wait(timeout=5)
            failure = f"{name} exceeded its {timeout}-second bound"
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    errors = [line for line in text.splitlines() if FAILURE.search(line)]
    evidence = [line.split("Display: ", 1)[-1] for line in text.splitlines()
                if re.search(r"CIRE_NAV_(READY|TEST_|PROBE_|GALLERY_|PASS)", line)]
    return dict(passed=code == 0 and not failure and not errors and marker in text, exitCode=code, failure=failure or None,
                seconds=round(time.monotonic() - started, 1), log=str(log), errors=errors, evidence=evidence)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", choices=("native", "probe", "gallery", "all"), default="all")
    args = parser.parse_args()
    folder = ROOT / "Saved/NavChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True, exist_ok=False)
    headless = ["-nullrhi"]
    reports: dict[str, dict] = {}
    if args.only in ("native", "all"):
        reports["native"] = run("native", [*headless, "-CireNavTests"], folder, "CIRE_NAV_PASS", 600)
    if args.only in ("probe", "all"):
        reports["probe"] = run("probe", [*headless, "-CireNavProbe", "-benchmark", "-fps=30",
                                         f"-CireNavProbeSummary={folder / 'probe.txt'}"], folder, "CIRE_NAV_PROBE_PASS", 900)
    if args.only in ("gallery", "all"):
        record = run("gallery", ["-CireNavGallery", "-RenderOffscreen", "-ForceRes", "-ResX=1920", "-ResY=1080", "-windowed",
                                 "-ExecCmds=t.MaxFPS 60"], folder, "CIRE_NAV_GALLERY_DONE", 600)
        captures = []
        match = re.search(r"CIRE_NAV_GALLERY_DONE captures=\d+ dir=(.+)", "\n".join(record["evidence"]))
        if match:
            for path in sorted(Path(match.group(1).strip()).glob("*.png")):
                with path.open("rb") as stream:
                    header = stream.read(24)
                size = struct.unpack(">II", header[16:24]) if header[:8] == b"\x89PNG\r\n\x1a\n" else (0, 0)
                captures.append(dict(path=str(path), width=size[0], height=size[1], bytes=path.stat().st_size))
        record["captures"] = captures
        record["passed"] = record["passed"] and len(captures) == 4 and all(c["bytes"] > 10000 for c in captures)
        reports["gallery"] = record
    passed = all(r["passed"] for r in reports.values())
    (folder / "report.json").write_text(json.dumps(dict(passed=passed, results=reports), indent=2) + "\n", encoding="utf-8")
    for name, record in reports.items():
        print(f"{name}: {'PASS' if record['passed'] else 'FAIL'} ({record['seconds']} s)")
        for line in record["evidence"] + record["errors"]:
            print("  " + line)
        if record["failure"]:
            print("  " + record["failure"])
    print(f"Report: {folder / 'report.json'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
