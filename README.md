# ESP 模块化终端服务(esp-modular-terminal-service)

以 ESP32-S3 为节点、树莓派为终端的模块化服务平台。ESP 功能模块负责传感器与执行面,经 LinkIt 总线接入控制桥;控制桥再通过 RC522 NFC 读卡与 USB-UART(PBR1 协议)连接树莓派终端服务。首个实测功能模块为智能盆栽环境监测模块。

## 平台组成

| 部分 | 位置 | 说明 |
|---|---|---|
| 功能模块固件 | `esp/S3_Pi_Module/` | 当前智能盆栽环境模块:传感器、LCD、供电保护策略、LinkIt、认证 BLE/LAN、插件包嵌入 |
| 控制桥固件 | `esp/S3_PiBridge_LinkIt_Host/` | LinkIt 模块通道 + RC522 NFC 读卡 + USB-UART/PBR1 上行,2026-10 实机验证,附 `build.ps1` 与主机测试 |
| 共享组件 | `esp/shared/` | 传感器设备、业务数据面、LVGL 运行时等通用组件 |
| 模块开发 SDK | `module-devkit/` | 可脱离主仓库独立开发新模块的工具包、模板与完整示例 |
| 同步工具 | `tools/` | 正式固件与 SDK 导出副本的一致性检查/导出 |
| 历史存档 | `archive/` | 历史交接快照,仅作追溯,不是构建入口 |

树莓派端终端服务(插件运行时、页面)在树莓派侧维护,不在本仓库。桥板侧接口合同(PBR1 帧格式、`rfid_card` NFC 事件、串口参数)见 [`esp/S3_PiBridge_LinkIt_Host/README.md`](esp/S3_PiBridge_LinkIt_Host/README.md)。

## 快速开始

按角色进入:

**固件开发** — 阅读 [`esp/README.md`](esp/README.md),使用 ESP-IDF 5.5.x 构建对应工程;桥板工程在工程目录运行 `.\build.ps1` 与 `tests/run_host_tests.py`(Windows)。

**新模块开发** — 进入 `module-devkit/`:

```sh
python module_devkit.py doctor
python module_devkit.py init /path/to/my-module --module-id 200 --name "我的模块"
python module_devkit.py validate /path/to/my-module
```

详细流程、签名与构建说明见 [`module-devkit/README.md`](module-devkit/README.md)。

**树莓派端** — 先读桥板 README 的 PBR1 协议与 NFC 事件合同;串口按 `/dev/serial/by-id/` 稳定识别桥板。模块身份(module_id 等)须与既有 Pi profile 一致,详见 SDK 文档。
