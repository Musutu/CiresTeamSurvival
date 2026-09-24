"""Validate and merge Astra combat tuning. Uses only Python's standard library."""
import argparse
import copy
import datetime
import json
import math
import os
from pathlib import Path
import re
import shutil
import tempfile

SCHEMA = json.loads(Path(__file__).with_name('CombatTuningSchema.json').read_text(encoding='utf-8'))
POLICIES = {'ignore', 'stop', 'pierce', 'reflect'}
ARRAYS = {'skillshots': ('skillshot', 64), 'constructs': ('construct', 32), 'summons': ('summon', 32), 'roleSkills': ('roleSkill', 32)}
ROLE_IDS = {'second_wind', 'last_stand', 'challenge_of_iron', 'seismic_reprisal', 'starfall', 'spectral_hunt', 'mass_aegis', 'wellspring'}

def checked_object(value, allowed, label):
    if not isinstance(value, dict) or set(value) - set(allowed):
        raise ValueError(f'Invalid or unknown {label} field')

def checked_numbers(value, bounds):
    for key, number in value.items():
        if key not in bounds:
            continue
        low, high = bounds[key]
        if type(number) not in (int, float) or not math.isfinite(number) or not low <= number <= high:
            raise ValueError(f'Out of range {key}')

def validate_spec(kind, value):
    rules = SCHEMA[kind]
    checked_object(value, rules['defaults'], kind)
    result = {**copy.deepcopy(rules['defaults']), **copy.deepcopy(value)}
    checked_numbers(result, rules['bounds'])
    for key, default in rules['defaults'].items():
        item = result[key]
        if type(default) is bool and type(item) is not bool:
            raise ValueError(f'{key} must be boolean')
        if key in ('hitLimit', 'reflectionLimit', 'count', 'archetypeVisual') and item != int(item):
            raise ValueError(f'{key} must be a whole number')
        if key.endswith('Collision') or key == 'protectionResponse':
            if item not in POLICIES:
                raise ValueError(f'Invalid {key}')
        if key == 'color':
            if not isinstance(item, list) or len(item) != 4 or any(type(v) not in (int, float) or not math.isfinite(v) or not (0.03 if i == 3 else 0) <= v <= (1 if i == 3 else 8) for i, v in enumerate(item)):
                raise ValueError('Invalid linear RGBA color')
    if kind == 'skillshot':
        if not isinstance(result['abilityName'], str) or len(result['abilityName']) > 80 or not isinstance(result['visualStyle'], str) or not 1 <= len(result['visualStyle']) <= 32:
            raise ValueError('Invalid skillshot name or appearance')
    if kind == 'construct' and result['kind'] not in ('wall', 'protection'):
        raise ValueError('Invalid construct kind')
    return result

def validate_bundle(value):
    checked_object(value, ('schemaVersion', 'engine', 'engineVersion', 'profile', 'units', 'globals', *ARRAYS), 'combat bundle')
    if type(value.get('schemaVersion')) is not int or value['schemaVersion'] != 1 or value.get('engine') != 'Unreal' or value.get('engineVersion') != '5.8.3' or value.get('units') != 'centimeters' or value.get('profile') not in ('CireCombatTuning', 'CireCombatTuningPatch'):
        raise ValueError('Expected Cire combat tuning schema 1 for Unreal 5.8.3 in centimeters')
    result = copy.deepcopy(value)
    globals_ = value.get('globals', {})
    checked_object(globals_, SCHEMA['globals']['defaults'], 'global tuning')
    checked_numbers(globals_, SCHEMA['globals']['bounds'])
    if value['profile'] == 'CireCombatTuning' and set(globals_) != set(SCHEMA['globals']['defaults']):
        raise ValueError('Full tuning requires every global field')
    result['globals'] = globals_
    for key, (kind, limit) in ARRAYS.items():
        entries = value.get(key, [] if key in ('summons', 'roleSkills') else None)
        if not isinstance(entries, list) or len(entries) > limit:
            raise ValueError(f'Missing or excessive {key}')
        seen = set()
        result[key] = []
        for entry in entries:
            if not isinstance(entry, dict):
                raise ValueError('Recipe must be an object')
            identifier = entry.get('id')
            if not isinstance(identifier, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', identifier) or identifier in seen:
                raise ValueError('Recipe IDs must be unique simple identifiers')
            seen.add(identifier)
            spec = validate_spec(kind, {k: v for k, v in entry.items() if k != 'id'})
            if kind == 'roleSkill':
                if identifier not in ROLE_IDS:
                    raise ValueError('Unknown native role skill')
                if identifier in ('seismic_reprisal', 'starfall') and (spec['radius'] < 20 or spec['warningSeconds'] < .2):
                    raise ValueError('Area ultimates require radius >=20 and warning >=0.2 seconds')
                if identifier == 'spectral_hunt' and (spec['durationSeconds'] < .1 or spec['castRange'] < 50):
                    raise ValueError('Spectral Hunt requires a positive lifetime and cast range')
            result[key].append({'id': identifier, **spec})
    return result

def read_bundle(path):
    if path.stat().st_size > 256 * 1024:
        raise ValueError('Combat tuning exceeds 256 KB')
    return validate_bundle(json.loads(path.read_text(encoding='utf-8-sig')))

def merge(current, incoming):
    incoming = validate_bundle(incoming)
    if current is None:
        if incoming['profile'] != 'CireCombatTuning':
            raise ValueError('Import a complete tuning file before applying patches')
        return incoming
    current = validate_bundle(current)
    if current['profile'] != 'CireCombatTuning':
        raise ValueError('Target must be a complete tuning file')
    result = copy.deepcopy(current)
    result['globals'].update(incoming['globals'])
    for key in ARRAYS:
        by_id = {entry['id']: entry for entry in result[key]}
        by_id.update({entry['id']: entry for entry in incoming[key]})
        result[key] = list(by_id.values())
    return validate_bundle(result)

def import_file(source, target, check=False):
    source, target = Path(source).resolve(), Path(target).resolve()
    if source == target:
        raise ValueError('Source and target must differ')
    result = merge(read_bundle(target) if target.exists() else None, read_bundle(source))
    encoded = (json.dumps(result, indent=2, ensure_ascii=False) + '\n').encode('utf-8')
    if len(encoded) > 256 * 1024:
        raise ValueError('Merged combat tuning exceeds 256 KB')
    backup = None
    if not check:
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
            backup = target.with_name(target.name + '.' + stamp + '.bak')
            shutil.copy2(target, backup)
        descriptor, temporary = tempfile.mkstemp(prefix=target.name + '.', suffix='.tmp', dir=target.parent)
        try:
            with os.fdopen(descriptor, 'wb') as file:
                file.write(encoded)
                file.flush()
                os.fsync(file.fileno())
            os.replace(temporary, target)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
    return {'validated': True, 'written': not check, 'target': str(target), 'backup': str(backup) if backup else None, **{key: len(result[key]) for key in ARRAYS}}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--target', type=Path, default=Path(__file__).resolve().parents[1] / 'Content/Data/CombatTuning.json')
    parser.add_argument('--check', action='store_true', help='Validate the complete merged result without writing or backing up')
    args = parser.parse_args()
    try:
        print(json.dumps(import_file(args.source, args.target, args.check)))
    except (ValueError, OSError, TypeError) as error:
        parser.exit(1, f'Combat tuning import rejected: {error}\n')

if __name__ == '__main__':
    main()
