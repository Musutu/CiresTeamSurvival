"""Read measured loadout reports and compare champion contributions without inferring casts."""
import argparse
import json
from pathlib import Path
import statistics


def contributions(document, read_report):
    groups = {}
    for case in document['cases']:
        actual = read_report(case['report'])
        initial = {p['name']: p for p in actual['initialParticipants'] if 'skills' in p and p['team'] == 0}
        for final in actual['finalHeroes']:
            if final['team'] != 0:
                continue
            start = initial[final['name']]
            key = (case['wave'], case.get('enemies', 5), case['preset'], final['name'])
            groups.setdefault(key, []).append((start, final))
    rows = []
    for (wave, enemies, preset, name), members in sorted(groups.items()):
        first = members[0][0]
        if any(m[0]['skills'] != first['skills'] for m in members):
            raise ValueError(f'Loadout changed within comparison group: {preset}: {name}')
        finals = [m[1] for m in members]
        rows.append(dict(wave=wave, enemies=enemies, preset=preset, champion=name, role=first['draftRole'],
                         level=first['level'], basicRange=first['basicAttackRange'], skills=first['skills'],
                         runs=len(members), medianDamage=statistics.median(m['damage'] for m in finals),
                         medianDps=statistics.median(m['dps'] for m in finals),
                         medianHealing=statistics.median(m['healing'] for m in finals),
                         medianHps=statistics.median(m['hps'] for m in finals),
                         medianTeamDamageShare=statistics.median(m['teamDamageShare'] for m in finals),
                         zeroHealingRuns=sum(m['healing'] == 0 for m in finals)))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('comparison', type=Path, help='loadouts.json from RunLoadoutLab.py')
    args = parser.parse_args()
    document = json.loads(args.comparison.read_text(encoding='utf-8-sig'))
    if document.get('kind') != 'measured-loadout-comparison':
        parser.error('Expected a measured loadout comparison')
    rows = contributions(document, lambda p: json.loads(Path(p).read_text(encoding='utf-8-sig')))
    notes = ('Champion totals are measured effective combat-meter attribution, including owned summons credited by the game. '
             'Equipped skill IDs are recorded, but per-ability cast/hit counts are not present in these reports. '
             'A loadout difference or increased champion contribution does not prove that its changed ultimate fired, '
             'or that a specific ability caused the outcome. Zero healing can indicate low injury, range/AI/cooldown '
             'constraints, or protective prevention; it is not a healing-capacity measurement.')
    result = {'kind': 'measured-champion-contributions', 'notes': notes, 'rows': rows}
    directory = args.comparison.parent
    (directory / 'contributions.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    lines = ['# Measured champion contributions', '', notes, '',
             '| Wave | Enemies | Loadout | Champion | DPS | Team damage | Effective healing | Zero healing runs |',
             '|---:|---:|---|---|---:|---:|---:|---:|']
    for row in rows:
        name = row['champion'].split(' | ', 1)[-1]
        lines.append(f'| {row["wave"]} | {row["enemies"]} | {row["preset"]} | {name} | {row["medianDps"]:.1f} | '
                     f'{row["medianTeamDamageShare"]:.1%} | {row["medianHealing"]:.1f} | '
                     f'{row["zeroHealingRuns"]}/{row["runs"]} |')
    lines += ['', 'Values are medians within each same-wave, same-loadout champion group. '
              'The JSON records full equipped IDs, level and range for auditing.', '']
    (directory / 'contributions.md').write_text('\n'.join(lines), encoding='utf-8')
    print(directory / 'contributions.md')


if __name__ == '__main__':
    main()
