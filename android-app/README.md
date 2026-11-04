# v1.5 · 一位小数与更长平均（2026-10-30）

- 主 Pitch、Roll、Grade、对向零偏/坡度以及地图角度均显示小数点后 1 位；计算、BLE、校准系数和保存记录保持完整精度，经纬度不降精度。
- BNO086 静止显示采用三帧中值、1.5 秒滚动向量平均和时间常数 0.35 秒的低通。gyro >1°/s 时清空长窗并绕过平均，沿用 0.035 秒运动低通；停止转动后从当前位置重新累积，避免混入上次姿态。
- 对向测量每方向的稳定采样由 3 秒延长至 5 秒；GPS 稳定平均及 ESP32 的稳定窗口也改为 5 秒。原有 ±0.2° / gyro / 数据有效性判据保留。
- 芯片融合、IMU 校准保存、对向求零点和 20 Hz 更新继续使用。更长平均会增加静止读数的收敛时间；不能消除焊接应力、温漂或持续零偏变化。
- App APK 已更新；固件稳定窗口的变化需要重新烧录 ESP32 才生效。App 的显示平滑和对向/GPS 5 秒窗口安装新版后即可生效。

---

# v1.4 · 对向求差 / Pitch 零偏校正（2026-10-30）

安装 dist/LiveTilt-debug.apk，可覆盖更新；ESP32 固件、接线、芯片 DCD 和原有六面 NVS 校准均不修改。

1. 连接后展开 **180° reversal zero · Pitch**，同一面贴着坡面，朝坡下点 **Capture first direction**。
2. 保持至少 3 秒稳定：未经过 App 显示平滑的六轴角度在窗口均值 ±0.2° 内，角速度 ≤1°/s，帧间隔 ≤250 ms。芯片融合/Gyro 质量须至少 Medium；校准期间不采样。
3. 沿同一坡面调头 180°，同一面贴地，保持相同接触位置；不要翻面。点 **Capture opposite · 180°**，再稳定采样 3 秒。
4. 查看两次均值、零偏及坡度，确认确实是相反方向后点 **Apply Pitch zero**。`zero=(first+opposite)/2`，`slope=abs(first-opposite)/2`。例如 −8°、+12° 得零偏 +2°、坡度 10°；应用后两向分别为 −10°、+10°。
5. 主 Pitch、Grade、3D、相对 Set zero 和新 GPS 记录都使用同一修正向量；横向 Roll 不作零偏校正。地图记录额外保存零偏和原始两向均值，以便追溯。已有地图记录保持原值。
6. Cancel 保留先前已应用的对向零偏；Clear reversal zero 撤销它。此零偏只用于本次连接：断线、传感器 uptime 回退、IMU/六面校准状态或系数改变时清除；重新连接后需重做。不会写入或清除传感器校准。六轴模式不提供航向，App 无法自动验证 180°；正反两次若不在同一坡面/位置，结果会有系统误差。接近竖直或翻面不允许采样。

六轴芯片融合、20 Hz 更新、静止中值去尖峰和自适应低通保留。测量页新增实时处理状态：可看 IMU profile saved/not saved、Fusion 质量与当前滤波链路。`saved` 表示保存记录，不等同于绝对误差已验证；Calibration 内仍显示 Accel/Gyro/Fusion 等级。旧六面系数按 v1.3 设计仅用于加速度诊断。

新增 `node tests/reversal.test.cjs`；`node tests/mobile-ui.test.cjs` 同时验证两向采样、应用/取消/清除、相对零点、地图保存、断线/重启/校准变更失效，及芯片融合路径静态去抖、尖峰抑制、运动响应。合成噪声 SD 比约 0.401，不能当作实际精度提升。

---

# v1.3 · BNO086 内置六轴融合

使用新版 ESP32 mode=6 固件。蓝牙数据及显示以名义 20 Hz 更新；芯片六轴 Game Rotation Vector 替代旧 ESP32 互补融合。App 对芯片结果只做中值去尖峰和随运动调整的轻量低通，跳过旧的半秒均值与卡尔曼链路。保留 v1.2 的 X/Y 显示映射。

安装 dist/LiveTilt-debug.apk 后，在 Calibration 点 Start IMU calibration：静置几秒，在 4–6 个不同方向各保持几秒，再放稳。Accel、Gyro 均 High 且稳定后点 Save IMU calibration。校准保存在 BNO086 中。旧六面系数只影响诊断加速度；不校正芯片四元数。旧版固件仍可走旧显示路径。

实际转动和归位重复性需使用固定夹具验证。Saved profile 只表示保存记录，当前质量由 Accel/Gyro/Fusion 等级显示。下文 0.5 s 刷新及旧六面校准描述仅适用于历史版本。

---

# Live Tilt · Android

独立 Android App，直接通过 BLE 连接 **MAO_SLOPE**。不需要电脑转发、网页服务器或联网账号。实时测量和保存 GPS 点可离线使用；地图底图需要手机联网。支持 Android 8.0 及以上。

## 使用

1. 安装 `dist/LiveTilt-debug.apk`，打开 **Live Tilt**。
2. 接通 ESP32 电源，先退出电脑上的 `laptop/viewer.py`，让电脑释放 MAO_SLOPE 的蓝牙连接。
3. 手机开启蓝牙，在 App 点击 **Connect**，允许“附近的设备”权限。Android 8–11 还要求位置权限和系统 Location 服务，用于 BLE 扫描；GPS 地图记录另外申请精确位置权限。
4. 连接后显示 **Live · 6-axis**。角度、Grade 和 3D 每 0.5 秒刷新一次，后台持续处理所有帧。
5. 点击 **Set zero** 归零，**Reset zero** 恢复绝对值。临时零点不写入设备；关闭/重建 App 后需重新设置。
6. 校准在折叠的 **Calibration** 中：开始、六面逐一朝上停稳后选面、全部完成后保存。校准保存在 ESP32，六轴固件无需修改。

断线时撤下旧读数并自动重连。扫描中可点击 Cancel，连接后可点击 Disconnect。App 转入后台会断开 BLE；返回前台后恢复连接。仅在连接显示页保持屏幕点亮，不持续后台采样。

## GPS 自动记录与地图（v1.1）

1. 在 **01 · Measure** 点击 **Enable GPS recording**，允许“使用 App 时”的**精确位置**，打开手机 Location。拒绝后可在系统设置授权，再点 **Retry GPS**。
2. 连接传感器并停稳：连续至少 **3 秒**，每帧的 Roll 和 Pitch 都在各自窗口均值 **±0.2°** 内时，自动保存一次。检测使用未经手机显示平滑的融合/校正向量，保存窗口平均角度；显示算法保持原样。
3. 仅接受报告精度 **≤20 m**、年龄 **≤10 秒**的定位；等待 GPS 时不保存。保存经纬度、定位精度、时间、Pitch、Roll 和归零/校准状态；Grade 在查看时按 `100 × tan(Pitch)` 计算。
4. **02 · Map** 显示地图与彩色坡度点。可拖动、用 +/− 缩放、点标记或最近 100 条记录看详情；地图仍绘制所有已存点。**Latest point** 定位最后一个点，**My location** 定位当前有效 GPS。
5. 相对上一条记录，移动至少 `max(10 m, 两次定位精度)`，或任一角度变化 ≥0.5°，才保存新点。归零、恢复绝对值或校准变更会建立新的参考，避免混淆。
6. 记录写入手机本地存储，重启 App 后仍保留，最多 10,000 条；达到上限后保留旧记录并暂停新增。**Pause GPS recording** 停止定位；进入后台会暂停 GPS 与 BLE，回前台重新等待稳定与新定位。

GPS 来自手机，ESP32 固件与接线无需修改。App 启动后需手动开启本次 GPS 记录。室内可能一直等不到足够精确的定位。断网时已存点与列表仍可看，未缓存的底图无法加载。经纬度是坐标，不做街道地址反查。

地图使用 [OpenStreetMap 标准瓦片](https://operations.osmfoundation.org/policies/tiles/)，只加载当前视口，保留默认 HTTP 缓存和可见署名。瓦片请求会向地图服务器透露所查看的地图区域；记录本身不上传。定位按 [Android 前台精确位置权限](https://developer.android.com/develop/sensors-and-location/location/permissions) 实现。

## 与电脑版本一致的功能

- 三轴加速度 + 三轴陀螺仪融合来自现有 ESP32 固件。
- 中值去尖峰、0.5 秒滚动平均、自适应卡尔曼显示平滑。
- BNO086 显示已交换旧版 Roll/Pitch 数值：Pitch 随 X 倾斜变化，Roll 随 Y 倾斜变化；保留各数值原有符号。主 Pitch、Grade 和新 GPS 记录使用同一映射。已有地图记录保留原值。
- Grade = 100 × tan(显示的 Pitch)，角度和百分比均为两位小数。
- 3D 图的投影方向已调整：X/Y 在屏幕中的方向互换。校准面按钮仍对应传感器本体轴，不能按图示屏幕方向摆放。
- 当前界面保持英文和精简风格。

## 构建

这是标准的 Android Gradle 工程，原生 Java 处理 BLE，打包在 App 内的 HTML/Canvas 负责显示。不依赖手机浏览器的 Web Bluetooth。

用 Android Studio 打开本文件夹，安装 Android SDK 35，选择 JDK 17+（Gradle 9.1 支持本机 JDK 25），执行：

```sh
./gradlew :app:assembleDebug :app:lintDebug
```

APK 位于 `app/build/outputs/apk/debug/app-debug.apk`。Debug 签名供本地安装测试，不是 Play Store 发布签名。

使用命令行时，设置 `ANDROID_HOME` 为已安装的 SDK 路径，或在未提交的 `local.properties` 中设置 `sdk.dir=...`。构建需联网下载 Gradle/Google 官方依赖；App 的 INTERNET 权限用于 OpenStreetMap 地图底图。

## 测试

```sh
python3 tests/create_fixture.py
node tests/mobile-ui.test.cjs
node tests/tracking.test.cjs
mkdir -p tests/.classes
javac -d tests/.classes app/src/main/java/com/maoslope/livetilt/FrameAssembler.java tests/FrameAssemblerTest.java
java -cp tests/.classes com.maoslope.livetilt.FrameAssemblerTest
```

自动测试验证 STATIC2 二进制字段、丢片/乱序/超时/断流恢复、500 ms 刷新、归零和 Grade、X/Y 图示及滤波器。`VALIDATION.md` 记录实际完成的构建和验证范围。未连接真实 Android 手机之前，不能把桌面预览或自动测试当作手机 BLE 实测。

## 文件

- `app/src/main/java/.../BleClient.java`：扫描、连接、通知订阅、校准命令、重连和生命周期。
- `FrameAssembler.java`：严格校验 0xA6 分片，避免拼接跨帧数据。
- `MainActivity.java`：权限、原生蓝牙桥和加载本地界面及限定地图瓦片图片的 WebView。
- `app/src/main/assets/index.html`：当前显示算法及手机界面。
- `app/src/main/assets/tracking.js`：稳定检测、定位有效性与记录去重。
- `app/src/main/assets/map.js`：本地 Canvas 地图、瓦片、标记与拖动缩放。
- `app/src/main/assets/protocol.js`：136-byte STATIC2 解码，包括六轴融合标志。

BLE UUID、数据帧和命令与根项目现有固件一致。没有修改设备协议，也没有启用磁力计。

参考：[Android BLE 权限](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions)、[GATT 数据传输](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data)。
