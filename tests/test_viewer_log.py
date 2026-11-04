import argparse
import csv
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'laptop'))
from viewer import Viewer


def data(stable=True):
    return dict(protocol=2,sequence=1,uptime_ms=100,valid=True,stable=stable,state=5 if stable else 1,
                calibrating=False,saved=False,faces=0,calibration_result=0,sensor_mode=1,sensor_status=6,
                sensor_error=0,roll=0,pitch=0,slope=0,grade_x=0,grade_y=0,grade=0,magnitude=9.8,
                noise=.02,drift=.01,temperature=25,accel=[0,0,9.8],corrected=[0,0,9.8],
                filtered_accel=[0,0,9.8],mean_accel=[0,0,9.8],gyro=[None]*3,bias=[0]*3,scale=[1]*3)

class ViewerLogTest(unittest.TestCase):
    def test_initial_stable_is_not_recovery_and_marks_persist_in_log(self):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'log.csv'
            v=Viewer(argparse.Namespace(csv=str(path),demo=False))
            v.accept(data());self.assertIsNone(v.latest['recovery_s'])
            v.pending_mark='return-1';v.accept(data())
            self.assertEqual(v.latest['last_mark'],'return-1')
            v.accept(data(False));v.accept(data())
            self.assertIsNotNone(v.latest['recovery_s'])
            v.log_file.close()
            with path.open() as f: rows=list(csv.DictReader(f))
            self.assertEqual(rows[1]['mark'],'return-1')
            self.assertEqual(rows[0]['gyro_x'],'')
            self.assertEqual(rows[0]['calibration_result'],'0')
            self.assertTrue(Path(str(path)+'.meta.json').exists())
            with self.assertRaises(FileExistsError):Viewer(argparse.Namespace(csv=str(path),demo=False))
