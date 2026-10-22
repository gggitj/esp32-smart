# 智能盆栽 · 环境看板（SDK 完整示例）

本示例由 `module_devkit.py init` 生成，再实现真实模块业务。仅依赖此 SDK、ESP-IDF 5.5.4、与 Pi 匹配的 ARM64/Qt 6.4 工具链，不需主程序源码。不修改 Pi 主程序、配置或发布公钥。

仓库内正式ESP代码已归并到 `esp/S3_Pi_Module/` 与 `esp/shared/`；本目录的 `firmware/` 保留为可单独分发的SDK导出副本。维护关系及一致性检查见 [SDK维护说明](../../docs/MAINTENANCE.md)，不手工维护两个不同版本。

## 硬件与身份

- 适配当前模块板 ESP32-S3 / 16MB Flash，module_id=80；不是所有新 ID 都能绕过 Pi 预配置身份接入。
- BME280 SDA38/SCL39；BH1750 SDA4/SCL5；MAX17043 软件 I²C SDA6/SCL7。
- LCD ST7789 240×240，保留参考板 DC8/RES9、1MHz、双初始化及小缓冲。
- LCD目标语言为简体中文，纯黑背景；Noto Sans CJK SC中文子集搭配Roboto数字字体。设计及测试见 [LCD说明](firmware/components/ui_env/LCD-DESIGN.md)，来源和许可见 [字体说明](firmware/components/ui_env/fonts/README.md)。RGB565字节序只在驱动转换一次。
- LinkIt、认证数据面由 SDK 提供；本例保留参考模块的 LinkIt 任务和已验证 DET 重启释放逻辑。
- **没有土壤湿度或浇水硬件**。页面明确区分空气湿度，不虚构土壤读数和执行器。

## 页面与历史

- 原创植物插画、温度/空气湿度/光照/气压图示卡片、实时/过期状态。
- 最近24小时与最近7天温度趋势，分别按15分钟/2小时聚合；均值及最低～最高值，缺测不连线。
- 原始记录按分钟聚合，保留最近7天，内存有界；没有真实历史时不造数据。
- 历史在 Pi 的宿主 `data_directory()` 下 `planter/<实例哈希>.json`，通过 QSaveFile 原子写入，每分钟及同步停止时保存。异常断电最多丢失约一分钟未提交样本。
- 插件保持加载、收到实时数据时持续记录；Pi/模块离线、插件未加载时不会补造历史。
- 电量、电压与供电来源分开显示；数据无效用 `--`，过期明确提示。

## 电源保护：必须阅读

MAX17043 不能判断整机由 USB 还是电池供电。本例不会依据“电量计有效/无效”推断供电来源。

- 默认 `CONFIG_PLANTER_EXTERNAL_POWER_GPIO=-1`：尚未接供电检测，来源 `unknown`，禁止来源不明触发自动休眠，避免直接供电误关机；**此时必须依赖电池保护板避免过放**。
- 接入经过电平与占用检查的 PG/VBUS 有效信号后，再设置检测 GPIO 和有效电平。5V 不得直接进入 GPIO。
- 仅在确认整机不存在 USB/外部供电路径时，才可显式启用 `PLANTER_BATTERY_ONLY_CONFIRMED`；不能拿该选项假装自动检测。
- 外部供电立即抑制低压休眠；确认电池供电、测量新鲜有效、低于3.40V持续20秒才请求 Pi 断开，3秒收尾后再检查外部电源并休眠。
- 删除旧版“启动时一读到低压就立即休眠”行为；计量失效/数据过期重置低压计时。
- 深度睡眠**不是物理断电**。大按钮自锁控制器、VBUS检测和负载开关尚未接入，不能宣称已完成按键自动切电。硬件设计参考桌面电源管理交接文档。
- `charging` 无硬件信号时为 null；电池测量失效不等同电池不存在。

## 构建与配包

```sh
# 下面在项目根目录执行；独立分发时按SDK实际位置调整路径。
python3 module-devkit/module_devkit.py validate module-devkit/examples/smart-planter
cmake -S module-devkit/examples/smart-planter -B /path/to/host-build \
  -DMODULE_SDK_DIR="$PWD/module-devkit/sdk" \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/pi-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/host-build --parallel 4
python3 module-devkit/module_devkit.py stage module-devkit/examples/smart-planter \
  /path/to/host-build /path/to/package --arch aarch64
```

签名时使用 Pi 现有公钥对应私钥；可以在 Pi 的临时目录中仅运行 SDK 配包/验签工具，私钥无需离开 Pi。本机交叉编译 `.so`，Pi 只运行和验证，不需要在 Pi 编译主程序。

配包必须包含 `manifest.json`、`libmodule.so` 和**整个 qml 目录**，不能只复制入口页面。SDK `release` 已支持收集全部 QML 文件并检查实际 ELF 架构；本例动态库运行依赖 Qt6 Core。

```sh
python3 module-devkit/tools/package_tool.py build /path/to/package /path/to/signed /secure/private.pem aarch64
python3 module-devkit/tools/package_tool.py install-local /path/to/signed /secure/public.pem /path/to/test-cache 80 aarch64
python3 module-devkit/module_devkit.py prepare-firmware /path/to/signed /path/to/embedded
export PI_MODULE_SDKCONFIG=/secure/sdkconfig
export PI_MODULE_PACKAGE_DIR=/path/to/embedded
module-devkit/firmware-build.sh module-devkit/examples/smart-planter LAN /path/to/firmware-build
```

私有配置必须包含既有 APP_PI_DATA 字段、与 Pi 一致的身份/token/热点配置和静态地址。参考板分区仍为原有0x177000应用分区，烧录前核对设备Flash与当前分区。不要把新生成的默认配置当作已 provision 的配置。

## 回归

```sh
cmake -S module-devkit/examples/smart-planter -B /path/to/tests \
  -DMODULE_SDK_DIR="$PWD/module-devkit/sdk" -DPLANTER_TESTING=ON
cmake --build /path/to/tests --parallel 4
ctest --test-dir /path/to/tests --output-on-failure
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software /path/to/tests/planter_preview \
  "$PWD/module-devkit/examples/smart-planter/qml/ModulePage.qml" /path/to/test-preview
```

preview 的曲线和数值是**显式测试数据**，不写入产品历史，不作为实机传感器证据。真实日/周历史需要实际运行积累，离屏预览不代表物理触摸验收。

LVGL 8.4.0 运行时源码子集随本例提供，其原始许可证见 `firmware/components/lvgl/LICENCE.txt`。植物插画由本例 Canvas 绘制，非植物健康或土壤状态测量结果。
