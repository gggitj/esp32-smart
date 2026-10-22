# 模块开发者工具包

面向固定 ESP32-S3 / LinkIt / Pi 终端平台的独立 SDK。复制本目录即可开发，不需要主仓库源码。主机 `.so` 在 Pi 上运行，ESP 负责传感器、控制面、数据面及插件包提供。

## 目录导航

| 路径 | 内容 |
| --- | --- |
| `module_devkit.py` | 初始化、环境检查、校验、资源整理、签名发布、准备固件嵌入包 |
| `firmware-build.sh` | 构建指定模块工程的固定 BLE/LAN 变体，不烧录 |
| `ai_guide.py` | 为模块生成 AI 开发上下文 |
| `mock-device.py` | 本机回环 TCP/HTTP 模拟设备入口 |
| `sdk/include/` | 稳定主机 C ABI 和 C++ 辅助头文件 |
| `sdk/esp32/` | 独立 LinkIt、控制面、业务数据面、标准板组件 |
| `template/` | 新模块的最小双端工程模板 |
| `examples/smart-planter/` | 完整环境模块：植物图示、日/周温度历史、电量、LCD与供电保护策略 |
| `tools/` | 自带的签名/验签配包器和模拟协议实现 |
| `docs/` | AI 分阶段指南、物理验收清单、SDK维护与同步说明 |
| `artifacts/` | 本机比赛构建产物，已忽略，不作为通用公开SDK分发 |

### 主要文档

- [AI coding 引导](docs/AI-CODING-GUIDE.md)
- [物理 Pi 验收清单](docs/PHYSICAL-PI-TEST.md)
- [SDK 与正式 ESP 源码的维护关系](docs/MAINTENANCE.md)
- [智能盆栽完整示例](examples/smart-planter/README.md)
- [智能盆栽已测/未测范围](examples/smart-planter/VALIDATION.md)
- [简体中文LCD布局与颜色传输](examples/smart-planter/firmware/components/ui_env/LCD-DESIGN.md)
- [中文字体、许可证及生成方式](examples/smart-planter/firmware/components/ui_env/fonts/README.md)

## 从零开始

下面从 SDK 目录执行；在主仓库中先 `cd module-devkit`。

```sh
python3 module_devkit.py doctor
python3 module_devkit.py init /path/to/my-module --module-id 200 --name "我的模块"
python3 ai_guide.py /path/to/my-module --module-id 200 --transport LAN
python3 module_devkit.py validate /path/to/my-module
```

让 AI 阅读生成的 `AI-CONTEXT.md` 和 `docs/AI-CODING-GUIDE.md`。最小模板不是完整业务实现；希望直接参考真实传感器、图示、历史存储和测试时，阅读智能盆栽示例。

**模块身份不能随意改后就保证接入。** 模块 ID、实例、地址、token 必须与既有 Pi profile 的接受范围一致；本项目实测智能盆栽使用 module_id=80。不修改 Pi 配置的前提下，应沿用已 provision 的身份及签名链。

## 主机构建与资源整理

```sh
cmake -S /path/to/my-module -B /path/to/host-build \
  -DMODULE_SDK_DIR="$PWD/sdk" \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/pi-aarch64-toolchain.cmake
cmake --build /path/to/host-build --parallel 4
python3 module_devkit.py stage /path/to/my-module /path/to/host-build \
  /path/to/package --arch aarch64
```

`stage` 核对实际 ELF 架构并收集整个 QML 目录，不只收集入口页面。输出目录必须为空，避免混入旧资源。Pi 插件需要 ARM64；本机 x86_64 库不能当作 Pi 产物。

## 签名与固件

```sh
python3 tools/package_tool.py build /path/to/package /path/to/signed /secure/private.pem aarch64
python3 tools/package_tool.py install-local /path/to/signed /secure/public.pem /path/to/test-cache 80 aarch64
python3 module_devkit.py prepare-firmware /path/to/signed /path/to/embedded
export PI_MODULE_SDKCONFIG=/secure/sdkconfig
export PI_MODULE_PACKAGE_DIR=/path/to/embedded
./firmware-build.sh /path/to/my-module LAN /path/to/firmware-build
```

签名可留在持有私钥的机器上完成。示例中的 `80` 应与实际模块一致；BLE和LAN使用独立构建目录。固件构建会将嵌入包复制到目标工程的 `firmware/`，这些生成文件已忽略。

也可使用 `module_devkit.py release PROJECT OUTPUT KEY --arch aarch64 --toolchain TOOLCHAIN --firmware-mode LAN --sdkconfig CONFIG` 完成构建和配包闭环。需要本机可读取签名私钥；不希望私钥离开 Pi 时，使用上面的 `stage` 和分离签名流程。

## 平台规则与边界

- 控制面走 LinkIt/PBR1；业务只走认证 BLE 或固定 Pi 热点 LAN/TCP，不跨模式回退。
- `attach` 不是业务就绪；认证、身份、序号及 `session_ready` 都通过才算建立会话。
- 沿用 Pi 当前发布公钥时无须修改 Pi 配置；包须由匹配私钥签名。SDK不自带私钥，不能凭公钥生成匹配私钥。
- 可脱离实机开发、构建和签名，前提是已有工具链与匹配私钥；最终硬件验收仍需实机。
- `pi_module_stop()` 同步停止所有线程、定时器、回调和发送后才允许卸载。
- 没有可靠外部电源检测接线时，供电来源为 unknown；不能用电池电量计推断USB供电。硬件过放保护、自锁按钮及真正切电不是软件或SDK可以替代的。
- 不自动刷写ESP、不改Pi信任配置、不停止终端服务。私有配置、密钥及Flash备份不公开分发。
