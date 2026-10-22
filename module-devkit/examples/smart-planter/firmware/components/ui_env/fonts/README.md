# 简体中文 LCD 字体资源

当前仅编译四份字体：

| 文件 | 内容 |
| --- | --- |
| `plant_zh_12.c` | Roboto ASCII/单位 + Noto Sans CJK SC 简体中文，12px |
| `plant_zh_14.c` | 同上，14px |
| `plant_roboto_24.c` | Roboto数值字符，24px |
| `plant_roboto_36.c` | Roboto数值字符，36px |

所有字体均为4bpp灰阶抗锯齿，无RGB子像素渲染，不压缩。固件直接编译C数组，不依赖Node、系统字体或生成时临时目录。源码顶部Opts仅记录生成命令。

## 来源与许可

- Google Roboto Regular：Apache-2.0，见 `Roboto-LICENSE.txt`。
- Noto Sans CJK SC Regular 2.004：SIL OFL 1.1，见 `Noto-CJK-COPYRIGHT.txt`。
- URL、原始字体SHA256、55个汉字及中文逗号见 `SOURCE.json`。
- 中文字面取自 `fonts-noto-cjk` TTC索引2（SC），不使用JP/TC字面替代简体中文。

## 重新生成

先核对 `SOURCE.json` 中的字体哈希，用FontTools提取SC字面：

```python
from fontTools.ttLib import TTCollection
TTCollection("NotoSansCJK-Regular.ttc").fonts[2].save("NotoSansCJKsc-Regular.otf")
```

使用 `lv_font_conv@1.5.3`，`GLYPHS` 为 `SOURCE.json` 的 `chinese.glyphs` 与 `chinese.punctuation` 拼接结果：

```sh
for size in 12 14; do
  lv_font_conv --font Roboto-Regular.ttf --range 0x20-0x7e,0xb0 \
    --font NotoSansCJKsc-Regular.otf --symbols "$GLYPHS" \
    --size "$size" --bpp 4 --format lvgl --no-compress -o "plant_zh_$size.c"
done
lv_font_conv --font Roboto-Regular.ttf --size 24 --bpp 4 --format lvgl \
  --symbols '0123456789.-%' --no-compress -o plant_roboto_24.c
lv_font_conv --font Roboto-Regular.ttf --size 36 --bpp 4 --format lvgl \
  --symbols '0123456789.-' --no-compress -o plant_roboto_36.c
```

新增LCD文案必须更新子集，运行 `../tests/` 的逐字覆盖、文字边界与灰阶测试。编译定义 `LV_LVGL_H_INCLUDE_SIMPLE=1` 已由组件CMake提供。
