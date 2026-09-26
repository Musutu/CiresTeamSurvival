"""Measure every roster champion against its role peers in the native balance lab (starts Unreal only when run).

Each case is one headless all-AI fight: the five fixture heroes (knight, scholar, ranger, lancer, summoner, all
with their full profile kits) against mixed NPCs at hero level = wave, with one fixture slot replaced by the
champion under test (-CireBalanceChampion). The lab records per-hero damage, healing, health lost (damage taken
net of same-frame healing) and deaths; this script aggregates them per champion and phase and flags anything
outside the role band.

  python Tools/RunChampionLab.py --waves 3,10,20 --repeats 3 --parallel 3
  python Tools/RunChampionLab.py --summarize Saved/BalanceLab/champions-<stamp>

Bands are review thresholds relative to the role's median (not design targets): damage dealers compare personal
DPS, healers personal HPS and team damage taken per second, tanks damage taken and deaths.
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import time
import uuid
from RunBalanceLab import EDITOR_ENV, ROOT, stop_owned

DPS = ['ranger', 'lancer', 'summoner', 'wizard', 'gunblade', 'witch_slayer', 'huntress', 'aetheri_artificer',
       'troll_berserker_melee', 'troll_berserker_ranged', 'ether_golem_bruiser']  # kits-complete: roster kits
HEALERS = ['scholar', 'aetheri_warden', 'paladin_holy', 'ether_golem_support', 'dryad', 'whisp', 'evergrove_centaur', 'keeper_of_light']
# kits-complete: roster tanks take the knight's slot; tanks compare damage output and survivability against the tank median.
ROSTER_TANKS = ['bear', 'paladin_righteous', 'dwarf_miner', 'ether_golem_tank', 'orc_chieftain', 'totemic_behemoth', 'drakish_footman']
TANK_TESTS = [('aetheri_warden', 0)] + [(c, 0) for c in ROSTER_TANKS]  # the Warden's tank hybrid in the knight's slot
TANKS = ['knight'] + [c + '@tank' for c in ROSTER_TANKS]
BAND = (0.8, 1.25)


def cases_for(waves, repeats):
    out = []
    for repeat in range(1, repeats + 1):
        for wave in waves:
            for champion in DPS + HEALERS:
                out.append(dict(wave=wave, champion=champion, slot=-1, repeat=repeat))
            for champion, slot in TANK_TESTS:
                out.append(dict(wave=wave, champion=champion, slot=slot, repeat=repeat))
    return out


def key(case):
    return (case['wave'], case['champion'], case['slot'])


def launch(engine, output, case, enemies, seconds):
    stem = f"w{case['wave']}-{case['champion']}-s{case['slot']}-r{case['repeat']}"
    log = output / (stem + '.log')
    command = [str(engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'), str(ROOT / 'CiresTeamSurvival.uproject'), '/Game/Maps/Citadel',
               '-game', '-nullrhi', '-nosound', '-unattended', '-nop4', '-NoSplash', '-NoLogWindow', '-CireBalanceLab', '-CireBalanceExit',
               f"-CireBalanceWave={case['wave']}", '-CireBalanceLoadout=thematic', f"-CireBalanceChampion={case['champion']}",
               f"-CireBalanceSlot={case['slot']}", '-CireBalanceBots=5', f'-CireBalanceEnemies={enemies}', f'-CireBalanceSeconds={seconds}',
               f'-abslog={log}']
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0), env=EDITOR_ENV)
    return dict(case=case, child=child, log=log, start=time.monotonic(), completed=None)


def report_for(log):
    """The lab logs its report path; read it from this case's own log (parallel-safe)."""
    try:
        text = log.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return None
    for line in text.splitlines():
        if 'CIRE_BALANCE_LAB_RESULT' in line and 'report=' in line:
            path = Path(line.split('report=', 1)[1].strip())
            try:
                return path, json.loads(path.read_text(encoding='utf-8-sig'))
            except (OSError, ValueError):
                return None
    return None


def row_from(case, path, data):
    heroes = [h for h in data.get('finalHeroes', []) if h.get('team') == 0]
    seconds = max(.001, data['seconds'])
    team_taken = sum(h.get('damageTaken', 0) for h in heroes)
    rows = []
    for h in heroes:
        rows.append(dict(wave=case['wave'], repeat=case['repeat'], caseChampion=case['champion'], caseSlot=case['slot'],
                         champion=h.get('champion', ''), labChampion=bool(h.get('labChampion')), role=h.get('draftRole', ''),
                         dps=h['dps'], hps=h['hps'], dtps=h.get('dtps', 0), damageTaken=h.get('damageTaken', 0),
                         maxHealth=h.get('maxHealth', 0), dead=bool(h.get('dead')), share=h.get('teamDamageShare', 0),
                         result=data['result'], seconds=seconds, teamDps=data['dps'], teamHps=data['hps'],
                         teamDtps=team_taken / seconds, alliesAlive=data['alliesAlive'], report=str(path),
                         petDtps=h.get('petDamageTaken', 0) / seconds, petDeaths=h.get('petDeaths', 0), petTargetShare=h.get('petTargetShare', 0),
                         tankShare=data.get('tankTargetShare', 0)))
    return rows


def median(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else 0.0


def summarize(rows):
    phases = sorted({r['wave'] for r in rows})
    out = {}
    for wave in phases:
        at = [r for r in rows if r['wave'] == wave]
        per = {}
        # Personal numbers: the tested champion's own row in its own cases, plus every fixture appearance.
        for r in at:
            name = r['champion'] + ('@tank' if r['labChampion'] and r['caseSlot'] == 0 else '')
            per.setdefault(name, []).append(r)
        stats = {}
        for name, group in per.items():
            own = [r for r in group if r['labChampion']] or group
            stats[name] = dict(samples=len(own), dps=median(r['dps'] for r in own), hps=median(r['hps'] for r in own),
                               dtps=median(r['dtps'] for r in own), deaths=sum(r['dead'] for r in own) / max(1, len(own)),
                               teamDtps=median(r['teamDtps'] for r in own), teamSeconds=median(r['seconds'] for r in own),
                               wins=sum(r['result'] == 'allies_won' for r in own), share=median(r['share'] for r in own),
                               petDtps=median(r.get('petDtps', 0) for r in own), petDeaths=median(r.get('petDeaths', 0) for r in own),
                               petTargetShare=median(r.get('petTargetShare', 0) for r in own), tankShare=median(r.get('tankShare', 0) for r in own))
        dps_median = median(stats[c]['dps'] for c in DPS if c in stats)
        heal_median = median(stats[c]['hps'] for c in HEALERS if c in stats)
        tank_dps_median = median(stats[c]['dps'] for c in TANKS if c in stats)
        # Survivability: share of max health lost per second (lower is sturdier); the ratio is median / own, so > 1 = sturdier.
        for c in TANKS:
            if c in stats:
                own = [r for r in per[c] if r['labChampion']] or per[c]
                stats[c]['hpLossPerSecond'] = median(r['dtps'] / max(1.0, r['maxHealth']) for r in own)
        tank_loss_median = median(stats[c]['hpLossPerSecond'] for c in TANKS if c in stats)
        flags = {}
        for name, s in stats.items():
            f = []
            if name in DPS and dps_median > 0:
                s['dpsVsRole'] = s['dps'] / dps_median
                if not BAND[0] <= s['dpsVsRole'] <= BAND[1]: f.append('dps_outside_role_band')
            if name in HEALERS and heal_median > 0:
                s['hpsVsRole'] = s['hps'] / heal_median
                if not BAND[0] <= s['hpsVsRole'] <= BAND[1]: f.append('hps_outside_role_band')
            if name in TANKS and tank_dps_median > 0:
                s['dpsVsRole'] = s['dps'] / tank_dps_median
                if not BAND[0] <= s['dpsVsRole'] <= BAND[1]: f.append('tank_dps_outside_role_band')
                if s.get('hpLossPerSecond', 0) > 0 and tank_loss_median > 0:
                    s['survivalVsRole'] = tank_loss_median / s['hpLossPerSecond']
                    if not BAND[0] <= s['survivalVsRole'] <= BAND[1]: f.append('tank_survival_outside_role_band')
            if s['deaths'] > .34: f.append('dies_often')
            flags[name] = f
            s['flags'] = f
        out[wave] = dict(stats=stats, dpsMedian=dps_median, healMedian=heal_median, tankDpsMedian=tank_dps_median, tankLossMedian=tank_loss_median)
    return out


def write(output, rows):
    summary = summarize(rows)
    (output / 'champions.json').write_text(json.dumps(dict(schemaVersion=1, kind='measured-champion-comparison', band=BAND,
                                                           summary=summary, rows=rows), indent=2) + '\n', encoding='utf-8')
    lines = ['# Champion balance lab', '', f'Role band {BAND[0]}-{BAND[1]} x role median. DTPS = health lost per second (net of same-frame healing).', '']
    for wave, block in summary.items():
        lines += [f'## Wave / level {wave}', '', f"DPS role median {block['dpsMedian']:.1f}, healer HPS median {block['healMedian']:.1f}", '',
                  f"Tank DPS median {block['tankDpsMedian']:.1f} (tanks also show survival vs the tank median: >1 = sturdier)", '',
                  '| Champion | n | DPS | vs role | survival | HPS | DTPS | deaths | team DTPS | fight s | wins | flags |', '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|']
        for name, s in sorted(block['stats'].items()):
            ratio = s.get('dpsVsRole', s.get('hpsVsRole'))
            surv = s.get('survivalVsRole')
            lines.append(f"| {name} | {s['samples']} | {s['dps']:.1f} | {'' if ratio is None else f'{ratio:.2f}'} | {'' if surv is None else f'{surv:.2f}'} | {s['hps']:.1f} | {s['dtps']:.1f} | "
                         f"{s['deaths']:.0%} | {s['teamDtps']:.1f} | {s['teamSeconds']:.1f} | {s['wins']} | {', '.join(s['flags']) or '-'} |")
        pets = [(n, s) for n, s in sorted(block['stats'].items()) if s.get('petDtps') or s.get('petTargetShare')]
        if pets:
            lines += ['', 'Companions (credited to the owner above: pet damage is in the owner DPS):', '', '| Owner | pet DTPS | pet deaths / fight | share of monster targeting on the pet |', '|---|---:|---:|---:|']
            lines += [f"| {n} | {s['petDtps']:.1f} | {s['petDeaths']:.2f} | {s['petTargetShare']:.0%} |" for n, s in pets]
        lines.append('')
    (output / 'champions.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return summary


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--engine', type=Path, default=Path('F:/UE_5.8'))
    p.add_argument('--waves', default='3,10,20')
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--enemies', type=int, default=8)
    p.add_argument('--seconds', type=float, default=120)
    p.add_argument('--timeout', type=float, default=420)
    p.add_argument('--parallel', type=int, default=3)
    p.add_argument('--only', default='', help='comma list of champions to run (default: all)')
    p.add_argument('--label', default='')
    p.add_argument('--summarize', type=Path)
    a = p.parse_args()
    if a.summarize:
        rows = json.loads((a.summarize / 'champions.json').read_text(encoding='utf-8'))['rows']
        write(a.summarize, rows); print((a.summarize / 'champions.md').read_text(encoding='utf-8')); return
    waves = [int(v) for v in a.waves.split(',')]
    pending = cases_for(waves, a.repeats)
    if a.only:
        keep = set(a.only.split(',')); pending = [c for c in pending if c['champion'] in keep]
    output = ROOT / 'Saved/BalanceLab' / ('champions-' + (a.label + '-' if a.label else '') + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])
    output.mkdir(parents=True)
    print(f'CHAMPION_LAB_START cases={len(pending)} output={output}', flush=True)
    running, rows, failures = [], [], []
    while pending or running:
        while pending and len(running) < a.parallel:
            busy = {key(r['case']) for r in running}
            index = next((i for i, c in enumerate(pending) if key(c) not in busy), None)
            if index is None: break
            running.append(launch(a.engine, output, pending.pop(index), a.enemies, a.seconds))
        time.sleep(1)
        for run in list(running):
            now = time.monotonic(); found = report_for(run['log'])
            if found and run['completed'] is None: run['completed'] = now
            done = run['child'].poll() is not None
            if not done and run['completed'] is not None and now - run['completed'] > 10: stop_owned(run['child']); done = True
            if not done and now - run['start'] > a.timeout: stop_owned(run['child']); done = True
            if not done: continue
            running.remove(run); found = report_for(run['log']); case = run['case']
            if not found:
                failures.append(dict(case=case, log=str(run['log']))); print('CHAMPION_LAB_FAIL ' + json.dumps(case), flush=True); continue
            new = row_from(case, *found); rows += new
            me = next((r for r in new if r['labChampion']), None)
            print('CHAMPION_LAB_CASE ' + json.dumps(dict(case, result=found[1]['result'], seconds=round(found[1]['seconds'], 1),
                  dps=round(me['dps'], 1) if me else None, hps=round(me['hps'], 1) if me else None)), flush=True)
            write(output, rows)
    if rows: write(output, rows)
    (output / 'failures.json').write_text(json.dumps(failures, indent=2) + '\n', encoding='utf-8')
    print(f'CHAMPION_LAB_REPORT {output} cases={len(rows) // 5} failures={len(failures)}', flush=True)


if __name__ == '__main__':
    main()
