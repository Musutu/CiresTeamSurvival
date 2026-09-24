"""Run bounded measured Unreal balance cases; only launches when invoked explicitly."""
import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import time
import uuid
from ImportCombatTuning import read_bundle
from SimulateBalance import simulate

ROOT=Path(__file__).resolve().parents[1]

def find_reports(reports, before, wave):
    matches=[]
    for file in reports.glob('runtime-*.json'):
        if file in before: continue
        try: data=json.loads(file.read_text(encoding='utf-8-sig'))
        except (OSError,ValueError): continue  # The game may still be writing it.
        if data.get('kind')=='measured-runtime' and f'wave {wave} |' in data.get('scenario','') and data.get('result'):
            matches.append((file,data))
    return matches

def stop_owned(child):
    child.terminate()
    try: child.wait(timeout=10)
    except subprocess.TimeoutExpired: child.kill();child.wait(timeout=10)

def run(engine, waves, duration, timeout):
    reports=ROOT/'Saved/BalanceLab';reports.mkdir(parents=True,exist_ok=True)
    output=reports/('measured-'+time.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6]);output.mkdir()
    tuning=read_bundle(ROOT/'Content/Data/CombatTuning.json');rows=[]
    for wave in waves:
        before=set(reports.glob('runtime-*.json'))
        log=output/f'wave-{wave}.log';stdout=output/f'wave-{wave}-stdout.log'
        command=[str(engine/'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'),str(ROOT/'CiresTeamSurvival.uproject'),'/Game/Maps/Citadel','-game','-nullrhi','-nosound','-unattended','-nop4','-NoSplash','-NoLogWindow','-CireBalanceLab','-CireBalanceExit',f'-CireBalanceWave={wave}','-CireBalanceBots=5','-CireBalanceEnemies=5',f'-CireBalanceSeconds={duration}',f'-abslog={log}','-stdout','-FullStdOutLogOutput']
        print(f'MEASURED_START wave={wave} log={log}',flush=True)
        start=time.monotonic()
        with stdout.open('wb') as stream:
            child=subprocess.Popen(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            completed_at=None;shutdown='natural_exit'
            try:
                while child.poll() is None:
                    now=time.monotonic();matching=find_reports(reports,before,wave)
                    if len(matching)==1 and completed_at is None: completed_at=now
                    if completed_at is not None and now-completed_at>=10:
                        # Older game binaries may not yet implement CireBalanceExit.
                        # A complete report is a completed measurement, not an exit-code pass.
                        stop_owned(child);shutdown='terminated_after_complete_report';break
                    if now-start>=timeout:
                        stop_owned(child)
                        raise RuntimeError(f'Wave {wave} exceeded {timeout}s wall time; stopped owned PID {child.pid}; see {stdout}')
                    time.sleep(.5)
            finally:
                if child.poll() is None: stop_owned(child)
            code=child.returncode
        matching=find_reports(reports,before,wave)
        if len(matching)!=1 or (code and shutdown=='natural_exit'): raise RuntimeError(f'Wave {wave}: exit={code}, matching reports={len(matching)}; see {stdout}')
        file,actual=matching[0]
        forecast=simulate(tuning,wave=wave,level=wave,seconds=duration)
        row={'wave':wave,'measuredResult':actual['result'],'measuredSeconds':actual['seconds'],'measuredDps':actual['dps'],'measuredHps':actual['hps'],'measuredTankShare':actual['tankTargetShare'],'measuredVictimSwitches':actual['victimSwitches'],'measuredAlliesAlive':actual['alliesAlive'],'forecastResult':forecast['outcome'],'forecastSeconds':forecast['seconds'],'forecastDps':forecast['dps'],'forecastHps':forecast['hps'],'forecastTankShare':forecast['tankTargetShare'],'runtimeReport':str(file),'log':str(log),'wallSeconds':time.monotonic()-start}
        row.update(processExitCode=code,processShutdown=shutdown)
        rows.append(row);print('MEASURED_COMPLETE '+json.dumps(row),flush=True)
        (output/'comparison.json').write_text(json.dumps({'schemaVersion':1,'kind':'measured-versus-modeled','notes':'Measured values use full native fixture loadouts, real AI/navigation/collision and random hit/crit rolls. Forecast is the documented simpler expected-value subset; discrepancies identify missing model behavior, not engine regressions by themselves.','cases':rows},indent=2)+'\n',encoding='utf-8')
    with (output/'comparison.csv').open('w',newline='',encoding='utf-8') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    lines=['# Measured fights and modeled forecasts','','One actual headless fight per wave; results include random combat outcomes. No balance settings changed.','',
           '| Wave | Measured result | Seconds | DPS | HPS | Tank share | Forecast seconds | Forecast DPS |','|---:|---|---:|---:|---:|---:|---:|---:|']
    for r in rows: lines.append(f"| {r['wave']} | {r['measuredResult']} | {r['measuredSeconds']:.2f} | {r['measuredDps']:.1f} | {r['measuredHps']:.1f} | {r['measuredTankShare']:.0%} | {r['forecastSeconds']:.2f} | {r['forecastDps']:.1f} |")
    lines+=['','Measured loadouts include the real tank toolkit, area skills, positioning, summons and live crit/miss rolls. The forecast omits several of those behaviors. This comparison is diagnostic, not proof of final balance.','', 'Process shutdown is recorded separately in JSON/CSV. A completed report with runner termination is a measured result, not a successful natural-exit assertion.','', 'Raw reports:']
    lines += ['- '+r['runtimeReport'] for r in rows]
    (output/'comparison.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    return output

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--engine',type=Path,default=Path('F:/UE_5.8'));p.add_argument('--waves',default='1,10,30');p.add_argument('--seconds',type=float,default=90);p.add_argument('--timeout',type=float,default=240)
    a=p.parse_args()
    try:
        waves=[int(v) for v in a.waves.split(',')]
        if not waves or len(waves)>12 or len(waves)!=len(set(waves)) or any(not 1<=w<=1000 for w in waves): raise ValueError('Use 1..12 unique waves in 1..1000')
        if not 5<=a.seconds<=300 or not a.seconds+30<=a.timeout<=900: raise ValueError('Invalid game duration or wall-clock timeout')
        print('MEASURED_REPORT '+str(run(a.engine,waves,a.seconds,a.timeout)))
    except (RuntimeError,ValueError,OSError) as error:p.exit(1,str(error)+'\n')

if __name__=='__main__': main()
