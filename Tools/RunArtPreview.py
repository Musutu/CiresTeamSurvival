"""Bounded offscreen Tripo render validation; does not depend on exit code alone."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys


def run_editor(command, timeout, env=None, **kwargs) -> subprocess.CompletedProcess:
    """subprocess.run() for an editor that skips UBT SDK setup and kills the whole process tree on timeout."""
    # AutoSDK is off on this machine, so every editor boot otherwise runs "Build.bat -Mode=ValidatePlatforms"
    # and blocks on Build.bat's machine-wide lock file while any other worktree compiles. Editors here target Win64.
    child = subprocess.Popen(command, env={**(env or os.environ), "UE_SKIP_UBT_SDK_SETUP": "1"}, **kwargs)
    try:
        return subprocess.CompletedProcess(command, child.wait(timeout=timeout))
    except subprocess.TimeoutExpired:
        kill_tree(child)
        child.kill()
        child.wait()
        raise


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def main():
    root = Path(__file__).resolve().parent.parent
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = root / "Saved/ArtChecks" / stamp
    output.mkdir(parents=True, exist_ok=False)
    log = output / "preview.log"
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe",
               str(root / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel",
               "-game", "-CireArtPreview", "-RenderOffscreen", "-ForceRes",
               "-windowed", "-ResX=1920", "-ResY=1080", "-unattended",
               "-nosound", "-NoLiveCoding", f"-abslog={log}"]
    print("Rendering Tripo reference, idle, walk and jog checks at 1920x1080.", flush=True)
    failure = None
    code = None
    try:
        result = run_editor(command, 120, cwd=root, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        code = result.returncode
    except (OSError, subprocess.TimeoutExpired) as error:
        failure = str(error)
    contents = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    passed = "CIRE_ART_PREVIEW_PASS" in contents
    failed = "CIRE_ART_PREVIEW_FAIL" in contents
    if not failure and (code != 0 or not passed or failed):
        failure = f"exit={code}, explicit_pass={passed}, explicit_fail={failed}"
    paths = re.findall(r"CIRE_ART_PREVIEW_CAPTURE name=\S+ file=(.+)", contents)
    captures = []
    for path in paths:
        item = {"path": path.strip()}
        try:
            with Path(item["path"]).open("rb") as image:
                header = image.read(24)
            if header[:8] != b"\x89PNG\r\n\x1a\n":
                raise ValueError("not a PNG")
            width, height = struct.unpack(">II", header[16:24])
            item.update(width=width, height=height)
            if (width, height) != (1920, 1080):
                failure = failure or "capture resolution mismatch"
        except (OSError, ValueError, struct.error) as error:
            item["error"] = str(error)
            failure = failure or "missing/invalid capture"
        captures.append(item)
    if len(captures) != 5:
        failure = failure or f"expected five captures, got {len(captures)}"
    report = {"passed": failure is None, "failure": failure, "exit_code": code,
              "log": str(log), "captures": captures,
              "visual_review_required": True,
              "checks": [line for line in contents.splitlines() if "CIRE_ART_PREVIEW_POSE_CHECK" in line]}
    report_path = output / "report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"CIRE_ART_CHECK_{'PASS' if not failure else 'FAIL'}: {report_path}", flush=True)
    if failure:
        print(failure, flush=True)
    return 0 if failure is None else 1


if __name__ == "__main__":
    sys.exit(main())
