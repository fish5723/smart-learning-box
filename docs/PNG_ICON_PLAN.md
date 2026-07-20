# PNG 图标替代方案

## 目标

用设计精美的 PNG 图标替换当前使用的 LVGL 内置符号（`LV_SYMBOL_*`），提升整体视觉品质。

## 当前状态

| 位置 | 当前图标 | 用途 |
|------|------|------|
| Home 功能卡片 | `LV_SYMBOL_EDIT` / `LV_SYMBOL_IMAGE` / `LV_SYMBOL_PLAY` / `LV_SYMBOL_OK` | 4 个功能入口 |
| Home 状态栏 | `LV_SYMBOL_WIFI` / `LV_SYMBOL_BATTERY_FULL` | WiFi / 电量 |
| OCR 页面 | `LV_SYMBOL_IMAGE` / `LV_SYMBOL_LEFT` | 空状态 / 返回 |
| AI 页面 | `LV_SYMBOL_CHARGE` / `LV_SYMBOL_OK` / `LV_SYMBOL_LEFT` | 头像 / 在线 / 返回 |
| Game Center | `LV_SYMBOL_BELL` / `LV_SYMBOL_REFRESH` / `LV_SYMBOL_PLUS` 等 | Banner / 游戏图标 |
| Achievement | `LV_SYMBOL_CHARGE` / `LV_SYMBOL_OK` / `LV_SYMBOL_CLOSE` | 头像 / 徽章 |

## 技术方案

### 路径 A：编译期内嵌（第一阶段，无需 TF 卡）

```
PNG 设计稿
    ↓
LVGL Online Image Converter (https://lvgl.io/tools/imageconverter)
    ↓
.c 文件 (lv_img_xxx) → 编译进固件
    ↓
lv_image_set_src(img, &icon_xxx)
```

**格式选择：**

| 格式 | 色深 | 64×64 文件大小 | 适用场景 |
|------|------|------|------|
| `CF_INDEXED_4BIT` | 16 色 | ~2 KB | 简单线性图标 |
| `CF_INDEXED_8BIT` | 256 色 | ~4 KB | 带渐变的图标 |
| `CF_TRUE_COLOR` | 16M 色 | ~16 KB | 照片级图标 |

**推荐**：图标使用 `CF_INDEXED_4BIT`，10 张图标 ≈ 20KB，完全可接受。

**LVGL 转换参数：**
- Color format: `CF_INDEXED_4BIT` 或 `CF_TRUE_COLOR_ALPHA`
- Output format: C array
- Dithering: Floyd-Steinberg（如果需要渐变）

### 路径 B：TF 卡运行时加载（第二阶段）

```
TF 卡: /sdcard/icons/xxx.bin
    ↓
lv_image_set_src(img, "S:/icons/ai_teacher.bin")
```

需要 TF 卡驱动就绪后使用，优点是图标可以随时替换，不占 Flash。

## 需要的图标清单

### P0（核心功能图标，~10 张）

| 图标 | 使用位置 | 尺寸 | 说明 |
|------|------|------|------|
| `icon_ai` | Home 卡片 / AI 头像 | 64×64 | AI 老师 |
| `icon_ocr` | Home 卡片 / OCR 占位 | 64×64 | 拍照解题 |
| `icon_game` | Home 卡片 / Game Banner | 64×64 | 游戏中心 |
| `icon_achievement` | Home 卡片 / 徽章 | 64×64 | 成长中心 |
| `icon_back` | 各页面返回按钮 | 32×32 | 返回箭头 |
| `icon_wifi` | 状态栏 | 24×24 | WiFi 信号 |
| `icon_battery` | 状态栏 | 24×24 | 电量 |
| `icon_home` | 导航 | 32×32 | 首页 |

### P1（装饰图标，~8 张）

| 图标 | 使用位置 | 尺寸 |
|------|------|------|
| `icon_math` | Game Center 卡片 | 48×48 |
| `icon_english` | Game Center 卡片 | 48×48 |
| `icon_star` | 积分/等级 | 20×20 |
| `icon_camera` | OCR 按钮 | 32×32 |
| `icon_send` | AI 发送按钮 | 24×24 |
| `icon_lock` | 锁定徽章 | 24×24 |
| `icon_crown` | 等级卡片 | 32×32 |
| `icon_target` | 任务进度 | 24×24 |

## 代码改动示例

```c
// 旧代码（LV_SYMBOL）
lv_obj_t *icon = lv_label_create(card);
lv_label_set_text(icon, LV_SYMBOL_EDIT);
lv_obj_set_style_text_font(icon, &lv_font_montserrat_36, 0);

// 新代码（PNG）
LV_IMG_DECLARE(icon_ai_teacher);
lv_obj_t *icon = lv_image_create(card);
lv_image_set_src(icon, &icon_ai_teacher);
```

## 图标风格方向（待用户确认）

- **Material Design 3** (Google) — 圆润、色彩丰富
- **Fluent Design** (Microsoft) — 简洁、线性
- **SF Symbols** (Apple) — 统一、精确
- **自定义** — 配合深色主题的发光/霓虹风格

## 依赖

- [ ] 用户确认图标风格
- [ ] 用户提供 PNG 文件，或使用开源图标集
- [ ] TF 卡驱动（路径 B 需要）
