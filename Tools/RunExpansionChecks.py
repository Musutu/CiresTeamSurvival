"""Run bounded native, two-client replication, and real replay integration checks.

Only child processes created by this runner are terminated. No editor/build or
asset generation is performed. All probes require explicit development flags.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
FAILURE = re.compile(r"CIRE_\S*(?:FAIL|ERROR)|Fatal error:|Assertion failed:|Ensure condition failed:")


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""


def editor_env() -> dict[str, str]:
    # AutoSDK is off on this machine, so every editor boot otherwise runs
    # "Build.bat -Mode=ValidatePlatforms" and blocks on Build.bat's machine-wide
    # lock file while any other worktree compiles. The probes only target Win64.
    return {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def stop(child: subprocess.Popen | None) -> None:
    if child is None or child.poll() is not None:
        return
    if os.name == "nt":
        # Kill the whole tree so a Build.bat child spawned by the editor cannot outlive it.
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)
    child.terminate()
    try:
        child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait(timeout=5)


def result(logs: list[Path], codes: list[int | None], markers: list[str], failure: str = "") -> dict:
    texts = [read(path) for path in logs]
    errors = [line for text in texts for line in text.splitlines() if FAILURE.search(line)]
    return {
        "passed": not failure and not errors and all(code == 0 for code in codes)
                  and all(marker in text for marker, text in zip(markers, texts)),
        "failure": failure or None,
        "exitCodes": codes,
        "logs": [str(path) for path in logs],
        "errors": errors,
        "evidence": [line for text in texts for line in text.splitlines()
                     if "CIRE_" in line and any(word in line for word in ("PASS", "READY", "SAVED", "SEEK", "LIST"))],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", choices=("native", "network", "replay", "all"), default="all")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--project", type=Path, default=ROOT / "CiresTeamSurvival.uproject")
    parser.add_argument("--port", type=int, default=7784)
    parser.add_argument("--fps", type=int, default=60, help="development probe frame limit, 15..240")
    # Generous by default: editors started while other worktrees hold the UnrealBuildTool mutex stall at startup.
    parser.add_argument("--timeout", type=int, default=600, help="seconds allowed per native probe / server startup")
    args = parser.parse_args()
    if not args.editor.is_file() or not args.project.is_file():
        parser.error("editor executable or project is missing")
    if not 1024 <= args.port <= 65535:
        parser.error("port must be between 1024 and 65535")
    if not 15 <= args.fps <= 240:
        parser.error("fps must be between 15 and 240")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = args.project.resolve().parent / "Saved/ExpansionChecks" / stamp
    output.mkdir(parents=True, exist_ok=False)
    common = ["-nullrhi", "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash", f"-ExecCmds=t.MaxFPS {args.fps}"]
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    reports: dict[str, dict] = {}

    def save(name: str, record: dict) -> None:
        reports[name] = record
        (output / "report.json").write_text(json.dumps({"passed": all(r["passed"] for r in reports.values()), "results": reports}, indent=2) + "\n", encoding="utf-8")
        print(f"{name}: {'PASS' if record['passed'] else 'FAIL'}", flush=True)
        for line in record["evidence"] + record["errors"]:
            print(line, flush=True)
        if record["failure"]:
            print(record["failure"], flush=True)

    def launch(address: str, switches: list[str], log: Path) -> subprocess.Popen:
        command = [str(args.editor.resolve()), str(args.project.resolve()), address, *switches, f"-abslog={log}", *common]
        return subprocess.Popen(command, cwd=args.project.resolve().parent, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, creationflags=creation, env=editor_env())

    for name, flag, marker in (("native", "CireCombatExpansionProbe", "CIRE_COMBAT_EXPANSION_PASS"),
                               ("replay", "CireReplayProbe", "CIRE_REPLAY_INTEGRATION_PASS")):
        if args.only not in (name, "all"):
            continue
        log = output / f"{name}.log"
        child = None
        failure = ""
        try:
            print(f"Starting {name} checks", flush=True)
            child = launch("/Game/Maps/Citadel", ["-game", f"-{flag}"], log)
            child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            failure = f"{name} probe timed out after {args.timeout} seconds"
        except OSError as error:
            failure = str(error)
        finally:
            stop(child)
        save(name, result([log], [child.returncode if child else None], [marker], failure))

    if args.only in ("network", "all"):
        logs = [output / "server.log", output / "client0.log", output / "client1.log"]
        children: list[subprocess.Popen] = []
        failure = ""
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as check:
                check.bind(("127.0.0.1", args.port))
            print(f"Starting dedicated server and two remote clients on UDP {args.port}", flush=True)
            children.append(launch("/Game/Maps/Citadel", ["-server", f"-port={args.port}", "-CireExpansionNetServer"], logs[0]))
            deadline = time.monotonic() + args.timeout
            while "CIRE_EXPANSION_NET_SERVER_READY" not in read(logs[0]):
                if children[0].poll() is not None:
                    raise RuntimeError(f"server exited before readiness: {children[0].returncode}")
                if time.monotonic() >= deadline:
                    raise TimeoutError("server readiness timeout")
                time.sleep(.25)
            for index in (1, 2):
                children.append(launch(f"127.0.0.1:{args.port}", ["-game", "-CireExpansionNetClient"], logs[index]))
            deadline = time.monotonic() + max(95, args.timeout)
            while any(child.poll() is None for child in children):
                if time.monotonic() >= deadline:
                    raise TimeoutError("two-client expansion probe timeout")
                if any(FAILURE.search(read(log)) for log in logs):
                    raise RuntimeError("network probe reported a validation failure")
                if any(child.poll() not in (None, 0) for child in children):
                    raise RuntimeError("a network child exited unsuccessfully")
                time.sleep(.25)
        except (OSError, RuntimeError, TimeoutError) as error:
            failure = str(error)
        finally:
            for child in reversed(children):
                stop(child)
        codes = [children[i].returncode if i < len(children) else None for i in range(3)]
        network_result = result(logs, codes, ["CIRE_EXPANSION_NET_SERVER_PASS", "CIRE_EXPANSION_NET_CLIENT_PASS", "CIRE_EXPANSION_NET_CLIENT_PASS"], failure)
        movement_markers = ["CIRE_MOVEMENT_NET_SERVER_PASS", "CIRE_MOVEMENT_NET_CLIENT_PASS", "CIRE_MOVEMENT_NET_CLIENT_PASS"]
        if not all(marker in read(log) for marker, log in zip(movement_markers, logs)):
            network_result["passed"] = False
            network_result["failure"] = network_result["failure"] or "Movement owner/proxy network subprobe did not pass"
        save("network", network_result)
    print(f"Report: {output / 'report.json'}", flush=True)
    return 0 if reports and all(record["passed"] for record in reports.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
