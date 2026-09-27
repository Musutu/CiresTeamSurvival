"""Run a Tools/Blender script in the portable headless Blender (Docs/RigAudit.md).

Always launch through this wrapper with the UE python (never through Git Bash: MSYS rewrites the "/"-style arguments):

    F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/RunBlender.py qa_turnaround.py -- --in X.fbx --out Saved/Blender/qa/X

The script runs as  blender.exe -b --factory-startup --python Tools/Blender/<script> -- <args> , with a timeout (the process
tree is killed on expiry) and a log in Saved/Blender/logs/<script>-<stamp>.log. Exit code: Blender's, or 124 on timeout.
The Blender scripts print BLENDER_RESULT {json} lines; they are echoed and collected into the log's .json twin.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BLENDER = Path(os.environ.get("CIRE_BLENDER", r"F:\Blender\blender-5.2.1-windows-x64\blender.exe"))


def run(script: str, args: list[str], timeout: int = 900, blender: Path = DEFAULT_BLENDER, quiet: bool = False) -> tuple[int, list[dict], Path]:
    path = Path(script)
    if not path.is_absolute():
        path = ROOT / "Tools" / "Blender" / path
    if not path.exists():
        raise FileNotFoundError(path)
    if not blender.exists():
        raise FileNotFoundError(f"Blender not found: {blender}")
    logs = ROOT / "Saved" / "Blender" / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:-3]
    log = logs / f"{path.stem}-{stamp}.log"
    command = [str(blender), "-b", "--factory-startup", "--python-exit-code", "3", "--python", str(path), "--", *args]
    env = {**os.environ, "CIRE_PROJECT_ROOT": str(ROOT), "PYTHONUNBUFFERED": "1"}
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    started = time.time()
    results: list[dict] = []
    with open(log, "w", encoding="utf-8", errors="replace") as out:
        out.write(" ".join(f'"{c}"' if " " in c else c for c in command) + "\n")
        out.flush()
        child = subprocess.Popen(command, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT, env=env, creationflags=creation)
        try:
            code = child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
            child.kill()
            code = 124
    text = log.read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        if line.startswith("BLENDER_RESULT "):
            try:
                results.append(json.loads(line[len("BLENDER_RESULT "):]))
            except json.JSONDecodeError:
                pass
        if not quiet and (line.startswith("BLENDER_") or "Traceback" in line or "Error:" in line):
            print(line)
    seconds = time.time() - started
    (log.with_suffix(".json")).write_text(json.dumps({"script": path.name, "args": args, "exit": code, "seconds": round(seconds, 2),
                                                      "results": results}, indent=1), encoding="utf-8")
    if not quiet:
        print(f"BLENDER_RUN {path.name} exit={code} seconds={seconds:.1f} log={log}")
    return code, results, log


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("script", help="script in Tools/Blender (or an absolute path)")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--blender", type=Path, default=DEFAULT_BLENDER)
    argv = sys.argv[1:]
    rest: list[str] = []
    if "--" in argv:
        cut = argv.index("--")
        argv, rest = argv[:cut], argv[cut + 1:]
    args = parser.parse_args(argv)
    code, _, _ = run(args.script, rest, args.timeout, args.blender)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
