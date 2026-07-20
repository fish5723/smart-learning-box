# 构建状态与迁移记录

## 环境信息

| 项目 | 值 |
|------|-----|
| 芯片 | ESP32-P4 (RISC-V 双核 400MHz) |
| ESP-IDF | 5.5.4 |
| LVGL | 9.4.0 |
| 模组 | 16MB NOR Flash + 32MB PSRAM |
| WiFi 方案 | ESP32-C6 协处理器 (esp_wifi_remote, SDIO) |

## 当前构建状态

- 构建系统: Ninja + CMake
- 目标: `esp32p4`
- 构建结果: ✅ **编译通过**

## 迁移与修复记录

### 1. 托管组件依赖

`esp_wifi_remote` 不是 ESP-IDF 内置组件，需通过 IDF Component Manager 安装。

**修复**: 在 `main/idf_component.yml` 中添加:
```yaml
espressif/esp_wifi_remote: "^1.0.0"
```

自动下载的依赖组件:
| 组件 | 版本 | 用途 |
|------|------|------|
| espressif/esp_wifi_remote | 1.6.1 | ESP32-P4 远程 WiFi |
| espressif/esp_hosted | 2.12.9 | Hosted WiFi 协议 |
| espressif/eppp_link | 1.1.5 | EPPP 链路层 |
| espressif/esp_serial_slave_link | 1.1.2 | 串行从机链路 |
| espressif/wifi_remote_over_eppp | 0.3.2 | 基于 EPPP 的远程 WiFi |
| lvgl/lvgl | 9.4.0 | 图形界面 |

### 2. 文件名修正

`main/CMakeLists.txt` 引用 `app_main.c`，但实际文件名为 `main.c`。

**修复**: 重命名 `main.c` → `app_main.c`

### 3. Flash 大小配置

分区表 (partitions.csv) 需要 16MB 空间，默认配置为 2MB。

**修复**: 在 `sdkconfig.defaults.esp32p4` 中添加:
```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
```

分区布局:
| 分区 | 类型 | 大小 | 起始 |
|------|------|------|------|
| nvs | data/nvs | 24KB | 0x9000 |
| phy_init | data/phy | 4KB | 0xF000 |
| factory | app/factory | 4MB | 0x10000 |
| storage | data/fat | ~11.7MB | 0x410000 |

### 4. LCD 头文件适配 (IDF 5.x API 变更)

`esp_lcd_panel_handle.h` 在 ESP-IDF 5.x 中已被拆分。

**修复**:
- `lcd.h`: `esp_lcd_panel_handle.h` → `esp_lcd_types.h`
- `system_init.c`: 添加 `#include "esp_lcd_mipi_dsi.h"`
- `main/CMakeLists.txt`: 添加 `REQUIRES esp_lcd_jd9165`

### 5. Build 目录清理

迁移过程中需手动删除 `build/` 目录以避免 CMake 缓存冲突。

---

## 编译命令

```bash
# 在 ESP-IDF 环境 (export.ps1 / export.bat) 中执行:
idf.py fullclean
idf.py set-target esp32p4
idf.py build
```

当前镜像大小: **~926 KB** (.bin 文件)
DIRAM 使用率: **25.69%** (剩余 ~486 KB)

---

## 已知问题

- `warning: cannot assign memory region sram_seg to chip memory type` — ESP32-P4 已知无害警告，可忽略
- `sdkconfig.defaults` 中 `I2C_MASTER_ISR_IRAM_SAFE` 和 `CAMERA_OV5647` 在 P4 平台不存在，仅为警告，不影响构建
