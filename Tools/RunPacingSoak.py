"""Measure match length per phase with the bots-only headless wave soak (starts Unreal only when run).

Runs `-CireWaveSoak` (real match clock, real wave flow, 30 Hz fixed step, bots on both teams) and splits the
game time into: wave fighting (spawn -> clear), Skill Shop breathers (clear -> next spawn), prep (Skill Shop +
Armory window), arena and recovery, per cycle and for the whole match.

  python Tools/RunPacingSoak.py --cycles 3 --label before
  python Tools/RunPacingSoak.py --parse Saved/WaveSoak/pacing-before.txt

Bots-only is an upper bound for wave time: bots shop but play below a human team.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
from RunBalanceLab import EDITOR_ENV, ROOT, kill_tree

PHASES = {0: 'survival', 1: 'prep', 2: 'arena', 3: 'finished', 4: 'recovery'}


def parse(text):
    events, wave_deaths = [], []
    for line in text.splitlines():
        if m := re.search(r'CIRE_WAVE_SOAK_SPAWN wave=(\d+) type=(\S+) round=(\d+) t=([\d.]+)', line):
            events.append(('spawn', float(m[4]), int(m[1]), int(m[3]), m[2]))
        elif m := re.search(r'CIRE_WAVE_SOAK_CLEAR wave=(\d+) took=([\d.]+) lives=(\d+)/(\d+)(?: deaths=(\d+))?(?: level=([\d.]+) max_health=(\d+))?', line):
            events.append(('clear', None, int(m[1]), float(m[2]), (int(m[3]), int(m[4]))))
            if m[5] is not None: wave_deaths.append((int(m[1]), float(m[2]), int(m[5]), int(m[3]), int(m[4]), float(m[6] or 0), float(m[7] or 0)))
        elif m := re.search(r'CIRE_WAVE_SOAK_PHASE (\d+)->(\d+) round=(\d+) t=([\d.]+) waited=([\d.]+)', line):
            events.append(('phase', float(m[4]), int(m[1]), int(m[2]), float(m[5]), int(m[3])))
    waves, breathers, phase_time, cycles = [], [], {}, []
    last_clear_t, spawn_t = None, {}
    cycle = dict(waves=0.0, breathers=0.0, prep=0.0, arena=0.0, recovery=0.0, first_delay=0.0)
    t_first_spawn = None
    survival_since = 0.0
    for e in events:
        if e[0] == 'spawn':
            t = e[1]; spawn_t[e[2]] = t
            if t_first_spawn is None: t_first_spawn = t; cycle['first_delay'] = t
            elif last_clear_t is not None and last_clear_t >= survival_since: breathers.append(t - last_clear_t); cycle['breathers'] += t - last_clear_t
            elif last_clear_t is not None: cycle['first_delay'] += t - survival_since  # recovery -> the cycle's first spawn
            last_clear_t = None
        elif e[0] == 'clear':
            waves.append(e[3]); cycle['waves'] += e[3]
            last_clear_t = spawn_t.get(e[2], 0) + e[3]
        elif e[0] == 'phase':
            _, t, old, new, waited, rnd = e
            name = PHASES.get(old, str(old))
            if old in (1, 2, 4):
                phase_time.setdefault(name, []).append(waited); cycle[name] += waited
            if new == 0: survival_since = t
            if new == 0 or new == 3:
                cycle['total'] = sum(v for k, v in cycle.items() if k != 'total'); cycles.append(cycle)
                cycle = dict(waves=0.0, breathers=0.0, prep=0.0, arena=0.0, recovery=0.0, first_delay=0.0)
    match = re.findall(r'CIRE_WAVE_SOAK_(PASS|FAIL) [^\n]*t=(\d+)', text)
    levels = re.findall(r'CIRE_WAVE_SOAK_LEVEL mean=([\d.]+) t=([\d.]+)', text)
    deaths = re.findall(r'CIRE_WAVE_SOAK_(?:PASS|FAIL) [^\n]*hero_deaths=(\d+)', text)
    lives = re.findall(r'CIRE_WAVE_SOAK_(?:PASS|FAIL) [^\n]*lives=(\d+)/(\d+)', text)
    return dict(waves=waves, breathers=breathers, phases=phase_time, cycles=cycles,
                verdict=match[-1][0] if match else None, matchSeconds=float(match[-1][1]) if match else None,
                lives=[int(v) for v in lives[-1]] if lives else None,
                levels=[(float(a), float(b)) for a, b in levels],
                heroDeaths=int(deaths[-1]) if deaths else None,
                waveDeaths=[dict(wave=w, seconds=s, deaths=d, lives=[e, k], level=lv, maxHealth=hp) for w, s, d, e, k, lv, hp in wave_deaths])


def report(result):
    lines = []
    w = result['waves']
    if w:
        lines.append(f"waves={len(w)} total={sum(w)/60:.1f} min mean={sum(w)/len(w):.0f}s max={max(w):.0f}s")
    b = result['breathers']
    if b: lines.append(f"breathers={len(b)} total={sum(b)/60:.1f} min mean={sum(b)/len(b):.1f}s")
    for name, values in result['phases'].items():
        lines.append(f"{name}: n={len(values)} total={sum(values)/60:.1f} min mean={sum(values)/len(values):.1f}s")
    for i, c in enumerate(result['cycles'], 1):
        lines.append(f"cycle {i}: " + ', '.join(f"{k}={v/60:.2f}m" for k, v in c.items()))
    lines.append(f"match={result['matchSeconds'] and result['matchSeconds']/60:.1f} min verdict={result['verdict']} lives={result['lives']}")
    if result.get('waveDeaths'):
        lines.append('champion level / max health at each clear: ' + ', '.join(f"w{d['wave']}:L{d['level']:.1f}/{d['maxHealth']:.0f}" for d in result['waveDeaths']))
        lines.append('champion deaths per wave: ' + ', '.join(f"w{d['wave']}:{d['deaths']}" for d in result['waveDeaths']) + f" (total {result['heroDeaths']})")
    if result['levels']: lines.append('levels: ' + ', '.join(f"L{l:.0f}@{t:.0f}s" for l, t in result['levels'][:12]))
    return '\n'.join(lines)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--engine', type=Path, default=Path('F:/UE_5.8'))
    p.add_argument('--cycles', type=int, default=3)
    p.add_argument('--label', default='run')
    p.add_argument('--timeout', type=float, default=5400)
    p.add_argument('--parse', type=Path)
    a = p.parse_args()
    if a.parse:
        result = parse(a.parse.read_text(encoding='utf-8', errors='replace')); print(report(result)); return
    out = ROOT / 'Saved/WaveSoak'; out.mkdir(parents=True, exist_ok=True)
    summary = out / f'pacing-{a.label}.txt'; log = out / f'pacing-{a.label}.log'
    command = [str(a.engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'), str(ROOT / 'CiresTeamSurvival.uproject'), '/Game/Maps/Citadel',
               '-game', '-CireWaveSoak', f'-CireWaveSoakCycles={a.cycles}', '-CireWaveSoakSeconds=7200', '-nullrhi', '-nosound', '-benchmark',
               '-fps=30', '-unattended', '-nop4', '-NoSplash', f'-CireWaveSoakSummary={summary}', f'-abslog={log}']
    start = time.monotonic()
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0), env=EDITOR_ENV)
    try: code = child.wait(timeout=a.timeout)
    except subprocess.TimeoutExpired: kill_tree(child); child.kill(); code = -1
    text = summary.read_text(encoding='utf-8', errors='replace') if summary.exists() else (log.read_text(encoding='utf-8', errors='replace') if log.exists() else '')
    result = parse(text); result['exitCode'] = code; result['wallSeconds'] = time.monotonic() - start
    (out / f'pacing-{a.label}.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(report(result)); print(f'PACING_SOAK {a.label} exit={code} wall={result["wallSeconds"]:.0f}s summary={summary}')


if __name__ == '__main__':
    main()
