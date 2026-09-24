import copy
import json
from pathlib import Path
import tempfile
import unittest
from ImportCombatTuning import import_file, merge, validate_bundle

FIXTURE = Path(__file__).resolve().parents[1] / 'Content/Data/CombatTuning.json'

class ImportTests(unittest.TestCase):
    def setUp(self):
        self.original = json.loads(FIXTURE.read_text(encoding='utf-8'))
        self.patch = {**{k: self.original[k] for k in ('schemaVersion', 'engine', 'engineVersion', 'units')}, 'profile': 'CireCombatTuningPatch', 'globals': {'critChance': .08}, 'skillshots': [{**self.original['skillshots'][0], 'speed': 1999}], 'constructs': [], 'summons': []}

    def test_merge_preserves_other_recipes_and_globals(self):
        before = copy.deepcopy(self.original)
        result = merge(self.original, self.patch)
        self.assertEqual(self.original, before)
        self.assertEqual(result['globals']['critChance'], .08)
        self.assertEqual(result['globals']['tankDamageThreatMultiplier'], 5)
        self.assertEqual(result['skillshots'][0]['speed'], 1999)
        self.assertEqual(result['skillshots'][1:], self.original['skillshots'][1:])
        self.assertEqual(result['constructs'], self.original['constructs'])
        self.assertEqual(result['summons'], self.original['summons'])
        self.assertEqual(result['roleSkills'], self.original['roleSkills'])

    def test_role_recipe_patch_is_bounded_and_preserves_other_roles(self):
        self.patch['roleSkills']=[{**self.original['roleSkills'][0], 'maxHealthFraction': .22}]
        result=merge(self.original,self.patch)
        self.assertEqual(result['roleSkills'][0]['maxHealthFraction'],.22)
        self.assertEqual(result['roleSkills'][1:],self.original['roleSkills'][1:])
        for field,value in [('maxHealthFraction',1.1),('flatPower',float('nan')),('cooldownSeconds',0),('durationSeconds',31),('id','unimplemented_role_spell')]:
            invalid=copy.deepcopy(self.original);invalid['roleSkills'][0][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):validate_bundle(invalid)
        for identifier,field,value in [('starfall','warningSeconds',0),('seismic_reprisal','radius',0),('spectral_hunt','durationSeconds',0)]:
            invalid=copy.deepcopy(self.original)
            next(s for s in invalid['roleSkills'] if s['id']==identifier)[field]=value
            with self.subTest(identifier=identifier),self.assertRaises(ValueError):validate_bundle(invalid)
        invalid=copy.deepcopy(self.original);invalid['roleSkills'].append(invalid['roleSkills'][0])
        with self.assertRaises(ValueError):validate_bundle(invalid)

    def test_legacy_bundle_without_role_recipes_is_compatible(self):
        legacy=copy.deepcopy(self.original);legacy.pop('roleSkills')
        self.assertEqual(validate_bundle(legacy)['roleSkills'],[])
        self.assertEqual(merge(self.original,legacy)['roleSkills'],self.original['roleSkills'])

    def test_invalid_update_does_not_write_or_backup(self):
        with tempfile.TemporaryDirectory() as tmp:
            target, source = Path(tmp) / 'target.json', Path(tmp) / 'patch.json'
            target.write_text(json.dumps(self.original), encoding='utf-8')
            before = target.read_bytes()
            self.patch['skillshots'][0]['speed'] = 0
            source.write_text(json.dumps(self.patch), encoding='utf-8')
            with self.assertRaises(ValueError):
                import_file(source, target)
            self.assertEqual(target.read_bytes(), before)
            self.assertEqual(list(Path(tmp).glob('*.bak')), [])

    def test_check_and_atomic_backup(self):
        with tempfile.TemporaryDirectory() as tmp:
            target, source = Path(tmp) / 'target.json', Path(tmp) / 'patch.json'
            target.write_text(json.dumps(self.original), encoding='utf-8')
            source.write_text(json.dumps(self.patch), encoding='utf-8')
            before = target.read_bytes()
            self.assertFalse(import_file(source, target, check=True)['written'])
            self.assertEqual(target.read_bytes(), before)
            result = import_file(source, target)
            self.assertEqual(Path(result['backup']).read_bytes(), before)
            self.assertEqual(json.loads(target.read_text())['skillshots'][0]['speed'], 1999)
            self.assertEqual(list(Path(tmp).glob('*.tmp')), [])

    def test_schema_rejects_unknown_policies_duplicate_ids_nonfinite_and_caps(self):
        for field, value in [('speed', float('nan')), ('worldCollision', 'explode'), ('hitLimit', 1.5), ('canCrit', 1)]:
            invalid = copy.deepcopy(self.original)
            invalid['skillshots'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_bundle(invalid)
        invalid = copy.deepcopy(self.original)
        invalid['summons'][0]['count'] = 4
        with self.assertRaises(ValueError): validate_bundle(invalid)
        invalid = copy.deepcopy(self.original)
        invalid['constructs'].append(invalid['constructs'][0])
        with self.assertRaises(ValueError): validate_bundle(invalid)
        invalid = copy.deepcopy(self.original)
        invalid['globals']['unknown'] = 1
        with self.assertRaises(ValueError): validate_bundle(invalid)

    def test_patch_requires_existing_full_tuning(self):
        with self.assertRaises(ValueError): merge(None, self.patch)
        self.assertEqual(merge(None, self.original), self.original)

if __name__ == '__main__': unittest.main()
