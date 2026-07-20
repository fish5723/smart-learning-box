# TF 卡字库方案

## 动机

当前 4 个 CJK 字体内嵌在固件中，占用空间巨大：

| 字号 | 文件 | 大小 |
|------|------|------|
| 14px | `fonts/fontcn14.c` | 2.46 MB |
| 16px | `fonts/fontcn16.c` | 3.04 MB |
| 20px | `fonts/fontcn20.c` | 4.26 MB |
| 24px | `fonts/fontcn24.c` | 5.92 MB |
| **合计** | | **~15.7 MB** |

ESP32-P4 模组 NOR Flash 仅 **16MB**，字体文件已接近物理上限。固件代码 + LVGL + ESP-IDF 框架还需 ~1-2MB，几乎无法容纳更多功能。

**结论：字库必须迁移到 TF 卡。**

## 目标

1. 从固件中移除 4 个内嵌字体 .c 文件
2. 将字体以二进制格式存放在 TF 卡中
3. 启动时从 TF 卡加载字体，供 LVGL 使用
4. 释放 Flash 空间给 P0 功能（WiFi、AI、相机）

## 技术方案对比

### 方案 A：LVGL BinFont（推荐）

```
TF 卡存储: fonts/*.bin (LVGL 二进制字体格式)
    ↓
启动时加载到 PSRAM
    ↓
LVGL 直接使用 (lv_binfont_create)
```

**优点：**
- 渲染速度快，LVGL 原生支持
- 内存可控，可只加载需要的字号
- 与当前内嵌字体使用方式一致（位图字体）

**缺点：**
- 需要预先生成各字号的 .bin 文件
- 一个字号一个文件，管理略繁琐
- 字符集固定，无法动态添加生僻字

**生成工具：**
```bash
# LVGL 官方字体转换器命令行版
lv_font_conv --font SourceHanSerifSC-VF.ttf \
  --format bin \
  --size 16 \
  --bpp 4 \
  --range 0x20-0x7F,0x4E00-0x9FFF \
  -o fontcn16.bin
```

### 方案 B：FreeType 实时渲染

```
TF 卡存储: fonts/SourceHanSerifSC-VF.ttf (一个文件 ~15MB)
    ↓
LVGL FreeType 引擎
    ↓
实时渲染任意字号
```

**优点：**
- 一个 TTF 文件覆盖所有字号
- 支持任意字号（12/14/16/18/20/22/24...）
- 抗锯齿效果好

**缺点：**
- CPU 开销大（实时渲染中文字体）
- 需要 PSRAM 缓存渲染结果
- ESP32-P4 上渲染速度待验证
- FreeType 库增加固件体积

**配置：**
```c
// sdkconfig
CONFIG_LV_USE_FREETYPE=y
CONFIG_LV_FREETYPE_CACHE_SIZE=1024  // KB
```

### 建议

**先走方案 A（BinFont），方案 B 作为备选。** BinFont 与当前内嵌位图字体使用方式一致，迁移成本最低。如果后续需要动态字号再用 FreeType 补充。

## 文件结构

```
TF 卡根目录:
  /fonts/
    ├── fontcn14.bin     (~2.5 MB, GB2312 + ASCII)
    ├── fontcn16.bin     (~3.0 MB)
    ├── fontcn20.bin     (~4.3 MB)
    ├── fontcn24.bin     (~5.9 MB)
    └── emoji.bin        (可选, 表情符号)
  /icons/
    ├── ai_teacher.bin
    ├── ocr_camera.bin
    └── ...
  /roms/
    ├── mario.nes
    └── ...
```

## 实现步骤

### Step 1: TF 卡驱动 + FAT 文件系统

```c
// bsp/sd_card/sd_card.c
esp_err_t sd_card_init(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // 如果硬件用 SPI 模式，改用 SDSPI_HOST_DEFAULT()

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    // 根据原理图设置引脚: CLK, CMD, D0, D1, D2, D3

    sdmmc_card_t *card;
    return esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot,
                                    &mount_config, &card);
}
```

**需要确认的硬件信息：**
- [ ] TF 卡槽接口模式：SDMMC(4-bit) 还是 SPI？
- [ ] 引脚映射：CLK / CMD / D0 / D1 / D2 / D3（SDMMC）或 SCK / MOSI / MISO / CS（SPI）
- [ ] 卡检测引脚 (CD) 是否有用？

### Step 2: LVGL BinFont 加载器

```c
// app/font_loader/font_loader.c
#include "lvgl.h"

static lv_font_t *s_font_cjk_14 = NULL;
static lv_font_t *s_font_cjk_16 = NULL;
static lv_font_t *s_font_cjk_20 = NULL;
static lv_font_t *s_font_cjk_24 = NULL;

esp_err_t font_loader_init(void)
{
    // 从 TF 卡加载二进制字体
    s_font_cjk_14 = lv_binfont_create("S:/fonts/fontcn14.bin");
    s_font_cjk_16 = lv_binfont_create("S:/fonts/fontcn16.bin");
    s_font_cjk_20 = lv_binfont_create("S:/fonts/fontcn20.bin");
    s_font_cjk_24 = lv_binfont_create("S:/fonts/fontcn24.bin");

    if (!s_font_cjk_16) {
        ESP_LOGE(TAG, "Failed to load CJK fonts from SD card");
        return ESP_FAIL;
    }

    // 赋值给全局指针（保持与现有代码兼容）
    g_font_cjk_14 = s_font_cjk_14;
    g_font_cjk_16 = s_font_cjk_16;
    g_font_cjk_20 = s_font_cjk_20;
    g_font_cjk_24 = s_font_cjk_24;

    return ESP_OK;
}
```

### Step 3: 修改初始化流程

```c
// system_init.c 中的变化
void system_init(void)
{
    lcd_init();
    touch_init();
    sd_card_init();       // 🆕 在 LVGL 之前挂载 TF 卡
    lvgl_port_init();

    xTaskCreate(main_task, "main", 8192, NULL, 3, NULL);
}

void main_task(void *arg)
{
    esp_lv_adapter_lock(-1);

    font_loader_init();   // 🆕 从 TF 卡加载字体（替代 home_fonts_init）
    home_init();
    ai_init();
    ocr_init();
    game_center_init();
    achievement_init();

    esp_lv_adapter_unlock();
    // ...
}
```

### Step 4: 清理固件中的字体内嵌

```cmake
# main/CMakeLists.txt — 删除以下行：
# "fonts/fontcn14.c"
# "fonts/fontcn16.c"
# "fonts/fontcn20.c"
# "fonts/fontcn24.c"
```

从 `home_ui.c` 中移除 `LV_FONT_DECLARE(fontcn1x)` 声明和 `home_fonts_init()` 中的内嵌字体加载代码。

## 兼容方案（TF 卡未插入时）

```c
esp_err_t font_loader_init(void)
{
    // 优先从 TF 卡加载
    if (sd_card_is_mounted()) {
        return font_loader_from_sd();
    }

    // 回退：使用固件内置的最小字库（仅 ASCII + 常用字）
    ESP_LOGW(TAG, "SD card not found, using fallback font");
    return font_loader_fallback();
}
```

## 依赖

- [ ] **TF 卡硬件原理图**（确认 SDMMC vs SPI、引脚映射）
- [ ] SDMMC/SPI 驱动开发
- [ ] FAT 文件系统挂载
- [ ] LVGL binfont 或 FreeType 组件启用
- [ ] 字体 .bin 文件生成脚本

## 收益

| 项目 | 迁移前 | 迁移后 |
|------|--------|--------|
| 字体 Flash 占用 | ~15.7 MB | 0 |
| 可用 Flash | ~0.3 MB | ~14 MB+ |
| 支持的字符集 | GB2312 固定 | 可按需扩展 |
| 字号灵活性 | 4 档固定 | 可增加更多字号 |
