## Smart Learning Box — main 组件
idf_component_register(
    SRCS
        "app_main.c"
        "system_init.c"
        "bsp/lcd/lcd.c"
        "bsp/touch/touch.c"
        "bsp/camera/camera.c"
        "bsp/lvgl/lvgl_port.c"
        "app/home/home.c"
        "app/home/home_ui.c"
        "app/ai/ai.c"
        "app/ai/ai_ui.c"
        "app/ai/ai_llm.c"
        "app/ai/ai_llm_parser.c"
        "app/ocr/ocr.c"
        "app/ocr/ocr_ui.c"
        "app/ocr/ocr_album_ui.c"
        "app/cache/answer_cache.c"
        "app/photos/photo_history.c"
        "app/photos/photo_history_ui.c"
        "app/wrong_book/wrong_book.c"
        "app/wrong_book/wrong_book_ui.c"
        "bsp/wifi/wifi.c"
        "bsp/wifi/c6_ota.c"
        "bsp/time/sys_time.c"
        "bsp/battery/battery.c"
        "bsp/storage/storage.c"
        "bsp/storage/sd_card.c"
        "bsp/storage/crash_log.c"
        "bsp/web_server/web_server.c"
        "app/wifi/wifi_ui.c"
        "app/game_center/game_center.c"
        "app/game_center/game_center_ui.c"
        "app/game_center/game2048/game2048.c"
        "app/game_center/game2048/game2048_ui.c"
        "app/game_center/math_quiz/math_quiz.c"
        "app/game_center/math_quiz/math_quiz_ui.c"
        "app/game_center/word_king/word_king.c"
        "app/game_center/word_king/word_king_ui.c"
        "app/game_center/more_games/more_games.c"
        "app/game_center/game_db/game_db.c"
        "app/game_center/nes_sel/nes_category_ui.c"
        "app/game_center/nes_sel/nes_game_list_ui.c"
        "app/achievement/achievement.c"
        "app/achievement/achievement_ui.c"
        "app/boot/boot_ui.c"

        "app/font_loader/font_loader.c"
        "app/icon_loader/icon_loader.c"
        "app/nes_emu/nes_emu.c"
        "app/nes_emu/nes_emu_ui.c"
        "app/nes_emu/nes_render.c"
        "app/nes_emu/platform/nes_core.c"
        "app/nes_emu/virtual_gamepad.c"
        "app/nes_emu/save_manager.c"
        "app/nes_emu/rom_loader.c"

        # ── Nofrendo NES core (from esp32-nesemu) ──
        "app/nes_emu/core/nes.c"
        "app/nes_emu/core/nes_apu.c"
        "app/nes_emu/core/nesstate.c"
        "app/nes_emu/core/libsnss.c"
        "app/nes_emu/core/vid_drv.c"
        "app/nes_emu/core/nofrendo_stubs.c"
        "app/nes_emu/core/vrcvisnd.c"
        "app/nes_emu/core/mmc5_snd.c"
        "app/nes_emu/core/cpu/nes6502.c"
        "app/nes_emu/core/cpu/dis6502.c"
        "app/nes_emu/core/ppu/nes_ppu.c"
        "app/nes_emu/core/ppu/nes_pal.c"
        "app/nes_emu/core/ppu/nesinput.c"
        "app/nes_emu/core/mmc/nes_mmc.c"
        "app/nes_emu/core/mmc/mmclist.c"
        "app/nes_emu/core/rom/nes_rom.c"
        "app/nes_emu/core/bitmap/bitmap.c"

        # Mappers 0-231 (40 files)
        "app/nes_emu/core/mapper/map000.c"
        "app/nes_emu/core/mapper/map001.c"
        "app/nes_emu/core/mapper/map002.c"
        "app/nes_emu/core/mapper/map003.c"
        "app/nes_emu/core/mapper/map004.c"
        "app/nes_emu/core/mapper/map005.c"
        "app/nes_emu/core/mapper/map007.c"
        "app/nes_emu/core/mapper/map008.c"
        "app/nes_emu/core/mapper/map009.c"
        "app/nes_emu/core/mapper/map011.c"
        "app/nes_emu/core/mapper/map015.c"
        "app/nes_emu/core/mapper/map016.c"
        "app/nes_emu/core/mapper/map018.c"
        "app/nes_emu/core/mapper/map019.c"
        "app/nes_emu/core/mapper/map024.c"
        "app/nes_emu/core/mapper/map032.c"
        "app/nes_emu/core/mapper/map033.c"
        "app/nes_emu/core/mapper/map034.c"
        "app/nes_emu/core/mapper/map040.c"
        "app/nes_emu/core/mapper/map041.c"
        "app/nes_emu/core/mapper/map042.c"
        "app/nes_emu/core/mapper/map046.c"
        "app/nes_emu/core/mapper/map050.c"
        "app/nes_emu/core/mapper/map064.c"
        "app/nes_emu/core/mapper/map065.c"
        "app/nes_emu/core/mapper/map066.c"
        "app/nes_emu/core/mapper/map070.c"
        "app/nes_emu/core/mapper/map073.c"
        "app/nes_emu/core/mapper/map075.c"
        "app/nes_emu/core/mapper/map078.c"
        "app/nes_emu/core/mapper/map079.c"
        "app/nes_emu/core/mapper/map085.c"
        "app/nes_emu/core/mapper/map087.c"
        "app/nes_emu/core/mapper/map093.c"
        "app/nes_emu/core/mapper/map094.c"
        "app/nes_emu/core/mapper/map099.c"
        "app/nes_emu/core/mapper/map160.c"
        "app/nes_emu/core/mapper/map229.c"
        "app/nes_emu/core/mapper/map231.c"
        "app/nes_emu/core/mapper/mapvrc.c"
        "fonts/fontcn14.c"
        "fonts/fontcn16.c"
        "fonts/fontcn20.c"
        "fonts/fontcn24.c"
        "common/json_utils.c"
        "manager/camera_manager.c"
        "manager/http_manager.c"
        "manager/ui_manager.c"
        "vision/vision_core.c"
        "vision/vision_ocr_parser.c"
        "vision/vision_ocr.c"
    INCLUDE_DIRS
        "."
        "common"
        "manager"
        "vision"
        "app/home"
        "app/ai"
        "app/ocr"
        "app/cache"
        "app/photos"
        "app/wrong_book"
        "bsp/wifi"
        "bsp/time"
        "bsp/battery"
        "bsp/storage"
        "bsp/web_server"
        "bsp/camera"
        "app/wifi"
        "app/game_center"
        "app/game_center/game2048"
        "app/game_center/math_quiz"
        "app/game_center/word_king"
        "app/game_center/more_games"
        "app/game_center/game_db"
        "app/game_center/nes_sel"
        "app/achievement"
        "app/boot"

        "app/font_loader"
        "app/icon_loader"
        "app/nes_emu"
        "app/nes_emu/core"
        "app/nes_emu/core/cpu"
        "app/nes_emu/core/ppu"
        "app/nes_emu/core/mmc"
        "app/nes_emu/core/mapper"
        "app/nes_emu/core/rom"
        "app/nes_emu/core/bitmap"
    REQUIRES
        freertos
        esp_system
        spi_flash
        nvs_flash
        esp_lcd
        esp_lcd_jd9165
        esp_lcd_touch
        esp_lvgl_adapter
        esp_hw_support
        esp_timer
        esp_netif
        esp_event
        esp_wifi
        esp_wifi_remote
        esp_http_client
        esp_http_server
        esp-tls
        mbedtls
        json
        driver
        lvgl
        esp_driver_cam
        esp_driver_isp
        esp_driver_i2c
        esp_driver_jpeg
        esp_adc
        fatfs
        bootloader_support
        esp_app_format
        espressif__libpng
)

# ── 强制 JPEG 走软件解码器 (esp_lv_decoder) ──
# 根因: ESP32-P4 硬件 JPEG 解码器无法解码本机硬件编码器输出的 YUV422 JPEG,
#   报 "number of data units ... different ..." (MCU 计数校验失败)。
#   相机拍照存的 800x640 JPEG 两维均 16 整除 → esp_lv_decoder 强制走 HW 路径 → 必失败。
# 处理: 覆盖组件的 ESP_LV_ENABLE_HW_JPEG=0, 让 JPEG 改用软件解码 (YUV422 正常)。
#   仅相册缩略图/导入预览用到 JPEG 显示 (图标是 PNG, 实时预览是裸 RGB565), 影响面小。
#   不改 camera.c / CSI / ISP / LVGL / display / 托管组件源码。
# 用全局 build 编译宏注入: 该宏仅被 esp_lv_decoder 引用, 其它组件无副作用;
#   组件的 COMPONENT_LIB 是 INTERFACE target, 无法用 target_compile_definitions,
#   故走 idf_build_set_property(COMPILE_DEFINITIONS ...)。
idf_build_set_property(COMPILE_DEFINITIONS "ESP_LV_ENABLE_HW_JPEG=0" APPEND)

# ── 双线程软件渲染 (sdkconfig CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2) ──
# 曾因 OCR 页面栈溢出强制单线程, 现恢复双线程利用 P4 双核并行绘制。
# 若 OCR 页面栈溢出复现, 优先增大 CONFIG_LV_DRAW_THREAD_STACK_SIZE (当前 32KB)。