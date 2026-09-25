"""Role-tag consistency: native SkillRoleTags table vs Astra data vs roster examples.

The native rules table (Source/CiresTeamSurvival/Rules/CiresRules.cpp) is the
authority used by the server's offer generator. This test keeps the data mirrors
and the roster's thematic examples consistent with it.
"""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
RULES = (ROOT / 'Source/CiresTeamSurvival/Rules/CiresRules.cpp').read_text(encoding='utf-8')
ROSTER = json.loads((ROOT / 'Content/Data/ChampionRoster.json').read_text(encoding='utf-8'))
ASTRA = json.loads((ROOT / 'Content/Data/AstraAbilities.json').read_text(encoding='utf-8-sig'))
BITS = {'RoleTank': 1, 'RoleDamage': 2, 'RoleSupport': 4, 'RoleAll': 7}
NAMES = {'tank': 1, 'damage': 2, 'healer': 4, 'support': 4}
TABLE = {i: sum(BITS[b.strip()] for b in expr.split('|'))
         for i, expr in re.findall(r'\{"([a-z_]+)",\s*(Role[A-Za-z]+(?:\s*\|\s*Role[A-Za-z]+)*)\}', RULES)}
CATALOG = {i: k for i, k in re.findall(r'\{"([a-z_]+)",\s*"[^"]+",\s*SkillKind::(Active|Passive|Ultimate)\}', RULES)}
PRIMARY = {'tank': 1, 'damage': 2, 'healer': 4}


def profile_mask(c):
    mask = PRIMARY[c['threatRole']]
    for r in c['roles']:
        mask |= NAMES[r]
    return mask


class SkillRoleTests(unittest.TestCase):
    def test_every_catalog_skill_is_tagged(self):
        self.assertEqual(set(TABLE), set(CATALOG))
        for skill, mask in TABLE.items():
            self.assertTrue(1 <= mask <= 7, skill)
        for skill, kind in CATALOG.items():
            if kind == 'Passive' and skill != 'executioner':
                self.assertEqual(TABLE[skill], 7, skill)

    def test_astra_role_mirror_matches_native_table(self):
        for ability in ASTRA['abilities']:
            self.assertIn('roles', ability, ability['id'])
            mask = sum({'tank': 1, 'damage': 2, 'support': 4}[r] for r in ability['roles'])
            self.assertEqual(mask, TABLE[ability['id']], ability['id'])

    def test_each_role_has_deep_pools(self):
        for bit in (1, 2, 4):
            allowed = [s for s, m in TABLE.items() if m & bit]
            counts = {k: sum(CATALOG[s] == k for s in allowed) for k in ('Active', 'Passive', 'Ultimate')}
            self.assertGreaterEqual(counts['Active'], 9, bit)
            self.assertGreaterEqual(counts['Passive'], 4, bit)
            self.assertGreaterEqual(counts['Ultimate'], 4, bit)

    def test_implemented_roster_examples_are_offerable_to_their_champion(self):
        for c in ROSTER['champions']:
            mask = profile_mask(c)
            for s in c['actives'] + [c['passive'], c['ultimate']]:
                if s['status'] == 'implemented':
                    self.assertTrue(TABLE[s['id']] & mask, f"{c['id']} example {s['id']} outside its roles")

    def test_every_profile_has_explicit_role_and_presentation(self):
        buckets = {1: 0, 2: 0, 4: 0}
        hybrids = 0
        for c in ROSTER['champions']:
            self.assertIn(c['threatRole'], PRIMARY)
            buckets[PRIMARY[c['threatRole']]] += 1
            if bin(profile_mask(c)).count('1') > 1:
                hybrids += 1
            self.assertIn(c.get('difficulty'), (1, 2, 3), c['id'])
            self.assertTrue(0 < len(c.get('lore', '')) <= 200, c['id'])
            self.assertTrue(0 < len(c.get('classType', '')) <= 48, c['id'])
        self.assertTrue(all(n >= 5 for n in buckets.values()), buckets)
        self.assertGreaterEqual(hybrids, 3)
        self.assertEqual(profile_mask(next(c for c in ROSTER['champions'] if c['id'] == 'wizard')), 2 | 4)


if __name__ == '__main__':
    unittest.main()
