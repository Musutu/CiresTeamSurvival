import unittest
from AnalyzeLoadoutLab import contributions


def report(damage, healing=0, skills=None):
    return {'initialParticipants': [dict(name='Tank', team=0, skills=skills or ['iron_guard'],
                                        draftRole='Tank', level=10, basicAttackRange=220)],
            'finalHeroes': [dict(name='Tank', team=0, damage=damage, dps=damage/10,
                                 healing=healing, hps=healing/10, teamDamageShare=.5),
                            dict(name='Enemy', team=1)]}


class ContributionTests(unittest.TestCase):
    def test_medians_and_effective_healing_exposure(self):
        data = {'a': report(100), 'b': report(300, 30), 'c': report(200)}
        document = {'cases': [dict(wave=10, preset='thematic', report=key) for key in data]}
        row, = contributions(document, data.__getitem__)
        self.assertEqual(row['medianDamage'], 200)
        self.assertEqual(row['medianDps'], 20)
        self.assertEqual(row['zeroHealingRuns'], 2)
        self.assertEqual(row['skills'], ['iron_guard'])
        self.assertNotIn('castCount', row)

    def test_changing_equipped_ids_within_group_rejected(self):
        data = {'a': report(100), 'b': report(300, skills=['second_wind'])}
        document = {'cases': [dict(wave=10, preset='thematic', report=key) for key in data]}
        with self.assertRaises(ValueError):
            contributions(document, data.__getitem__)


if __name__ == '__main__':
    unittest.main()
