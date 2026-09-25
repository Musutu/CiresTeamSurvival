"""wave-director: summarize -CireWaveSoak logs (wave/cycle durations, lives, rescues, level-up times).

Usage: SummarizeWaveSoak.py <soak.txt|soak.log> [more...]
"""
from __future__ import annotations

import re
import statistics
import sys
from pathlib import Path


def summarize(path: Path) -> str:
    text = path.read_text(encoding="utf-8", errors="replace")
    waves = [float(m) for m in re.findall(r"CIRE_WAVE_SOAK_CLEAR wave=\d+ took=([\d.]+)", text)]
    cycles = [float(m) for m in re.findall(r"CIRE_WAVE_SOAK_CYCLE round=\d+ took=([\d.]+)", text)]
    levels = re.findall(r"CIRE_WAVE_SOAK_LEVEL mean=([\d.]+) t=([\d.]+)", text)
    marches = len(re.findall(r"CIRE_WAVE_SOAK_(?:RESCUE|FAILSAFE) march", text))
    despawns = len(re.findall(r"CIRE_WAVE_SOAK_(?:RESCUE|FAILSAFE) despawn", text))
    verdict = re.findall(r"CIRE_WAVE_SOAK_(PASS|FAIL) ([^\n]*)", text)
    lives = re.findall(r"lives=(\d+)/(\d+)", verdict[-1][1]) if verdict else []
    lines = [f"== {path.name}"]
    if waves:
        lines.append(f"waves cleared={len(waves)} mean={statistics.mean(waves):.1f}s median={statistics.median(waves):.1f}s "
                     f"min={min(waves):.1f}s max={max(waves):.1f}s over150={sum(w > 150 for w in waves)} over90={sum(w > 90 for w in waves)}")
    if cycles:
        lines.append("cycles=" + ", ".join(f"{c:.0f}s ({c / 60:.1f} min)" for c in cycles))
    if levels:
        lines.append("mean hero level reached: " + ", ".join(f"L{float(level):.0f}@{float(t):.0f}s" for level, t in levels[:6]))
    lines.append(f"failsafe marches={marches} despawns={despawns}")
    if lives:
        lines.append(f"final lives Ember/Dusk={lives[0][0]}/{lives[0][1]} (lost {200 - int(lives[0][0]) - int(lives[0][1])})")
    if verdict:
        lines.append(f"verdict={verdict[-1][0]}")
    return "\n".join(lines)


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        print(summarize(Path(arg)))
