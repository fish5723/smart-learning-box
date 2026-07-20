/**
 * @file icon_loader.h
 * @brief PNG 图标加载器 — 从 TF 卡 emoji 文件夹加载图标替换 LV_SYMBOL
 *
 * 功能:
 *   - 从 /sdcard/emoji/目录*.png 加载 PNG 图标为 LVGL image 对象
 *   - 自动检测 SD 卡可用性，SD 不可用时回退到 LV_SYMBOL 文本
 *   - 支持运行时缩放 (原图 128×128, 可显示为任意尺寸)
 *   - 提供简化的 lv_image_create_with_icon() 便捷创建函数
 *
 * 使用示例:
 *   // 创建图标图片
 *   lv_obj_t *icon = icon_loader_create_image(parent, ICON_ROBOT, 64, 64);
 *
 *   // 或直接设置已有 image 的 src
 *   icon_loader_set_image(img, ICON_GAME_HANDLE);
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════
   图标 ID 枚举 — 与 TF 卡 emoji 文件夹中的 PNG 对应
   ═══════════════════════════════════════════════ */
typedef enum {
    /* P0 — 核心功能图标 */
    ICON_ROBOT,             /* 机器人 → AI 老师 */
    ICON_FLASH_CAMERA,      /* 闪光相机 → 拍照解题 */
    ICON_GAME_HANDLE,       /* 游戏手柄 → 游戏中心 */
    ICON_TROPHY,            /* 奖杯 → 成长中心/成就 */
    ICON_LEFT_ARROW,        /* 左箭头 → 返回按钮 */
    ICON_SIGNAL,            /* 信号格 → WiFi 状态 */
    ICON_BATTERY,           /* 电池 → 电量 */
    ICON_HOUSE,             /* 带花园的房子 → 首页 */

    /* P1 — 装饰/功能图标 */
    ICON_BOOKS,             /* 书本 → 学习 */
    ICON_BULLSEYE,          /* 靶心 → 目标/任务 */
    ICON_CROWN,             /* 皇冠 → 等级/排名 */
    ICON_FIRE,              /* 火焰 → 连续学习/热度 */
    ICON_BICEPS,            /* 肌肉 → 力量/成就 */
    ICON_GEM,               /* 宝石 → 积分/奖励 */
    ICON_GLOWING_STAR,      /* 发光星星 → 高分 */
    ICON_HUNDRED_POINTS,    /* 100分 → 满分 */
    ICON_LIGHT_BULB,        /* 灯泡 → 提示/AI建议 */
    ICON_CRYING_FACE,       /* 大哭 → 错题/失败 */
    ICON_PARTY_POPPER,      /* 礼花 → 庆祝/升级 */
    ICON_PENCIL,            /* 铅笔 → 编辑/书写 */
    ICON_RED_HEART,         /* 红心 → 健康/生命值 */
    ICON_ROCKET,            /* 火箭 → 快速进步 */
    ICON_SKULL,             /* 骷髅 → 危险/错误 */
    ICON_SMILE,             /* 眯眼微笑 → 开心/正确 */
    ICON_SUNGLASSES,        /* 墨镜微笑 → 酷/自信 */
    ICON_STAR,              /* 星星 → 评分/收藏 */
    ICON_THINKING_FACE,     /* 思考 → 思考中/AI处理 */
    ICON_THUMBS_UP,         /* 点赞 → 确认/好 */
    ICON_GIFT,              /* 礼物 → 奖励 */
    ICON_LOCK,              /* 锁 → 锁定/未解锁 */
    ICON_SEND,              /* 发送托盘 → 发送 */
    ICON_UPPERCASE,         /* 大写字母 → 输入/键盘 */
    ICON_ENVELOPE_ARROW,    /* 带箭头信封 → 邮件/通知 */
    ICON_DIGITS,            /* 数字 → 数学/计算 */

    ICON_COUNT
} icon_id_t;

/* ═══════════════════════════════════════════════
   图标信息结构
   ═══════════════════════════════════════════════ */
typedef struct {
    icon_id_t   id;
    const char *filename;       /* TF 卡上文件名 (如 "机器人-128x128.png") */
    const char *lv_symbol;      /* 回退 LV_SYMBOL (如 LV_SYMBOL_EDIT) */
    const char *fallback_text;  /* 无符号回退文字 (如 "AI") */
} icon_info_t;

/* ═══════════════════════════════════════════════
   公开 API
   ═══════════════════════════════════════════════ */

/**
 * @brief 初始化图标加载器
 *
 * 扫描 TF 卡 emoji 目录，缓存可用图标路径。
 * 必须在 sd_card_init() 之后调用。
 *
 * @return ESP_OK 成功
 */
esp_err_t icon_loader_init(void);

/**
 * @brief 反初始化图标加载器
 */
void icon_loader_deinit(void);

/**
 * @brief 检查图标加载器是否可用 (SD 卡已挂载且 emoji 目录可访问)
 * @return true SD 卡图标可用
 */
bool icon_loader_is_available(void);

/**
 * @brief 获取图标信息
 * @param id 图标 ID
 * @return 图标信息指针 (静态数据，不需要释放)，失败返回 NULL
 */
const icon_info_t *icon_loader_get_info(icon_id_t id);

/**
 * @brief 获取图标的 LVGL 文件路径 (如 "S:/emoji/机器人-128x128.png")
 * @param id 图标 ID
 * @return 文件路径字符串，SD 不可用时返回 NULL
 */
const char *icon_loader_get_path(icon_id_t id);

/**
 * @brief 为已有 lv_image 对象设置图标源
 *
 * 优先使用 PNG 图标(需 SD 卡), 回退到 LV_SYMBOL 文本标签替代。
 *
 * @param img   lv_image object指针
 * @param id    图标 ID
 * @param size  显示尺寸 (px), 图标等比缩放
 */
void icon_loader_set_image(lv_obj_t *img, icon_id_t id, int size);

/**
 * @brief 创建带图标的 lv_image 对象
 *
 * @param parent  父对象
 * @param id      图标 ID
 * @param width   显示宽度 (px)
 * @param height  显示高度 (px); 传 0 表示等于 width
 * @return lv_obj_t* (lv_image 或 lv_label fallback)
 */
lv_obj_t *icon_loader_create_image(lv_obj_t *parent, icon_id_t id,
                                    int width, int height);

#ifdef __cplusplus
}
#endif
