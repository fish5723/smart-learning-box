# 小霸王（NES）游戏模拟器移植方案

## 目标

在智趣宝盒中集成 NES/Famicom 模拟器，通过 TF 卡加载 ROM 文件，使用触摸屏作为虚拟手柄，实现经典红白机游戏的游玩体验。

## 可行性分析

### ESP32-P4 性能评估

| 指标 | NES 要求 | ESP32-P4 能力 | 结论 |
|------|------|------|------|
| CPU | ~1.79 MHz (6502) + PPU | 双核 RISC-V 400MHz | ✅ 远超 |
| RAM | NES: 2KB WRAM + 2KB VRAM | 32MB PSRAM | ✅ 绰绰有余 |
| 分辨率 | 256×240 (NES 原始) | 1024×600 支持缩放 | ✅ 可 2x/3x 缩放 |
| 帧率 | 60fps (NTSC) / 50fps (PAL) | MIPI DSI 60Hz | ✅ 可全速 |
| 音频 | NES APU (5 通道) | I2S 输出 | ⚠️ 需音频驱动 |
| 输入 | 2 个手柄 (各 8 键) | GT911 触摸屏 | ⚠️ 虚拟手柄 |
| 存储 | ROM ~40KB - 512KB | TF 卡 ≥4GB | ✅ 充分 |

**结论：ESP32-P4 运行 NES 模拟器性能完全足够，主要工作是移植和适配。**

## 可选模拟器内核

### 1. Nofrendo（推荐）

| 项目 | 说明 |
|------|------|
| 语言 | C |
| 许可 | GPL |
| 精度 | 高（周期级 PPU） |
| 移植难度 | 中等 |
| ESP32 移植 | 已有 [esp32-nofrendo](https://github.com/mesummer/esp32-nofrendo) |

### 2. InfoNES

| 项目 | 说明 |
|------|------|
| 语言 | C |
| 许可 | GPL |
| 精度 | 中等 |
| 移植难度 | 低 |
| ESP32 移植 | 已有 [nesemu_esp32](https://github.com/li2727/nesemu_esp32) |

### 3. Nestopia UE

| 项目 | 说明 |
|------|------|
| 语言 | C++ |
| 精度 | 最高（周期精确） |
| 移植难度 | 高 |
| 备注 | 对 P4 性能消耗较高，不推荐首选用 |

**推荐顺序：先试 InfoNES（移植最简单）→ 如果兼容性不够再换 Nofrendo。**

## 架构设计

```
┌──────────────────────────────────────────────────┐
│                   智趣宝盒                         │
│                                                   │
│  ┌──────────┐   ┌──────────┐   ┌──────────────┐  │
│  │  LVGL UI  │   │ NES 核心  │   │  音频输出    │  │
│  │  (Core 1) │   │ (Core 0) │   │  (I2S DMA)  │  │
│  │           │   │          │   │              │  │
│  │ ROM 浏览器│◄──│ Nofrendo │──►│  APU 模拟    │  │
│  │ 虚拟手柄  │   │ 6502+PPU │   │  5通道混音   │  │
│  └──────────┘   └──────────┘   └──────────────┘  │
│        │              │                 │          │
│        ▼              ▼                 ▼          │
│  ┌──────────────────────────────────────────────┐  │
│  │                  TF 卡                        │  │
│  │  /roms/*.nes    /saves/*.sav                  │  │
│  └──────────────────────────────────────────────┘  │
│                                                   │
│  触摸屏 GT911 (I2C)   MIPI DSI LCD (1024×600)     │
└──────────────────────────────────────────────────┘
```

### 任务分配

| Core | 任务 | 说明 |
|------|------|------|
| Core 0 | NES 模拟器主循环 | 6502 + PPU 模拟 + APU 音频 |
| Core 1 | LVGL UI 渲染 | ROM 浏览器、虚拟手柄叠加层、设置菜单 |

### 帧缓冲对接

```c
// 方案：模拟器渲染到内存缓冲区 → 通过 LVGL canvas 或直接刷到 display

// NES 输出: 256×240 RGB565
static uint16_t nes_framebuffer[256 * 240];

// 缩放后拷贝到 LVGL display buffer
void nes_frame_callback(const uint16_t *fb) {
    // 方式 A: LVGL image 缩放
    lv_image_set_src(nes_screen, fb);  // LVGL 自动缩放

    // 方式 B: 手动 2x 最近邻缩放 → 512×480
    scale2x_nearest(fb, scaled_buffer, 256, 240);
    // 直接刷到 MIPI DSI framebuffer 指定区域
}
```

### 虚拟手柄布局

```
┌──────────────────────────────────────────────┐
│                                              │
│          NES 画面 (512×480, 居中)             │
│                                              │
│  ┌──────┐              ┌──────┬──────┐      │
│  │  ←   │              │  Ⓑ  │  Ⓐ  │      │
│  ├──┬───┤              ├──────┼──────┤      │
│  │↑ │ ↓ │              │SELECT│START │      │
│  ├──┼───┤              └──────┴──────┘      │
│  │  →   │                                   │
│  └──────┘                                   │
└──────────────────────────────────────────────┘
```

**按键映射：**

| NES 手柄 | 触摸虚拟键 | 位置 |
|------|------|------|
| ↑↓←→ | 方向键区域 | 左下角 |
| A | Ⓐ 按钮 | 右下角 |
| B | Ⓑ 按钮 | 右下角 |
| Select | SELECT | 右下角 |
| Start | START | 右下角 |

所有虚拟按键使用 `LV_OBJ_FLAG_CLICKABLE` + `LV_EVENT_PRESSED`/`LV_EVENT_RELEASED` 事件，直接写入模拟器的手柄状态寄存器。

### 音频输出

ESP32-P4 的 I2S 外设可以输出 PCM 音频：

```c
// I2S 配置
i2s_chan_config_t chan_cfg = {
    .id = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .dma_desc_num = 4,
    .dma_frame_num = 240,  // 每帧采样数
    .auto_clear = true,
};

// NES APU 输出 ~44.1kHz, 16-bit mono → I2S
// 使用双缓冲 DMA，模拟器每帧产生 ~735 采样点
```

**简化方案（MVP 阶段可暂时静音）：** 第一版先不接音频，专注于画面显示和输入。音频是独立模块，不影响核心玩法验证。

## 实现阶段

### Phase 1: 最小原型（3-4 天）

```
目标：在屏幕上看到 NES 游戏画面，能操作
```

- [ ] 选择并移植模拟器内核（InfoNES 或 Nofrendo）
- [ ] 实现 framebuffer → LVGL display 对接
- [ ] 实现触摸屏虚拟手柄（8 个按键）
- [ ] 从 TF 卡加载 ROM 文件
- [ ] 用一个简单 ROM 验证（如 Super Mario Bros.）

### Phase 2: 完善体验（3-4 天）

```
目标：可玩的游戏体验
```

- [ ] ROM 浏览器 UI（列表/网格浏览 TF 卡中的 ROM）
- [ ] 存档/读档功能（.sav 文件存 TF 卡）
- [ ] 虚拟手柄皮肤优化（半透明叠加、按键反馈动画）
- [ ] 音频 I2S 输出
- [ ] 帧率显示 / 性能监控
- [ ] 暂停 / 快进 功能

### Phase 3: 差异化（2-3 天）

```
目标：智趣宝盒独有的特色功能
```

- [ ] 游戏化积分：玩 NES 游戏也赚学习积分
- [ ] AI 游戏助手：卡关时 AI 给出提示
- [ ] 游戏时间管理（家长控制）
- [ ] 竖屏模式模拟 GameBoy 体验（可选）

## 代码结构

```
main/app/nes_emu/
├── nes_emu.h             # 模拟器模块入口
├── nes_emu.c             # 初始化、主循环、状态管理
├── nes_ui.h              # ROM 浏览器 + 游戏画面 UI
├── nes_ui.c              # LVGL 页面实现
├── nofrendo/             # 模拟器内核（第三方代码）
│   ├── cpu.c / cpu.h     # 6502 CPU
│   ├── ppu.c / ppu.h     # 图像处理单元
│   ├── apu.c / apu.h     # 音频处理单元
│   ├── mmc.c / mmc.h     # Mapper/MMC
│   └── ...
├── virtual_gamepad.c     # 触摸虚拟手柄
├── rom_loader.c          # ROM 文件加载
└── save_manager.c        # 存档管理

# 新增到 main/CMakeLists.txt
"app/nes_emu/nes_emu.c"
"app/nes_emu/nes_ui.c"
"app/nes_emu/virtual_gamepad.c"
"app/nes_emu/rom_loader.c"
"app/nes_emu/save_manager.c"
"app/nes_emu/nofrendo/*.c"
```

## 集成到现有 UI

在 Game Center 页面增加第 5 张卡片或替换"更多游戏"卡片：

```c
// game_center_ui.c 中的 s_games[] 数组
{LV_SYMBOL_VIDEO, "经典小霸王", "重温红白机经典", "+15 积分", "开始挑战",
 COLOR_ICON_NES, true},  // unlocked = true，最高优先级
```

NES 页面作为独立 LVGL screen 存在，从 Game Center 导航进入。

## 法律注意事项

- NES 模拟器本身是合法的（逆向工程/学术研究）
- ROM 文件版权：用户需自行拥有正版游戏卡带才能合法使用 ROM
- Nintendo 对 ROM 分发持严格态度，智趣宝盒不预装任何 ROM
- 模拟器可内置一个开源/自制 ROM（如公共领域的 homebrew 游戏）

## 依赖

- [ ] TF 卡驱动（存放 ROM）
- [ ] 选择合适的模拟器内核并移植
- [ ] I2S 音频驱动（ESP-IDF 内置）
- [ ] 触摸屏能处理多点触控（GT911 支持 5 点 → 方向键+A 同时按）
- [ ] ROM 文件的合法性确认

## 参考资料

- Nofrendo ESP32 移植: https://github.com/mesummer/esp32-nofrendo
- InfoNES ESP32 移植: https://github.com/li2727/nesemu_esp32  
- NESDev Wiki (PPU/APU 参考): https://www.nesdev.org/wiki/
- GT911 多点触控: 当前 `bsp/touch/touch.c` 已有 `touch_read_multi()` API
