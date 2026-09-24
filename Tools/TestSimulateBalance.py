import copy
from pathlib import Path
import unittest
from ImportCombatTuning import read_bundle
from SimulateBalance import simulate, enemy_stats, hero_stats, review

class BalanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.data=read_bundle(Path(__file__).resolve().parents[1]/'Content/Data/CombatTuning.json')

    def test_deterministic_forecast_and_effective_healing_threat(self):
        first=simulate(self.data,seconds=60)
        self.assertEqual(first,simulate(self.data,seconds=60))
        self.assertEqual(first['kind'],'modeled-expected-value')
        self.assertGreater(first['healing'],0)
        self.assertAlmostEqual(first['healingThreat'],first['healing']*self.data['globals']['healingThreatMultiplier'],places=6)
        self.assertLessEqual(first['damage'],first['enemyInitialHealth']+.001)
        self.assertTrue(all(r['health']<=r['maxHealth'] for r in first['roles']))

    def test_fixed_damage_health_curve_and_hero_native_stat_formulas(self):
        for kind in ('basic','bruiser','caster','ranged'):
            h1,d1=enemy_stats(self.data['globals'],1,kind);h40,d40=enemy_stats(self.data['globals'],40,kind)
            self.assertGreater(h40,h1);self.assertEqual(d1,d40)
        tank=hero_stats(10,'tank');self.assertEqual(tank['stats'],[38,19,19]);self.assertEqual(tank['maxHealth'],950)
        ranger=hero_stats(10,'ranger');self.assertEqual(ranger['baseDamage'],50);self.assertAlmostEqual(ranger['attackInterval'],1.5/1.38)

    def test_health_increase_delays_kill_without_changing_base_damage(self):
        data=copy.deepcopy(self.data)
        for key in ('normalMonsterDamage','bruiserMonsterDamage','casterMonsterDamage','rangedMonsterDamage'): data['globals'][key]=0
        base=simulate(data,level=10,enemy_count=1,seconds=60)
        data['globals']['waveHealthBase']*=2
        harder=simulate(data,level=10,enemy_count=1,seconds=60)
        self.assertGreater(harder['ttk'],base['ttk'])
        self.assertEqual(harder['enemyBaseDamageByType'],base['enemyBaseDamageByType'])

    def test_stronger_crit_changes_output_and_contact_can_avoid_projectiles(self):
        base=simulate(self.data,level=10,seconds=10,projectile_contact=0)
        contacted=simulate(self.data,level=10,seconds=10,projectile_contact=1)
        self.assertGreater(contacted['damage'],base['damage'])
        data=copy.deepcopy(self.data);data['globals']['critChance']=1;data['globals']['critMultiplier']=2
        critical=simulate(data,level=10,seconds=10,projectile_contact=0)
        self.assertGreater(critical['damage'],base['damage'])

    def test_review_gates_are_explicit_and_bounds_reject_nonfinite_input(self):
        row=simulate(self.data,seconds=10)
        flags=review([row],min_ttk=1,max_ttk=2,min_tank_share=1)
        self.assertTrue(flags)
        for kwargs in ({'seconds':float('nan')},{'projectile_contact':1.1},{'enemy_count':21},{'level':10001},{'wave':0},{'step':0}):
            with self.subTest(kwargs=kwargs),self.assertRaises(ValueError):simulate(self.data,**kwargs)
        with self.assertRaises(ValueError):review([row],min_ttk=20,max_ttk=10)

if __name__=='__main__': unittest.main()
