"""World edit probe (world-editor, Docs/WorldEditor.md): a real town load with a world edit set on a dedicated server
and a remote client.

    python Tools/RunWorldEditProbe.py [--port 18010] [--set ProbeTest] [--timeout 1800]

The server loads the Medieval Kingdom town with -CireWorldEdit=<set> and checks every removed piece is gone (removed
while the town streamed, found by its stable id) and every twin the set keeps is there. A client then joins, follows the
replicated set name + hash, streams its own copy of the town and runs the same checks, then leaves. The server switches
the set off live (the removed buildings load again), rebuilds the navmesh and checks it changed over them.
Both processes run -NoTimeouts: a client blocks for minutes while its town streams.
Only child processes created here are terminated. Writes Saved/WorldEditProbe/<stamp>/{server.log, client.log, report.json}.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}
FAILURE = re.compile(r"CIRE_WORLD_EDIT_PROBE_(?:CHECK_FAIL|SERVER_FAIL|CLIENT_FAIL)|Fatal error:|Assertion failed:")


def kill_tree(child) -> None:
    if child is not None and os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=18010)
    parser.add_argument("--set", default="ProbeTest")
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    folder = ROOT / "Saved/WorldEditProbe" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True, exist_ok=False)
    server_log, client_log = folder / "server.log", folder / "client.log"
    common = ["-nullrhi", "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-NoTimeouts", "-CireNoReplay", "-CireWorldEditProbe"]
    project = str(ROOT / "CiresTeamSurvival.uproject")
    server_cmd = [str(EDITOR), project, "/Game/Maps/Citadel", "-server", f"-port={args.port}", "-CireTown", f"-CireWorldEdit={args.set}", f"-abslog={server_log}", *common]
    client_cmd = [str(EDITOR), project, f"127.0.0.1:{args.port}", "-game", f"-abslog={client_log}", *common]
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    started = time.monotonic()
    server = client = None
    failure = ""
    try:
        server = subprocess.Popen(server_cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=EDITOR_ENV, creationflags=flags)
        print("server started: loading the town with the set", flush=True)
        while True:
            text = read(server_log)
            if "CIRE_WORLD_EDIT_PROBE_NAV_BEFORE" in text or "CIRE_WORLD_EDIT_PROBE_SERVER_" in text:
                break
            if server.poll() is not None:
                raise RuntimeError(f"server exited early ({server.returncode})")
            if time.monotonic() - started > args.timeout:
                raise TimeoutError("server never finished its load checks")
            time.sleep(2)
        print("server checked its town; starting the client", flush=True)
        client = subprocess.Popen(client_cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=EDITOR_ENV, creationflags=flags)
        while server.poll() is None or client.poll() is None:
            if time.monotonic() - started > args.timeout:
                raise TimeoutError("probe timeout")
            if client.poll() is not None and "CIRE_WORLD_EDIT_PROBE_CLIENT_" not in read(client_log):
                raise RuntimeError(f"client exited without a result ({client.returncode})")
            time.sleep(2)
    except (OSError, RuntimeError, TimeoutError) as error:
        failure = str(error)
    finally:
        kill_tree(client)
        kill_tree(server)
    stext, ctext = read(server_log), read(client_log)
    errors = [line for line in (stext + ctext).splitlines() if FAILURE.search(line)]
    evidence = [line.split("Display: ", 1)[-1].split("Warning: ", 1)[-1] for line in (stext + "\n" + ctext).splitlines()
                if re.search(r"CIRE_WORLD_EDIT_(PROBE|BEGIN|SAVINGS|LIVE|MISMATCH|MISSING)|CIRE_WORLD_EDIT realm", line)]
    passed = (not failure and not errors and "CIRE_WORLD_EDIT_PROBE_SERVER_PASS" in stext and "CIRE_WORLD_EDIT_PROBE_CLIENT_PASS" in ctext)
    report = dict(passed=passed, failure=failure or None, seconds=round(time.monotonic() - started, 1), server_log=str(server_log),
                  client_log=str(client_log), errors=errors, evidence=evidence)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for line in evidence + errors:
        print("  " + line)
    if failure:
        print("  " + failure)
    print(f"CIRE_WORLD_EDIT_PROBE_{'PASS' if passed else 'FAIL'}: {folder / 'report.json'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
