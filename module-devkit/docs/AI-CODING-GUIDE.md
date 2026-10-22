# AI 模块开发引导组件

本文件是给 AI coding 工具的操作契约。把整个 `module-devkit/` 目录提供给 AI，并让它先阅读本文件，再修改模块工程。

本文命令从主仓库根目录举例；独立SDK使用者去掉命令中的 `module-devkit/` 前缀即可。`examples/`、`sdk/` 等路径均相对SDK根目录。

## AI 的目标

已有环境模块硬件时，优先阅读 `examples/smart-planter/README.md` 和 `VALIDATION.md`，复用真实日/周历史、电量、图示和原生测试。用 `module_devkit.py stage PROJECT BUILD PACKAGE --arch aarch64` 收集完整资源；不得只打包入口QML而漏掉同目录组件。

引导开发者从零完成一个双端模块：

1. 明确模块功能、硬件、模块 ID、传输模式和数据格式；
2. 生成主机插件、QML 页面和 manifest；
3. 生成 ESP32-S3 固件业务逻辑；
4. 使用 SDK 内置的 LinkIt 控制面和 module data service；
5. 构建主机 ARM64 插件和 BLE/LAN 固件；
6. 使用平台现有签名私钥生成可信包；
7. 运行模拟测试；
8. 生成物理 Pi 验收清单并指导逐项测试。

## 不可违反的规则

- 不读取、打印、提交或复制私钥、token、Wi-Fi 密码和私有 sdkconfig。
- 不生成新的公钥替换目标 Pi 的信任链。
- 不把 `send_control` 当业务数据通道；业务使用 `send_data`/`module_data_service_publish`。
- LAN 模式只连接固定 Pi 热点；不自动回退 BLE。
- BLE/LAN 使用独立 build 目录。
- 不把编译成功说成物理验收成功。
- 不烧录、不重启 Pi 服务，除非开发者明确确认目标设备和操作。
- `pi_module_stop()` 必须停止线程、定时器、回调和异步发送。

## AI 工作流程

### 阶段 0：需求确认

必须向开发者确认：

- 模块名称和 `module_id`（1–255）；
- ESP32-S3 板型和 GPIO/I2C/SPI 引脚；
- 传输模式：`BLE` 或固定 Pi `LAN`；
- 传感器/执行器功能；
- 命令和数据 JSON/二进制格式；
- 页面需要显示和控制的内容；
- 是否已有平台签名私钥路径（只确认路径，不读取内容）。

没有这些信息时，不要猜硬件引脚或协议字段；先生成待确认清单。

### 阶段 1：初始化

```sh
python3 module-devkit/module_devkit.py init ./my-module \
  --module-id <ID> --name "<模块名>"
```

检查生成的：

- `manifest.json`
- `module.cpp`
- `qml/ModulePage.qml`
- `firmware/main/main.c`
- `firmware/main/Kconfig.projbuild`

### 阶段 2：实现业务

AI 只修改模块工程，不修改 SDK 内置协议组件。实现顺序：

1. ESP32 硬件初始化和驱动；
2. 命令回调和参数校验；
3. 传感器采样/执行器控制；
4. `module_data_service_publish()` 发布数据；
5. 主机 `pi_module_on_data()` 解析数据；
6. `pi_module_action()` 发送命令；
7. QML 页面状态、错误和加载显示。

### 阶段 3：脱机检查

```sh
python3 module-devkit/module_devkit.py doctor
python3 module-devkit/module_devkit.py validate ./my-module
```

若拥有匹配平台私钥，可发布：

```sh
python3 module-devkit/module_devkit.py release ./my-module ./release \
  /secure/platform-private.pem \
  --arch aarch64 \
  --toolchain /secure/pi-toolchain.cmake \
  --firmware-mode LAN \
  --sdkconfig /secure/sdkconfig
```

AI 必须检查输出中的 `.so` 为 `aarch64`，并验证 `package.json` 使用目标 Pi 公钥通过验签。不能把测试密钥包称为生产可信包。

### 阶段 4：模拟测试

```sh
python3 module-devkit/mock-device.py ./release/signed-package \
  --module-id <ID>
```

AI 应测试 action、数据解析、异常数据、断开和重连逻辑。模拟测试不代表真实 BLE、Wi-Fi、LinkIt 或传感器通过。

### 阶段 5：实机验收

AI 必须打开 `PHYSICAL-PI-TEST.md`，逐项询问开发者并记录：

- 控制桥 attach；
- `module-transport-v1`；
- 固定 Pi 热点或 BLE；
- token/session_ready；
- 真实命令和数据；
- 断开/重连；
- QML 页面；
- 固件和包 SHA256。

任何未执行项目必须标记为 `NOT TESTED`，不得标记 PASS。

## AI 每轮输出格式

```text
当前阶段：需求 / 初始化 / 主机 / 固件 / 构建 / 签名 / 模拟 / 实机
已完成：
当前要做：
需要开发者确认：
执行命令：
预期结果：
风险或未测试项：
下一步：
```
