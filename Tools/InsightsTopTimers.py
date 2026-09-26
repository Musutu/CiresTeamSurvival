"""Top CPU timers of a thread inside an Insights trace region (town-perf), headless.

    python Tools/InsightsTopTimers.py <trace.utrace> [--region CireTownFight0] [--thread GameThread] [--top 40]

Runs UnrealInsights -NoUI with TimingInsights.ExportTimerStatistics and prints the timers with the largest
exclusive time per frame of the region (the CSV is kept next to the trace).
"""
from __future__ import annotations

import argparse
import csv
import subprocess
from pathlib import Path

INSIGHTS = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealInsights.exe")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("trace")
    p.add_argument("--region", default="CireTownFight0")
    p.add_argument("--thread", default="GameThread")
    p.add_argument("--top", type=int, default=40)
    p.add_argument("--frames", type=float, default=0, help="frames in the region (per-frame ms); 0 = print totals")
    p.add_argument("--sort", default="exclusive", choices=("exclusive", "inclusive"))
    p.add_argument("--start", type=float, default=-1, help="seconds from the trace start (instead of --region)")
    p.add_argument("--end", type=float, default=-1)
    p.add_argument("--force", action="store_true", help="re-export even when the CSV exists")
    a = p.parse_args()
    trace = Path(a.trace).resolve()
    out = trace.with_name(f"{trace.stem}.{a.region}.{a.thread}.csv")
    window = f"-startTime={a.start} -endTime={a.end}" if a.start >= 0 else f"-region={a.region}"
    if a.start >= 0:
        out = trace.with_name(f"{trace.stem}.{int(a.start)}-{int(a.end)}.{a.thread}.csv")
    cmd = f'TimingInsights.ExportTimerStatistics {out.as_posix()} -threads={a.thread} {window}'  # no inner quotes: the value is re-parsed
    # The value must start with its own quote (-Key="..."), so the command line is written by hand.
    line = f'"{INSIGHTS}" -OpenTraceFile="{trace}" -NoUI -AutoQuit -log -ExecOnAnalysisCompleteCmd="{cmd}"'
    if not out.exists() or a.force:
        subprocess.run(line, check=False, timeout=1800)
    if not out.exists():
        print(f"no export written ({out})")
        return 1
    rows = list(csv.DictReader(out.open(encoding="utf-8", errors="replace")))
    if not rows:
        print("empty export")
        return 1
    keys = list(rows[0].keys())
    incl = "Incl" if "Incl" in keys else next(k for k in keys if "Incl" in k and "Total" in k)
    excl = "Excl" if "Excl" in keys else next(k for k in keys if "Excl" in k and "Total" in k)
    count = next((k for k in keys if k.lower() in ("count", "instance count")), None)
    key = excl if a.sort == "exclusive" else incl
    rows.sort(key=lambda r: float(r[key] or 0), reverse=True)
    scale = 1000.0 / a.frames if a.frames else 1000.0
    unit = "ms/frame" if a.frames else "ms total"
    print(f"{'excl ' + unit:>18} {'incl ' + unit:>18} {'count':>8}  timer")
    for r in rows[: a.top]:
        print(f"{float(r[excl] or 0) * scale:18.2f} {float(r[incl] or 0) * scale:18.2f} {r.get(count, '') if count else '':>8}  {r['Name']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
