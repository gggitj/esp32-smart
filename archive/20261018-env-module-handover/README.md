# ESP32 环境模块 固件（2026-10-18 运行版）

## 2026-10-19 通信修复工作说明

本目录是历史交接包，`flash/` 中的镜像不自动代表修改后源码的最新构建。

当前源码修复：固定 LAN 模式不初始化 BLE；`package_transport=data` 时不启动额外 HTTP 包服务器，插件包和业务数据都走认证 TCP。修复依据是实机在 LinkIt 握手后 Wi-Fi 初始化报 `ESP_ERR_NO_MEM`，Pi 随后报告 `Host unreachable`，尚未进入包下载和签名验证。

目标 Pi 当前配置固定访问模块 `192.168.50.2:4580`，因此本次私有构建同时使用该静态地址、`255.255.255.0` 掩码和 `192.168.50.1` 网关。不将 Wi-Fi 密码或会话 token 写入本说明。

物理供电识别和一天/一周趋势图为后续业务需求；MAX17043 可读不等于正在由电池供电，未确认 VBUS/充电芯片检测接线前不能把电压推断成真实供电来源。

### 本次实机通信验收

- 模块 USB 序列号 `5CF8047843`，ESP32-S3，16 MiB Flash；桥板未刷写。
- 已在工作区之外保存完整 16 MiB Flash 备份，刷写前比对原分区表与本次构建一致，仅更新 `0x10000` 应用分区。
- 模块重启时短暂释放 DET，再声明接入，以使主机观察到新插入而不是复用已关闭会话。
- 实测固定 Pi LAN 连接成功，`session_open` token 匹配，`session_ready` 返回成功，持续发布数据。
- Pi 实测完成包下载、签名验证、插件加载并进入 `ready`。
- 已在 Pi 真实桌面打开模块页，确认显示温度、湿度、气压、光照实时值。
- 当前仍为内嵌 `2.0.4` 原有页面；一天/一周趋势、电量页面和外部供电检测未在本次通信修复中完成，不能声称已经验收。
- Pi 当前是 labwc/Wayland + Xwayland，X 根窗口截图全黑不代表终端黑屏；实际画面用 Wayland 截图确认。

这是模块板 `5CF8047843` 当前实际运行的固件：**LCD 界面 + 三传感器 + 电量计 + 低压关机 + WiFi/BLE/LinkIt**。

### 验证状态

| 项目 | 状态 |
|---|---|
| 传感器采集（BME280/BH1750/MAX17043） | ✅ 实机验证 |
| 业务 JSON（含 battery_V / battery_pct） | ✅ 实机验证 |
| BLE / LinkIt / 桥板 PBR1 | ✅ 实机验证 |
| LCD 驱动（能出图、三色正确） | ✅ 实机验证 |
| LCD 新版布局（分级字号+光照条） | ⚠️ **已编译，尚未上机目视确认** |
| 树莓派仪表盘 QML | ✅ `qmllint` 通过 + 离屏渲染截图确认 |
| 树莓派插件 `.so` | ⚠️ 需在 Pi 上重编（见 §5.1）|
| 低压关机真实触发 | ⚠️ 仅验证启动逻辑，未做真实放电测试 |

---

## 1. 直接烧录（不需要编译）

| 文件 | 烧录地址 |
|---|---|
| `flash/bootloader.bin` | `0x0` |
| `flash/partition-table.bin` | `0x8000` |
| `flash/s3_pogo_device.bin` | `0x10000` |

```bash
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 flash/bootloader.bin \
  0x8000 flash/partition-table.bin \
  0x10000 flash/s3_pogo_device.bin
```

- 芯片：**ESP32-S3**，Flash **16MB**
- Windows 上端口通常是 `COM19`，Linux 上是 `/dev/ttyACM*`
- 烧录前核对序列号：模块板 = `5CF8047843`（桥板是 `5CF8045563`，别烧错）

---

## 2. 源码

```
source/esp/
  S3_Pi_Module/            模块固件（本次运行版）
    main/main.c            主入口、业务数据组包
    main/idf_component.yml LVGL 依赖声明
    components/lcd_st7789/ 屏幕驱动
    components/ui_env/     界面
    components/power_guard/低压关机
  S3_PiBridge_LinkIt_Host/ 桥固件（PBR1 控制面）
  shared/
    module_data_service/   TCP/BLE 业务数据服务
    pi_sensor_device/      BME280 + BH1750 + MAX17043 驱动（含软件 I2C）
  managed_components/lvgl__lvgl/  LVGL 8.4.0（已随包附带，无需联网）
```

构建（ESP-IDF **v5.5.4**）：

```bash
idf.py -B build build
```

首次构建不会再下载 LVGL，仓库里已经带了。

**注意**：`main/` 下必须有 `embedded_package.json` 和 `embedded_package.tar.zst`
（签名插件包，已随包附带，版本 2.0.4），否则构建会直接报错退出。

---

## 3. 硬件接线

### LCD（1.54" IPS 240x240 ST7789，8 脚）

| LCD 丝印 | 接到 ESP32-S3 |
|---|---|
| GND | GND |
| VCC | 3V3 |
| SCL | GPIO12 |
| SDA | GPIO11 |
| **RES** | **GPIO9** |
| **DC** | **GPIO8** |
| CS | GPIO10 |
| BLK | GPIO13 |

**DC 和 RES 与丝印是交叉的**——实测只有 `DC=GPIO8 / RES=GPIO9` 才能出图。
这是本模块的硬件事实，换屏要重新验证。

### 传感器

| 设备 | 接线 | 地址 |
|---|---|---|
| BME280 | SDA=GPIO38 SCL=GPIO39, VCC=3V3 | 0x76 |
| BH1750 | SDA=GPIO4 SCL=GPIO5, VCC=3V3, ADDR 悬空 | 0x23 |
| MAX17043 | `+`→电池正极，`−`/GND→地，SDA=**GPIO6** SCL=**GPIO7** | 0x36 |

MAX17043 用**软件 I2C**（两个硬件控制器已被 BME280/BH1750 占满）。
**它必须接电池，接 3.3V 或 5V 都读不出电量。**

---

## 4. 屏幕驱动参数（改动前请先读）

```
SPI2, mode 0, 1 MHz
两次面板初始化：先 dc=9/cs=10/rst=8，再 dc=8/cs=10/rst=9
invert_color = true
swap_xy = false, mirror = false/false, gap = 0/0
逐行 draw_bitmap，传输完成回调释放信号量后才复用缓冲
```

踩过的坑：

- 20MHz / 10MHz 在杜邦线上不稳定，**1MHz 才稳**
- 单次初始化不出图，必须两次，顺序不能反
- 整屏一次发 115KB 会黑屏
- LVGL 必须在同一任务里跑 `lv_timer_handler()` 并更新标签（非线程安全）
- 绘制缓冲 240×16 ×2；改大（如 40 行）会把堆吃光导致任务创建失败

---

## 5. LCD 本地界面（240x240）

深色背景，1 秒刷新。字号 32/24/14 三级层次：

```
ENV MONITOR                USB
─────────────────────────────
 24.2 C            ← 32px 白色，主数值
 52.7 %RH          ← 24px

─────────────────────────────
PRESSURE      1015.8 hPa
LIGHT         17 lux
████░░░░░░░░
DARK - turn a light on

─────────────────────────────
POWER         3.99 V   74%
```

- 光照分五档：`<50 DARK`、`<150 DIM`、`<500 GOOD`、`<1000 BRIGHT`、`>=1000 TOO BRIGHT`，
  与树莓派端的分级完全一致；进度条按对数刻度（1~2000 lux）
- 电池低于 3.50 V 数字变红；无电池显示 `BAT --  (USB)`
- **LCD 只能用 ASCII**：LVGL 自带的 Montserrat 没有中文字形，中文会显示成方块，
  所以屏上标签是英文

---

## 5.1 树莓派仪表盘（Qt 插件页）

`source/pi_terminal/modules/example/qml/ExamplePage.qml` 已重写：

```
环境监测                        [报文诊断]

┌──────────┐ ┌──────────┐ ┌──────────┐
│室内温度   │ │室外温度   │ │光照强度   │
│ 24.2 °C  │ │ 18.5 °C  │ │ 180 lux  │
│比室外+5.7 │ │上海 多云  │ │████░░    │
│          │ │ 12~22°C  │ │适中·适合  │
└──────────┘ └──────────┘ └──────────┘

┌──────────────────────────────────────┐
│ 最近 10 分钟温度       ─ 室内  ─ 室外 │
│                                      │
│        ╱╲___╱                        │
│  - - - - - - - - - - - - - - - -     │
└──────────────────────────────────────┘

湿度 52.7%   气压 1015.8 hPa   电池 3.99V 74%
```

功能说明：

| 功能 | 实现 |
|---|---|
| 室内温度曲线 | 每次收到数据记录一点，1 秒抽样兜底，保留 400 点 |
| 时间轴自适应 | 数据少时自动缩到 20 秒，多了逐步拉长，最多 10 分钟 |
| 室外对比 | **Open-Meteo** 公开 API（无需 key），10 分钟刷新一次 |
| 天气预报 | 当日最高/最低温 + 天气现象（晴/多云/雨…）|
| 光照评价 | 五档，带对数进度条，与 LCD 同阈值 |
| 网络不可用时 | 室外显示 `--`，室内曲线照常工作 |

**要改所在地**：`ExamplePage.qml` 顶部

```qml
property real weatherLat: 31.2304
property real weatherLon: 121.4737
property string weatherPlace: "上海"
```

插件版本已改为 **2.1.0**，manifest 的 `name` 改为「环境监测」。

### 树莓派上重编插件（必须做，否则看到的还是旧页面）

```bash
cd ~/Desktop/Multi-Function-ESP32-Debugger-master/pi_terminal
cmake --build build-native-arm64 --target example_module -j4
cp build-native-arm64/modules/example/libexample_module.so ~/pi_conf/pkgroot/
cp build-native-arm64/modules/example/manifest.json ~/pi_conf/pkgroot/
cp build-native-arm64/modules/example/qml/*.qml ~/pi_conf/pkgroot/qml/
```

然后按原有流程用 `package_tool.py` 签名打包，再嵌入 ESP：

```bash
python3 tools/package_tool.py build <包根目录> <输出目录> <私钥> aarch64
```

把输出的 `package.json` / `package.tar.zst` 改名为
`embedded_package.json` / `embedded_package.tar.zst` 放进 `S3_Pi_Module/main/`，
重新 `idf.py build` 并烧录。

> 天气依赖树莓派能访问外网。Pi 做热点时如果只有无线网卡，需要另接网线才能上网；
> 没有外网时天气区域显示 `--`，不影响室内曲线。

---

## 6. 低压关机

| 项 | 值 |
|---|---|
| 警告 | **3.50 V**（界面文字变红 + 日志） |
| 关机 | **3.40 V 持续 20 秒**（4 次 × 5 秒采样） |
| 动作 | 关背光 → 打印日志 → 深度睡眠 |
| 唤醒 | 每 **30 分钟**定时醒来复检；或按 RST |

**只在检测到电池时生效**（`battery_V` 为 `null` 时保持正常运行，因为那是 USB 供电）。

参数在 `components/power_guard/power_guard.c` 顶部：

```c
#define WARN_VOLTS        3.50f
#define SHUTDOWN_VOLTS    3.40f
#define SAMPLE_MS         5000
#define SHUTDOWN_SAMPLES  4
#define SLEEP_WAKE_S      1800ULL
```

---

## 7. 业务数据（给软件端）

模块身份：

| 项 | 值 |
|---|---|
| module_id | `80` |
| module_instance | `pogo-device-test` |
| token | `ESP_TEST_TOKEN` |
| TCP 端口 | `4580` |
| BLE 名称 | `POGO_DEVICE_TEST` |

TCP 帧：`大端 u32 长度(JSON+4) | JSON | 小端 u32 CRC32(ISO-HDLC)`

JSON 字段：

```json
{"source":"s3_sensor_module","heartbeat":1,"linkit":"wired-compatible",
 "temperature_C":24.35,"humidity_RH":51.84,"pressure_hPa":1015.69,
 "light_lux":17.5,"battery_V":3.99,"battery_pct":74}
```

- 传感器无效时对应字段是 **`null`**（不是 0）
- 无电池时 `battery_V` / `battery_pct` 为 `null`
- 传感器首条报文可能全为 `null`（采样尚未完成），属正常

详细协议见桌面 `PI_HANDOFF_20261017.md`。

---

## 8. 当前限制

- app 分区只剩约 **100KB（6%）**，再加功能需要改分区表
- TCP 单客户端
- token 是测试值，生产必须更换
- 低压关机的真实触发尚未实测（只验证了启动逻辑）
- LCD 显示是本地功能，不进入业务 JSON
