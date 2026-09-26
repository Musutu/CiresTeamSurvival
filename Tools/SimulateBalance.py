"""Deterministic expected-value balance forecast; never a measured engine result."""
import argparse
import copy
import csv
import hashlib
import heapq
import json
import math
import sys
from pathlib import Path
from ImportCombatTuning import read_bundle

ROOT = Path(__file__).resolve().parents[1]
ROLES = ['tank', 'healer', 'ranger', 'lancer', 'summoner']
KINDS = ['basic', 'bruiser', 'caster', 'ranged']
CONSTANTS = {'weaponDamage': 12, 'baseAttackInterval': 1.5, 'agilityAttackSpeedPerPoint': .01,
             'healthPerStrength': 10, 'legacyHealthPerStrength': 25, 'defensePerStrength': .1, 'manaPerIntelligence': 30, 'manaRegenFraction': .015,
             'energyRegen': 9, 'baseMissChance': .05, 'dodgeChance': .05,
             'healingBase': 90, 'healingPerIntelligence': 3, 'healingManaCost': 45, 'healingCooldown': 6,
             'enemyMeleeInterval': 1.8, 'enemyRangedInterval': 3.2,
             'bruiserSlamMultiplier': 2.5, 'bruiserSlamCooldown': 8,
             'poolDps': 12, 'poolDuration': 5, 'poolCooldown': 12}

def finite(value, lower, upper, name):
    if type(value) not in (float, int) or not math.isfinite(value) or not lower <= value <= upper:
        raise ValueError(f'{name} must be finite in [{lower}, {upper}]')
    return value

def enemy_stats(globals_, wave, kind):
    finite(wave, 1, 10000, 'wave')
    if int(wave) != wave: raise ValueError('Wave must be a whole number')
    if kind not in KINDS: raise ValueError('Unknown NPC archetype')
    health = (globals_['waveHealthBase'] + globals_['waveHealthPerWave'] * wave) * globals_[kind + 'HealthMultiplier']
    damage = globals_['normalMonsterDamage' if kind == 'basic' else kind + 'MonsterDamage']
    return health, damage

def hero_stats(level, role):
    finite(level, 1, 10000, 'hero level')
    if int(level) != level or role not in ROLES: raise ValueError('Invalid hero level/role')
    primary = 0 if role == 'tank' else 1 if role in ('ranger', 'lancer') else 2
    stats = [10 + (level - 1) * (2 if i == primary else 1) + (10 if i == primary else 0) for i in range(3)]
    # str-scaling: flat base (25 - 10) x starting STR keeps level-1 health; each STR adds 10 health and 0.1 armor/ward.
    start_strength = 20 if primary == 0 else 10
    base_health = start_strength * (CONSTANTS['legacyHealthPerStrength'] - CONSTANTS['healthPerStrength'])
    health, mana = base_health + stats[0] * CONSTANTS['healthPerStrength'], stats[2] * CONSTANTS['manaPerIntelligence']
    defense = stats[0] * CONSTANTS['defensePerStrength']
    return {'role': role, 'stats': stats, 'health': health, 'maxHealth': health, 'mana': mana, 'maxMana': mana,
            'energy': 100., 'mitigation': defense / (defense + 100), 'baseDamage': CONSTANTS['weaponDamage'] + stats[primary],
            'attackInterval': CONSTANTS['baseAttackInterval'] / (1 + stats[1] * CONSTANTS['agilityAttackSpeedPerPoint']),
            'basicNext': .25, 'spellNext': {}, 'gcd': 0., 'damage': 0., 'healing': 0., 'healingThreat': 0.}

def simulate(bundle, wave=1, level=1, enemy_count=5, seconds=180., projectile_contact=.75, area_contact=.5, step=.05):
    finite(seconds, 1, 600, 'seconds'); finite(projectile_contact, 0, 1, 'projectile contact'); finite(area_contact, 0, 1, 'area contact'); finite(step, .01, .2, 'step')
    if type(enemy_count) is not int or not 1 <= enemy_count <= 20: raise ValueError('enemy_count must be 1..20')
    g = bundle['globals']; shots = {v['id']: v for v in bundle['skillshots']}; summons = {v['id']: v for v in bundle.get('summons', [])}
    heroes = [hero_stats(level, role) for role in ROLES]
    enemies = []
    for i in range(enemy_count):
        kind = KINDS[i % 4]; hp, damage = enemy_stats(g, wave, kind)
        enemies.append({'kind': kind, 'health': hp, 'maxHealth': hp, 'damage': damage, 'threat': [1., 0., 0., 0., 0.], 'victim': 0, 'attackNext': 1., 'abilityNext': 4.})
    initial_health = sum(e['health'] for e in enemies)
    hit = (1 - CONSTANTS['baseMissChance']) * (1 - CONSTANTS['dodgeChance'])
    critical = 1 + g['critChance'] * (g['critMultiplier'] - 1)
    queue, serial, outgoing, effective_healing, healing_threat = [], 0, 0., 0., 0.
    damage_taken, switches, tank_ticks, target_ticks = 0., 0, 0, 0
    samples, next_sample = [], 0.
    active_summons = []

    def schedule(when, who, target, damage, enemy=False):
        nonlocal serial
        serial += 1; heapq.heappush(queue, (round(when, 9), serial, who, target, damage, enemy))

    def damage_enemy(source, target, raw):
        nonlocal outgoing
        if heroes[source]['health'] <= 0 or enemies[target]['health'] <= 0: return
        applied = min(enemies[target]['health'], max(0., raw)); enemies[target]['health'] -= applied
        heroes[source]['damage'] += applied; outgoing += applied
        enemies[target]['threat'][source] += applied * (g['tankDamageThreatMultiplier'] if source == 0 else g['dpsDamageThreatMultiplier'])

    def heal(source, target, raw):
        nonlocal effective_healing, healing_threat
        if heroes[target]['health'] <= 0: return
        applied = min(max(0., raw), heroes[target]['maxHealth'] - heroes[target]['health'])
        heroes[target]['health'] += applied; heroes[source]['healing'] += applied; effective_healing += applied
        engaged = [e for e in enemies if e['health'] > 0]
        if engaged:
            threat = applied * g['healingThreatMultiplier']; healing_threat += threat; heroes[source]['healingThreat'] += threat
            for e in engaged: e['threat'][source] += threat / len(engaged)

    now, outcome = 0., 'timeout'
    for tick in range(math.ceil(seconds / step) + 1):
        now = round(min(seconds, tick * step), 9)
        while queue and queue[0][0] <= now + 1e-8:
            _, _, who, target, raw, enemy = heapq.heappop(queue)
            if enemy:
                if enemies[who]['health'] > 0 and heroes[target]['health'] > 0:
                    applied = min(heroes[target]['health'], raw * (1 - heroes[target]['mitigation'])); heroes[target]['health'] -= applied; damage_taken += applied
            else: damage_enemy(who, target, raw)
        living_enemies = [i for i, e in enumerate(enemies) if e['health'] > 1e-8]
        living_heroes = [i for i, h in enumerate(heroes) if h['health'] > 1e-8]
        if not living_enemies: outcome = 'allies_won'; break
        if not living_heroes: outcome = 'allies_lost'; break
        target = living_enemies[0]
        for i in living_heroes:
            remaining = [j for j,e in enumerate(enemies) if e['health'] > 1e-8]
            if not remaining: break
            target = remaining[0]
            h = heroes[i]
            h['mana'] = min(h['maxMana'], h['mana'] + h['maxMana'] * CONSTANTS['manaRegenFraction'] * step)
            h['energy'] = min(100., h['energy'] + CONSTANTS['energyRegen'] * step)
            if now >= h['basicNext']:
                count=1+math.floor((now-h['basicNext'])/h['attackInterval'])
                damage_enemy(i, target, h['baseDamage'] * hit * critical * count); h['basicNext'] += h['attackInterval'] * count
            if not any(e['health'] > 1e-8 for e in enemies): break
            if now < h['gcd']: continue
            if i == 1 and now >= h['spellNext'].get('heal', 0):
                patient = min(living_heroes, key=lambda j: heroes[j]['health'] / heroes[j]['maxHealth'])
                if heroes[patient]['health'] < heroes[patient]['maxHealth'] * .8 and h['mana'] >= CONSTANTS['healingManaCost']:
                    h['mana'] -= CONSTANTS['healingManaCost']; h['spellNext']['heal'] = now + CONSTANTS['healingCooldown']; h['gcd'] = now + .9
                    heal(i, patient, CONSTANTS['healingBase'] + h['stats'][2] * CONSTANTS['healingPerIntelligence']); continue
            ability = 'piercing_shot' if i in (2, 3) else 'ember_lance' if i == 4 else None
            if ability and ability in shots:
                s = shots[ability]
                if now >= h['spellNext'].get(ability, 0) and h['mana'] >= s['manaCost'] and h['energy'] >= s['energyCost']:
                    h['mana'] -= s['manaCost']; h['energy'] -= s['energyCost']; h['spellNext'][ability] = now + max(.9, s['cooldownSeconds']); h['gcd'] = now + .9
                    schedule(now + s['warningSeconds'] + 500 / s['speed'], i, target, s['damage'] * projectile_contact * (critical if s['canCrit'] else 1))
            if i == 4 and now >= h['gcd']:
                for identifier in ('oathbound_guardian', 'spectral_pack'):
                    if identifier not in summons: continue
                    s = summons[identifier]
                    if now >= h['spellNext'].get(identifier, 0) and h['mana'] >= s['manaCost'] and h['energy'] >= s['energyCost']:
                        h['mana'] -= s['manaCost']; h['energy'] -= s['energyCost']; h['spellNext'][identifier] = now + max(.9, s['cooldownSeconds']); h['gcd'] = now + .9
                        # Companions are approximated as stationary damage sources;
                        # AI travel, individual health, enemy retargeting are omitted.
                        active_summons.append({'owner': i, 'expires': now + s['durationSeconds'], 'next': now + .5, 'damage': s['count'] * s['damage'] * hit * critical})
                        break
        for s in active_summons:
            if now < s['expires'] and now >= s['next'] and heroes[s['owner']]['health'] > 0:
                damage_enemy(s['owner'], target, s['damage']); s['next'] = now + 1.5 / 1.1
        active_summons = [s for s in active_summons if now < s['expires']]
        for i in living_enemies:
            e = enemies[i]
            if e['health'] <= 0: continue
            candidates = [j for j in living_heroes if heroes[j]['health'] > 0]
            if not candidates: break
            previous = e['victim']; best = max(candidates, key=lambda j: (e['threat'][j], j == previous, -j))
            if previous != best: switches += 1
            e['victim'] = best; target_ticks += 1; tank_ticks += best == 0
            if now >= e['abilityNext'] and e['kind'] in ('bruiser', 'caster'):
                if e['kind'] == 'bruiser':
                    schedule(now + 1.1, i, best, min(10000, e['damage'] * CONSTANTS['bruiserSlamMultiplier']) * area_contact, True)
                    e['abilityNext'] = now + CONSTANTS['bruiserSlamCooldown']; e['attackNext'] = max(e['attackNext'], now + 1.8)
                else:
                    for pulse in range(10): schedule(now + 1.2 + (pulse + 1) * .5, i, best, CONSTANTS['poolDps'] * .5 * area_contact, True)
                    e['abilityNext'] = now + CONSTANTS['poolCooldown']; e['attackNext'] = max(e['attackNext'], now + 1.9)
            elif now >= e['attackNext']:
                ranged = e['kind'] in ('caster', 'ranged')
                e['attackNext'] = now + (CONSTANTS['enemyRangedInterval'] if ranged else CONSTANTS['enemyMeleeInterval'])
                schedule(now + (1.4 if ranged else 0), i, best, e['damage'] * (projectile_contact if ranged else hit), True)
        if now >= next_sample:
            samples.append({'seconds': now, 'damage': outgoing, 'healing': effective_healing, 'enemyHealth': sum(max(0, e['health']) for e in enemies), 'allyHealth': sum(max(0, h['health']) for h in heroes)})
            next_sample = now + 1
        if now >= seconds: break
    duration = max(step, now)
    return {'kind': 'modeled-expected-value', 'wave': wave, 'heroLevel': level, 'enemies': enemy_count, 'outcome': outcome,
            'seconds': now, 'ttk': now if outcome == 'allies_won' else None, 'enemyInitialHealth': initial_health,
            'damage': outgoing, 'dps': outgoing / duration, 'healing': effective_healing, 'hps': effective_healing / duration,
            'incomingDamage': damage_taken, 'healingThreat': healing_threat, 'tankTargetShare': tank_ticks / max(1, target_ticks),
            'victimSwitches': switches, 'alliesAlive': sum(h['health'] > 0 for h in heroes),
            'enemyBaseDamageByType': {k: enemy_stats(g, wave, k)[1] for k in KINDS},
            'roles': [{'role': h['role'], 'health': h['health'], 'maxHealth': h['maxHealth'], 'damage': h['damage'], 'healing': h['healing'], 'healingThreat': h['healingThreat']} for h in heroes], 'samples': samples}

def review(rows, min_ttk=8., max_ttk=90., max_dps_growth=1.15, min_tank_share=.5):
    for v, lo, hi, name in [(min_ttk,0,600,'min ttk'),(max_ttk,1,600,'max ttk'),(max_dps_growth,1,10,'max dps growth'),(min_tank_share,0,1,'min tank share')]: finite(v,lo,hi,name)
    if min_ttk >= max_ttk: raise ValueError('min ttk must be below max ttk')
    flags=[]
    for row in rows:
        reasons=[]
        if row['outcome'] != 'allies_won': reasons.append(row['outcome'])
        if row['ttk'] is not None and not min_ttk <= row['ttk'] <= max_ttk: reasons.append('ttk_outside_review_band')
        if row['tankTargetShare'] < min_tank_share: reasons.append('tank_target_share_below_review_band')
        if reasons: flags.append({'wave':row['wave'],'reasons':reasons})
    for a,b in zip(rows,rows[1:]):
        delta=b['heroLevel']-a['heroLevel']
        if delta>0 and a['dps']>0 and (b['dps']/a['dps'])**(1/delta)>max_dps_growth:
            flags.append({'wave':b['wave'],'reasons':['dps_growth_per_level_above_review_band']})
        if a['enemyBaseDamageByType']!=b['enemyBaseDamageByType']:
            flags.append({'wave':b['wave'],'reasons':['npc_damage_scaled_with_wave']})
    return flags

def report(bundle, waves, fixed_level=None, **options):
    rows=[simulate(bundle,wave=w,level=fixed_level or min(w,10000),**options) for w in waves]
    files=['Content/Data/CombatTuning.json','Source/CiresTeamSurvival/CireHero.cpp','Source/CiresTeamSurvival/CireNPCCombat.cpp','Source/CiresTeamSurvival/Rules/CiresRules.cpp','Source/CiresTeamSurvival/Rules/CireAttackRules.h']
    return {'schemaVersion':1,'kind':'modeled-expected-value','engineVersion':'5.8.3',
            'assumptions':{'progression':'fixed level '+str(fixed_level) if fixed_level else 'hero level = wave, capped at native level 10000; an explicit scenario assumption, not the XP economy',
                           'geometry':'stationary same-floor focus fire, constant 500cm projectile travel, no navigation or target spreading',
                           'loadout':'five roles; basic attacks, ranger/lancer piercing shot, summoner ember/companions, healer restoring light; excludes tank skills, guard, taunts, AoE multi-hit, proc/gear/economy rewards',
                           'summons':'temporary damage sources; omits companion deaths and enemy retargeting onto companions',
                           'probability':'fractional expected hit/crit damage; deterministic forecasts are not measured TTK distributions; continuous attack cadence has no render-frame cap',
                           'parameters':options,'constantsFromNativeRules':CONSTANTS},
            'sourceHashes':{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files if (ROOT/f).exists()},'tuning':bundle,'rows':rows}

def write_report(data, output):
    output.mkdir(parents=True,exist_ok=True)
    (output/'forecast.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
    columns=['wave','heroLevel','outcome','ttk','dps','hps','enemyInitialHealth','tankTargetShare','victimSwitches','alliesAlive']
    with (output/'forecast.csv').open('w',newline='',encoding='utf-8') as file:
        writer=csv.DictWriter(file,fieldnames=columns,extrasaction='ignore');writer.writeheader();writer.writerows(data['rows'])
    lines=['# Modeled balance forecast','', '**Expected-value model; not an Unreal runtime measurement.**','',
           '| Wave | Hero level | Result | TTK s | DPS | HPS | Enemy HP | Tank target share |', '|---:|---:|---|---:|---:|---:|---:|---:|']
    for r in data['rows']: lines.append(f"| {r['wave']} | {r['heroLevel']} | {r['outcome']} | {r['ttk'] if r['ttk'] is not None else '—'} | {r['dps']:.1f} | {r['hps']:.1f} | {r['enemyInitialHealth']:.0f} | {r['tankTargetShare']:.0%} |")
    lines+=['','## Review flags','',json.dumps(data['reviewFlags'],indent=2),'','Thresholds are review prompts, not automatic balance changes. Their chosen values are in forecast.json.','', '## Model assumptions','']
    lines += [f'- {key}: {value}' for key,value in data['assumptions'].items()]
    (output/'forecast.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    try:
        cached_plotting=ROOT/'Saved/BalanceLab/.plotdeps'
        if cached_plotting.is_dir(): sys.path.insert(0,str(cached_plotting))
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        rows=data['rows'];waves=[r['wave'] for r in rows]
        fig,axes=plt.subplots(2,2,figsize=(11,7));fig.suptitle('Cire balance forecast — modeled, not runtime measured')
        for ax in axes.flat: ax.grid(alpha=.2);ax.set_xlabel('Wave')
        axes[0,0].plot(waves,[r['enemyInitialHealth'] for r in rows],marker='o',label='Enemy health');axes[0,0].plot(waves,[sum(h['maxHealth'] for h in r['roles']) for r in rows],marker='s',label='Team health');axes[0,0].legend();axes[0,0].set_ylabel('Health')
        axes[0,1].plot(waves,[r['dps'] for r in rows],marker='o',label='Effective DPS');axes[0,1].plot(waves,[r['hps'] for r in rows],marker='s',label='Effective HPS');axes[0,1].legend()
        axes[1,0].plot(waves,[r['ttk'] if r['ttk'] is not None else float('nan') for r in rows],marker='o');axes[1,0].axhspan(data['thresholds']['minTtk'],data['thresholds']['maxTtk'],alpha=.12,color='green');axes[1,0].set_ylabel('TTK seconds (gaps = no victory)')
        axes[1,1].plot(waves,[r['tankTargetShare']*100 for r in rows],marker='o');axes[1,1].set_ylim(0,105);axes[1,1].set_ylabel('Tank share of NPC target time, %')
        fig.tight_layout();fig.savefig(output/'forecast-curves.png',dpi=150);plt.close(fig)
    except ImportError:
        # A previous chart must not silently accompany a newer JSON/CSV report.
        (output/'forecast-curves.png').unlink(missing_ok=True)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tuning',type=Path,default=ROOT/'Content/Data/CombatTuning.json');p.add_argument('--out',type=Path,default=ROOT/'Saved/BalanceLab/forecast')
    p.add_argument('--waves',default='1,3,5,10,20,40,80,100,200,500,1000');p.add_argument('--fixed-level',type=int);p.add_argument('--enemies',type=int,default=5);p.add_argument('--seconds',type=float,default=180)
    p.add_argument('--projectile-contact',type=float,default=.75);p.add_argument('--area-contact',type=float,default=.5)
    p.add_argument('--min-ttk',type=float,default=8);p.add_argument('--max-ttk',type=float,default=90);p.add_argument('--max-dps-growth',type=float,default=1.15);p.add_argument('--min-tank-share',type=float,default=.5);p.add_argument('--fail-on-gate',action='store_true')
    args=p.parse_args()
    try:
        waves=[int(v) for v in args.waves.split(',')]
        if not waves or len(waves)>32 or waves!=sorted(set(waves)): raise ValueError('Use up to 32 unique ascending waves')
        if args.fixed_level is not None: finite(args.fixed_level,1,10000,'fixed level')
        data=report(read_bundle(args.tuning),waves,args.fixed_level,enemy_count=args.enemies,seconds=args.seconds,projectile_contact=args.projectile_contact,area_contact=args.area_contact)
        data['inputFile']={'path':str(args.tuning.resolve()),'sha256':hashlib.sha256(args.tuning.read_bytes()).hexdigest()}
        data['thresholds']={'minTtk':args.min_ttk,'maxTtk':args.max_ttk,'maxDpsGrowthPerLevel':args.max_dps_growth,'minTankTargetShare':args.min_tank_share}
        data['reviewFlags']=review(data['rows'],args.min_ttk,args.max_ttk,args.max_dps_growth,args.min_tank_share)
        write_report(data,args.out)
        print(json.dumps({'kind':data['kind'],'out':str(args.out.resolve()),'scenarios':len(waves),'reviewFlags':data['reviewFlags']}))
        return 2 if args.fail_on_gate and data['reviewFlags'] else 0
    except (ValueError,OSError,TypeError) as error: p.exit(1,f'Balance forecast rejected: {error}\n')

if __name__=='__main__': raise SystemExit(main())
