# 智趣宝盒 Smart Learning Box

基于 **ESP32-P4 (RISC-V 双核 400MHz)** 的 AI 游戏化学习终端。全国大学生物联网设计竞赛项目。

## 功能

| 模块 | 说明 |
|------|------|
| **AI 教师** | 对接大模型 API（Qwen3-VL + 豆包），支持自然语言对话与学科答疑 |
| **OCR 拍照搜题** | OV5647 摄像头拍照 → 视觉模型文字识别 → 解题推理全链路，<10s 返回答案 |
| **NES 游戏模拟器** | 移植 InfoNES 内核，帧率 20-50fps，触摸虚拟手柄，支持 ROM 加载与存档管理 |
| **游戏中心** | 2048、数学闯关、单词王等教育类小游戏 |
| **错题本** | 自动归档解答记录，形成学习闭环 |
| **成就系统** | 积分、等级、徽章，游戏化学习激励 |

## 硬件

| 组件 | 规格 |
|------|------|
| MCU | ESP32-P4 (RISC-V 双核 400MHz, 32MB PSRAM) |
| 协处理器 | ESP32-C6 (WiFi 6 / BLE 5, SDIO) |
| 显示 | 7" IPS 1024×600, MIPI-DSI, JD9165 驱动 |
| 触摸 | GT911 电容触摸 |
| 摄像头 | OV5647, MIPI-CSI, 800*800 |
| 音频 | MEMS 麦克风 + 外接喇叭 (I2S/ES8311)（未实现） |
| 存储 | 32GB TF 卡 (FatFS) + 16MB NOR Flash |

## 软件架构

```
APP
├── AI        → AI 对话 + 大模型流式响应
├── OCR       → 拍照搜题 + 相册导入解题
├── GAME      → 2048 / 数学闯关 / 单词王 / NES 模拟器
├── ACHIEVEMENT → 积分、等级、徽章
└── HOME      → 首页功能入口

BSP
├── LCD       → MIPI-DSI 显示驱动
├── TOUCH     → GT911 触摸驱动
├── CAMERA    → OV5647 驱动
├── WIFI      → ESP32-C6 远程 WiFi (SDIO)
├── STORAGE   → TF 卡 FatFS 读写
└── WEB_SERVER → HTTP 服务

LVGL 9.x → 图形用户界面
```

## 开发环境

- **ESP-IDF** 5.5.x
- **LVGL** 9.4.0
- **工具链** RISC-V GCC
- **构建** CMake + Ninja

```bash
idf.py set-target esp32p4
idf.py build
idf.py flash monitor
```

## 镜像大小

当前固件约 **926KB**，PSRAM 占用约 **25.7%**。

## 许可

MIT License
