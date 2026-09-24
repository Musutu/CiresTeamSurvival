"""Bounded loopback test: dedicated server plus two real clients on opposing teams.

Fixture chat remains between these three locally created test processes. Only owned
child processes are terminated. No player account, external server, or live game is used.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import socket
import subprocess
import sys
import time


def read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def stop_child(child: subprocess.Popen[bytes]) -> None:
    if child.poll() is not None:
        return
    child.terminate()
    try:
        child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait(timeout=5)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--project", type=Path, default=root / "CiresTeamSurvival.uproject")
    parser.add_argument("--port", type=int, default=7783)
    parser.add_argument("--startup-timeout", type=float, default=60)
    parser.add_argument("--probe-timeout", type=float, default=85)
    parser.add_argument("--tripo-champions", action="store_true", help="Verify imported Tripo meshes, locomotion and materials on both remote clients")
    args = parser.parse_args()
    if not args.editor.is_file() or not args.project.is_file():
        parser.error("UnrealEditor-Cmd or project does not exist")
    if not 1024 <= args.port <= 65535:
        parser.error("port must be 1024..65535")
    if not 1 <= args.startup_timeout <= 120 or not 1 <= args.probe_timeout <= 120:
        parser.error("timeouts must be 1..120 seconds")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as check:
        try:
            check.bind(("127.0.0.1", args.port))
        except OSError as error:
            parser.error(f"UDP port {args.port} is unavailable: {error}")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = args.project.resolve().parent / "Saved" / "InterfaceSmoke" / stamp
    output.mkdir(parents=True, exist_ok=False)
    logs = {name: output / f"{name}.log" for name in ("server", "client0", "client1")}
    common = ["-nullrhi", "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-ExecCmds=t.MaxFPS 60"]
    client_art_flags = ["-CireTripoChampions"] if args.tripo_champions else []
    commands = {
        "server": [str(args.editor), str(args.project.resolve()), "/Game/Maps/Citadel", "-server", "-MULTIHOME=127.0.0.1", f"-port={args.port}", "-CireInterfaceServer", f"-abslog={logs['server']}", *common],
        **{name: [str(args.editor), str(args.project.resolve()), f"127.0.0.1:{args.port}", "-game", "-CireInterfaceClient", f"-abslog={logs[name]}", *common, *client_art_flags] for name in ("client0", "client1")},
    }
    children: dict[str, subprocess.Popen[bytes]] = {}
    failure = ""
    start = time.monotonic()
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    try:
        print(f"Starting interface probe at loopback port {args.port}.", flush=True)
        children["server"] = subprocess.Popen(commands["server"], cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags)
        deadline = time.monotonic() + args.startup_timeout
        while "CIRE_INTERFACE_SERVER_READY" not in read_log(logs["server"]):
            if children["server"].poll() is not None:
                raise RuntimeError("server exited before readiness")
            if time.monotonic() > deadline:
                raise TimeoutError("server readiness timeout")
            time.sleep(0.25)
        print("Dedicated server ready; starting two remote clients.", flush=True)
        for name in ("client0", "client1"):
            children[name] = subprocess.Popen(commands[name], cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags)
        deadline = time.monotonic() + args.probe_timeout
        while any(child.poll() is None for child in children.values()):
            for name, path in logs.items():
                contents = read_log(path)
                if "CIRE_INTERFACE_CLIENT_FAIL" in contents or "CIRE_INTERFACE_SERVER_FAIL" in contents:
                    raise RuntimeError(f"{name} reported an assertion failure")
            if time.monotonic() > deadline:
                raise TimeoutError("interface probe timeout")
            time.sleep(0.25)
        for name, child in children.items():
            marker = "CIRE_INTERFACE_SERVER_PASS" if name == "server" else "CIRE_INTERFACE_CLIENT_PASS"
            if child.returncode != 0 or marker not in read_log(logs[name]):
                raise RuntimeError(f"{name}: exit={child.returncode}, success marker present={marker in read_log(logs[name])}")
    except (OSError, RuntimeError, TimeoutError) as error:
        failure = str(error)
    finally:
        for name in ("client0", "client1", "server"):
            if name in children:
                stop_child(children[name])
    evidence = [f"{name}: {line}" for name, path in logs.items() for line in read_log(path).splitlines() if "CIRE_INTERFACE_" in line]
    report = {
        "passed": not failure,
        "failure": failure or None,
        "duration_seconds": round(time.monotonic() - start, 2),
        "tripo_champions": args.tripo_champions,
        "exit_codes": {name: child.returncode for name, child in children.items()},
        "logs": {name: str(path) for name, path in logs.items()},
        "coverage": ["two actual remote clients on opposing teams", "opposing PvE actors excluded from replication", "party/all chat routing",
                     "unmapped target's lethal damage event arrives with stable identity/head location, outgoing ownership, and opposing-team privacy",
                     "self and ally selection RPCs, invalid null selection preserves target, explicit ground-clear RPC",
                     "private nonreplicated collision-free selection ring, overlay restoration, isolated unit selection trace channel",
                     "opponent selection/damage rejected outside arena", "arena opponent replication and selection RPC roundtrip",
                     "damage/healing event RPC and authoritative meter replication", "enemy actors removed in recovery", "next survival cycle privacy"],
        "evidence": evidence,
    }
    if args.tripo_champions:
        report["coverage"].extend([
            "all drafted allied Tripo meshes and skeleton-matched locomotion on remote clients",
            "finite capsule-aligned feet transforms and expected champion heights",
            "imported PBR materials preserved during selection and after overlay restoration",
        ])
    report_path = output / "report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for line in evidence:
        print(line)
    print(f"CIRE_INTERFACE_SMOKE_{'PASS' if not failure else 'FAIL'}: {report_path}")
    if failure:
        print(failure, file=sys.stderr)
    return 0 if not failure else 1


if __name__ == "__main__":
    raise SystemExit(main())
