# Orange Pi 动态模块终端

本目录是统一仓库中的 Orange Pi 产品源码。推荐从仓库根目录使用
`build_scripts/build_terminal.sh`、`build_scripts/run_terminal.sh` 和
`build_scripts/test_terminal.sh`；本目录下的 `scripts/` 是内部实现脚本。

**界面更新：** 当前为 Google Material 图标启动器，模块功能直接出现在主页，打开/关闭采用缩放动画；保留轻 GPU 模糊、自适应、触摸和大字设置。状态区显示真实设备状态。指标和截图见 [界面验收](UI_ACCEPTANCE.md)。

应用功能页使用固定工作区，任务栏支持多个应用同时打开和切换；应用表面支持最大化/最小化，Qt主窗口保持不变。浏览器支持地址跳转及触摸操作。
应用页不提供内部自由窗口或贴靠层；任务栏负责应用任务切换。
模块主动断开使用 ACK 等待和有限重试；插件页面清空后才进入插件释放流程。文件程序以当前用户家目录为起始路径，并拒绝绝对路径、路径穿越和软链接越界。

C++17 / Qt 6 QML / CMake；Pogo 控制桥通过 USB Serial/JTAG 仅报告接入，BLE/LAN 负责业务，签名 `.so` 插件动态加载。此目录是可在本机运行和调试的终端工程，依据根目录 `docs` 的迁移资料继续实现。

## 本机直接运行

在仓库根目录执行：

```bash
bash 启动香橙派桌面调试.sh
```

脚本会增量编译 Debug 版本并启动模拟器和真实 Qt 窗口。第一次接入选择“仅本次”或“始终信任”，直接点主页“示例模块”图标，再点“开始”。关闭应用页后会缩回主页，模块连接保留；连接管理负责重试/断开。关闭整个窗口时模拟服务也会退出。

也可在本目录分别执行：

```bash
# 本机已经安装完成；换一台 Linux 电脑时先执行此命令
python3 scripts/setup-desktop.py
bash scripts/build.sh
bash scripts/run-desktop.sh
bash scripts/test.sh
```

目标 Orange Pi 不能直接复用桌面构建产物。使用最终镜像的 ARM64 sysroot 和匹配的 Qt host tools 执行
`bash scripts/build-cross.sh`，再在目标机以 root 设置 `PRODUCT_CONFIG`、`PUBLISHER_KEY` 和
`CONTROL_DEVICE` 执行 `bash scripts/install-target.sh`。该脚本会安装产品配置、发布者公钥、稳定串口路径和
user systemd service；启用服务前必须确认运行用户属于 `dialout`，且图形会话已提供 DISPLAY/Wayland 和 D-Bus。

本机 SDK 位于 `~/.local/share/orange-pi-terminal-sdk/`，不是 `/tmp`，不需要 sudo。使用 Qt 6.5.3、系统 GCC 和独立 Python 工具环境；没有替换系统 Qt 或用户已有的 Qt 6.10。

构建产物 `build/orange_pi_terminal`，配置和模拟包 `.desktop/`，测试结果 `build/test-results.xml`。真实运行截图见 `build/desktop-live.png`。

## 已实现的终端功能

- PBR1 串口控制帧、CRC、热插拔事件；桌面使用真实伪终端验证串口接收路径。
- BLE GATT 和 TCP 数据帧、身份 token、心跳、断线重连、触点拔出保留会话。
- HTTP(S) 大包下载；BLE/LAN 认证数据链路的小包分片接收，签名验证后才请求压缩包。
- Ed25519 / SHA-256 / 架构 / ABI / 清单校验、原子缓存安装、插件加载和卸载。
- 每会话 Qt StateMachine；独立 `ModuleCardModel`、`PageProvider`、宿主所有的数据模型。
- “仅本次 / 始终信任 / 拒绝”，信任绑定公钥与清单；失败卡片和重试。
- 清单驱动的动作按钮、两个插件示例页面、Material 主题/模块图标、FBO 金字塔背景及边缘卡片模糊。
- 多标签浏览器、书签和缩放、当前用户家目录内的文本编辑/重命名/删除、网络、持久日志、TTS 设置。
- 内置触摸计算器、Canvas 触摸画图板和 UTF-8 文本便笺；这些功能在终端进程内运行，不调用外部 GNOME 应用。
- 一键桌面模拟、Debug 符号、CMake preset、GDB 调试、自动测试和帧事件统计。

## 重要边界

**本机桌面链路已验证，不代表整个硬件技术路线已完成验收。** 当前 ESP32 原固件仍使用原 LinkIt 业务链路，不能直接当作本工程的 PBR1/BLE/LAN 固件。现有硬件固件的改造与烧录、真实 BLE、香橙派 ARM64 sysroot/交叉链接和板端 GPU 性能尚未完成。本机没有这些硬件验收条件。

详见 [本机调试](DESKTOP_DEBUG.md)、[技术规格与协议](TECHNICAL_SPEC.md)、[验证记录与缺口](VALIDATION.md)。
