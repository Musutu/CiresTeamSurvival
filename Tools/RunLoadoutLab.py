"""Measure full native skill-set variants; starts Unreal only when explicitly run."""
import argparse
import csv
import json
from pathlib import Path
import statistics
import subprocess
import time
import uuid
from RunBalanceLab import ROOT, find_reports, stop_owned

PRESETS=('thematic','tank_last_stand','tank_challenge','tank_seismic','support_aegis','support_wellspring','dps_starfall','dps_hunt')

def summarize(cases,dps_ratio=1.25,ttk_ratio=.8):
    groups={}
    for case in cases:groups.setdefault((case['wave'],case.get('enemies',5),case['preset']),[]).append(case)
    rows=[]
    for (wave,enemies,preset),group in sorted(groups.items()):
        wins=[c for c in group if c['result']=='allies_won']
        row={'wave':wave,'enemies':enemies,'preset':preset,'runs':len(group),'wins':len(wins),
             'medianDps':statistics.median(c['dps'] for c in group),
             'medianHps':statistics.median(c['hps'] for c in group),
             'medianTankShare':statistics.median(c['tankTargetShare'] for c in group),
             'medianVictorySeconds':statistics.median(c['seconds'] for c in wins) if wins else None,
             'minimumAlliesAlive':min(c['alliesAlive'] for c in group),'reviewFlags':[]}
        base=groups.get((wave,enemies,'thematic'),[])
        if base and preset!='thematic':
            base_dps=statistics.median(c['dps'] for c in base)
            base_wins=[c for c in base if c['result']=='allies_won']
            row['dpsVsThematic']=row['medianDps']/base_dps if base_dps>0 else None
            row['victoryTimeVsThematic']=row['medianVictorySeconds']/statistics.median(c['seconds'] for c in base_wins) if wins and base_wins else None
            if row['dpsVsThematic'] is not None and row['dpsVsThematic']>dps_ratio:row['reviewFlags'].append('dps_above_comparison_threshold')
            if row['victoryTimeVsThematic'] is not None and row['victoryTimeVsThematic']<ttk_ratio:row['reviewFlags'].append('victory_time_below_comparison_threshold')
            if len(wins)/len(group)<len(base_wins)/len(base):row['reviewFlags'].append('lower_observed_win_rate')
        if len(group)<3:row['reviewFlags'].append('fewer_than_three_repeats')
        if any(c['result']=='timeout' for c in group):row['reviewFlags'].append('encounter_timeout')
        rows.append(row)
    return rows

def run_case(engine,output,wave,preset,repeat,duration,timeout,enemies=5):
    reports=ROOT/'Saved/BalanceLab';before=set(reports.glob('runtime-*.json'))
    stem=f'wave-{wave}-{preset}-{repeat}'
    log=output/(stem+'.log');stdout=output/(stem+'-stdout.log')
    command=[str(engine/'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'),str(ROOT/'CiresTeamSurvival.uproject'),
             '/Game/Maps/Citadel','-game','-nullrhi','-nosound','-unattended','-nop4','-NoSplash','-NoLogWindow',
             '-CireBalanceLab','-CireBalanceExit',f'-CireBalanceWave={wave}',f'-CireBalanceLoadout={preset}',
             '-CireBalanceBots=5',f'-CireBalanceEnemies={enemies}',f'-CireBalanceSeconds={duration}',f'-abslog={log}',
             '-stdout','-FullStdOutLogOutput']
    def matching():return [(p,d) for p,d in find_reports(reports,before,wave) if d.get('loadoutPreset')==preset]
    print(f'LOADOUT_START wave={wave} preset={preset} repeat={repeat} log={log}',flush=True)
    start=time.monotonic();completed=None;shutdown='natural_exit'
    with stdout.open('wb') as stream:
        child=subprocess.Popen(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,
                               creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:
            while child.poll() is None:
                now=time.monotonic()
                if len(matching())==1 and completed is None:completed=now
                if completed is not None and now-completed>=10:
                    stop_owned(child);shutdown='terminated_after_complete_report';break
                if now-start>=timeout:
                    stop_owned(child);raise RuntimeError(f'{stem} exceeded wall timeout; owned PID {child.pid} stopped; {log}')
                time.sleep(.5)
        finally:
            if child.poll() is None:stop_owned(child)
    found=matching()
    if len(found)!=1 or (child.returncode and shutdown=='natural_exit'):
        raise RuntimeError(f'{stem}: exit={child.returncode}, reports={len(found)}; {stdout}')
    path,data=found[0]
    if data['result'] not in ('allies_won','allies_lost','timeout'):raise RuntimeError(f'{stem}: incomplete result {data["result"]}')
    row={k:data[k] for k in ('result','seconds','dps','hps','tankTargetShare','threatLeadRatio','victimSwitches','alliesAlive')}
    row.update(wave=wave,enemies=enemies,preset=preset,repeat=repeat,report=str(path),log=str(log),
               processExitCode=child.returncode,processShutdown=shutdown,wallSeconds=time.monotonic()-start)
    print('LOADOUT_COMPLETE '+json.dumps(row),flush=True);return row

def write_report(output,cases,dps_ratio,ttk_ratio):
    rows=summarize(cases,dps_ratio,ttk_ratio)
    document={'schemaVersion':1,'kind':'measured-loadout-comparison','thresholds':{'dpsVsThematic':dps_ratio,'victoryTimeVsThematic':ttk_ratio},
              'notes':'Actual authoritative 5-player AI encounters against the reported number of mixed NPCs. Comparisons use the same wave, enemy count, stats and team composition; variants change only the named role ultimate. Live random combat and AI are not deterministic or paired. Thresholds flag investigation, not proof an ability is overpowered. Full skill sets are development fixtures available at the tested level, not normal unlock pacing. No model forecast is substituted for a measurement and no balance defaults are changed.',
              'summaries':rows,'cases':cases}
    (output/'loadouts.json').write_text(json.dumps(document,indent=2)+'\n',encoding='utf-8')
    with (output/'loadouts.csv').open('w',newline='',encoding='utf-8') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(cases[0]));writer.writeheader();writer.writerows(cases)
    lines=['# Measured role skill sets','',document['notes'],'',
           f'Review gates: median DPS > {dps_ratio:.2f}× thematic, or median victory time < {ttk_ratio:.2f}× thematic. Fewer than three repeats is flagged.',
           '', '| Wave | Enemies | Preset | Wins/runs | Victory seconds | DPS | HPS | Tank share | Review flags |',
           '|---:|---:|---|---:|---:|---:|---:|---:|---|']
    for r in rows:
        seconds='—' if r['medianVictorySeconds'] is None else f'{r["medianVictorySeconds"]:.2f}'
        lines.append(f'| {r["wave"]} | {r["enemies"]} | {r["preset"]} | {r["wins"]}/{r["runs"]} | {seconds} | {r["medianDps"]:.1f} | {r["medianHps"]:.1f} | {r["medianTankShare"]:.0%} | {", ".join(r["reviewFlags"]) or "none"} |')
    lines+=['','Raw reports include exact skill IDs, initial stats/range, per-hero DPS/HPS/damage share, effective totals, and the tuning snapshot.','']
    lines += [f'- Wave {c["wave"]}, {c["preset"]}, repeat {c["repeat"]}: {c["report"]}' for c in cases]
    (output/'loadouts.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--engine',type=Path,default=Path('F:/UE_5.8'))
    p.add_argument('--waves',default='10');p.add_argument('--presets',default=','.join(PRESETS));p.add_argument('--repeats',type=int,default=3)
    p.add_argument('--seconds',type=float,default=90);p.add_argument('--timeout',type=float,default=240)
    p.add_argument('--enemies',type=int,default=5)
    p.add_argument('--dps-ratio',type=float,default=1.25);p.add_argument('--ttk-ratio',type=float,default=.8)
    a=p.parse_args()
    try:
        waves=[int(v) for v in a.waves.split(',')];presets=a.presets.split(',')
        if not 1<=len(waves)<=4 or len(set(waves))!=len(waves) or any(not 1<=w<=1000 for w in waves):raise ValueError('Use 1..4 unique waves, each 1..1000')
        if len(set(presets))!=len(presets) or 'thematic' not in presets or any(v not in PRESETS for v in presets):raise ValueError('Include thematic and known unique presets')
        if not 1<=a.repeats<=10 or not 5<=a.seconds<=300 or not a.seconds+30<=a.timeout<=900:raise ValueError('Invalid repetitions or time bounds')
        if not 1<=a.enemies<=20:raise ValueError('Use 1..20 enemies')
        if not 1<a.dps_ratio<=10 or not 0<a.ttk_ratio<1:raise ValueError('Invalid comparison thresholds')
        output=ROOT/'Saved/BalanceLab'/('loadouts-'+time.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6]);output.mkdir(parents=True)
        cases=[]
        for wave in waves:
            for repeat in range(1,a.repeats+1):
                for preset in presets:
                    cases.append(run_case(a.engine,output,wave,preset,repeat,a.seconds,a.timeout,a.enemies))
                    write_report(output,cases,a.dps_ratio,a.ttk_ratio)
        print('LOADOUT_REPORT '+str(output),flush=True)
    except (ValueError,OSError,RuntimeError) as error:p.exit(1,str(error)+'\n')

if __name__=='__main__':main()
