# 240×240 LCD：纯黑 Material 适配

## 设计依据

对照 Google Material Design 3 官方资料：

- Typography：<https://m3.material.io/styles/typography/type-scale-tokens>
- Color roles：<https://m3.material.io/styles/color/roles>

本屏是无触摸的240×240信息显示，不是完整Material组件实现或官方认证界面。按用户要求采用纯黑surface，而非照搬Material默认带色暗表面；保留Material信息层级、语义颜色、间距和高对比度原则。

## 具体规格

- 背景像素固定 `#000000`；IPS背光漏光不属于像素颜色设置可消除的现象。
- 主数值/正文为白色，说明为中性灰 `#BDBDBD`；字体不使用随机多色。
- 默认显示语言为简体中文；中文采用 Noto Sans CJK SC Regular 12/14px子集，数字采用 Google Roboto 24/36px；仅标准计量单位保留国际符号。
- 左右16px留白，4/8px间距节奏，按标题/温湿度/压力与光照/电量分区。
- 温度36px、湿度24px形成主次层级，单位独立显示；文本定宽、禁跑马灯，极值检查不截字。
- 光照进度仅表达照度，淡绿色不是植物健康判定；LOW低电量用明确文字，不依赖颜色识别。
- “外部供电” / “电池供电” / “供电待检测”仅依据固件实际供电检测状态，不把缺测伪装为USB。
- 测量超过5秒未更新则显示缺测，不持续显示旧数据。
- 保留小DMA缓冲、1MHz SPI和当前硬件需要的两次初始化，不扩大FBO/整屏RAM缓存。

## 字体异色修复边界

LVGL使用 `LV_COLOR_DEPTH=16`、`LV_COLOR_16_SWAP=0`。ESP小端内存中的RGB565像素，必须转换为ST7789要求的高字节在先。由LCD驱动唯一转换，不能再在字体或LVGL里重复交换。

字体边缘的灰阶不像白色0xFFFF那样字节对称，直接发小端内存会产生彩色边缘。驱动现在逐行转换到固定DMA缓冲，等待行完成后复用；全部行完成才通知LVGL整块完成。该过程避免渲染器提前修改正在传输的文字缓冲。

逻辑修复不替代实际屏幕确认；RGB/BGR面板顺序、背光、线缆和供电等物理因素仍需单独排除。

## 回归入口

```sh
cmake -S esp/S3_Pi_Module/components/ui_env/tests -B /tmp/opencode/lcd-view-tests
cmake --build /tmp/opencode/lcd-view-tests --parallel 4
ctest --test-dir /tmp/opencode/lcd-view-tests --output-on-failure
```

SDK副本内运行时可给CMake传 `-DLVGL_ROOT=/path/to/sdk-example/firmware/components/lvgl`。

测试真实LVGL视图：正常/缺测/极值，所有文字边界、纯黑边缘、字体颜色和RGB565传输字节；输出PPM到测试构建目录用于像素预览。预览不是物理LCD照片，不代表已确认实屏颜色。
