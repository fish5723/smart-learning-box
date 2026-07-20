# 智趣宝盒项目开发规范

项目名称：
智趣宝盒（Smart Learning Box）

项目目标：
开发一款基于ESP32-P4的AI游戏化学习终端。

开发环境：
- ESP-IDF 5.5.2
- LVGL 9.x
- VSCODE

硬件资源：
- 开发板：本产品是一款ESP32-P4核心板，并集成 ESP32-C6，支持 Wi-Fi 6 和蓝牙 5 无线连接。它提供丰富的人机交互接口，包括 MI-PI-CSI（集成图像信号处理器 ISP)、MIPI-DSI、SPI、12S、12C、LED PWM、MCPWM、RMT、ADC、UART、TWAI等。此外，支持 USB OTG 2.0 HS。
ESP32-P4 采用 400MHz 双核 RISC-V处理器，支持最大32MB PSRAM，具备 USB 2.0、MIPI-CSI/DSI、H.264 编码等外设，满足低成本、高性能、低功耗的多媒体开发需求。此外，ESP32-P4 集成数字签名外设和专用密钥管理单元，确保数据与操作安全。

##主要功能特性：
·搭载 RISC-V32位双核与单核处理器的高性能 MCU
· 128KB HP ROM, 16KB LP ROM, 768KB HP L2MEM, 32KB LP SRAM, 8KB TCM
·强大的图像与语音处理能力，图像与语音处理接口包括 JPEG 编解码器、像素处理加速器(PPA)、图像信号处理器(ISP)、H264 视频编码器
·芯片封装内叠封 32MB PSRAM，模组集成 16MB Nor Flash
·板上引出 MIPI-CSI、MIPI-DSI、USB 2.0OTG、SDIO3.0SD顶部用外设接口

- 显示触摸屏:7英寸IPS1024*600屏幕是GT911电容触摸MIPIDSI
- 摄像头: OV5640 MIPI-CSI (2592×1944)
- 扩展存储: 16GB TF卡 (SPI/SDIO/FatFS)
- 音频输入: 板载MEMS麦克风 (I2S/ES8311)
- 音频输出: 外接喇叭模块 (I2S/ES8311 PA)

项目架构：
本项目采用经典嵌入式软件架构：
```
text
APP
│
├── AI
├── OCR
├── GAME2048
├── ACHIEVEMENT
└── HOME

BSP
│
├── LCD
├── TOUCH
├── CAMERA
├── WIFI
└── STORAGE

USER
│
├── app_main.c
└── system_init.c

LVGL

ASSETS

DOCS

TEST
```

---

## 1. APP（应用层）

应用层负责实现产品功能和业务逻辑。

本层不直接操作硬件。

所有硬件访问必须通过 BSP 层接口完成。

### APP/HOME

首页功能模块。

负责：

* 首页界面
* 页面跳转
* 功能入口管理

示例接口：

```c
home_init();
home_show();
home_hide();
```

---

### APP/AI

AI教师模块。

负责：

* DeepSeek接入
* 对话管理
* 提示词管理
* 学习辅导

示例接口：

```c
ai_init();
ai_send_question();
ai_get_response();
```

---

### APP/OCR

OCR识题模块。

负责：

* 图像获取
* OCR识别
* 题目解析
* 结果输出

示例接口：

```c
ocr_init();
ocr_recognize();
```

---

### APP/GAME2048

数学2048模块。

负责：

* 游戏逻辑
* 分数统计
* 游戏状态保存

示例接口：

```c
game2048_init();
game2048_start();
game2048_move();
```

---

### APP/ACHIEVEMENT

成就系统。

负责：

* 积分统计
* 成就解锁
* 学习记录

示例接口：

```c
achievement_init();
achievement_update();
```

---

## 2. BSP（板级支持包）

BSP负责驱动硬件。

所有硬件相关代码均放在本层。

---

### BSP/LCD

显示驱动模块。

硬件：

* MIPI DSI LCD

负责：

* LCD初始化
* 背光控制
* 显示接口

示例接口：

```c
lcd_init();
lcd_set_backlight();
```

---

### BSP/TOUCH

触摸驱动模块。

硬件：

* GT911

负责：

* 触摸初始化
* 坐标读取

示例接口：

```c
touch_init();
touch_read();
```

---

### BSP/CAMERA

摄像头驱动模块。

硬件：

* OV5640

负责：

* 摄像头初始化
* 图像采集

示例接口：

```c
camera_init();
camera_capture();
```

---

### BSP/WIFI

网络模块。

负责：

* WiFi连接
* 网络状态管理

示例接口：

```c
wifi_init();
wifi_connect();
```

---

### BSP/STORAGE

存储模块。

负责：

* Flash读写
* 配置保存
* 数据存档

示例接口：

```c
storage_init();
storage_read();
storage_write();
```

---

## 3. USER（用户入口层）

项目启动入口。

负责：

* 系统初始化
* 模块初始化
* 任务创建

本层不实现业务逻辑。

---

### app_main.c

项目主入口。

示例：

```c
void app_main(void)
{
    system_init();
}
```

---

### system_init.c

系统初始化管理。

负责：

* BSP初始化
* LVGL初始化
* APP初始化
* 创建双任务（LVGL UI 任务 + 主逻辑任务）

示例：

```c
void system_init(void)
{
    // 1. BSP 初始化
    lcd_init();
    touch_init();
    wifi_init();

    // 2. LVGL 初始化
    lv_init();
    lvgl_display_init();
    lvgl_input_init();

    // 3. APP 初始化
    home_init();
    game2048_init();

    // 4. 启动双任务
    xTaskCreate(lvgl_task,  "lvgl",  8192, NULL, 5, NULL);
    xTaskCreate(main_task,  "main",  8192, NULL, 3, NULL);
}
```

---

## 4. LVGL（界面层）

负责全部图形界面。

包含：

* 页面创建
* 控件管理
* 动画效果
* 事件处理

禁止：

* 网络请求
* AI调用
* OCR处理

UI层只负责显示。

---

## 5. ASSETS（资源文件）

项目资源目录。

包含：

* 图片
* 图标
* 字体
* 动画资源

示例：

```text
ASSETS/
├── fonts/
├── images/
├── icons/
└── themes/
```

---

## 6. DOCS（项目文档）

项目文档目录。

包含：

```text
DOCS/
├── MVP.md
├── HARDWARE.md
├── ROADMAP.md
├── ARCHITECTURE.md
└── API.md
```

说明：

* MVP：最小可行产品定义
* HARDWARE：硬件说明
* ROADMAP：开发路线图
* ARCHITECTURE：架构设计
* API：接口说明

---

## 7. TEST（测试目录）

测试代码目录。

用于：

* 单元测试
* 模块验证
* 性能测试

比赛期间可简化使用。

---

## 模块调用关系

```text
APP
 ↓
BSP
 ↓
Hardware

APP
 ↓
LVGL

USER
 ↓
初始化APP/BSP/LVGL
```

原则：

1. APP不能直接访问硬件
2. LVGL不能处理业务逻辑
3. BSP只负责驱动
4. USER只负责启动
5. 模块之间通过接口通信

```
```

# 智趣宝盒项目开发规则

## 当前开发阶段

项目当前处于：

**V1 MVP（最小可行产品）开发阶段**

目标：

优先完成比赛演示功能。

暂不追求复杂架构和高并发设计。

---

# 一、开发原则

## 1. 功能优先

优先实现功能可运行。

避免过度设计。

禁止为了架构而架构。

---

## 2. 渐进式开发

开发顺序：

硬件验证

↓

LVGL界面

↓

游戏功能

↓

AI功能

↓

OCR功能

↓

成就系统

↓

性能优化

---

## 3. 简单优先

优先采用最简单、最稳定的实现方案。

禁止提前设计未来可能用到的复杂系统。

---

## 4. 可维护性优先

代码应：

* 易阅读
* 易修改
* 易扩展

避免炫技式代码。

---

# 二、架构规则

项目采用：

APP + BSP + USER

架构。

---

## APP职责

负责：

* AI功能
* OCR功能
* 游戏功能
* 成就系统
* 页面业务逻辑

禁止：

* 操作GPIO
* 操作I2C
* 操作SPI
* 操作摄像头寄存器

APP不得直接访问硬件。

---

## BSP职责

负责：

* LCD驱动
* GT911驱动
* 摄像头驱动
* WiFi管理
* 存储管理

禁止：

* AI逻辑
* OCR逻辑
* 游戏逻辑

BSP只负责硬件驱动。

---

## USER职责

负责：

* 系统启动
* 模块初始化
* 程序入口

禁止：

* 编写业务逻辑

---

## LVGL职责

负责：

* 页面显示
* 控件管理
* 动画效果
* 用户交互

禁止：

* AI请求
* OCR处理
* 网络请求

LVGL只负责界面显示。

---

# 三、当前阶段RTOS规则

ESP-IDF 底层运行在 FreeRTOS 之上。项目采用**多任务模式**解决 UI 卡死问题，同时控制复杂度在合理范围内。

---

## 常驻任务（2个，系统初始化时创建，全生命周期运行）

### 任务1：LVGL UI 任务（高优先级）

```c
xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 5, NULL);
```

职责：
- 定时调用 `lv_timer_handler()`（每 ~5ms）
- 处理触摸输入
- 页面渲染和动画

禁止：
- 网络请求
- 耗时操作

### 任务2：主逻辑任务（普通优先级）

```c
xTaskCreate(main_task, "main", 8192, NULL, 3, NULL);
```

职责：
- WiFi 连接
- 游戏逻辑
- 成就系统

---

## 临时任务（按需创建，完成即销毁）

以下场景允许在常驻任务之外**按需创建临时任务**。临时任务生命周期限定在单次操作内，操作完成后必须自毁（`vTaskDelete(NULL)`）。

### OCR 识别任务

```c
xTaskCreate(ocr_task, "ocr_req", 49152, param, 3, NULL);
```

职责：拍照 + Vision API OCR + 文本 LLM 讲解（两阶段）。阻塞式 HTTP/SSE，耗时 3-10 秒。

### 摄像头预览任务

```c
xTaskCreate(camera_preview_task, "cam_preview", 65536, NULL, 3, NULL);
```

职责：持续抓帧（~2fps）→ 更新预览区。用户点击"确认拍摄"或"取消"时停止。

### AI 对话任务

```c
xTaskCreate(ai_request_task, "ai_req", 24576, param, 3, NULL);
```

职责：DeepSeek 文本 LLM 请求 + SSE 流式读取。阻塞式 HTTP/SSL，耗时 2-5 秒。

### 临时任务约束

- 必须由 `s_busy` 或等效标志保证**同一时刻只有一个同类实例**
- 预览任务通过 `volatile bool` 停止信号退出循环后再自毁
- 栈大小按需分配（预览 64KB，OCR 48KB，AI 24KB；大缓冲区必须在堆上分配，不可压在任务栈上）
- HTTP/SSE 阻塞调用必须有总超时保护（LLM 60s，Vision 120s）

---

## RTOS 机制使用规则

### 允许使用

| 机制 | 用途 | 约束 |
|------|------|------|
| `xTaskCreate` | 创建常驻任务 + 上述临时任务 | 临时任务用完即删 |
| `vTaskDelete(NULL)` | 临时任务自毁 | 仅用于临时任务 |
| `lv_async_call` | 跨任务通知 UI 刷新 | 首选方案 |
| `lv_timer_create` | LVGL 内周期性操作（如预览帧轮询） | LVGL 上下文内执行 |
| `xSemaphoreCreateMutex` | 保护跨任务共享资源 | 仅限 camera 帧缓冲等硬件资源 |
| `volatile bool` | 任务间信号（停止/完成标志） | 单写入者模式 |
| `vTaskDelay` | 非阻塞延时 | |

### 禁止使用

1. Queue
2. EventGroup
3. 消息总线
4. 任务管理器（`eTaskGetState` 等调试 API）
5. 递归 Mutex
6. 二值/计数信号量（除 Mutex 外）

---

## 任务间通信（按场景选择）

### 场景 A：一次性通知（主 → UI）

```c
// 主任务：写入共享变量 → 通知
ai_response = "你好，同学！";
lv_async_call(update_ai_label, NULL);

// UI 任务：lv_async_call 回调
void update_ai_label(void *arg) {
    lv_label_set_text(ai_label, ai_response);
}
```

### 场景 B：持续数据流（预览帧）

```c
// 预览任务：Mutex 保护下投递帧
xSemaphoreTake(s_preview_mutex, portMAX_DELAY);
if (s_pending_frame == NULL) {
    s_pending_frame = new_frame;  // 转移所有权
}
xSemaphoreGive(s_preview_mutex);

// LVGL timer：定期消费帧（LVGL 上下文）
xSemaphoreTake(s_preview_mutex, 0);  // 非阻塞
uint8_t *frame = s_pending_frame;
s_pending_frame = NULL;
xSemaphoreGive(s_preview_mutex);
// ... 更新 lv_image ...
```

### 场景 C：停止信号

```c
// 任何任务：设置停止标志
s_preview_running = false;  // volatile bool

// 被控任务：循环检查
while (s_preview_running) { ... }
```

---

## 为什么不是单线程

| 场景 | 单线程后果 | 多任务效果 |
|------|-----------|-----------|
| WiFi 连接（2-10秒） | UI 冻结数秒 | UI 正常显示连接动画 |
| DeepSeek HTTP 请求（1-5秒） | 屏幕卡死 | 用户可操作其他功能 |
| 摄像头预览（持续 2fps） | 不可能实现 | 实时预览 + UI 响应 |
| 2048 游戏动画 | 动画卡顿 | 流畅运行 |

---

# 四、命名规范

## 文件命名

采用：

模块名.c

模块名.h

示例：

lcd.c

touch.c

camera.c

game2048.c

---

## 函数命名

采用：

模块名_功能名()

示例：

lcd_init()

touch_read()

camera_capture()

game2048_move()

ai_send_question()

---

## 宏定义

全部大写。

示例：

LCD_WIDTH

LCD_HEIGHT

WIFI_SSID

---

## 结构体

采用：

xxx_t

示例：

touch_point_t

game_state_t

ocr_result_t

---

# 五、代码规范

## 注释要求

所有公共函数必须添加注释。

示例：

/**

* @brief 初始化LCD
*
* @return ESP_OK 成功
  */
  esp_err_t lcd_init(void);

---

## 禁止超长函数

单个函数原则上：

不超过100行。

---

## 禁止魔法数字

错误：

if(x > 1024)

正确：

#define LCD_WIDTH 1024

if(x > LCD_WIDTH)

---

## 禁止重复代码

相同逻辑应封装成函数。

---

# 六、开发优先级

Phase 1

硬件验证

* LCD
* GT911
* LVGL

---

Phase 2

基础功能

* 首页
* 2048游戏

---

Phase 3

联网功能

* WiFi
* DeepSeek

---

Phase 4

比赛亮点

* OCR识题
* AI讲解

---

Phase 5

扩展功能

* 成就系统
* 学习报告

---

# 七、代码生成规则（AI开发）

使用Codex生成代码时：

必须先输出：

1. 模块设计说明

2. 文件结构

3. 接口设计

确认后再生成代码。

禁止直接生成大量代码。

---


# 八、最终目标

在比赛截止前完成：

* 首页
* AI老师
* OCR识题
* 数学2048
* 成就系统

形成完整：

游戏 → 学习 → AI辅导 → 成就激励

闭环系统。
# 九、第三方组件管理

优先采用 ESP-IDF 官方组件和 Espressif Component Registry 中经过验证的组件。

禁止重复造轮子。

当前推荐组件：

显示驱动：

* esp_lcd

触摸驱动：

* esp_lcd_touch
* esp_lcd_touch_gt911

图形界面：

* lvgl/lvgl

网络：

* esp_wifi
* esp_http_client

存储：

* nvs_flash
* fatfs

原则：

1. 优先使用官方组件
2. 优先使用社区成熟方案
3. 官方组件无法满足需求时才允许自行编写驱动
4. 新增第三方组件必须记录版本号

---

# 十、错误处理规范

所有 BSP 层接口必须返回：

```c
esp_err_t
```

示例：

```c
esp_err_t lcd_init(void);

esp_err_t touch_init(void);

esp_err_t camera_init(void);

esp_err_t wifi_connect(void);
```

调用方必须检查返回值。

示例：

```c
ESP_ERROR_CHECK(lcd_init());

ESP_ERROR_CHECK(touch_init());
```

禁止：

```c
lcd_init();

touch_init();
```

忽略返回结果。

---

# 十一、日志规范

统一使用 ESP-IDF Log 系统。

模块内定义：

```c
static const char *TAG = "LCD";
```

日志级别：

```c
ESP_LOGI(TAG, "LCD Init Success");

ESP_LOGW(TAG, "Touch Not Ready");

ESP_LOGE(TAG, "Camera Init Failed");
```

规则：

INFO：

正常运行信息

WARN：

可恢复异常

ERROR：

严重错误

禁止：

```c
printf("hello");
```

作为正式日志方案。

---

# 十二、配置管理规范

项目统一使用：

NVS

管理配置数据。

禁止应用层直接访问 NVS。

所有配置必须通过 Storage 模块访问。

例如：

```c
storage_set_wifi_ssid();

storage_set_wifi_password();

storage_set_api_key();

storage_get_score();
```

允许存储：

* WiFi信息
* API Key
* 游戏记录
* 学习记录
* 用户配置

禁止：

各模块直接创建自己的 NVS Namespace。

统一由 Storage 模块管理。

---

# 十三、LVGL与BSP边界规范

职责划分：

BSP负责：

* LCD驱动
* GT911驱动
* 摄像头驱动

LVGL负责：

* 页面显示
* 控件管理
* 用户交互

关系：

```text
BSP/LCD
   ↓
LVGL Display Driver

BSP/TOUCH
   ↓
LVGL Input Driver
```

原则：

1. LVGL不得直接操作硬件寄存器
2. BSP不得创建LVGL页面
3. UI逻辑放在LVGL层
4. 硬件逻辑放在BSP层

---

# 十四、资源管理规范（ASSETS）

资源目录：

```text
ASSETS/
├── fonts/
├── images/
├── icons/
└── themes/
```

资源来源：

字体：

```text
TTF
```

图片：

```text
PNG
JPG
```

编译前统一转换为：

```text
LVGL资源文件
```

生成：

```text
assets/fonts/*.c

assets/images/*.c
```

由 LVGL 直接引用。

禁止：

运行时动态下载资源。

---

# 十五、测试策略

当前项目处于 MVP 阶段。

采用人工验证方式。

测试方法：

1. 串口日志验证
2. 屏幕显示验证
3. 功能操作验证

示例：

LCD测试：

* 是否正常显示

触摸测试：

* 是否正确获取坐标

WiFi测试：

* 是否成功联网

AI测试：

* 是否成功获取回复

后续如项目规模扩大，再引入：

* Unity
* CppUTest
* Mock测试

当前阶段不强制使用自动化测试框架。

---

# 十六、当前项目状态

项目当前处于：

MVP开发阶段

当前目标：

1. LCD点亮
2. GT911触摸
3. LVGL运行
4. 首页UI
5. 数学2048

暂不开发：

* 本地AI推理
* 语音识别
* 微信小程序
* 多设备联机
* 人脸识别

生成代码时必须优先保证以上目标实现。

---



