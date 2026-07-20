/*
 * @file game_db.h
 * @brief NES 游戏数据库模块 — 从 SD 卡 JSON 文件加载游戏元数据到 PSRAM 索引
 *
 * 数据来源:
 *   /sdcard/database/game_metadata.json — name, type, tags  (用于分类浏览)
 *   /sdcard/database/game_database.json — default_path      (用于启动 ROM)
 *
 * 内存策略: 所有大块分配使用 heap_caps_malloc(…, MALLOC_CAP_SPIRAM) 放入 PSRAM;
 *           JSON 解析后的 cJSON 树立即释放, 仅保留紧凑索引 (~95 KB).
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 游戏条目 (紧凑, 每条 ~76 字节, 1251 条 ≈ 95 KB PSRAM) ── */
typedef struct {
    uint16_t id;          /* 1 ~ 1251 */
    char     folder[8];   /* "0001" ~ "1252" */
    char     name[64];    /* 游戏名称 (英文 / 罗马音) */
    uint8_t  type;        /* 分类索引 0~9 */
} game_entry_t;

/* ── 分类类型 ID 常量 (与 game_metadata.json 的 type 字符串对应) ── */
#define GAME_TYPE_ACTION       0   /* 动作冒险 */
#define GAME_TYPE_SHOOTER      1   /* 射击 */
#define GAME_TYPE_RPG          2   /* 角色扮演 */
#define GAME_TYPE_STRATEGY     3   /* 策略模拟 */
#define GAME_TYPE_SPORTS       4   /* 体育 */
#define GAME_TYPE_PUZZLE       5   /* 益智 */
#define GAME_TYPE_BOARD        6   /* 棋牌桌游 */
#define GAME_TYPE_ADVENTURE    7   /* 冒险文字 */
#define GAME_TYPE_EDUCATION    8   /* 教育音乐 */
#define GAME_TYPE_OTHER        9   /* 特殊/其他 */
#define GAME_TYPE_COUNT       10

/* ── 公共 API ── */

/**
 * @brief 初始化游戏数据库 (读取 SD 卡 JSON → 建 PSRAM 索引)
 * @return ESP_OK 成功; 其他值表示失败 (SD 未挂载 / 文件缺失 / 解析错误)
 */
esp_err_t game_db_init(void);

/**
 * @brief 释放数据库资源
 */
void game_db_deinit(void);

/**
 * @brief 获取分类数量 (固定 10)
 */
int game_db_get_type_count(void);

/**
 * @brief 获取分类名称字符串
 * @param type  分类索引 0~9
 * @return "动作冒险" / "射击" / ... / NULL(无效索引)
 */
const char *game_db_get_type_name(uint8_t type);

/**
 * @brief 获取指定分类下的所有游戏指针列表
 * @param type  分类索引 0~9
 * @param list  输出: 指向内部 game_entry_t* 数组 (调用方不得释放)
 * @return 列表长度 (0 表示无游戏或 type 无效)
 */
int game_db_get_games_by_type(uint8_t type, game_entry_t **list);

/**
 * @brief 根据游戏 id 获取完整 ROM 路径
 * @param id    游戏编号 (1~1251)
 * @param path  输出缓冲区
 * @param len   缓冲区长度 (建议 ≥ 128)
 * @return ESP_OK 成功; ESP_ERR_NOT_FOUND 表示 id 无效
 */
esp_err_t game_db_get_rom_path(uint16_t id, char *path, size_t len);

/**
 * @brief 返回已加载的游戏总数
 */
int game_db_get_total_count(void);

#ifdef __cplusplus
}
#endif
