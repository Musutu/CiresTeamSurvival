"""Roster content checks; native ParseJson/RunValidationSmoke tests loading."""
import hashlib
import json
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]
DATA=json.loads((ROOT/'Content/Data/ChampionRoster.json').read_text(encoding='utf-8'))
PROFILES={c['id']:c for c in DATA['champions']}
RULES=(ROOT/'Source/CiresTeamSurvival/Rules/CiresRules.cpp').read_text(encoding='utf-8')
KNOWN={id:kind.lower() for id,kind in re.findall(r'\{"([a-z_]+)",\s*"[^"]+",\s*SkillKind::(Active|Passive|Ultimate)\}',RULES)}

class RosterTests(unittest.TestCase):
    def test_unique_twenty_two_profiles_and_empty_start(self):
        self.assertEqual(len(PROFILES),22)
        self.assertEqual(len(PROFILES),len(DATA['champions']))
        for c in PROFILES.values():
            self.assertEqual(c['startsWithSkills'],[])
            self.assertEqual(len(c['actives']),6)
            ids=[s['id'] for s in c['actives']]+[c['passive']['id'],c['ultimate']['id']]
            self.assertEqual(len(set(ids)),8)
    def test_implemented_claims_match_actual_native_pool_and_slot(self):
        for c in PROFILES.values():
            for kind,skills in [('active',c['actives']),('passive',[c['passive']]),('ultimate',[c['ultimate']])]:
                for s in skills:
                    self.assertIn(s['status'],('implemented','planned'))
                    if s['status']=='implemented':self.assertEqual(KNOWN.get(s['id']),kind,s['id'])
                    else:self.assertNotIn(s['id'],KNOWN,s['id'])
    def test_current_five_full_sets_are_implemented(self):
        for id in ('knight','ranger','scholar','lancer','summoner'):
            c=PROFILES[id]
            self.assertTrue(all(s['status']=='implemented' for s in c['actives']+[c['passive'],c['ultimate']]))
    def test_tank_examples_exclude_ally_heals_and_ranged_reach(self):
        for c in PROFILES.values():
            if c['threatRole']=='tank':
                self.assertFalse({'restoring_light','purify','sanctuary','renewal','wellspring'}&{s['id'] for s in c['actives']+[c['ultimate']]})
            if c['basicAttackRange']>300:
                self.assertEqual(c['basicAttackRange'],1500 if c['attackStyle'] in ('bow','axes') else 1300 if c['attackStyle']=='lance' else 1200)
            else:self.assertEqual(c['basicAttackRange'],220)
        self.assertIn('second_wind',[s['id'] for s in PROFILES['knight']['actives']])
    def test_primary_and_role_are_independent_of_prototype(self):
        for c in PROFILES.values():
            self.assertEqual(c[c['primaryStat']],20)
            self.assertEqual(sum(c[k] for k in ('strength','agility','intelligence')),40)
        self.assertEqual(PROFILES['ether_golem_bruiser']['threatRole'],'damage')
        self.assertEqual(PROFILES['troll_berserker_melee']['basicAttackRange'],220)
        self.assertEqual(PROFILES['troll_berserker_melee']['primaryStat'],'agility')
        self.assertEqual(PROFILES['paladin_holy']['attackStyle'],'flail')
        self.assertEqual(PROFILES['paladin_holy']['primaryStat'],'intelligence')
    def test_hybrid_variants(self):
        self.assertEqual(sum(c['familyId']=='paladin' for c in PROFILES.values()),2)
        self.assertEqual(sum(c['familyId']=='ether_golem' for c in PROFILES.values()),3)
        self.assertEqual(sum(c['familyId']=='troll_berserker' for c in PROFILES.values()),2)
    def test_dragon_sequence_explicitly_planned(self):
        s=next(s for s in PROFILES['drakish_footman']['actives'] if s['id']=='drakish_dragon_oath')
        self.assertEqual(s['status'],'planned')
        for phrase in ('exactly two','furthest','weak fireball','threat','Timed dragon form'):
            self.assertIn(phrase,s['mechanic'])
    def test_source_preserved_and_art_not_claimed(self):
        source=Path(DATA['source']['path'])
        if source.exists():self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),DATA['source']['sha256'])
        self.assertEqual(DATA['source']['encoding'],'windows-1252')
        for c in PROFILES.values():self.assertEqual(c['artStatus'],'prototype_fallback')

if __name__=='__main__':unittest.main()
