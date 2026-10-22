# SDK源码归属与维护

## 正式工程与独立SDK

- 仓库正式模块固件：`esp/S3_Pi_Module/`。
- 共享传感器、数据面及LVGL：`esp/shared/`。
- SDK示例的固件：`module-devkit/examples/smart-planter/firmware/`，是正式源码的独立导出副本。
- 主机插件、QML及其业务测试：只在SDK示例维护，不放进ESP执行代码。
- SDK通用模板、CLI、AI引导属于SDK本身，不覆盖到正式ESP工程。
- 用户提供的 `esp/ESP32_Env_Module_Firmware_20261018/` 保留为历史参考，不是当前维护入口。

为满足外部开发者拿不到主仓库的条件，SDK不能通过符号链接或相对主仓库路径引用正式固件；部分源文件必须随包提供。它们是有清单的导出副本，不是两个并行维护分支。

## 同步工具（仅主仓库维护者使用）

从主仓库根目录运行：

```sh
python3 tools/sync_module_devkit.py --check
python3 tools/sync_module_devkit.py --write
```

工具使用明确映射，校验每个源文件和副本SHA256，清单位于 `sdk/SOURCE-MANIFEST.json`。

- `--check` 只读。
- `--write` 只更新尚未被手工修改的已登记副本，或新增缺失文件。
- SDK副本与上次登记哈希不符时拒绝覆盖，需维护者先审查合并。
- 多余文件只报告，不自动删除；不复制 sdkconfig、密钥或嵌入包。
- 固件顶层/main CMake 各有必要差异：正式工程使用仓库组件及外部包目录；SDK示例使用自带SDK组件及构建脚本准备的包文件。它们不做逐字同步。

## SDK目录说明

- 根目录保留稳定CLI入口和README，避免移动脚本破坏独立使用命令。
- `docs/` 集中开发流程及验收文档。
- `examples/smart-planter/tests/` 保存长期原生回归及QML布局预览，不复制测试产物。
- LCD长期测试随固件放在 `examples/smart-planter/firmware/components/ui_env/tests/`；字体C数组、来源清单、Roboto及Noto许可证完整保留，目标文案为简体中文。
- `artifacts/` 仅保存本机有效发布产物，固件可能含私有设备配置，已gitignore。
- 分发SDK源码时排除 `artifacts/`、生成的嵌入包、构建目录、缓存和任何私有配置。LVGL许可证必须保留。
