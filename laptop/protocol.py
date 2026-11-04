"""Version 1 firmware telemetry. Reassembly never combines partial frames."""
import math
import struct
import time

SERVICE = '12345678-1234-1234-1234-1234567890ab'
TELEMETRY = 'abcdefab-1234-5678-1234-abcdefabcdef'
CONTROL = 'abcdefac-1234-5678-1234-abcdefabcdef'
METADATA = 'abcdefad-1234-5678-1234-abcdefabcdef'
FRAME = struct.Struct('<II4f4f3f3f3f3fb4BB')
STATIC_FRAME = struct.Struct("<II15f9f6fb7B")
assert STATIC_FRAME.size == 136
assert FRAME.size == 94


def decode(payload):
    v = FRAME.unpack(payload)
    clean = lambda values: [x if math.isfinite(x) else None for x in values]
    return dict(sequence=v[0], uptime_ms=v[1], raw=clean(v[2:6]),
                filtered=clean(v[6:10]), accel=clean(v[10:13]),
                gyro=clean(v[13:16]), linear=clean(v[16:19]),
                slope_raw=clean(v[19:20])[0], slope=clean(v[20:21])[0],
                residual=clean(v[21:22])[0], temperature=v[22],
                calibration=list(v[23:27]), valid=bool(v[27] & 1),
                stationary=bool(v[27] & 2), rejected=bool(v[27] & 4),
                saved=bool(v[27] & 8), kalman=bool(v[27] & 16),
                acceleration_held=bool(v[27] & 32), sensor_missing=bool(v[27] & 128), reacquired=bool(v[27] & 64))


STATES = ['Sensor fault', 'Moving', 'Waiting for motion to settle', 'Collecting stability samples', 'Unstable window: collecting again', 'Stable measurement']

def decode_static(payload):
    v = STATIC_FRAME.unpack(payload)
    clean = lambda xs: [x if math.isfinite(x) else None for x in xs]
    floats = clean(v[2:32])
    flags, state, faces, mode, status, error, result = v[33:40]
    return dict(protocol=2, sequence=v[0], uptime_ms=v[1],
                accel=floats[0:3], corrected=floats[3:6], filtered_accel=[None]*3 if flags & 32 else floats[6:9],
                fusion_accel=floats[6:9] if flags & 32 else [None]*3,has_fusion=bool(flags & 32),
                gyro=floats[9:12], mean_accel=floats[12:15],
                magnitude=floats[15], noise=floats[16], drift=floats[17],
                roll=floats[18], pitch=floats[19], slope=floats[20],
                grade_x=floats[21], grade_y=floats[22], grade=floats[23],
                bias=floats[24:27], scale=floats[27:30], temperature=None if v[32] == -128 else v[32],
                chip_fusion=mode == 6, imu_calibrating=bool(flags & 64), imu_saved=bool(flags & 128),
                attitude_accuracy=status & 3, accel_accuracy=(status >> 2) & 3, gyro_accuracy=(status >> 4) & 3,
                valid=bool(flags & 1), calibrating=bool(flags & 2), saved=bool(flags & 4),
                has_gyro=bool(flags & 8), stable=bool(flags & 16), state=state,
                state_text=STATES[state] if state < len(STATES) else 'Unknown state',
                faces=faces, sensor_mode=mode, sensor_status=status, sensor_error=error,
                calibration_result=result)


class Reassembler:
    def __init__(self):
        self.sequence = None
        self.marker = None
        self.parts = []
        self.started = 0
        self.incomplete = 0

    def feed(self, packet):
        if len(packet) < 5 or packet[0] not in (0xA5, 0xA6):
            return None
        _, seq, index, count = packet[:4]
        expected_size = FRAME.size if packet[0] == 0xA5 else STATIC_FRAME.size
        expected_count = (expected_size+15)//16
        expected_length = min(16, expected_size-index*16)+4
        if count != expected_count or index >= count or len(packet) != expected_length:
            return None
        now = time.monotonic()
        if index == 0:
            if self.parts:
                self.incomplete += 1
            self.sequence, self.parts, self.started = seq, [bytes(packet[4:])], now
            self.marker = packet[0]
            return None
        if packet[0] != self.marker or seq != self.sequence or index != len(self.parts) or now-self.started > .5:
            if self.parts:
                self.incomplete += 1
            self.parts = []
            return None
        self.parts.append(bytes(packet[4:]))
        if len(self.parts) != count:
            return None
        payload = b''.join(self.parts)
        self.parts = []
        if len(payload) != expected_size or payload[0] != seq:
            return None
        return decode(payload) if packet[0] == 0xA5 else decode_static(payload)
