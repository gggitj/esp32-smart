# 物理 Pi 终端验收记录

SDK 自动发布和编译不等于物理验收。部署人员必须逐项填写并保留日志；本文件不包含凭据。

本文件的勾选记录为早期 SDK 基线，不代表所有后续构建。当前智能盆栽3.0.0的实际固件、页面与历史测试范围见 [示例验收记录](../examples/smart-planter/VALIDATION.md)。仓库正式ESP源码已归并到 `esp/S3_Pi_Module/`，SDK保留独立导出副本，见 [维护说明](MAINTENANCE.md)。

## 脱机能力声明

在使用与目标 Pi 现有发布公钥匹配的签名私钥时，模块开发者可以脱离实机完成可信包、ARM64 主机插件、QML 页面、ESP32 固件及 BLE/LAN 构建；不需要修改 Pi 终端配置或公钥。脱机结果只能证明构建、签名、验签和协议代码可用，不能替代下方的物理链路验收。

## 发布前

- [ ] `module_devkit.py doctor` 通过
- [ ] host 插件为 `aarch64` ELF：`file lib*.so`
- [ ] `manifest.json`、嵌入 manifest 和签名 descriptor 一致
- [ ] BLE 或 LAN 只选择一种，不能混用 build 目录
- [ ] 固件包来自本次签名包，未把私钥放入固件工程
- [ ] 确认 Pi 当前已配置平台发布公钥；若沿用平台公钥，不修改 Pi。只有平台更换发布公钥时，才按发布流程更新信任配置

## Pi 现场

- [ ] 记录 Pi 型号、系统、Qt 版本和终端版本
- [ ] 控制桥服务 active，记录 PID/重启计数
- [ ] 模块物理连接后观察 LinkIt 握手完成
- [ ] 观察控制桥发出 `attach` 和 `module-transport-v1`
- [ ] 主机插件加载成功，`pi_module_on_attach` 返回成功
- [ ] 业务通道完成 token 校验和 `session_ready`
- [ ] LAN 模式确认模块连接固定 Pi 热点，业务走认证 TCP
- [ ] BLE 模式确认业务没有偷偷回退 LAN
- [ ] 执行至少一个 action，并收到真实模块数据
- [ ] 验证模块主动断开、主机确认、重新连接
- [ ] 停止/卸载时无残留线程、回调或旧插件映射

## 结果

```text
模块 ID：
传输模式：BLE / LAN
固件 SHA256：
主机包 SHA256：
Pi/Qt/终端版本：
结果：PASS / FAIL
失败日志路径：
```

## 本次 SDK 实测记录（2026-10-19）

- [x] Pi SSH 可达，确认架构为 `aarch64`
- [x] 使用 Pi 当前终端配置对应的签名私钥生成包
- [x] 使用 Pi 当前终端配置对应的公钥验签
- [x] Pi 本机编译出 ARM64 主机插件
- [x] 临时缓存安装验证通过
- [x] 本机 ESP32-S3 LAN 固件编译通过
- [x] 本机 ESP32-S3 BLE 固件编译通过
- [ ] 真实 ESP32 模块烧录
- [ ] 真实 LinkIt attach
- [ ] 真实 `session_ready`
- [ ] Pi QML 页面物理终端显示
- [ ] 真实硬件数据、断开和重连

当前未完成项的原因：本次会话发现 Pi 上有控制桥串口 `/dev/ttyACM0`，但没有确认已连接可烧录的模块 ESP32；未在未确认目标的情况下写入 Flash。上述未完成项不能标记为 PASS。
