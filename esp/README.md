# ESP 固件源码入口

## 当前有效代码

| 目录 | 职责 |
| --- | --- |
| `S3_Pi_Module/` | 当前智能盆栽环境模块：传感器、LCD、供电保护策略、LinkIt、认证 BLE/LAN、插件包嵌入 |
| `S3_PiBridge_LinkIt_Host/` | 既定控制桥固件，本次未修改 |
| `shared/module_data_service/` | TCP/BLE 会话及插件包传输 |
| `shared/pi_sensor_device/` | BME280、BH1750、MAX17043（含采样时间和有效标记） |
| `shared/lvgl/` | LVGL 8.4.0 运行时子集，保留原许可证 |

正式模块源码以 `S3_Pi_Module/` 为维护入口。`module-devkit/examples/smart-planter/firmware/` 是供外部开发者独立使用的导出副本，不依赖主仓库。

```sh
# 从仓库根目录检查正式固件与SDK副本的一致性
python3 tools/sync_module_devkit.py --check
```

归并后已用 ESP-IDF 5.5.4 验证正式工程 LAN 构建与 SDK 示例 LAN 构建均通过；本轮仅整理和编译，没有烧录设备。

## 构建

加载 ESP-IDF 5.5.4，提供私有配置和 SDK 生成的签名嵌入包后：

```sh
export PI_MODULE_SDKCONFIG=/secure/sdkconfig
export PI_MODULE_PACKAGE_DIR=/path/to/embedded-package
build_scripts/build_pi_module_variant.sh LAN /path/to/module-lan-build
```

不自动烧录。模块板与控制桥必须按 USB by-id 分辨；Flash、分区及备份必须在每次烧录前核对。私有配置不在本次合并中覆盖。

## 历史参考包

`ESP32_Env_Module_Firmware_20261018/` 是用户提供的历史交接包，包含当时的完整源码、配置和镜像。**不是当前正式构建入口**，本次不删除，也不以新版镜像覆盖它。最新模块业务、保护逻辑和页面开发说明以当前工程及 SDK 示例为准。

## 电源保护边界

供电来源未知时不自动低压休眠，避免直接供电误关机；必须用电池保护硬件兜底。可靠供电来源识别需要实际 PG/VBUS 检测接线，大按键硬件断电尚未接入。仅有 MAX17043 不足以判定整机供电来源。
