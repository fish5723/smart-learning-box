#!/usr/bin/env bash
# 一键生成 7 个字号的 CJK 子集字体（.bin），覆盖：源码用字 + GB2312常用字 + 中文标点
# 前置：安装 Node.js (nodejs.org)，然后：  npm install -g lv_font_conv
#
# 用法：  bash gen_fonts.sh
#
set -e

FONT="assets/fonts/SourceHanSansSC-Normal.otf"     # 思源黑体（项目自带）
SYMFILE="main/app/font_loader/used_chars.txt"      # 7026 个字符的子集
OUT="F:/fonts"                                      # TF 卡 fonts 目录（按需修改）

if ! command -v lv_font_conv >/dev/null 2>&1; then
  echo "错误：未找到 lv_font_conv。请先安装 Node.js，然后运行：npm install -g lv_font_conv"
  exit 1
fi

SYMS="$(cat "$SYMFILE")"
echo "字符集大小: ${#SYMS} 字符"
echo "字体源: $FONT"
echo "输出目录: $OUT"
mkdir -p "$OUT"

for size in 12 14 16 18 20 24 26; do
  echo "==> 生成 fontcn${size}.bin ..."
  lv_font_conv \
    --font "$FONT" \
    --size "$size" \
    --bpp 4 \
    --no-compress \
    --format bin \
    --symbols "$SYMS" \
    --output "$OUT/fontcn${size}.bin"
done

echo ""
echo "完成！生成的文件："
ls -la "$OUT"/fontcn*.bin
echo ""
echo "文件应比原来（含全部2万字形）小很多，启动加载时间大幅缩短。"
