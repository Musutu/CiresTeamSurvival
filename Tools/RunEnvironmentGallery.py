"""Render the seven bounded native town views (gate, market, residential, square, castle, overview, escort) and verify their fixture checks."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time


# AutoSDK is off on this machine, so every editor boot otherwise runs "Build.bat -Mode=ValidatePlatforms"
# and blocks on Build.bat's machine-wide lock file while any other worktree compiles. Probes only target Win64.
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    folder = root / "Saved/EnvironmentGalleryChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    log = folder / "gallery.log"
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(root / "CiresTeamSurvival.uproject"),
               "/Game/Maps/Citadel", "-game", "-CireEnvironmentGallery", "-RenderOffscreen", "-ForceRes",
               "-ResX=1920", "-ResY=1080", "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding",
               "-ExecCmds=t.MaxFPS 60", f"-abslog={log}"]
    started = time.monotonic()
    failure = ""
    with (folder / "console.log").open("wb") as output:
        child = subprocess.Popen(command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), env=EDITOR_ENV)
        try:
            code = child.wait(timeout=480)
        except subprocess.TimeoutExpired:
            kill_tree(child)
            child.terminate()
            try:
                code = child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                code = child.wait(timeout=5)
            failure = "Environment gallery exceeded its 480-second process bound"
    contents = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    match = re.search(r"CIRE_ENVIRONMENT_GALLERY_PASS captures=7 checks=(\d+) directory=(.+)", contents)
    errors = [line for line in contents.splitlines() if re.search(r"CIRE_\S*(?:FAIL|ERROR|BLOCKER)|Fatal error:|Assertion failed:|Ensure condition failed:", line)]
    passed = code == 0 and match is not None and not failure and not errors
    captures = []
    if match:
        for path in sorted(Path(match.group(2).strip()).glob("*.png")):
            with path.open("rb") as stream:
                header = stream.read(24)
            valid = len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n"
            size = struct.unpack(">II", header[16:24]) if valid else (0, 0)
            passed = passed and size == (1920, 1080) and path.stat().st_size > 10000
            captures.append(dict(path=str(path), width=size[0], height=size[1], bytes=path.stat().st_size))
    passed = bool(passed and len(captures) == 7)
    report = dict(passed=passed, exitCode=code, seconds=round(time.monotonic()-started, 2),
                  checks=int(match.group(1)) if match else None, failure=failure, errors=errors, log=str(log), captures=captures)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
