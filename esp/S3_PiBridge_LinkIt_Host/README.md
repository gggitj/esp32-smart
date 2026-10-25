# 桥板固件(S3_PiBridge_LinkIt_Host)与 RC522 NFC 读卡

本工程是 ESP32-S3 桥板固件:通过板载 CH343 USB-UART 以 PBR1 二进制帧与电脑/树莓派通信,通过 UART1 承载 LinkIt 模块通道。本轮在原有固件上加入 RC522 ISO14443A 读 UID 功能并实机验证;只修改 ESP32 固件,树莓派端程序未修改。

## 当前验证状态(2026-10-25)

已在本机 ESP-IDF 5.5.4 完整构建并烧录桥板:bootloader、分区表、应用三段写入均通过 esptool hash 校验。复位后 PBR1 `status` 往返正常;RC522 在线(版本寄存器 0x92),已在串口监听中捕获真实 4 字节卡 UID 的 `rfid_card` 事件,PBR1 CRC 正确,固件累计成功上报计数由 8 增至 9。历史累计 `read_errors` 存在,捕获时 `last_error=0`、`tx_failures=0`。本地验证记录保存在工程下 `verification/`(运行输出,已被 git 忽略,不入库)。

尚未完成:树莓派端接收、Pi 页面展示、两张卡/持卡/移卡完整回归、模块板重新接回后的 LinkIt 插拔回归、MAX17043 电量读取。软件单元测试通过不代表上述实机项已通过。

## 接线和 GPIO

以下编号是开发板上标注的 **GPIO 编号**,不是排针序号。断电接线,RC522 使用 **3.3V**,共地。

| 用途 | 模块引脚 | 桥板 GPIO | 说明 |
|---|---|---|---|
| RC522 | SDA / SS | 10 | SPI 片选,不是 I²C SDA |
| RC522 | SCK | 12 | SPI 时钟 |
| RC522 | MOSI | 11 | ESP32 → RC522 |
| RC522 | MISO | 13 | RC522 → ESP32 |
| RC522 | RST | 14 | 复位 |
| RC522 | 3.3V / GND | 3V3 / GND | IRQ 不接 |
| MAX17043 预留 | SDA | 8 | I²C,后续驱动使用 |
| MAX17043 预留 | SCL | 9 | I²C,后续驱动使用 |
| MAX17043 预留 | ALRT(可选) | 7 | 不用报警中断时可不接 |
| 已有 LinkIt | TX / RX / DET | 17 / 18 / 21 | 保持原接线 |
| 已有电脑/Pi 通道 | UART0 TX / RX | 43 / 44 | 已接板载 CH343,无须再跳线 |

RC522 走 SPI3、模式 0、1 MHz。共分配/预留 13 个信号 GPIO;GPIO1、2、4、5、6、15、16、38~42、47 仍未分配,后续使用需核对具体开发板引出情况。GPIO19/20 留给原生 USB;GPIO26~37 是板上 Flash/PSRAM;GPIO0/3/45/46 为启动配置脚;GPIO48 按板载灯预留;ESP32-S3 没有 GPIO22~25。`main/bridge_pins.h` 在编译期检查 RC522 引脚不落入上述禁区,且不与 MAX17043 预留脚、LinkIt 脚、UART0 脚冲突。

MAX17043 目前**只有引脚预留,没有电量读取驱动**,固件未启动 I²C。RC522 仅支持 ISO14443A / 13.56 MHz 卡的 UID 读取(通常一次放一张卡),不支持其他 NFC 协议或 125 kHz 卡,不读写卡片内容。

## 桥板到电脑/树莓派通道

沿用板载 CH343 的 USB-UART:**115200、8N1、无流控**;固件不切换到 ESP32 原生 USB。Windows 上桥板枚举为某个 COM 口,每次操作前需重新核对端口与 CH343 序列号。

树莓派上用 USB 线连接同一 UART 口,建议按 USB 序列号稳定识别设备,而不是可能变动的 `/dev/ttyUSB*`:

```sh
ls -l /dev/serial/by-id/
# 桥板 CH343 的路径形如(序列号为占位符,以 ls 实际输出为准):
# /dev/serial/by-id/usb-1a86_USB_Serial_<CH343-SERIAL>-if00-port0
```

固件运行时关闭主、次控制台,PBR1 独占 UART0。sdkconfig 保留默认 `CONFIG_BOOT_ROM_LOG_ALWAYS_ON=y`,不烧写关闭 ROM 日志的 eFuse;复位时 UART0 上仍可能出现 ROM 启动文字噪声,接收端必须按 `PBR1` 帧头重新同步(见下文)。

## PBR1 帧格式(完整规范)

UART0 上承载的是 PBR1 二进制帧,总长 14+N 字节:

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 4 | 魔数 | ASCII `PBR1`(`50 42 52 31`) |
| 4 | 1 | direction | `1` = 桥板→主机(事件);`0` = 主机→桥板(命令) |
| 5 | 1 | kind | `1` = JSON 事件;`2` = JSON 命令;`3` = JSON 错误 |
| 6 | 4 | sequence | 无符号整数,小端;每方向独立递增 |
| 10 | 2 | payload 长度 | 无符号整数,小端,最大 2048 |
| 12 | N | payload | UTF-8 编码 JSON,N ≤ 2048 |
| 12+N | 2 | CRC16 | 小端 |

CRC16/CCITT-FALSE:多项式 `0x1021`,初值 `0xFFFF`,输入输出均不反射,异或输出 `0`。覆盖范围是 **direction 至 payload 末尾**(偏移 4 至 12+N),不含魔数和 CRC 自身。

接收端(树莓派)必须处理:

- **启动噪声**:复位后 ROM 文字先于帧出现,按魔数 `PBR1` 扫描重同步,丢弃噪声字节;
- **半包/粘包**:按帧头长度字段组装完整帧再校验,流式解析不能假设一次读到整帧;
- **CRC 错帧**:丢弃坏帧及其残留,从下一个魔数位置恢复,不要让一个坏帧阻塞后续帧;
- **序号重置**:桥板重启后 sequence 从 1 重新开始,不要因序号回绕/变小而丢帧;同向序号应单调递增,重复序号帧可丢弃;
- 主机→桥板命令帧使用相同封装(direction=0),桥板对 `ping`/`status` 命令回复 JSON 事件。

`tools/pbr1_tool.py` 是该协议的 Python 参考实现(编解码、串口 listen/send/verify、selftest),其中的 `iter_frames()` 展示了带噪声恢复的流式解析做法,树莓派端可直接参考或移植。

## `rfid_card` 事件

下面是**协议示例(合成占位数据),不是实机读卡结果**:

```json
{"event":"rfid_card","schema":"terminal-rfid-v1","reader_id":"front-reader","boot_id":"0123456789abcdef","uid":"04A1B2C3D4E5F6","uid_len":7,"uptime_ms":12345}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `event` | string | 固定 `rfid_card` |
| `schema` | string | 固定 `terminal-rfid-v1` |
| `reader_id` | string | 读头标识,来自 `CONFIG_BRIDGE_RFID_READER_ID`,默认 `front-reader` |
| `boot_id` | string | 每次启动随机生成的十六进制串,区分启动周期 |
| `uid` | string | **大写十六进制字符串,保留前导零**;必须按字符串处理,不能转整数 |
| `uid_len` | int | UID 字节数,取值 4 / 7 / 10 |
| `uptime_ms` | int | 本次启动后的毫秒数 |

`rfid_card` 是独立的读卡事件,**不依赖 LinkIt 模块 attach 状态,不冒充任何 module_id,不走外部模块的 BLE/LAN 业务通道**。树莓派端需保留现有 attach/detach 处理逻辑不变,单独按 `event == "rfid_card"` 路由;不要把所有 event 都当作模块插拔。结合 `boot_id` 与 `reader_id` 区分启动周期与读头。UID 只是识别信息,**不是安全认证凭据**,不能直接当作 Linux 登录或门禁认证使用。

原有 `ping`/`status` 的 JSON 回复新增 `rfid` 对象:

```json
{"event":"bridge_status","request_sequence":100,"attached":false,"module_id":101,
 "rfid":{"enabled":true,"online":true,"version":146,"reports":9,
         "read_errors":8,"tx_failures":0,"last_error":0}}
```

`enabled` 是否编译进读卡服务;`online` 读卡器当前是否应答(未接读卡器时 `online=false` 属预期,不影响 `ping`/`status` 回复);`version` RC522 版本寄存器(0x92=146);`reports` 本次启动累计成功发送的读卡事件数;`read_errors` 启动以来累计读错误(含历史值,不表示当前故障);`tx_failures` 发送失败计数;`last_error` 最近一次错误码,0 表示无。

## 读卡行为

读卡任务优先级 3(低于 LinkIt 的 8),每轮轮询间隔 100 ms。同一张卡持续放置在 天线上只上报一次;连续确认无卡 400 ms 后重新允许该卡上报(实际移开约 0.5 秒再放回即可再次触发);RF 错误、碰撞和读卡器掉线不计入移卡确认;不同 UID 可直接切换上报。RC522 缺失/掉线时每约 2 秒重试初始化,不影响 LinkIt 任务和状态回复。

固件内 LinkIt、状态回复与 RFID 共用一把发送互斥锁,保护全局序号分配和完整帧写入,两个任务并发发送时帧不会交错。

## 构建与软件检查

构建脚本为本机路径做了默认值,可用参数覆盖:`.\build.ps1 -IdfPath <ESP-IDF 路径> -ToolsPath <.espressif 路径>`。

```powershell
.\build.ps1
& 'S:\esp\.espressif\python_env\idf5.5_py3.10_env\Scripts\python.exe' .\tests\run_host_tests.py
```

- `sdkconfig` 被 git 忽略,新克隆中不存在;`build.ps1` 首次构建会直接从 `sdkconfig.defaults` 生成配置,构建成功后再把最终配置写回工程目录的 `sdkconfig` 供后续复用。
- 构建在英文临时暂存目录进行,规避本机 CMake/Kconfig 的中文路径兼容问题;暂存目录不删除,便于排查。
- 构建后脚本强制校验最终配置:NFC 选项存在、`CONFIG_BOOT_ROM_LOG_ALWAYS_ON=y`(拒绝任何关闭 ROM 日志的方案)、主/次控制台均为 `NONE`(拒绝任何会占用 UART0 的控制台方案),校验不过则不导出产物。
- 产物导出到 `artifacts/`(git 忽略):三份二进制、ELF、`sdkconfig.verified`、改写为平铺相对路径的 `flasher_args.json` 和 `sha256.json`。
- 修改 NFC 开关、引脚、`reader_id`、轮询/移卡时间:改工程 `sdkconfig` 中 `CONFIG_BRIDGE_RFID_*` 项;初始默认值见 `main/Kconfig.projbuild`。
- host tests 编译真实驱动 C 代码(模拟 SPI 响应验证 UID 级联、CRC-A/BCC、碰撞、超时、掉线恢复、去重),用两个 Windows 线程测试并发 PBR1 发送(200 帧 CRC/序号),并用 `pbr1_tool.py` 回验,再以正反例编译检查引脚守卫。需要本机 MinGW GCC(默认 `S:\Qt\Tools\mingw1310_64\bin\gcc.exe`,可用 `--cc` 指定)。软件测试不打开串口,也不能代替接线后的实机测试。

## 烧录参数

烧录 `artifacts/` 三份二进制:bootloader `0x0`、分区表 `0x8000`、应用 `0x10000`;ESP32-S3、DIO、16 MB Flash、80 MHz。始终显式指定桥板串口,不执行全片擦除,不写 eFuse。

## 实机检查流程

1. 断电核对 RC522 的 3.3V、GND 和五根 SPI/RST 信号(IRQ 留空),以及桥板串口身份。
2. 烧录后用 `pbr1_tool.py` 发 `status`,确认 `rfid.online=true`、`version=146`;读卡器未接时应离线但 `ping`/`status` 仍可回复。
3. 先开监听再刷卡;同一张卡保持不动只应上报一次,移开约 1 秒再放回应再次上报;保留原始串口字节解码核对 CRC 与连续序号。
4. 断电拔掉 RC522 后重启,应继续回复状态;恢复接线后应重新上线。

```powershell
& 'S:\esp\.espressif\python_env\idf5.5_py3.10_env\Scripts\python.exe' .\tools\pbr1_tool.py send <PORT> '{"command":"status"}' --seq 100
& 'S:\esp\.espressif\python_env\idf5.5_py3.10_env\Scripts\python.exe' .\tools\pbr1_tool.py listen <PORT> 15
```

同一桥板启动内重复发命令时递增 `--seq`(重复命令序号会被接收端丢弃);串口同一时刻只能被一个程序占用。卡片 UID 无需预先写入;重新打开监听不会重放之前已上报的事件,累计数看 `status.rfid.reports`。
