# Wheelchair Angle Measurement

An ESP32 and BNO086 project for measuring wheelchair tilt and slope, with Bluetooth Low Energy (BLE), an Android app, and a laptop browser viewer.

The current firmware uses the BNO086's internal six-axis fusion. The sections below describe current operation first, followed by historical BNO055 implementation notes. Historical register settings, wiring, and operating modes do not apply to the current BNO086 firmware.

## Current release: v1.5 (2026-10-30)

Android angle and grade values display one decimal place while retaining internal precision. The BNO086 display adds a 1.5-second rolling vector average when stationary and bypasses the long window during motion. Reversal measurements, GPS stable sampling, and the ESP32 stability window now use five seconds instead of three. Calibration behavior is retained.

See the [Android app instructions](android-app/README.md). Updating the app and flashing the firmware are separate steps. Successful builds do not replace physical drift testing.

### Pitch offset from a 180° reversal: v1.4 (2026-10-30)

The Android app includes **180° reversal zero · Pitch**. On the same slope, keep the same contact face and contact location, and take stable six-axis measurements facing downhill and uphill. Version 1.4 required at least three seconds per direction; v1.5 increases this to five seconds.

```text
zero offset = (angle1 + angle2) / 2
slope magnitude = abs(angle1 - angle2) / 2
```

After confirmation, the correction applies to Pitch, Grade, the 3D view, and new GPS records. Existing IMU calibration and display filtering remain available; this app feature does not require a firmware update.

The offset lasts for the current connection. Repeat the procedure after disconnection, a sensor restart, or a calibration change. Six-axis sensing provides no heading, so manually turn the device 180° along the slope without flipping it upside down. This estimates longitudinal zero offset; it does not correct scale error, arbitrary mounting rotation, or lateral Roll. The main screen also shows saved calibration records and filter status.

## Current hardware: ESP32 + BNO086

The firmware uses Adafruit BNO08x 1.2.5 and its SH-2/SHTP report interface. It does not access BNO055 registers.

### Wiring

Disconnect power before wiring and follow the actual module labels. This table applies to a 3.3 V BNO086 breakout with supporting power circuitry, rather than the bare chip.

| BNO086 module | ESP32 |
|---|---|
| 3V3 / 3.3V | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |
| RST / NRST | GPIO19 |
| INT | Not required; this I²C driver polls the sensor |

Configure the module for I²C: PS0=0 and PS1=0. SparkFun Qwiic modules use this by default; check the schematic for other modules. Leave BOOT at its default high level to avoid entering the bootloader. SA0/ADR selects the address; the firmware tries 0x4B and 0x4A.

SDA and SCL need pull-ups to 3.3 V. SparkFun modules already include them. Do not connect signal pins to 5 V. For a module labeled only VIN, check its supply requirements before connecting it; VIN may differ from 3V3.

Current I²C speed is 400 kHz. GPIO settings are in [platformio.ini](platformio.ini). If RST is unavailable, set `BNO_RESET` to `-1` for software reset, although a reset wire is recommended. Use short wires and a shared ground. Repeated disconnections require checking wiring and pull-ups; SPI may be an alternative if needed.

### Build, flash, and monitor

With PlatformIO installed and `pio` on your PATH:

```sh
pio run
pio run --target upload
pio device monitor --baud 115200
```

On the original development machine, the executable is `/Users/ericmao/.platformio/penv/bin/pio`. Add `--upload-port <port>` if automatic port detection fails.

Startup should print `BNO086 init=1`, followed by `ok=1` during operation. The BLE device name is `MAO_SLOPE`; service UUIDs remain unchanged. Reports older than 120 ms invalidate the reading. Prolonged report loss triggers reinitialization, and sensor resets trigger report resubscription.

Confirm the module's X/Y/Z markings when mounting it. Adjust `AXES` in [include/inclinometer.h](include/inclinometer.h) when the mounting changes. BNO086 diagnostic six-face coefficients use NVS namespace `bno086-static`, key `sixaxis-v1`, separately from old BNO055 coefficients.

### Internal six-axis fusion (2026-10-28)

The main orientation comes from `SH2_GAME_ROTATION_VECTOR`, requested at 100 Hz. The BNO086 fuses three-axis acceleration and three-axis gyroscope measurements internally; the magnetometer is disabled. A normalized quaternion projects world +Z into sensor coordinates to obtain the gravity direction, then applies the `AXES` mounting map. Euler angles derived from that quaternion are not integrated again with the gyroscope.

`SH2_ACCELEROMETER` and `SH2_GYROSCOPE_CALIBRATED` reports are requested at 50 Hz for motion and calibration diagnostics. Acceleration includes gravity and uses m/s²; gyroscope reports are converted from rad/s to °/s. Here, “raw” means before application of the diagnostic six-face correction, rather than raw ADC counts.

BLE transmission and Android v1.3 display updates run at a nominal 20 Hz. In chip fusion mode, the app bypasses the older 0.5-second window and random-walk Kalman filter. A three-frame median rejects isolated stationary spikes, followed by a low-pass filter with a 0.20-second time constant. Above 1°/s, the median is bypassed and the low-pass time constant becomes 0.035 seconds. Version 1.5 adds the stationary rolling average described above. There is no dead zone or last-angle lock. Smoother output does not establish absolute accuracy.

### IMU calibration

1. In the updated Android app, open Calibration and select **Start IMU calibration**.
2. Keep the device still for a few seconds, then hold it in four to six different orientations for a few seconds each.
3. Finish with the device resting on a stable support.
4. When both Accel and Gyro quality are High and the stability window passes, select **Save IMU calibration** to save the chip's DCD calibration.

Dynamic gyro calibration stays enabled during normal measurement. Dynamic accelerometer calibration is enabled only during explicit calibration and is disabled after a successful save. Startup does not automatically treat the current position as horizontal. **Set zero** establishes only a relative reference.

Legacy six-face NVS coefficients still apply to diagnostic acceleration, but never to the chip fusion quaternion. In `mode=6`, the Android app provides chip calibration and hides the legacy six-face controls.

### Protocol and status

The 136-byte STATIC2 format is retained. Current chip fusion uses `mode=6`, and metadata ends with `:SH2`. The preceding BNO086 application-fusion implementation used `mode=5` and metadata `STATIC2:ACCGYRO:FUSION6:BNO086`; its fixed status values should not be interpreted as current calibration quality.

| Field | Meaning in chip fusion mode |
|---|---|
| flags mask 64 | Chip calibration is active |
| flags mask 128 | This firmware has a saved DCD record; this does not guarantee current High quality |
| status bits 0–1 | Fusion quality |
| status bits 2–3 | Accelerometer quality |
| status bits 4–5 | Gyroscope quality |
| error=1 | Report is invalid |

Quality values are 0 Unreliable, 1 Low, 2 Medium, and 3 High. Temperature is unavailable: `temperature=-128`, decoded by the Python bridge as JSON `null`.

Orientation conversion, app mapping, calibration commands, motion following, and interrupted-data tests were recorded as passing, together with firmware/APK builds and lint. Physical return-position error still requires comparison using the same fixture and a reference angle; these checks do not establish absolute accuracy.

References: [CEVA BNO08X datasheet, §2.2.2](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf), [SH-2 calibration API](https://github.com/adafruit/Adafruit_BNO08x/blob/master/src/sh2.h), [Adafruit BNO08x driver](https://github.com/adafruit/Adafruit_BNO08x), and [SparkFun BNO086 hardware guide](https://docs.sparkfun.com/SparkFun_VR_IMU_Breakout_BNO086_QWIIC/hardware_overview/).

## Laptop viewer and logging

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r laptop/requirements.txt
.venv/bin/python laptop/viewer.py --csv diagnostics/measurement-01.csv
```

Open http://127.0.0.1:8765 and connect to `MAO_SLOPE`. On macOS, grant Bluetooth access to the terminal or editor. Run only one BLE bridge at a time. Options include `--no-browser`, `--port 8766`, and `--demo`. Demo data is explicitly simulated and cannot validate the sensor.

The viewer provides angle, grade, Roll/Pitch, a 3D view, relative zeroing, and diagnostic data. Translation and vibration can affect estimates during motion. Invalid, stale, or calibration-sampling data withdraws the current result; historical stable readings are labeled with their time. Uncalibrated readings are estimates. Heading is not measured.

## Diagnostics and validation

CSV logging requires a new destination path to avoid overwriting earlier sessions. Ctrl+C closes and flushes the log. A neighboring `.meta.json` file records units, simulation status, and analysis limitations. Firmware serial output at 115200 baud reports status, vector magnitude, window noise/direction change, angles, temperature availability, and health; calibration commands also report results.

For a **10–30 minute stationary test**, secure the device to a repeatable base in one orientation, avoid zeroing during the test, and record temperature changes where available. One logging session records continuously. Report per-minute stable means, within-window fluctuation, and peak-to-peak variation separately. Changes caused by lifting and replacing the device should not be counted as stationary drift.

For **repeated return-position tests**, mark `moving` before lifting the device, then mark `return-1` after replacing it on the same base in the same direction. Wait until stable and hold for about 10–20 seconds. Repeat with `moving`, `return-2`, and so on. Each marker is applied to the next received frame and remains active until the next marker. Name each physical return manually; a stable segment interrupted by noise is not necessarily a physical lift.

Compare stable return means, maximum difference from the first return, mean range, short-term standard deviation, recovery time, and available temperature. Recovery time runs from the receiver's first invalid result until stability returns, including connection/calibration delays; it does not precisely measure mechanical placement time.

```sh
.venv/bin/python laptop/analyze_log.py diagnostics/static-session.csv --output diagnostics/report.json
```

Reports include per-minute statistics, manually marked return groups, automatic stable segments, and recovery time. Roll uses circular means and wrapped deviations to handle the ±180° boundary. Without an external reference, `absolute_error` remains `NOT MEASURED`. Raw/filtered differences and short-term standard deviation do not measure absolute error. Historical BNO055 temperature readings are internal sensor readings, rather than calibrated ambient temperatures.

Existing validation commands:

```sh
.venv/bin/python -m unittest discover -s tests -v
c++ -std=c++17 -Wall -Wextra -I include tests/inclinometer_test.cpp -o /tmp/prototype-inclinometer-test
/tmp/prototype-inclinometer-test
pio run
```

See [diagnostics/VALIDATION.md](diagnostics/VALIDATION.md) for recorded results. Physical calibration, long-term drift, and return repeatability require real measurements; synthetic tests cannot replace them.

## Historical implementation notes

The following sections preserve the previous BNO055 behavior and development history. Current BNO086 instructions above take precedence.

### ESP32 six-axis TiltFusion (2026-10-21)

The default changed to **ACCGYRO**, combining three-axis acceleration and gyroscope data. ESP32 ran a six-axis complementary tilt observer at a nominal 50 Hz. Gyroscope prediction tracked gravity direction; accelerometer feedback constrained tilt drift and estimated observable gyro bias. The magnetometer was disabled, so absolute heading was unavailable. Gyroscope settings were ±2000°/s, 23 Hz bandwidth, and normal power. Six-face acceleration calibration remained in NVS.

The browser used median filtering, a short vector average, and adaptive Kalman display smoothing, bypassing the slow window and increasing gain during clear angular motion. Existing axis swaps and sign inversions were preserved. **Live · 6-axis** appeared only for valid fusion frames.

STATIC2 retained its 136-byte layout and 0xA6 fragmentation. Flags mask 32 identified the former filtered-vector slots as fused gravity in m/s². Metadata was `STATIC2:ACCGYRO:FUSION6`. The bridge decoded these slots as `fusion_accel` and left `filtered_accel` empty; older frames without the flag fell back to acceleration display. CSV added `has_gyro`, `has_fusion`, and three fusion-vector fields.

`tests/tilt_fusion_test.cpp` covers rotation direction, bias, flips, interrupted streams, and restart recovery. `tests/test_static_protocol.py` covers fusion fields; `tests/live_filter_test.js` covers smoothing and gyro-assisted tracking. Six-axis fusion cannot eliminate sustained translation, mounting error, or temperature drift.

### Earlier BNO055 static gravity inclinometer

This implementation read acceleration registers including gravity, corrected three-axis bias and scale, mapped device axes, low-pass filtered the vector, averaged a stable window, and calculated tilt. Fused Euler angles, fused gravity, linear acceleration, and gyro integration did not enter the main measurement. Old Kalman/filter files remained for historical tests.

Default mode was **ACCONLY (0x01)**. Setting `BNO_MEASUREMENT_MODE` to `OPERATION_MODE_ACCGYRO` (0x05) added gyro motion detection without orientation fusion. ACCONLY had no heading or gyro stationary criterion. Modes were checked at startup and during sampling; `SYS_STATUS=6` meant non-fusion operation. I²C completeness, unit registers, operating mode, and sensor health were checked, and failed frames were rejected.

A compatible 3.3 V BNO055 breakout used SDA→GPIO21, SCL→GPIO22, common ground, I²C mode, short wires, and suitable 3.3 V pull-ups. The firmware probed 0x28/0x29. Wiring was performed with power off.

Page 0 `ACC_DATA 0x08..0x0D` used 100 LSB/(m/s²); ACCGYRO `GYR_DATA 0x14..0x19` used 16 LSB/(°/s). `UNIT_SEL=0` and an identity axis map were explicitly configured. Page 1 acceleration settings were ±2g, 31.25 Hz bandwidth, and normal power. Nominal sampling was 50 Hz and BLE was 10 Hz, subject to scheduling.

Historical `Config` defaults in [include/inclinometer.h](include/inclinometer.h):

| Parameter | Historical default | Meaning |
|---|---:|---|
| lowpassSeconds | 0.35 s | First-order vector low-pass; 0 disables it |
| stableSeconds | 3 s | Continuous stability window; later increased to 5 s |
| settleSeconds | 1 s | Wait after motion or instability |
| maxGap | 0.12 s | Clear the window after a sampling gap |
| normTolerance | 0.8 m/s² | Allowed deviation from 9.80665 m/s² |
| noiseLimit | 0.08 m/s² | RMS corrected-vector deviation from the window mean |
| driftLimit | 0.08° | Direction difference between means of the first/last window thirds |
| accelStep | 0.25 m/s² | Adjacent corrected-vector jump limit |
| gyroInstant | 5°/s | Instantaneous gyro limit, when available |
| gyroMean / gyroRms | 0.3 / 1.5°/s | Window gyro limits, when available |

A 260-sample ring buffer supported 2–5-second windows at nominal 50 Hz. Gaps, motion, or instability discarded the window, reset the low-pass filter, and restarted collection after settling. Only vectors were averaged. The system did not average Euler angles, lock the last angle, or automatically subtract return-position differences. Overlapping, correlated windows do not provide absolute accuracy or confidence intervals. A norm near 1g is only one condition; sustained acceleration or very slow motion may still pass.

### Legacy six-face calibration and NVS

1. Allow temperature to stabilize. Use a repeatable, flat fixture with six mutually perpendicular faces; secure the sensor and avoid wire tension.
2. Start a new six-face capture. Collection uses raw readings without the old correction, and the main formal measurement pauses.
3. Point sensor +X, −X, +Y, −Y, +Z, and −Z upward in turn. After the stable indication, capture the corresponding face. Incorrect direction or an unstable window is rejected.
4. Save all six faces. For each axis, `bias=(plus+minus)/2`, `scale=2g/(plus-minus)`, and `corrected=(raw-bias)*scale`. Opposite-face means estimate bias and diagonal scale.
5. BNO055 coefficients use NVS namespace `bno-static`, key `sixaxis-v1`. Check calibration status and coefficients after restart. This pipeline does not use BNO055 fusion quality levels or import the older `bno-slope/offsets` fusion profile. Clear removes only the six-face profile.
6. Invalid coefficient ranges or incomplete data are rejected. Failed saves preserve the existing calibration, which also remains available after cancelling capture.

This procedure cannot fully correct nonorthogonal axes, arbitrary mounting rotation, temperature effects, or fixture error. Six-face calibration without a verified reference fixture does not guarantee ±0.1°.

### Angle definitions and relative zero

Device coordinates default to sensor coordinates. `AXES={1,2,3}` accepts signed axis permutations and should remain right-handed. Calibration is applied in sensor coordinates before mapping to device coordinates. XY is the measurement plane; +Z is the front-face normal.

For a stable mean vector `(x,y,z)`:

| Quantity | Formula and range |
|---|---|
| Roll | `atan2(y,z)`, −180° to +180°; positive raises +Y |
| Pitch | `atan2(-x,hypot(y,z))`, −90° to +90°; positive raises −X |
| Total plane tilt | `atan2(hypot(x,y),abs(z))`, 0° to 90° |
| X grade | `100*x/hypot(y,z)` |
| Y grade | `100*y/hypot(x,z)` |
| Total grade (%) | `100*hypot(x,y)/abs(z)` = `100*tan(total tilt)` |

An inverted horizontal plane still has zero total tilt; the UI separately identifies its downward-facing front. Vertical total grade has no finite value. The graphic reconstructs tilt from gravity without absolute heading.

The original relative-zero diagnostic stored the current gravity direction and displayed the angle between directions, 0°–180°, without changing absolute results, coefficients, or NVS. Reloading cleared it; startup never automatically declared the current slope horizontal. Later browser display zeroing is described below.

### Legacy STATIC2 protocol

Service, notify, control, and metadata UUIDs were preserved. Metadata was `STATIC2:ACCONLY` or `STATIC2:ACCGYRO`. The 136-byte little-endian structure is `<II15f9f6fb7B`:

- Sequence and uptime.
- Five three-axis vectors: raw, corrected, filtered, gyro, and mean.
- Magnitude, noise, direction change, roll, pitch, slope, gradeX, gradeY, and total grade.
- Three-axis bias and scale.
- Temperature, flags, state, faces, mode, status, error, and calResult.

Invalid angles use NaN, converted to JSON `null` by the bridge. Original flags masks were 1 valid, 2 calibration capture, 4 NVS calibration present, 8 gyro present, and 16 stable. Each fragment is `[0xA6,seqLow,index,count]` followed by up to 16 bytes, giving nine fragments. The bridge validates length, order, version, and timeout and never combines different frames. Older 0xA5 frames remain decodable for diagnostics but are not formal static measurements.

Commands: `CALSTART`, `F0` through `F5`, `SAVE`, `CALCANCEL`, and `CLEAR`. Result codes: 1 unstable, 2 wrong direction, 3 incomplete faces/invalid coefficients, 4 NVS failure, 5 success, 6 cancelled, and 7 cleared.

References: [Bosch BNO055 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bno055-ds000.pdf) and [Adafruit BNO055 driver](https://github.com/adafruit/Adafruit_BNO055/blob/master/Adafruit_BNO055.cpp).

### Continuous browser filtering (2026-10-21)

The browser angle, Grade, and 3D view used a three-frame median, a 0.5-second rolling vector average, and an adaptive random-walk Kalman filter on corrected acceleration. Lower stationary process noise increased smoothing; sustained motion increased gain. Small changes continued to track with filtering delay. Each new BLE frame updated the filter once, independently of rendering. This was not a gyro attitude EKF; ACCONLY supplied no gyro prediction.

The main number became signed Pitch, with `Grade = 100 × tan(Pitch)` and Roll shown separately. Set zero captured the current filtered direction; angle, grade, 3D, and history then shared the same relative coordinates. Reset zero restored absolute display, and reload cleared the reference. Calibration changes reset filtering and zero to avoid mixing coordinate systems. Invalid frames, interrupted streams, and restarts caused reinitialization. Browser filtering did not alter raw CSV data or firmware stable results.

`LiveVectorFilter` in `laptop/index.html` contains tuning parameters. `tests/live_filter_test.js` covers synthetic noise, step response, isolated spikes, slow rotation, and recovery. `diagnostics/live-filter-validation.json` records tests and stable-data replay results. These checks do not validate absolute accuracy, long-term drift, or real dynamic orientation accuracy. Stronger smoothing adds delay; translation and temperature drift can still bias angles.

Browser Roll/Pitch were swapped for the reversed mounting, with the main Pitch value taken from the former Roll and Grade following the same direction. A later browser update rendered angle, Grade, and 3D every 500 ms (2 Hz), while background processing continued for each BLE frame. Set zero and Reset zero updated immediately. Lower rendering frequency does not remove filter settling delay.
