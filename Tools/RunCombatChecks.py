"""Run bounded gameplay, telemetry and rendered combat fixtures; record explicit PASS evidence."""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")


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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", choices=("features", "telemetry", "art"))
    args = parser.parse_args()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = ROOT / "Saved/CombatChecks" / stamp
    output.mkdir(parents=True)
    cases = {
        "features": ("CireCombatFeaturesProbe", "CIRE_COMBAT_FEATURES_PASS"),
        "telemetry": ("CireTelemetryProbe", "CIRE_TELEMETRY_PASS"),
        "art": ("CireCombatArtPreview", "CIRE_COMBAT_ART_PREVIEW_PASS"),
    }
    results = {}
    for name, (flag, marker) in cases.items():
        if args.only and name != args.only:
            continue
        log = output / f"{name}.log"
        command = [str(EDITOR), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel",
                   "-game", "-unattended", "-nosplash", "-nosound", "-nop4", "-stdout", "-FullStdOutLogOutput",
                   f"-{flag}", f"-abslog={log}"]
        if name == "art":
            command += ["-RenderOffscreen", "-ForceRes", "-ResX=1920", "-ResY=1080", "-CireTripoChampions"]
        else:
            command += ["-nullrhi"]
        with (output / f"{name}-console.log").open("w", encoding="utf-8") as stream:
            try:
                result = run_editor(command, 140, stdout=stream, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                code = result.returncode
            except subprocess.TimeoutExpired:
                code = -1
        text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        failures = [line for line in text.splitlines() if re.search(r"CIRE_\S*(?:FAIL|ERROR)|Fatal error:|Assertion failed:|Ensure condition failed:", line)]
        record = {"passed": code == 0 and marker in text and not failures, "exitCode": code,
                  "log": str(log), "failures": failures,
                  "evidence": [line for line in text.splitlines() if "CIRE_" in line and ("PASS" in line or "LOADED" in line)]}
        if name == "art":
            match = re.search(r"CIRE_COMBAT_ART_PREVIEW_PASS.*directory=(.+)", text)
            record["captureDirectory"] = match.group(1).strip() if match else None
            if match:
                captures = sorted(Path(record["captureDirectory"]).glob("*.png"))
                sizes = []
                for path in captures:
                    with path.open("rb") as image:
                        header = image.read(24)
                    size = [int.from_bytes(header[16:20], "big"), int.from_bytes(header[20:24], "big")]
                    sizes.append({"path": str(path), "size": size})
                record["captures"] = sizes
                record["passed"] &= len(sizes) == 9 and all(item["size"] == [1920,1080] for item in sizes)
        results[name] = record
        (output / "report.json").write_text(json.dumps({"passed": all(r["passed"] for r in results.values()), "results": results}, indent=2), encoding="utf-8")
        print(f"{name}: {'PASS' if record['passed'] else 'FAIL'} {log}", flush=True)
        for failure in failures:
            print(failure, flush=True)
    print(f"Report: {output / 'report.json'}", flush=True)
    return 0 if all(r["passed"] for r in results.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
