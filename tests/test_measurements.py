import sys
from pathlib import Path
import math
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'laptop'))
from measurements import grades, comparison, ADA_CROSS, ADA_RAMP_RUNNING, calibration_ready


def quaternion(roll=0, pitch=0, yaw=0):
    r,p,y = [math.radians(v)/2 for v in (roll,pitch,yaw)]
    cr,sr,cp,sp,cy,sy=math.cos(r),math.sin(r),math.cos(p),math.sin(p),math.cos(y),math.sin(y)
    return [cr*cp*cy+sr*sp*sy,sr*cp*cy-cr*sp*sy,cr*sp*cy+sr*cp*sy,cr*cp*sy-sr*sp*cy]


class GradesTest(unittest.TestCase):
    def test_axis_and_heading_invariance(self):
        for yaw in (0,45,170):
            g=grades(quaternion(pitch=-math.degrees(math.atan(.05)),yaw=yaw))
            self.assertAlmostEqual(g['grade_x'],5)
            self.assertAlmostEqual(g['grade_y'],0)
            self.assertAlmostEqual(g['running'],5)
            self.assertAlmostEqual(g['cross'],0)
        g=grades(quaternion(roll=math.degrees(math.atan(1/48))))
        self.assertAlmostEqual(g['cross'],ADA_CROSS)
        self.assertAlmostEqual(g['grade_y'],ADA_CROSS)

    def test_true_cross_direction(self):
        g=grades(quaternion(roll=30,pitch=30))
        self.assertAlmostEqual(g['running'],-100*math.tan(math.radians(30)))
        self.assertAlmostEqual(g['cross'],100*math.tan(math.radians(30))/math.cos(math.radians(30)))
        self.assertNotAlmostEqual(g['cross'],g['grade_y'])
        self.assertAlmostEqual(grades(quaternion(roll=10),'Y')['running'],100*math.tan(math.radians(10)))

    def test_fusion_calibration(self):
        self.assertTrue(calibration_ready([0,3,3,0], 'IMUPLUS'))
        self.assertFalse(calibration_ready([3,3,2,0], 'IMUPLUS'))
        self.assertFalse(calibration_ready([0,3,3,0], 'NDOF'))
        self.assertTrue(calibration_ready([3,3,3,3], 'NDOF'))
        self.assertFalse(calibration_ready([3,3,3,3], 'unknown'))

    def test_limits_signs_and_invalid(self):
        self.assertEqual(comparison(-5,0),'within reference')
        self.assertEqual(comparison(5.01,0),'above reference')
        self.assertEqual(comparison(6,0,'ramp'),'within reference')
        self.assertEqual(comparison(ADA_RAMP_RUNNING+.01,0,'ramp'),'above reference')
        self.assertEqual(comparison(0,-ADA_CROSS-.01),'above reference')
        self.assertIsNone(grades(quaternion(pitch=90))['grade_x'])
        self.assertIsNone(grades([None]*4)['grade_x'])
        self.assertEqual(comparison(None,0),'undefined')

if __name__=='__main__': unittest.main()
