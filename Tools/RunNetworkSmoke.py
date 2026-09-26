"""Run an actual bounded dedicated-server + remote-client development smoke test.

Only the two child processes created here may be terminated by this script.
Probe hooks are excluded from shipping builds and require explicit command flags.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time


# AutoSDK is off on this machine, so every editor boot otherwise runs "Build.bat -Mode=ValidatePlatforms"
# and blocks on Build.bat's machine-wide lock file while any other worktree compiles. Probes only target Win64.
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def stop_child(child: subprocess.Popen[bytes] | None) -> None:
    if child is None or child.poll() is not None:
        return
    kill_tree(child)
    child.terminate()
    try:
        child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait(timeout=5)


def main() -> int:
    project_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--project", type=Path, default=project_root / "CiresTeamSurvival.uproject")
    parser.add_argument("--port", type=int, default=7781)
    parser.add_argument("--startup-timeout", type=float, default=60)
    parser.add_argument("--probe-timeout", type=float, default=45)
    args = parser.parse_args()
    if not args.editor.is_file() or not args.project.is_file():
        parser.error("UnrealEditor-Cmd or project file does not exist")
    if not 1024 <= args.port <= 65535:
        parser.error("choose an unprivileged UDP port from 1024 to 65535")
    if not 1 <= args.startup_timeout <= 120 or not 1 <= args.probe_timeout <= 90:
        parser.error("startup timeout must be 1..120 seconds; probe timeout 1..90 seconds")
    # Refuse to connect to an unrelated server already using the requested port.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as port_check:
        try:
            port_check.bind(("127.0.0.1", args.port))
        except OSError as error:
            parser.error(f"UDP port {args.port} is unavailable: {error}")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = args.project.resolve().parent / "Saved" / "NetworkSmoke" / stamp
    output.mkdir(parents=True, exist_ok=False)
    server_log, client_log = output / "server.log", output / "client.log"
    common = ["-nullrhi", "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-ExecCmds=t.MaxFPS 60"]
    server_command = [str(args.editor), str(args.project.resolve()), "/Game/Maps/Citadel", "-server", f"-port={args.port}",
                      "-CireNetServerProbe", f"-CireNetProbeTimeout={args.startup_timeout + args.probe_timeout:.0f}", f"-abslog={server_log}", *common]
    client_command = [str(args.editor), str(args.project.resolve()), f"127.0.0.1:{args.port}", "-game",
                      "-CireClientProbe", f"-abslog={client_log}", *common]
    server = client = None
    failure = ""
    server_exit = client_exit = None
    start = time.monotonic()
    creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    try:
        print(f"Starting dedicated network probe on 127.0.0.1:{args.port}", flush=True)
        server = subprocess.Popen(server_command, cwd=args.project.resolve().parent,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation_flags, env=EDITOR_ENV)
        deadline = time.monotonic() + args.startup_timeout
        while "CIRE_NET_SERVER_READY" not in read_log(server_log):
            if server.poll() is not None:
                raise RuntimeError(f"server exited before readiness (code {server.returncode})")
            if time.monotonic() > deadline:
                raise TimeoutError("server readiness timeout")
            time.sleep(0.25)
        print("Dedicated server ready; starting separate remote client.", flush=True)
        client = subprocess.Popen(client_command, cwd=args.project.resolve().parent,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation_flags, env=EDITOR_ENV)
        deadline = time.monotonic() + args.probe_timeout
        while client.poll() is None or server.poll() is None:
            if time.monotonic() > deadline:
                raise TimeoutError("network probe/disconnect timeout")
            if "CIRE_NET_CLIENT_FAIL" in read_log(client_log) or "CIRE_NET_SERVER_FAIL" in read_log(server_log):
                raise RuntimeError("probe reported a validation failure")
            time.sleep(0.25)
        server_exit, client_exit = server.returncode, client.returncode
        if server_exit != 0 or client_exit != 0:
            raise RuntimeError(f"unexpected process exit: server={server_exit}, client={client_exit}")
        if "CIRE_NET_CLIENT_PASS" not in read_log(client_log) or "CIRE_NET_SERVER_PASS" not in read_log(server_log):
            raise RuntimeError("one or both success markers are missing")
    except (OSError, RuntimeError, TimeoutError) as error:
        failure = str(error)
    finally:
        stop_child(client)
        stop_child(server)
        if server is not None:
            server_exit = server.returncode
        if client is not None:
            client_exit = client.returncode
    evidence = [line for path in (server_log, client_log) for line in read_log(path).splitlines() if "CIRE_NET_" in line]
    passed = not failure
    report = {
        "passed": passed,
        "failure": failure or None,
        "duration_seconds": round(time.monotonic() - start, 2),
        "server_exit_code": server_exit,
        "client_exit_code": client_exit,
        "server_log": str(server_log),
        "client_log": str(client_log),
        "evidence": evidence,
        "coverage": ["remote client connection", "replicated game state and champion stats", "authoritative draft RPC", "remote movement verified on client and server",
                     "client hostile classification and target RPC replication", "invalid shop/cast/draft rejection",
                     "same champion and 5v5 membership preserved on disconnect"],
    }
    report_path = output / "report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for line in evidence:
        print(line)
    print(f"CIRE_NETWORK_SMOKE_{'PASS' if passed else 'FAIL'}: {report_path}")
    if failure:
        print(failure, file=sys.stderr)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
