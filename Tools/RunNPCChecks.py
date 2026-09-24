"""Run the NPC role / Pack Leader / threat checks and write Saved/NPCChecks/<stamp>/report.json.

native  : -CireCombatExpansionProbe (includes CIRE_NPC_SMOKE, CIRE_NPC_DATA_SMOKE,
          CIRE_THREAT_RULES, CIRE_NPC_ROLES and every existing expansion suite)
preview : -CireNPCPackPreview offscreen 1920x1080 captures of a challenge pack + leader
network : dedicated server + one remote client verify replicated NPC UI state

Only child processes started here are stopped. Timeouts are generous because other
worktrees may hold the UnrealBuildTool mutex while the editor starts.
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


def stop(child: subprocess.Popen | None) -> None:
    if child is None or child.poll() is not None:
        return
    child.terminate()
    try:
        child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait(timeout=5)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", choices=("native", "preview", "network", "all"), default="all")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--port", type=int, default=7795)
    args = parser.parse_args()
    project = ROOT / "CiresTeamSurvival.uproject"
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = ROOT / "Saved/NPCChecks" / stamp
    output.mkdir(parents=True, exist_ok=False)
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    common = ["-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash"]
    reports: dict[str, dict] = {}

    def launch(address: str, switches: list[str], log: Path) -> subprocess.Popen:
        command = [str(args.editor), str(project), address, *switches, f"-abslog={log}", *common]
        return subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation)

    def save(name: str, logs: list[Path], codes: list, markers: list[str], failure: str = "", extra: dict | None = None) -> None:
        texts = [read(log) for log in logs]
        errors = [line for text in texts for line in text.splitlines() if FAILURE.search(line)]
        record = {
            "passed": not failure and not errors and all(code == 0 for code in codes) and all(m in t for m, t in zip(markers, texts)),
            "failure": failure or None, "exitCodes": codes, "logs": [str(log) for log in logs], "errors": errors,
            "evidence": [line.strip() for text in texts for line in text.splitlines()
                         if "CIRE_" in line and any(w in line for w in ("PASS", "CAPTURE", "UNIT", "STATE"))],
        }
        record.update(extra or {})
        reports[name] = record
        (output / "report.json").write_text(json.dumps({"passed": all(r["passed"] for r in reports.values()), "results": reports}, indent=2) + "\n", encoding="utf-8")
        print(f"{name}: {'PASS' if record['passed'] else 'FAIL'}", flush=True)
        for line in errors + ([failure] if failure else []):
            print("  " + line, flush=True)

    def single(name: str, switches: list[str], marker: str, timeout: int, extra=None) -> None:
        log = output / f"{name}.log"
        child, failure = None, ""
        try:
            print(f"Starting {name}", flush=True)
            child = launch("/Game/Maps/Citadel", ["-game", *switches], log)
            child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            failure = f"{name} timed out after {timeout}s"
        except OSError as error:
            failure = str(error)
        finally:
            stop(child)
        info = extra(read(log)) if extra else None
        save(name, [log], [child.returncode if child else None], [marker], failure, info)

    if args.only in ("native", "all"):
        single("native", ["-nullrhi", "-CireCombatExpansionProbe"], "CIRE_NPC_ROLES_PASS", 600)
    if args.only in ("preview", "all"):
        def captures(text: str) -> dict:
            return {"captures": re.findall(r"CIRE_NPC_PACK_PREVIEW_CAPTURE name=\d+ file=(\S+)", text), "visual_review_required": True}
        single("preview", ["-CireNPCPackPreview", "-RenderOffscreen", "-ForceRes", "-windowed", "-ResX=1920", "-ResY=1080"],
               "CIRE_NPC_PACK_PREVIEW_PASS", 300, captures)
    if args.only in ("network", "all"):
        logs = [output / "server.log", output / "client.log"]
        children: list[subprocess.Popen] = []
        failure = ""
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as check:
                check.bind(("127.0.0.1", args.port))
            print(f"Starting NPC network check on UDP {args.port}", flush=True)
            children.append(launch("/Game/Maps/Citadel", ["-server", f"-port={args.port}", "-CireNPCNetServer"], logs[0]))
            deadline = time.monotonic() + 240
            while "CIRE_NPC_NET_SERVER_READY" not in read(logs[0]):
                if children[0].poll() is not None:
                    raise RuntimeError(f"server exited before readiness: {children[0].returncode}")
                if time.monotonic() >= deadline:
                    raise TimeoutError("server readiness timeout")
                time.sleep(.25)
            children.append(launch(f"127.0.0.1:{args.port}", ["-game", "-nullrhi", "-CireNPCNetClient"], logs[1]))
            deadline = time.monotonic() + 240
            while any(child.poll() is None for child in children):
                if time.monotonic() >= deadline:
                    raise TimeoutError("NPC network probe timeout")
                if any(child.poll() not in (None, 0) for child in children):
                    raise RuntimeError("a network child exited unsuccessfully")
                time.sleep(.25)
        except (OSError, RuntimeError, TimeoutError) as error:
            failure = str(error)
        finally:
            for child in reversed(children):
                stop(child)
        codes = [children[i].returncode if i < len(children) else None for i in range(2)]
        save("network", logs, codes, ["CIRE_NPC_NET_SERVER_PASS", "CIRE_NPC_NET_CLIENT_PASS"], failure)
    print(f"Report: {output / 'report.json'}", flush=True)
    return 0 if reports and all(r["passed"] for r in reports.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
