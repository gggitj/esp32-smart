# 本机验证记录

## Material启动器重做后的补充验证

当前CTest为6组，全通过；普通DPR与1.5倍DPR各54个布局状态，检查Material图标、浅层模糊、真实状态区、缩放动画中间帧及触屏操作。端到端测试通过主页图标实际点开模块，验证关闭/重开保留连接以及动画结束后再卸载插件。26组文字配色最低对比度6.081:1。具体矩阵、截图及边界见 `UI_ACCEPTANCE.md`。

本轮新增并通过固定工作区、多任务切换、内部应用表面状态、当前用户家目录文件根目录、浏览器地址导航、链接接受和浏览器触摸拖动验证。

追加验证：多任务同时打开/任务切换、应用页无自由窗口/贴靠残留、内部应用表面最大化/最小化、Qt主窗口保持普通状态、浏览器触摸链接导航、当前用户家目录文件根目录和蓝牙错误反馈路径。

当前最终行为验证：多任务保持打开、任务切换不关闭其他任务、应用页无自由窗口/贴靠控件、内部应用表面最大化/最小化且不改变Qt主窗口、主动断开 ACK 等待/有限重试；全部6个CTest组通过。

下文的3组测试和性能数字为此前桌面功能版本记录，不代表本次新界面的帧率基准。

记录日期：2026-10-01（本机工具输出时间）。

## 实际环境

- Pop!_OS 22.04 / x86_64，GCC 11.4。
- Qt 6.5.3，CMake 4.4.3，Debug 构建及源码调试符号。
- SDK：`~/.local/share/orange-pi-terminal-sdk/`，不再依赖临时工具目录。
- 本机显示：X11；实际 OpenGL renderer 为 AMD Radeon RX 7600M XT / Mesa 25.1.5。
- Xvfb 用于可重复自动化；另在用户当前 `DISPLAY=:1` 上运行 GUI 自测。

## 已执行并通过

命令：`bash scripts/test.sh`。

```text
1/3 pibridge_protocol              Passed
2/3 runtime_models_state_files    Passed
3/3 signed_session_integration    Passed
100% tests passed, 0 tests failed out of 3
Total Test time: 19.80 sec
```

这3个是 CTest 测试组，包含以下具体检查，而不是只有3条业务断言：

| 检查 | 结果 |
|---|---|
| PBR1 CRC、分片、坏帧恢复、粘包、长度限制 | 通过 |
| 卡片模型持续索引、增量更新、删除 | 通过 |
| 两个会话页面模型数据隔离、释放 | 通过 |
| Qt 状态机禁止跳过验证/复活已关闭会话 | 通过 |
| 分片 transfer_id、大小边界 | 通过 |
| 文件读写、重命名、删除、越界/软链接拒绝 | 通过 |
| 签名包 HTTP 下载、安装、缓存复用 | 通过 |
| BLE 所用的小包协议经 TCP 回环传输并加载 | 通过；不是空口验收 |
| 触点拔出保留会话、旧 instance 数据拒绝 | 通过 |
| TCP 断线重连及重新认证 | 通过 |
| 用户断开、模块请求断开、策略拒绝 ACK 后继续运行 | 通过 |
| 未经信任确认不加载 `.so`，拒绝后正常释放 | 通过 |
| 校验器无法启动后修复配置并重试 | 通过 |
| 100次真实 QLibrary 加载/回调/卸载 | 通过；不是完整泄漏分析 |
| 缓存篡改、签名元数据篡改、压缩包篡改 | 均被拒绝 |
| PTY 串口→PBR1→信任窗口→插件页面→动作/数据→释放 | 通过 |
| WebEngine 第二标签、本地 HTML 加载、JavaScript 执行 | 通过 |

实际桌面与 Xvfb 均输出：

```text
UI_TEST_PASS: permission accepted, page mounted, action routed, data delivered,
page released, plugin unloaded, browser HTML/JavaScript/tab verified
```

还已执行：

- 根目录 `bash 启动香橙派桌面调试.sh --ui-test --quit-after 9000 ...`，自动编译并在当前桌面运行，退出码0。
- GDB 在 `ModuleCardModel::synchronize` 命中断点，回溯指向实际 `.cpp` 源码。
- `cmake --install build --prefix <本工程>/build/install-check`，程序、示例插件、SDK头文件/CMake导出、图标许可安装成功。

## 截图和帧统计

- `build/demo.png`：Xvfb自动化流程中的插件页面。
- `build/desktop-live.png`：实际桌面窗口、已加载插件及数据42。
- `build/desktop-metrics.json`：实际桌面一次短测试记录。
- `build/test-results.xml`：CTest JUnit记录；更完整日志在 `build/Testing/Temporary/LastTest.log`。

本次短测：约8.752秒，平均236.97次 `frameSwapped` 事件/秒，P95事件间隔4.381ms，主进程RSS463484 KiB。
这些是 Qt 帧交付事件和主进程采样，不是 GPU 单帧耗时；RSS 不含独立 WebEngine 子进程。
短测试也不能证明长期无泄漏，更不能推出香橙派达到相同帧率。

构建仍提示本机缺少 XKB 开发包，但实际 X11/Xvfb 运行通过；桌面 ICC 配置有告警，不影响本次窗口测试。

## 未完成/未验证

1. **原有 ESP32 固件的完整 PBR1、BLE/LAN 数据面适配和烧录尚未完成。** 现有原固件不能直接与新终端完整联调。桌面模拟器实现的是新协议参考行为。
2. 真实 BLE 控制器/GATT、真实串口电气连接和 Pogo 唤醒/去抖未验收。
3. 没有目标香橙派镜像 sysroot，ARM64 交叉链接和板端启动未验收。
4. 香橙派 GPU/FBO/30fps、触控/DPI、热点 DHCP、无线共存、TTS实际声音和 systemd 自动启动未验收。
5. 长时压力/完整内存泄漏分析、可靠关闭ACK重传和不可信网络TLS设备认证未完成。

因此当前交付可用于电脑端持续开发和调试，但不能宣称“整个硬件技术路线已全部实现并验证”。

测试密钥不是生产密钥：集成测试使用临时密钥并清理；交互桌面密钥保存在被忽略的 `.desktop/keys/`，仅供本机重复调试。
