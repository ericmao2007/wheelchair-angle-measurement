import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'laptop'))
from analyze_log import summarize

class DiagnosticsTest(unittest.TestCase):
    def test_only_stable_fixture_marks_are_compared(self):
        rows=[]
        for i,angle in enumerate([1,1.02,50,1.1,1.12]):
            rows.append(dict(stable=str(i!=2),valid='True',calibrating='False',elapsed_s=str(i*30),
                             slope=str(angle),roll='179.9' if i%2 else '-179.9',temperature=str(25+i),
                             placement='1' if i<2 else '2',recovery_s='4.1',
                             mark={0:'return-1',2:'moving',3:'return-2'}.get(i,'')))
        r=summarize(rows)
        self.assertEqual(r['stable_rows'],4)
        self.assertAlmostEqual(r['marked_slope_mean_range_deg'],.1)
        self.assertIn('NOT MEASURED',r['absolute_error'])
        self.assertNotIn('1',r['recovery_seconds_by_episode'])
        self.assertEqual(r['recovery_seconds_by_episode']['2'],4.1)

    def test_empty_log_is_not_accuracy_measurement(self):
        r=summarize([]);self.assertEqual(r['stable_rows'],0)
        self.assertIsNone(r['overall']['slope'])
