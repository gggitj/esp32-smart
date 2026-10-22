# 当前环境模块固件

本工程已合并 `module-devkit/examples/smart-planter/firmware/` 中完成并实测的模块业务代码，是仓库内正式维护入口。

- `main/main.c`：传感器数据、电量及供电状态组包、固定 LAN/BLE、包读取器、LinkIt任务。
- `components/lcd_st7789/`：当前标准板 LCD 驱动。
- `components/ui_env/`：240×240纯黑简体中文LCD，Noto SC/Roboto灰阶字体，未知供电不再标成 USB；包含字体来源、许可和像素测试。
- `components/power_guard/`：可靠来源及新鲜数据下的持续低压保护。
- `../shared/pi_sensor_device/`：传感器和电量计。
- `../shared/lvgl/`：LVGL运行库。

主机插件/QML 保留在 `module-devkit/examples/smart-planter/`，不混入 ESP 执行代码。配包工具仍使用独立 SDK，签名包经 `PI_MODULE_PACKAGE_DIR` 输入本工程。

构建与安全边界见 `../README.md`。中文LCD布局、RGB565传输约定及回归入口见 [LCD设计说明](components/ui_env/LCD-DESIGN.md)，字体许可与再生成说明见 [字体说明](components/ui_env/fonts/README.md)。

中文LCD已在此前任务中更新模块应用；本次仅归并和整理，不覆盖已有sdkconfig、不烧录、不重启Pi。SDK示例的对应副本通过仓库同步工具导出，不作为另一个独立维护分支。
