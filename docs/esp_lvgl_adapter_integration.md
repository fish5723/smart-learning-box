# esp_lvgl_adapter 集成文档

## 架构概览

```
┌─────────────────────────────────────────────────────────┐
│                    app_main()                            │
│                       │                                  │
│               system_init()                              │
│                 │        │         │                     │
│           lcd_init()  touch_init()  lvgl_adapter_init()  │
│                 │        │         │                     │
│                 ▼        ▼         ▼                     │
│  ┌──────────────┐ ┌──────────┐ ┌──────────────────────┐ │
│  │ JD9165BA     │ │ GT911    │ │ esp_lvgl_adapter     │ │
│  │ MIPI DSI     │ │ I2C      │ │  ├─ register_display │ │
│  │ 1024×600     │ │ 0x14     │ │  ├─ register_touch   │ │
│  │ RGB888       │ │          │ │  └─ start (task+tick) │ │
│  └──────────────┘ └──────────┘ └──────────────────────┘ │
│                                                        │
│          FreeRTOS Tasks                                 │
│          ├─ lvgl_task   (adapter 管理)                  │
│          └─ main_task   (app 逻辑)                      │
└─────────────────────────────────────────────────────────┘
```

## 数据流

### 显示链路

```
Kconfig (CONFIG_SMARTBOX_LCD_TEAR_AVOID_*)
  │
  ▼
lcd.c: get_tear_mode() → esp_lv_adapter_tear_avoid_mode_t
lcd.c: get_rotation() → esp_lv_adapter_rotation_t
  │
  ├─► esp_lv_adapter_get_required_frame_buffer_count()
  │     → num_fbs (1/2/3) → dpi_config.num_fbs
  │     → JD9165 panel 帧缓冲分配
  │
  └─► system_init.c: lvgl_adapter_init()
        → esp_lv_adapter_register_display()
          → LVGL display 创建 + 绘制缓冲 (PSRAM)
          → flush_cb → esp_lcd_panel_draw_bitmap
          → color_trans_done → lv_display_flush_ready
```

### 触摸链路

```
Kconfig (CONFIG_SMARTBOX_TOUCH_*)
  │
  ▼
touch.c: gt911_i2c_pins_t + esp_lcd_touch_config_t
  │
  ▼
esp_lcd_touch_gt911_create()
  ├─ I2C master 初始化 (I2C_NUM_1)
  ├─ GT911 硬件复位 + PID 验证
  ├─ esp_lcd_touch_t 虚函数表填充
  │   ├─ read_data   → I2C 读取状态+坐标
  │   ├─ get_xy      → 坐标解析 + SW 变换
  │   └─ del         → I2C 清理
  │
  ▼
esp_lv_adapter_register_touch(ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG)
  ├─ LVGL indev 创建
  ├─ 触摸读取回调注册
  └─ 可选: 中断模式 / 轮询模式
```

## 防撕裂模式 (Tear-Avoid)

| 模式 | 帧缓冲数 | 内存占用 | 适用场景 |
|------|---------|---------|---------|
| NONE | 1 | ~1.8 MB | 静态 UI |
| **DOUBLE_DIRECT** ★ | 2 | ~3.5 MB | 小区域/局部更新（推荐） |
| DOUBLE_FULL | 2 | ~3.5 MB | 全屏刷新/动画 |
| TRIPLE_PARTIAL | 3 | ~5.3 MB | 90°/270°旋转 + 局部刷新 |
| TRIPLE_FULL | 3 | ~5.3 MB | 90°/270°旋转 + 全屏动画 |

> ★ `DOUBLE_DIRECT` 是 MIPI DSI 默认推荐值，当前项目配置。

帧缓冲计算公式：
```
num_fbs = esp_lv_adapter_get_required_frame_buffer_count(tear_mode, rotation)

逻辑：
  rotation ∈ {90°, 270°} → 3
  tear_mode ∈ {TRIPLE_*}  → 3
  tear_mode ∈ {DOUBLE_*}  → 2
  tear_mode == NONE        → 1
```

## 组件依赖

```
idf_component.yml:
  lvgl/lvgl: 9.4.0
  espressif/esp_lvgl_adapter: '*'

CMakeLists.txt REQUIRES:
  esp_lcd_jd9165         ← JD9165BA LCD 驱动
  esp_lcd_touch          ← 触摸抽象接口
  esp_lcd_touch_gt911    ← GT911 驱动 (components/)
  esp_lvgl_adapter       ← LVGL 适配器
  lvgl                   ← LVGL 9.x
```

## Kconfig 关键配置

```ini
# sdkconfig.defaults 新增项

# FreeRTOS
CONFIG_FREERTOS_HZ=1000                          # 1ms tick 精度
CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y      # 允许 PSRAM 任务栈

# esp_lvgl_adapter
CONFIG_ESP_LVGL_ADAPTER_ENABLE_FPS_STATS=y       # FPS 统计
CONFIG_ESP_LVGL_ADAPTER_LVGL_THREAD_STACK_IN_PSRAM=y  # PSRAM 栈
CONFIG_ESP_LVGL_ADAPTER_PARTIAL_AUX_IMG_CACHE=y  # 图片缓存优化

# esp_lcd_touch
CONFIG_ESP_LCD_TOUCH_MAX_POINTS=5                # GT911 5 点触摸

# 项目 Kconfig (Kconfig.projbuild)
CONFIG_SMARTBOX_LCD_TEAR_AVOID_DOUBLE_DIRECT=y   # 双缓冲局部刷新
CONFIG_SMARTBOX_LCD_ROTATE_0=y                   # 横屏无旋转
```

## 线程模型

```
┌─────────────────────┐    ┌──────────────────────┐
│    main_task (P=3)   │    │   lvgl_task (P=6)     │
│    Stack: 8KB        │    │   Stack: 8KB (PSRAM)  │
├─────────────────────┤    ├──────────────────────┤
│ • app 业务逻辑       │    │ • lv_timer_handler()  │
│ • WiFi/AI/OCR       │    │ • 触摸轮询            │
│ • 游戏逻辑           │    │ • 绘制缓冲刷新         │
│                     │    │ • tick 定时器          │
│ 访问 LVGL API 前     │    │                       │
│ 必须 lock/unlock:    │    │                       │
│ esp_lv_adapter_lock  │◄───│ 共享 LVGL 互斥锁      │
│ ...lvgl calls...     │    │                       │
│ esp_lv_adapter_unlock│    │                       │
└─────────────────────┘    └──────────────────────┘
```

## API 使用示例

### 初始化（一次性）

```c
// 1. 硬件初始化
lcd_init();          // MIPI DSI + JD9165
touch_init();        // GT911 I2C

// 2. Adapter 初始化
const esp_lv_adapter_config_t cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
esp_lv_adapter_init(&cfg);

// 3. 注册显示
esp_lv_adapter_display_profile_t profile = {
    .interface = ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI,
    .rotation = ESP_LV_ADAPTER_ROTATE_0,
    .hor_res = 1024, .ver_res = 600,
    .buffer_height = 60,
    .use_psram = true,
    .require_double_buffer = true,
};
esp_lv_adapter_display_config_t disp_cfg = {
    .panel = lcd_get_panel(),
    .panel_io = lcd_get_panel_io(),
    .profile = profile,
    .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT,
    .te_sync = ESP_LV_ADAPTER_TE_SYNC_DISABLED(),
};
lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);

// 4. 注册触摸
const esp_lv_adapter_touch_config_t touch_cfg =
    ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch_get_handle());
esp_lv_adapter_register_touch(&touch_cfg);

// 5. 启动
esp_lv_adapter_start();
```

### App 层安全访问 LVGL

```c
// 在任何非 LVGL task 中操作 UI：
esp_lv_adapter_lock(-1);  // 无限等待
lv_obj_t *btn = lv_btn_create(lv_scr_act());
lv_label_set_text(label, "Hello");
esp_lv_adapter_unlock();
```

### 性能监控

```c
#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FPS_STATS
uint32_t fps;
esp_lv_adapter_get_fps(NULL, &fps);
ESP_LOGI(TAG, "FPS: %lu", (unsigned long)fps);
#endif
```

## GT911 驱动使用

```c
// 方式 1: 通过 touch BSP（推荐）
touch_init();
esp_lcd_touch_handle_t handle = touch_get_handle();

// 方式 2: 直接使用 GT911 驱动
gt911_i2c_pins_t i2c = { .scl = 11, .sda = 14, .addr = 0x14 };
esp_lcd_touch_config_t cfg = {
    .x_max = 1024, .y_max = 600,
    .rst_gpio_num = 12, .int_gpio_num = 13,
    .driver_data = &i2c,
};
esp_lcd_touch_handle_t tp;
esp_lcd_touch_gt911_create(NULL, &cfg, &tp);
```

## 文件清单

```
main/
├── app_main.c                  # 系统入口
├── system_init.c               # BSP→Adapter→App 初始化
├── system_init.h
├── CMakeLists.txt
├── bsp/
│   ├── lcd/lcd.c/h            # JD9165 MIPI DSI, 含 adapter num_fbs 计算
│   ├── touch/touch.c/h        # GT911 桥接层, 含 touch_get_handle()
│   └── lvgl/lvgl_port.c/h    # Adapter 封装层
├── app/
│   ├── home/                  # 首页（stub）
│   └── demo/demo_test.c/h    # 触摸+显示测试 Demo
└── idf_component.yml

components/
├── esp_lcd_jd9165/            # JD9165BA LCD 驱动
└── esp_lcd_touch_gt911/       # GT911 触摸驱动 (esp_lcd_touch_t)
    ├── include/esp_lcd_touch_gt911.h
    ├── esp_lcd_touch_gt911.c
    ├── CMakeLists.txt
    └── idf_component.yml

managed_components/
├── espressif__esp_lvgl_adapter/  # LVGL 适配器 (官方)
├── espressif__esp_lcd_touch/     # 触摸抽象接口 (官方)
└── lvgl__lvgl/                   # LVGL 9.4.0

sdkconfig.defaults                # Kconfig 默认值
```
