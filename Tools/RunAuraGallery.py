"""Render every buff/aura signature and the empowered attack frames offscreen.

Requires a successful current CiresTeamSurvivalEditor build. Launches one bounded
game process with -CireAuraGallery, stops only that child, validates the explicit
pass marker and 1920x1080 PNGs, and writes Saved/AuraGalleryChecks/<stamp>/report.json.
It does not modify a level, asset or player save. Visual review of the images is
still required; a technical pass is not an art-quality rating.
"""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

EXPECTED = 12


# AutoSDK is off on this machine, so every editor boot otherwise runs "Build.bat -Mode=ValidatePlatforms"
# and blocks on Build.bat's machine-wide lock file while any other worktree compiles. Probes only target Win64.
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def main():
    root = Path(__file__).resolve().parent.parent
    folder = root / "Saved/AuraGalleryChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    log, console = folder / "gallery.log", folder / "console.log"
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(root / "CiresTeamSurvival.uproject"),
               "/Game/Maps/Citadel", "-game", "-CireAuraGallery", "-CireTripoChampions", "-RenderOffscreen", "-ForceRes",
               "-ResX=1920", "-ResY=1080", "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding",
               "-ExecCmds=t.MaxFPS 60,r.AntiAliasingMethod 1", f"-abslog={log}"]
    started = time.monotonic()
    failure = ""
    with console.open("wb") as output:
        child = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), env=EDITOR_ENV)
        try:
            code = child.wait(timeout=200)
        except subprocess.TimeoutExpired:
            kill_tree(child)
            child.terminate()
            try:
                code = child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                code = child.wait(timeout=5)
            failure = "Gallery exceeded 200-second bound"
    contents = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    match = re.search(rf"CIRE_AURA_GALLERY_PASS captures={EXPECTED} directory=(.+)", contents)
    errors = [line for line in contents.splitlines() if re.search(r"CIRE_\S*(?:FAIL|ERROR|BLOCKER)|Fatal error:|Assertion failed:|Ensure condition failed:", line)]
    passed = code == 0 and match is not None and not errors
    captures = []
    if match:
        directory = Path(match.group(1).strip())
        for path in sorted(directory.glob("*.png")):
            with path.open("rb") as stream:
                header = stream.read(24)
            valid = header[:8] == b"\x89PNG\r\n\x1a\n" and len(header) == 24
            size = struct.unpack(">II", header[16:24]) if valid else (0, 0)
            passed = passed and size == (1920, 1080) and path.stat().st_size > 10000
            captures.append(dict(path=str(path), width=size[0], height=size[1], bytes=path.stat().st_size))
    passed = passed and len(captures) == EXPECTED
    evidence = [line for line in contents.splitlines() if "CIRE_AURA_GALLERY" in line]
    report = dict(passed=bool(passed), exitCode=code, seconds=round(time.monotonic() - started, 2), failure=failure,
                  errors=errors, evidence=evidence, log=str(log), captures=captures, visualReviewAccepted=False)
    (folder / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
