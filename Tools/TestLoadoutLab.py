import unittest
from RunLoadoutLab import summarize

def case(preset,dps=100,seconds=20,result='allies_won',wave=10):
    return dict(wave=wave,preset=preset,result=result,dps=dps,hps=15,tankTargetShare=.7,seconds=seconds,alliesAlive=5)

class LoadoutReportTests(unittest.TestCase):
    def test_same_wave_median_baseline_and_thresholds(self):
        data=[case('thematic',dps=v) for v in (90,100,110)]+[case('dps_hunt',dps=140,seconds=15) for _ in range(3)]
        row=next(r for r in summarize(data) if r['preset']=='dps_hunt')
        self.assertEqual(row['dpsVsThematic'],1.4)
        self.assertEqual(row['victoryTimeVsThematic'],.75)
        self.assertEqual(set(row['reviewFlags']),{'dps_above_comparison_threshold','victory_time_below_comparison_threshold'})
    def test_losses_do_not_become_fast_victories(self):
        row=next(r for r in summarize([case('thematic'),case('tank_seismic',seconds=1,result='allies_lost')]) if r['preset']=='tank_seismic')
        self.assertIsNone(row['medianVictorySeconds']);self.assertIsNone(row['victoryTimeVsThematic'])
        self.assertIn('lower_observed_win_rate',row['reviewFlags']);self.assertIn('fewer_than_three_repeats',row['reviewFlags'])
    def test_different_waves_not_compared(self):
        row=next(r for r in summarize([case('thematic',wave=1),case('dps_hunt',wave=30)]) if r['preset']=='dps_hunt')
        self.assertNotIn('dpsVsThematic',row)
    def test_timeout_flag_and_zero_damage_baseline(self):
        row=next(r for r in summarize([case('thematic',dps=0),case('support_aegis',result='timeout')]) if r['preset']=='support_aegis')
        self.assertIsNone(row['dpsVsThematic']);self.assertIn('encounter_timeout',row['reviewFlags'])
    def test_different_enemy_counts_not_compared(self):
        base=case('thematic');base['enemies']=5
        pressure=case('support_wellspring');pressure['enemies']=15
        row=next(r for r in summarize([base,pressure]) if r['preset']=='support_wellspring')
        self.assertEqual(row['enemies'],15);self.assertNotIn('dpsVsThematic',row)

if __name__=='__main__':unittest.main()
