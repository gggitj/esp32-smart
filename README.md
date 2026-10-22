# ESP32 Smart Planter

ESP32-S3 智能盆栽环境监测模块，包含固件、树莓派终端插件和独立开发者 SDK。

## 目录

- `esp/` — 正式 ESP32 固件
  - `S3_Pi_Module/` — 当前智能盆栽模块
  - `S3_PiBridge_LinkIt_Host/` — 控制桥固件
  - `shared/` — 通用传感器、通信和 LVGL 组件
- `module-devkit/` — 独立模块开发工具包，可脱离主仓库开发新模块
- `tools/` — 源码与 SDK 同步脚本

## 快速开始

1. 进入 `module-devkit/`
2. 运行 `python module_devkit.py doctor`
3. 运行 `python module_devkit.py init /path/to/my-module --module-id 200 --name "我的模块"`
4. 运行 `python module_devkit.py validate /path/to/my-module`

详细说明见 `module-devkit/README.md`。
