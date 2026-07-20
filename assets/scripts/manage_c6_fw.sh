#!/bin/bash
# C6 从机固件构建 + 刷写脚本
# 优化: 关蓝牙(省50KB RAM) + 降 WiFi 突发(防 SDIO 崩)
# 在 Git Bash 中运行
# 用法: ./manage_c6_fw.sh [build|flash|menuconfig]

set -e

SLAVE_DIR="E:/IOT_competition/smart-learning-box/managed_components/espressif__esp_hosted/slave"
IDF_PATH="${IDF_PATH:-E:/esp32-tool}"
C6_PORT="COM7"

action="${1:-build}"

source "$IDF_PATH/export.sh" > /dev/null 2>&1 || source "$IDF_PATH/export.fish" 2>/dev/null || {
    echo "请先 source ESP-IDF 环境"
    exit 1
}

cd "$SLAVE_DIR"

case "$action" in
    build)
        echo "=== 设置 C6 目标 ==="
        idf.py set-target esp32c6

        echo "=== 写入优化配置 ==="
        # 追加优化配置到 sdkconfig 中
        cat >> sdkconfig << 'EOF'
#
# SDIO HTTPS 优化: 关蓝牙 → 省 50KB 给 SDIO 缓冲
#
# CONFIG_BT_ENABLED is not set
# CONFIG_BT_CONTROLLER_ONLY is not set
CONFIG_BT_BLUEDROID_ENABLED=
#
# 降低 WiFi 突发: AMPDU RX BA 从 6 降到 2(Kconfig 最小允许值)
# 防止一次突发塞爆内部 RAM 导致 SDIO 写卡死
#
CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=y
CONFIG_ESP_WIFI_RX_BA_WIN=2
#
# 减少 WiFi 动态缓冲: 32→16, 省 ~25KB
#
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16
EOF

        echo "=== 构建 ==="
        idf.py build
        echo "=== 构建完成! ==="
        echo "运行 ./manage_c6_fw.sh flash 刷写"
        ;;

    flash)
        echo "=== 刷写到 C6 (${C6_PORT}) ==="
        idf.py -p "${C6_PORT}" flash
        echo "=== 刷写完成! ==="
        ;;

    menuconfig)
        echo "=== 打开菜单配置 ==="
        idf.py menuconfig
        ;;

    monitor)
        echo "=== C6 监视器 ==="
        idf.py -p "${C6_PORT}" monitor
        ;;

    clean)
        echo "=== 清理 ==="
        idf.py fullclean
        rm -f sdkconfig
        ;;

    *)
        echo "用法: $0 [build|flash|menuconfig|monitor|clean]"
        ;;
esac
