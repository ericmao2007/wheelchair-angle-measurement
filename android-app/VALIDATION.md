# v1.5 · 2026-10-30

- Angular/Grade format is one decimal, with signed rounded-zero handling; full precision remains in calculations, calibration and GPS records.
- Reversal and GPS tests require 5 seconds; explicit rejection of 3-second reversal samples added. UI binary-frame integration still covers reversal, zero, Grade/3D, GPS, calibration controls and invalidation.
- Chip-fused synthetic static angular-noise SD ratio: 0.214224 with longer smoothing. Gyro-assisted motion reaches the test target within 0.15° at 0.15 s; isolated spike rejection and invalid-data resets pass. Noise ratio is not absolute accuracy.
- Firmware default stability window is 5 seconds. C++ static detector tests pass with enough settling/collection time and include early-window rejection.
- assembleDebug, lintDebug and PlatformIO firmware compilation passed.
- No USB ESP32 was detected during this update. Sensor drift and physical calibration were not revalidated. App/code changes do not erase calibration storage.

# v1.4 · 2026-10-30

- Reversal model tests: −8°/+12° -> +2° offset / 10° slope, shallow same-sign measurements, flat slope, transverse Roll preservation, cancellation/clear, duplicate frames, movement, jitter, gaps and invalid/inverted data.
- Actual UI integration uses binary SH-2 frames: collect two independent stable windows, review/apply, preserve previous correction on cancel, correct main Pitch/Grade/3D/relative-zero/GPS consistently, preserve IMU saved indication, invalidate on disconnect/restart/calibration changes. No IMU write command is sent by reversal controls.
- Existing chip calibration command/save gating, protocol, GPS tracking and filter regressions pass. Added chip-specific synthetic static noise test: output/input angular SD ratio 0.401336; isolated spike rejection and 0.15 s motion-following test pass. These are algorithm tests, not hardware accuracy claims.
- assembleDebug and lintDebug pass (v1.4, versionCode 5).
- No USB serial sensor was present during this update. Real chip calibration persistence, 180° positioning, physical zero accuracy, Android phone rendering and field repeatability were not re-tested. Firmware and sensor calibration storage were not modified.

# v1.3 · 2026-10-28

- Firmware uses SH-2 Game Rotation Vector with normalized quaternion-to-gravity conversion. Independent C++ tests cover X/Y signs, yaw invariance, q/-q, inversion and invalid input.
- App UI integration tests cover chip accuracy fields, native calibration/save commands and save gating. Synthetic fused motion step reaches within 0.15 degrees in 0.15 seconds; this tests smoothing latency only.
- UI and BLE target 20 Hz. Existing protocol, zero/GPS, tracking and fallback filter tests pass. assembleDebug and lintDebug pass.
- ESP32 upload succeeded at 115200. A 12-second serial capture included 10 valid sample lines, no panic, and SH-2 quality status 59 (fusion High, accel Medium, gyro High). Initial startup invalid samples are expected. Capture: ../diagnostics/bno086-sh2-runtime.txt.
- Fixed driver event buffer lifetime: synchronous calibration commands can dispatch sensor reports, so event storage is persistent and bound before commands.
- Actual phone rendering latency, IMU calibration persistence and physical return-to-position accuracy remain unverified.

# BNO086 axis display update · 2026-10-28 · v1.2

- Exchanged displayed Roll/Pitch values; main Pitch, Grade and new GPS records share the updated mapping. Existing saved records retain their original values.
- Isolated X/Y tilt checks, UI/zero/GPS integration and filter tests passed; tracking tests passed.
- assembleDebug and lintDebug passed. APK signature verification passed.
- Android phone installation and physical axis direction have not been verified in this update.

# Validation · 2026-10-23 · Live Tilt 1.1

## Completed

- `:app:assembleDebug :app:lintDebug` passed with 0 errors. Six existing toolchain, target SDK, manifest and local JavaScript/backup warnings remain.
- APK verified with APK Signature Scheme v2. Package `com.maoslope.livetilt`, versionCode 2 / versionName 1.1, min SDK 26 / target SDK 35. Signing certificate matches the previous v1.0 APK, permitting an in-place update.
- Existing FrameAssembler tests: all 13 reassembly/recovery cases passed.
- Existing mobile protocol/UI and filter tests passed: six-axis decode, 500 ms display, BLE controls, signed grade, zero/reset, diagram orientation and stale-reading removal.
- Stability tests passed: ±0.2° threshold around the window mean, both axes, 3-second minimum, angular motion, stream gaps, reference changes and invalid samples. The detector sees every unsmoothed fused/corrected orientation frame, rather than the smoothed 2 Hz display.
- GPS/dedup tests passed: reject fixes older than 10 seconds or less precise than 20 m, reject invalid coordinates, suppress nearby similar points, accept changed positions/angles/references.
- Mock native bridge integration passed: live frames plus GPS save a point, repeated frames do not duplicate it, native save failure is surfaced without claiming success, zero starts a distinct reference, missing GPS prevents saves, pause calls native GPS shutdown, menu navigation and point details work.
- Playwright browser preview at 390 × 844: local assets loaded without JavaScript errors; three explicitly labelled demo records restored through the mocked native bridge, markers projected, a marker tap displayed angle/grade/location details, no horizontal overflow. All 6 visible real OpenStreetMap tiles loaded. Screenshots: `tests/gps-measure-preview.png`, `tests/gps-map-preview.png`.
- Location and BLE stop in the background; GPS requires explicit opt-in per app launch and foreground precise permission. Records use native private SharedPreferences; the UI reports failed writes. Network access is restricted to standard HTTPS map-tile image paths; all application scripts remain packaged assets.

## Not completed

`adb devices` showed no attached Android phone. Actual installation/update, location permission dialogs (grant/deny/approximate), GPS callbacks/accuracy indoors and outdoors, native persistence after force-stop/restart, ESP32 BLE + GPS together, phone-specific WebView map loading and lifecycle recovery have not been tested on hardware. Browser mocks do not establish those native behaviors. No improvement to absolute angle accuracy is claimed.

## Phone acceptance test

1. Install `dist/LiveTilt-debug.apk` over the previous app; close the computer BLE viewer, connect MAO_SLOPE and confirm live readings.
2. Enable GPS recording, grant precise location while using the app, turn on Location, and wait outdoors for accuracy ≤20 m. Hold both angles within ±0.2° for 3 seconds; verify one saved point and no repeated point while stationary.
3. Open **02 · Map**; confirm base map, angle markers, tap details, pan, +/−, Latest point and My location. Move beyond 10 m (or reported accuracy, whichever is larger), stop and verify another point. A ≥0.5° angle change at the same location should also produce another point after stability.
4. Set/reset zero and check that new points identify relative/absolute values. Calibration and invalid/disconnected sensor readings must not create points.
5. Pause recording, deny location, choose approximate only, disable phone Location, go indoors or use stale fixes; verify waiting/error messages and no false saves. Use Retry GPS after restoring precise permission/Location.
6. Background/resume, force-stop/reopen and test without internet. Existing records must persist; GPS must be manually enabled after app restart. With no internet, saved markers/list remain usable while uncached base-map tiles may be unavailable.

Optional browser test: install Playwright with Chromium and run `node tests/map-preview.cjs`. `PLAYWRIGHT_CHROMIUM_EXECUTABLE` can select an existing Chromium executable. Test GPS data is injected only in the browser test, never packaged into the APK.
