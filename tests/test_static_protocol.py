import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'laptop'))
from protocol import STATIC_FRAME,Reassembler


def packets(seq=12,flags=1|4|16,temperature=25):
    vectors=[.1,.2,9.8]*5
    metrics=[9.81,.02,.03,1.2,-.5,1.3,1,2,2.3]
    payload=STATIC_FRAME.pack(seq,1000,*vectors,*metrics,*([0]*3+[1]*3),temperature,flags,5,63,1,6,0,5)
    return [bytes([0xA6,seq&255,i,9])+payload[i*16:(i+1)*16] for i in range(9)]

class StaticProtocolTest(unittest.TestCase):
    def test_full_field_mapping(self):
        r=Reassembler()
        for p in packets():d=r.feed(p)
        self.assertEqual(d['protocol'],2);self.assertTrue(d['stable']);self.assertTrue(d['saved'])
        self.assertFalse(d['has_gyro']);self.assertEqual(d['sensor_status'],6)
        self.assertEqual(d['faces'],63);self.assertAlmostEqual(d['noise'],.02)
        self.assertAlmostEqual(d['pitch'],-.5);self.assertEqual(d['scale'],[1,1,1])
        self.assertEqual(d['temperature'],25);self.assertEqual(d['calibration_result'],5)

    def test_unavailable_temperature(self):
        r=Reassembler()
        for p in packets(temperature=-128):d=r.feed(p)
        self.assertIsNone(d['temperature'])

    def test_six_axis_vector_is_distinct_from_accel_lowpass(self):
        r=Reassembler()
        for p in packets(flags=1|4|8|32):d=r.feed(p)
        self.assertTrue(d['has_gyro']);self.assertTrue(d['has_fusion'])
        self.assertEqual(d['filtered_accel'],[None]*3)
        self.assertAlmostEqual(d['fusion_accel'][2],9.8,places=5)

    def test_missing_fragment_recovers(self):
        r=Reassembler()
        for p in packets()[:3]+packets()[4:]:self.assertIsNone(r.feed(p))
        for p in packets(13):d=r.feed(p)
        self.assertEqual(d['sequence'],13);self.assertEqual(r.incomplete,1)

    def test_cross_version_and_length_rejected(self):
        r=Reassembler();r.feed(packets()[0])
        p=bytearray(packets()[1]);p[0]=0xA5
        self.assertIsNone(r.feed(p))
        self.assertIsNone(r.feed(packets()[-1]+b'xx'))
        for p in packets(14):d=r.feed(p)
        self.assertEqual(d['sequence'],14)
