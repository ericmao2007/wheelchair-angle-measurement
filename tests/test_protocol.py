import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'laptop'))
from protocol import FRAME, Reassembler


def packets(sequence=257, flags=15, invalid=False):
    floats = [1., 0., 0., 0.]*2 + [0., 0., 9.81] + [0., 0., 0.]*2 + [30., 29., .1]
    if invalid:
        floats[-2] = float('nan')
    data = FRAME.pack(sequence, 1000, *floats, 25, 3, 3, 3, 3, flags)
    return [bytes([0xA5, sequence & 255, i, 6])+data[i*16:(i+1)*16] for i in range(6)]


class ProtocolTest(unittest.TestCase):
    def test_complete(self):
        r = Reassembler()
        frames = [r.feed(p) for p in packets()]
        self.assertTrue(all(f is None for f in frames[:-1]))
        self.assertEqual(frames[-1]['sequence'], 257)
        self.assertTrue(frames[-1]['stationary'])
        self.assertTrue(frames[-1]['saved'])
        self.assertAlmostEqual(frames[-1]['slope'], 29)

    def test_loss_and_recovery(self):
        r = Reassembler()
        p = packets()
        for i in [0, 1, 3, 4, 5]:
            self.assertIsNone(r.feed(p[i]))
        for part in packets(258):
            data = r.feed(part)
        self.assertEqual(data['sequence'], 258)
        self.assertEqual(r.incomplete, 1)

    def test_cross_frame_and_invalid(self):
        r = Reassembler()
        r.feed(packets()[0])
        self.assertIsNone(r.feed(packets(258)[1]))
        self.assertIsNone(r.feed(b'garbage'))
        self.assertIsNone(r.feed(bytes([0xA5, 0, 0, 255, 1])))
        for part in packets(259, flags=0, invalid=True):
            data = r.feed(part)
        self.assertIsNone(data['slope'])
        self.assertFalse(data['valid'])

    def test_filter_and_missing_flags(self):
        r = Reassembler()
        for part in packets(flags=16|32|128):
            data = r.feed(part)
        self.assertTrue(data['kalman'])
        self.assertTrue(data['acceleration_held'])
        self.assertTrue(data['sensor_missing'])
        self.assertFalse(data['valid'])

    def test_duplicate_and_rollover(self):
        r = Reassembler()
        p = packets(255)
        r.feed(p[0]); r.feed(p[1])
        self.assertIsNone(r.feed(p[1]))
        for part in packets(256):
            data = r.feed(part)
        self.assertEqual(data['sequence'], 256)


if __name__ == '__main__':
    unittest.main()
