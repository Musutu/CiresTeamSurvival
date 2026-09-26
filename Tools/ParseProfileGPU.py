"""Summarise a ProfileGPU dump from an Unreal log (town-perf): the passes of each pipeline down to a depth, by inclusive time.

    python Tools/ParseProfileGPU.py <log> [--depth 3] [--min-ms 0.3] [--occurrence N]
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("log")
    p.add_argument("--depth", type=int, default=3)
    p.add_argument("--min-ms", type=float, default=0.3)
    p.add_argument("--occurrence", type=int, default=0, help="which ProfileGPU dump in the log (0 = first)")
    a = p.parse_args()
    lines = Path(a.log).read_text(encoding="utf-8", errors="replace").splitlines()
    dumps: list[list[str]] = []
    current: list[str] | None = None
    pipeline = ""
    for line in lines:
        body = line.split("LogRHI: Display: ", 1)[-1] if "LogRHI: Display: " in line else None
        if body is None:
            continue
        m = re.match(r"\s*(Graphics|Compute|Copy) pipeline (\d+)", body)
        if m:
            pipeline = f"{m.group(1)} {m.group(2)}"
            if m.group(1) == "Copy" and (current is None or current and current[-1].startswith("#")):
                current = []
                dumps.append(current)
            if current is not None:
                current.append(f"# {pipeline}")
            continue
        if current is None or "┃" not in body:
            continue
        cols = body.split("┃")
        if len(cols) < 5:
            continue
        inc, name = cols[2], cols[3]
        t = re.search(r"([\d.]+) ms", inc)
        if not t:
            continue
        depth = (len(name) - len(name.lstrip(" "))) // 2
        current.append(f"{depth}|{float(t.group(1))}|{name.strip()}")
    if not dumps:
        print("no ProfileGPU dump found")
        return 1
    dump = dumps[min(a.occurrence, len(dumps) - 1)]
    for entry in dump:
        if entry.startswith("#"):
            print(entry)
            continue
        depth, ms, name = entry.split("|", 2)
        if int(depth) <= a.depth and float(ms) >= a.min_ms:
            print(f"{'  ' * int(depth)}{float(ms):7.3f} ms  {name}")
    print(f"({len(dumps)} dumps in log)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
