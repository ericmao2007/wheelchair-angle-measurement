# v1.5：一位小数 / 延长平均（2026-10-30）

Android 角度/Grade 显示改为一位小数，内部精度保留。BNO086 静止显示增加 1.5 秒滚动向量平均，运动时绕过长窗。对向测量、GPS 稳定采样和 ESP32 稳定窗口从 3 秒改为 5 秒。校准逻辑保留。详见 [App 说明](android-app/README.md)。App 更新与固件烧录分别生效，当前构建不能代替实机漂移排查。

---

# App v1.4：对向测量估计 Pitch 零偏（2026-10-30）

Android App 新增 **180° reversal zero · Pitch**。在同一坡面、同一贴地面和相同接触位置，朝下/朝上各采集至少 3 秒稳定六轴角度，计算 `零偏=(角度1+角度2)/2`、`坡度=|角度1-角度2|/2`，确认后修正 Pitch、Grade、3D 和新 GPS 记录。原有 IMU 校准与显示滤波保留，固件无需更新。详情见 [Android 使用说明](android-app/README.md)。

零偏按本次连接生效；断线、传感器重启或校准变化需重做。六轴无航向，需人工确保沿坡面调头 180°，不能上下翻面。此功能估计纵向零偏，不修正比例误差、任意安装旋转或横向 Roll。主界面增加校准保存记录和滤波链路状态。

---

# BNO086 内置六轴融合修正（2026-10-28）

本节替代下文旧的自写 TiltFusion 路径。主姿态使用 `SH2_GAME_ROTATION_VECTOR`（100 Hz 请求），由 BNO086 内部融合三轴加速度和三轴陀螺仪，不启用磁力计。归一化四元数投影世界 +Z 到传感器坐标得到重力方向，再应用 AXES 安装映射；不会对四元数欧拉角再做陀螺积分。加速度、已校准陀螺仪仍以 50 Hz 请求用于运动/校准诊断；gyro 的 rad/s 转 °/s 保留。I²C 改为 400 kHz，接线不变。报告过期 120 ms 时读数无效。

BLE 发送和 Android v1.3 刷新均为名义 20 Hz；芯片内部融合报告请求为 100 Hz。App 在芯片融合模式跳过旧 0.5 s 短窗及随机游走卡尔曼：静止时三帧中值抑制孤立尖峰，再用时间常数 0.20 s 的低通；gyro 大于 1°/s 时绕过中值，改为 0.035 s 低通。无死区、无锁定上次角度；不会把平滑等同于绝对精度。

在新版 App 的 Calibration 中点击 Start IMU calibration：先静置几秒，再在 4–6 个不同姿态各保持几秒，最后静置于稳固支撑。Accel/Gyro 均为 High 且稳定窗口通过后，Save IMU calibration 才可点击；保存到芯片 DCD。正常测量启用陀螺仪动态校准；加速度动态校准只在显式校准期间开启，成功保存后关闭。不能把开机时任意姿态自动设成水平。Set zero 仅作相对零点。

原六面 NVS 系数仍用于诊断加速度，不会应用到芯片融合四元数；新版 App 在 mode=6 时提供真正的芯片校准，隐藏旧六面入口。flags bit64 表示芯片校准中、bit128 表示有本固件保存 DCD 的记录（不代表当前精度已达 High）；mode=6 表示芯片 Game Rotation Vector。status 低两位为融合质量，中间两位为加速度质量，接下来两位为 gyro 质量，均为 0 Unreliable / 1 Low / 2 Medium / 3 High。error=1 表示报告失效。metadata 后缀为 `:SH2`。

姿态转换、App 映射/校准指令/运动跟随及数据中断测试通过，固件和 APK 编译与 lint 通过。真实归位误差仍需同一夹具和参考角对比；这些测试不能证明绝对精度。

依据：[CEVA BNO08X 数据手册 §2.2.2](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf)、[SH-2 校准 API](https://github.com/adafruit/Adafruit_BNO08x/blob/master/src/sh2.h)。

---

# 当前硬件：ESP32 + BNO086（2026-10-28）

固件现使用 Adafruit BNO08x 1.2.5 的 SH-2/SHTP 报告接口，不再访问 BNO055 寄存器。以 20,000 µs 周期订阅包含重力的 `SH2_ACCELEROMETER`（m/s²）及 `SH2_GYROSCOPE_CALIBRATED`（rad/s，固件转换为 °/s）。这里 raw 指应用六面校准前的加速度报告，不是 ADC 原始计数。仍运行 ESP32 上的 TiltFusion；未采用芯片内置 rotation vector。磁力计未订阅。

## BNO086 接线（当前 esp32dev 配置）

先断电，按模块实际丝印连接；下表适用于带供电外围电路的 3.3 V BNO086 breakout，不是裸芯片引脚表。

| BNO086 模块 | ESP32 |
|---|---|
| 3V3 / 3.3V | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |
| RST / NRST | GPIO19 |
| INT | 此 I²C 驱动使用轮询，无需连接 |

模块应设置为 I²C：PS0=0、PS1=0。SparkFun Qwiic 模块默认如此，无需改焊；其他模块按其原理图配置。BOOT 保持模块默认高电平，不能拉低进入 bootloader。SA0/ADR 选择地址，固件自动尝试 0x4B 和 0x4A。保持 SDA/SCL 上拉到 3.3 V；SparkFun 模块已有上拉。不要给信号引脚接 5 V。只有 VIN 丝印的其他型号模块，需先核对其供电说明，不能假定 VIN 等同 3V3。

默认 I²C 100 kHz。GPIO 可在 platformio.ini 修改；若模块未引出 RST，可将 BNO_RESET 改为 -1 使用软件复位，但建议接上复位线。BNO08x 的 I²C 时序较敏感，短线共地；实机若反复掉线，需要排查接线/上拉，必要时另改 SPI。

## 烧录与检查

```sh
/Users/ericmao/.platformio/penv/bin/pio run
/Users/ericmao/.platformio/penv/bin/pio run --target upload
/Users/ericmao/.platformio/penv/bin/pio device monitor --baud 115200
```

启动日志应出现 `BNO086 init=1`，后续 `ok=1`。设备名仍为 MAO_SLOPE，网页及 BLE UUID 不变。metadata 为 `STATIC2:ACCGYRO:FUSION6:BNO086`。报告间断超过 120 ms 撤下有效读数，持续缺失会重新初始化；芯片报告复位后重新订阅。

换传感器后必须重新六面校准。BNO086 使用独立 NVS namespace `bno086-static`、key `sixaxis-v1`，不会套用旧 BNO055 的系数。模块坐标按新板的 X/Y/Z 丝印确认，安装变更可调整 include/inclinometer.h 的 AXES。

保留 136-byte STATIC2 格式。mode=5 是应用六轴兼容标签；status=6/error=0 表示有效报告，status=0/error=1 表示报告缺失，不再代表 BNO055 的硬件寄存器。当前不读取温度，temperature=-128 表示不可用，Python bridge 解码为 null。ESP32 编译通过；实机通信、校准及动态精度尚未验证。

参考：[Adafruit BNO08x 驱动](https://github.com/adafruit/Adafruit_BNO08x)、[SparkFun BNO086 硬件说明](https://docs.sparkfun.com/SparkFun_VR_IMU_Breakout_BNO086_QWIIC/hardware_overview/)。

---

以下为 BNO055 历史记录，其中寄存器、接线、温度及模式配置不适用于当前 BNO086 固件；通用测量、网页和六面校准说明供参考，冲突以本文顶部为准。

# 当前版本：六轴实时倾角仪（2026-10-21）

默认模式已改为 **ACCGYRO**：三轴加速度 + 三轴陀螺仪。ESP32 在名义 50 Hz 运行 TiltFusion 六轴互补倾角观测器：陀螺仪预测重力方向，加速度反馈约束倾角漂移并估计可观测的陀螺仪偏置。磁力计未启用，无绝对航向。陀螺仪配置 ±2000°/s、23 Hz 带宽、normal power。六面校准仍作用于加速度，保存在原 NVS 中。

网页继续使用中值/短窗平均/自适应卡尔曼进行显示平滑；有明显角速度时绕过慢速短窗并提高增益。主界面保持简洁，显示角度、Grade、Roll/Pitch、3D 和归零；已有轴对调和正负反转保留。连接状态 **Live · 6-axis** 仅在收到真实的有效融合帧时出现。

BLE 保持 136-byte STATIC2 外形和 0xA6 分片。新增 flags bit 32 表示原 filtered 三轴槽为融合重力向量（m/s²）；metadata 为 STATIC2:ACCGYRO:FUSION6。bridge 将其明确解码为 fusion_accel，并把 filtered_accel 留空，避免把融合结果冒充原加速度低通。旧协议仍可读取，未带融合标记时回退加速度显示。CSV 新增 has_gyro、has_fusion 和 fusion_accel 三轴字段。

验证：tests/tilt_fusion_test.cpp 覆盖转动方向、偏置、翻转/断流/重启恢复；tests/test_static_protocol.py 覆盖融合字段；tests/live_filter_test.js 覆盖显示平滑与陀螺仪辅助跟随。六轴融合不能消除持续平移加速度、安装误差和温漂；真实动态精度、长期漂移及归位重复性仍需受控实验。下文为此前静态方案的详细参考，遇到模式、融合和显示行为冲突时以本节为准。

---

# ESP32 + BNO055 静态重力倾角仪

主测量在 ESP32 上完成：读取 **包含重力的加速度寄存器**，校正三轴零偏/比例，映射到设备坐标，对三轴向量低通并做稳定窗口平均，最后计算倾角。没有融合欧拉角、融合 gravity、linear acceleration 或陀螺仪积分进入主测角。旧 Kalman/filter 文件仅保留为历史测试，不参与新固件。

默认 **ACCONLY（0x01）**。可将 `platformio.ini` 中 `BNO_MEASUREMENT_MODE` 改为 `OPERATION_MODE_ACCGYRO`（0x05），仍无姿态融合，只将 gyro 用于运动检测。ACCONLY 没有航向或陀螺仪静止判据。模式在启动及采样时校验；`SYS_STATUS=6` 表示无融合运行。系统检查 I²C 完整读数、单位寄存器、工作模式和传感器健康。失败帧不作为测量。

## 接线与启动

BNO055 breakout 必须支持 3.3 V 供电/逻辑，按实际模块标签连接；默认 SDA→GPIO21、SCL→GPIO22、GND 共地，I²C 模式，短线，适当的 3.3 V 上拉。固件探测 0x28/0x29。断电后接线。

```sh
/Users/ericmao/.platformio/penv/bin/pio run
/Users/ericmao/.platformio/penv/bin/pio run --target upload --upload-port /dev/cu.usbserial-0001
python3 -m venv .venv
.venv/bin/python -m pip install -r laptop/requirements.txt
.venv/bin/python laptop/viewer.py --csv diagnostics/measurement-01.csv
```

打开 http://127.0.0.1:8765；设备名 MAO_SLOPE。macOS 为终端/编辑器开启蓝牙权限。只运行一个 BLE bridge。`--no-browser`、`--port 8766` 可用。`--demo` 为明确标注的模拟数据，不能验证传感器。

界面显示原始/校正/低通/稳定均值加速度、|a|、窗口噪声及方向变化、温度、校准系数、角度及坡度百分比。网页在移动中使用校正加速度持续更新 3D、roll/pitch 和坡度，标为“实时估计”；主角度与 3D 始终采用最新校正加速度，无论运动或静止都不切换为稳定窗口平均；稳定窗口仅用于诊断、校准和历史结果。实时估计不需要等待停稳，但平移加速度/振动会影响角度，不代表动态姿态精度已验证。数据故障、过期或六面采样时撤下当前结果；历史稳定值带“上次有效测量”与时间。没有航向显示。初次未校准可以观察原始表现，但标为未校准估计。

## 测量管线与配置

`src/main.cpp` 直接读取 page 0 `ACC_DATA 0x08..0x0D`，单位 `m/s²`（100 LSB/(m/s²)）；ACCGYRO 时读取 `GYR_DATA 0x14..0x19`，单位 `deg/s`（16 LSB/(deg/s)）。明确设置 `UNIT_SEL=0`，identity axis map。page 1 设置加速度 ±2g、31.25 Hz 带宽、normal power，然后恢复 page 0。主采样名义 50 Hz，BLE 名义 10 Hz；实际频率取决于 I²C/BLE 调度。

集中参数在 [include/inclinometer.h](include/inclinometer.h) 的 `Config`：

| 参数 | 默认 | 含义 |
|---|---:|---|
| lowpassSeconds | 0.35 s | 三轴向量一阶低通，设 0 禁用 |
| stableSeconds | 3 s | 连续稳定窗口，建议 2–5 s |
| settleSeconds | 1 s | 运动/不稳定后重新等待 |
| maxGap | 0.12 s | 超时清空窗口 |
| normTolerance | 0.8 m/s² | 与 9.80665 的差；为未校准观察保留余量 |
| noiseLimit | 0.08 m/s² | 窗口校正向量相对均值的 RMS |
| driftLimit | 0.08° | 窗口首末各 1/3 均值方向夹角 |
| accelStep | 0.25 m/s² | 相邻校正向量突变 |
| gyroInstant | 5°/s | 可用时的瞬时角速度门限 |
| gyroMean / gyroRms | 0.3 / 1.5°/s | 可用时的窗口角速度门限 |

环形缓冲 260 个样本，支持名义 50 Hz 下 2–5 s 窗口。采样间断、运动或不稳定会丢弃整窗，重置低通，等待后从当前位置重新收集，避免带入前次移动尾迹。只平均向量，不平均欧拉角，不锁定上次角度，不自动扣除归位差。噪声窗口重叠且低通样本相关；窗口波动/方向变化不是绝对精度或置信区间。模长接近 1g 只是多个条件之一；持续加速度、极慢运动仍可能通过静止判据。

## 六面校准与 NVS

1. 让设备温度稳定，使用可重复、平整且互相垂直的六面支撑。传感器本体必须固定，避免导线拉动。
2. 点击“开始新的六面采样”。此时只用原始传感器数据收集，暂不应用旧六面系数；主正式测量暂停。
3. 分别让**传感器坐标** +X、−X、+Y、−Y、+Z、−Z 朝上。每面停稳，界面到“稳定测量”后点击对应按钮。方向错误或窗口未稳定会拒绝采样。
4. 六面全采后点击保存。每轴 `bias=(plus+minus)/2`，`scale=2g/(plus-minus)`；应用 `corrected=(raw-bias)*scale`。只使用对应轴正负两个面的均值，估计三轴零偏和对角比例。
5. 保存至 ESP32 NVS namespace `bno-static`、key `sixaxis-v1`。检查 UI 校准状态/系数，重启后应恢复；不依赖 BNO055 融合校准的 0–3 等级，不将旧 `bno-slope/offsets` 融合 profile 导入本管线。清除按钮只清除此六面 profile。
6. 保存前系数范围/完整性不符会拒绝；保存失败不会替换当前有效校准。旧校准在取消新采样后继续使用。

六面校准不能完整修正轴不正交、任意安装旋转、温度系数或支撑面误差。未用参考夹具确认的“六面”不保证 ±0.1°。

## 角度、平面与相对零点

设备坐标默认等于传感器坐标。`AXES={1,2,3}` 可改为带符号的轴排列，应保持右手坐标。先在传感器坐标应用校准，再映射到设备坐标。XY 是测量平面，+Z 为正面法向。

对稳定平均向量 `(x,y,z)`：

- roll = `atan2(y,z)`，[-180°,180°]，正值表示 +Y 抬高；倒置时接近 ±180°。
- pitch = `atan2(-x,hypot(y,z))`，[-90°,90°]，正值表示 −X 抬高。
- 平面总倾角 = `atan2(hypot(x,y),abs(z))`，[0°,90°]；倒置水平仍是 0°，UI 另标注正面向下。
- X/Y 坡度 = `100*x/hypot(y,z)` / `100*y/hypot(x,z)`，正号表示沿设备正轴上升。
- 总坡度百分比 = `100*hypot(x,y)/abs(z)` = `100*tan(总倾角)`；竖直无有限值。

图形从重力方向重建倾斜示意，航向未测量，不赋予绝对 heading 含义。

“相对归零”只记录当前重力方向，另外显示两方向夹角 [0°,180°]；不会更改上方绝对结果、传感器系数或 NVS。页面重载清除相对零点；开机绝不把当前坡度自动设为水平。

## 长时静置与翻转归位诊断

启动 `--csv` 后持续写 CSV，目标文件必须是新路径（防止覆盖旧日志），Ctrl+C 会关闭/flush；旁边 `.meta.json` 记录单位、模拟标志和分析边界。固件串口 115200 每秒输出状态、模长、窗口噪声/方向变化、角度、温度、health；校准命令输出结果。

**静置 10–30 分钟：**固定于不变的定位底座，保持同一姿态，不操作归零；记录温度变化。启动一次会话即可连续写入，不需人工保持脚本轮询。每分钟稳定均值的变化、窗内波动、整段稳定结果的峰峰值分别报告。不要把拿起/放回的不同姿态混到“静态漂移”里。

**反复翻转后归位：**离开底座前在页面记录 `moving`，放回同一底座、同一方向后记录 `return-1`，等待稳定并保持约 10–20 秒；重复 `moving` / `return-2` 等。标记按收到的下一帧落盘，并持续归组到下一个标记。标记及稳定片段编号用于分析，片段被噪声打断不等于一次真实拿起；每次归位必须人工命名。比较各 return 的稳定均值、相对首次最大差、均值范围、短期 SD、恢复时间和温度。恢复时间从接收端第一次无效结果到恢复稳定计时，含连接/校准等待，并非真实机械落座时刻的精确测量。

```sh
.venv/bin/python laptop/analyze_log.py diagnostics/static-session.csv --output diagnostics/report.json
```

报告包含每分钟统计、人工归位组、自动稳定片段及恢复时间；roll 使用圆周均值/包裹偏差，避免 ±180° 分支平均错误。没有外部参考仪器时 `absolute_error` 始终为 NOT MEASURED。统计只描述噪声、时间变化和重复性；不把 Raw/Filtered 差或短期 SD 当成绝对误差。温度是 BNO055 内部读数，非校准环境温度。

## 协议与检查

保留服务/notify/control/metadata UUID。新 metadata 为 `STATIC2:ACCONLY` / `STATIC2:ACCGYRO`。STATIC2 是 136-byte little-endian `<II15f9f6fb7B`：序号/uptime；raw/corrected/filtered/gyro/mean 五个三轴向量；模长/噪声/方向变化/roll/pitch/slope/gradeX/gradeY/grade；bias/scale；温度；flags/state/faces/mode/status/error/calResult。无效角度为 NaN，由 bridge 转 JSON null。flags：1读数有效、2校准采样中、4有NVS校准、8有gyro、16稳定。每包 `[0xA6,seqLow,index,count]` + 最多16字节，9片。bridge 检查长度/顺序/版本/超时，不拼接跨帧；旧 0xA5 协议仍可解码诊断，但不显示为正式静态测量。

命令 `CALSTART`、`F0`..`F5`、`SAVE`、`CALCANCEL`、`CLEAR`。结果 1未稳定、2方向不符、3六面未齐/系数无效、4NVS失败、5成功、6取消、7清除。

```sh
.venv/bin/python -m unittest discover -s tests -v
c++ -std=c++17 -Wall -Wextra -I include tests/inclinometer_test.cpp -o /tmp/prototype-inclinometer-test
/tmp/prototype-inclinometer-test
/Users/ericmao/.platformio/penv/bin/pio run
```

验证记录见 [diagnostics/VALIDATION.md](diagnostics/VALIDATION.md)。实机六面校准、长期漂移及归位重复性必须实际完成，不能用合成测试替代。

驱动依据：[Bosch BNO055 数据手册](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bno055-ds000.pdf)、[Adafruit BNO055 驱动](https://github.com/adafruit/Adafruit_BNO055/blob/master/Adafruit_BNO055.cpp)。

## 连续网页滤波（2026-10-21）

网页主角度、Grade 和 3D 使用三帧中值去尖峰、0.5 秒连续向量平均，再对三轴校正加速度进行自适应随机游走卡尔曼滤波。静止过程噪声已进一步降低；较大持续动作自动提高跟随增益，小变化持续跟随但存在平滑延迟。主数字现显示带正负的 Pitch（前后倾斜），Grade = 100 × tan(Pitch)，与归零使用同一向量；正值显示 +，负值显示 −，Roll 在下方独立显示。每个 BLE 新帧仅更新一次，不随网页重绘重复滤波；静止时加强平滑，持续运动时提高跟随增益。不等待稳定窗口，不冻结零附近的小角度变化。这里不是陀螺仪姿态 EKF，ACCONLY 没有 gyro 数据用于预测。

Set zero 使用当前滤波方向；随后角度、坡度、3D 和历史曲线采用相同相对坐标。清除零点恢复绝对显示，页面重载清除零点。校准状态或系数改变时重置滤波与相对零点，避免混用不同校准坐标；断流、设备重启及无效帧后重新初始化。原始 CSV 与固件稳定结果不受网页滤波影响。

调参在 laptop/index.html 的 LiveVectorFilter。tests/live_filter_test.js 覆盖合成噪声、阶跃跟随、孤立尖峰、慢速转动与异常恢复。diagnostics/live-filter-validation.json 保存测试与已有稳定数据片段回放结果；这些检查不构成绝对精度、长期漂移或真实动态姿态验证。更强平滑会增加响应延迟，平移加速度和温漂仍可能产生角度偏差。

网页 Roll / Pitch 已按反向安装需求互换；主数字显示互换后的 Pitch（原 Roll），Grade 同步按该方向计算。

界面角度、Grade 和 3D 每 500 ms 刷新一次（2 Hz）；后台仍逐帧处理 BLE 数据，ESP32 六轴融合采样不变。Set zero / Reset zero 立即更新显示。降低刷新频率不消除滤波收敛延迟。
