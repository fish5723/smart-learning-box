/*
 * @file game_db.c
 * @brief NES 游戏数据库 — 从 SD JSON → PSRAM 索引  (Phase 1: 数据链路, 无 UI)
 *
 * 解析流程:
 *   1. 读 /sdcard/database/game_metadata.json → game_entry_t[]        (PSRAM)
 *   2. 读 /sdcard/database/game_database.json → default_path 数组     (PSRAM)
 *   3. 构建 10 个分类的指针列表
 *
 * 内存:  文件缓冲 / cJSON 树 / entry 数组 全部显式 heap_caps_malloc(…, SPIRAM)
 *        JSON 解析完即释放文件缓冲与 cJSON 树, 仅保留紧凑索引 (~250 KB PSRAM 常驻).
 */

#include "game_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "bsp/storage/sd_card.h"

static const char *TAG = "NES_DB";

/* ── 分类名称查找表 ── */
static const char *s_type_names[GAME_TYPE_COUNT] = {
    "动作冒险",          /* 0 */
    "射击",              /* 1 */
    "角色扮演",          /* 2 */
    "策略模拟",          /* 3 */
    "体育",              /* 4 */
    "益智",              /* 5 */
    "棋牌桌游",          /* 6 */
    "冒险文字",          /* 7 */
    "教育音乐",          /* 8 */
    "特殊/其他",         /* 9 */
};

#define ROM_PATH_MAX     128

/* ── 内部状态 (全部在 PSRAM) ── */
static struct {
    bool           initialized;
    game_entry_t  *entries;                           /* [count]     PSRAM   ~95 KB */
    uint16_t       count;
    char          *rom_paths;                          /* count × ROM_PATH_MAX, PSRAM, 扁平化 */
    uint16_t       type_counts[GAME_TYPE_COUNT];
    game_entry_t  *type_lists[GAME_TYPE_COUNT];       /* [type_counts[i]] of entry*, PSRAM */
} s_db;

/* ── 内部辅助 ── */

static uint8_t type_str_to_id(const char *s)
{
    for (int i = 0; i < GAME_TYPE_COUNT; i++) {
        if (strcmp(s, s_type_names[i]) == 0) return (uint8_t)i;
    }
    return GAME_TYPE_OTHER;   /* 未识别 → 特殊/其他 */
}

/** 读整个文件到 PSRAM (调用方负责 heap_caps_free) */
static char *read_file_psram(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0) {
        ESP_LOGE(TAG, "empty/error file: %s", path);
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = heap_caps_malloc((size_t)fsize + 1, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "PSRAM alloc failed for %s (%ld B)", path, fsize);
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_size) *out_size = rd;
    return buf;
}

/* ── 公共 API ── */

esp_err_t game_db_init(void)
{
    if (s_db.initialized) return ESP_OK;

    /* ── 挂载检查 ── */
    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted — skip DB load");
        return ESP_ERR_NOT_SUPPORTED;
    }

    const char *mp = sd_card_get_mount_point();

    /* ================================================================
     *  Pass 1: 解析 game_metadata.json → game_entry_t[]
     * ================================================================ */
    char meta_path[128];
    snprintf(meta_path, sizeof(meta_path), "%s/database/game_metadata.json", mp);
    ESP_LOGI(TAG, "Loading %s ...", meta_path);

    size_t meta_size = 0;
    char *meta_buf = read_file_psram(meta_path, &meta_size);
    if (!meta_buf) return ESP_FAIL;

    cJSON *meta_root = cJSON_Parse(meta_buf);
    heap_caps_free(meta_buf);           /* 文件缓冲已用完, 立即释放 */
    if (!meta_root) {
        ESP_LOGE(TAG, "metadata.json parse error: %s",
                 cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "unknown");
        return ESP_FAIL;
    }

    cJSON *games_arr = cJSON_GetObjectItem(meta_root, "games");
    if (!games_arr || !cJSON_IsArray(games_arr)) {
        ESP_LOGE(TAG, "metadata.json: missing 'games' array");
        cJSON_Delete(meta_root);
        return ESP_FAIL;
    }

    int total = cJSON_GetArraySize(games_arr);
    if (total <= 0 || total > 2000) {
        ESP_LOGE(TAG, "metadata.json: unexpected game count %d", total);
        cJSON_Delete(meta_root);
        return ESP_FAIL;
    }

    s_db.entries = heap_caps_calloc((size_t)total, sizeof(game_entry_t),
                                    MALLOC_CAP_SPIRAM);
    if (!s_db.entries) {
        ESP_LOGE(TAG, "PSRAM alloc failed for %d entries", total);
        cJSON_Delete(meta_root);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < total; i++) {
        cJSON *obj = cJSON_GetArrayItem(games_arr, i);
        if (!obj) continue;

        game_entry_t *e = &s_db.entries[i];

        cJSON *jid = cJSON_GetObjectItem(obj, "id");
        e->id = jid ? (uint16_t)cJSON_GetNumberValue(jid) : (uint16_t)(i + 1);

        cJSON *jf = cJSON_GetObjectItem(obj, "folder");
        if (jf && cJSON_IsString(jf)) {
            strncpy(e->folder, jf->valuestring, sizeof(e->folder) - 1);
            e->folder[sizeof(e->folder) - 1] = '\0';
        }

        cJSON *jn = cJSON_GetObjectItem(obj, "name");
        if (jn && cJSON_IsString(jn)) {
            strncpy(e->name, jn->valuestring, sizeof(e->name) - 1);
            e->name[sizeof(e->name) - 1] = '\0';
        }

        cJSON *jt = cJSON_GetObjectItem(obj, "type");
        e->type = (jt && cJSON_IsString(jt)) ? type_str_to_id(jt->valuestring)
                                              : GAME_TYPE_OTHER;
    }
    s_db.count = (uint16_t)total;

    cJSON_Delete(meta_root);   /* cJSON 树释放 */

    /* ================================================================
     *  Pass 2: 解析 game_database.json → 提取 default_path
     * ================================================================ */
    char db_path[128];
    snprintf(db_path, sizeof(db_path), "%s/database/game_database.json", mp);
    ESP_LOGI(TAG, "Loading %s ...", db_path);

    size_t db_size = 0;
    char *db_buf = read_file_psram(db_path, &db_size);
    if (!db_buf) {
        /* 没有 database.json 不是致命错误——ROM 路径解析会失败但分类浏览仍可用 */
        ESP_LOGW(TAG, "game_database.json not found — ROM path lookup disabled");
        s_db.rom_paths = NULL;
    } else {
        cJSON *db_root = cJSON_Parse(db_buf);
        heap_caps_free(db_buf);
        if (!db_root) {
            ESP_LOGW(TAG, "database.json parse error — ROM path lookup disabled");
            s_db.rom_paths = NULL;
        } else {
            cJSON *db_games = cJSON_GetObjectItem(db_root, "games");
            if (db_games && cJSON_IsArray(db_games)) {
                s_db.rom_paths = heap_caps_calloc((size_t)s_db.count, ROM_PATH_MAX,
                                                   MALLOC_CAP_SPIRAM);
                if (s_db.rom_paths) {
                    int db_total = cJSON_GetArraySize(db_games);
                    for (int i = 0; i < db_total && i < s_db.count; i++) {
                        cJSON *obj = cJSON_GetArrayItem(db_games, i);
                        if (!obj) continue;
                        cJSON *jdp = cJSON_GetObjectItem(obj, "default_path");
                        if (jdp && cJSON_IsString(jdp)) {
                            /* 拼接完整 SD 路径 */
                            snprintf(&s_db.rom_paths[i * ROM_PATH_MAX],
                                     ROM_PATH_MAX, "%s/%s", mp, jdp->valuestring);
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "PSRAM alloc failed for rom_paths");
                }
            }
            cJSON_Delete(db_root);
        }
    }

    /* ================================================================
     *  Pass 3: 构建按分类索引的指针列表
     * ================================================================ */
    memset(s_db.type_counts, 0, sizeof(s_db.type_counts));

    /* 3a: 先统计各分类数量 */
    for (int i = 0; i < s_db.count; i++) {
        uint8_t t = s_db.entries[i].type;
        if (t < GAME_TYPE_COUNT) s_db.type_counts[t]++;
    }

    /* 3b: 分配指针数组并填充 */
    for (int t = 0; t < GAME_TYPE_COUNT; t++) {
        uint16_t n = s_db.type_counts[t];
        if (n == 0) { s_db.type_lists[t] = NULL; continue; }

        s_db.type_lists[t] = heap_caps_calloc(n, sizeof(game_entry_t),
                                               MALLOC_CAP_SPIRAM);
        if (!s_db.type_lists[t]) {
            ESP_LOGE(TAG, "PSRAM alloc failed for type[%d] list (%d)", t, n);
            s_db.type_counts[t] = 0;
            continue;
        }
        uint16_t idx = 0;
        for (int i = 0; i < s_db.count && idx < n; i++) {
            if (s_db.entries[i].type == (uint8_t)t)
                s_db.type_lists[t][idx++] = s_db.entries[i];
        }
    }

    s_db.initialized = true;

    /* ── 启动统计输出 ── */
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "total games=%d", s_db.count);
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "type:");
    for (int t = 0; t < GAME_TYPE_COUNT; t++) {
        ESP_LOGI(TAG, "  %s %d", s_type_names[t], s_db.type_counts[t]);
    }
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ROM path cache: %s",
             s_db.rom_paths ? "READY" : "DISABLED (no database.json)");

    return ESP_OK;
}

void game_db_deinit(void)
{
    if (!s_db.initialized) return;

    if (s_db.entries)       { heap_caps_free(s_db.entries);       s_db.entries   = NULL; }
    if (s_db.rom_paths)     { heap_caps_free(s_db.rom_paths);     s_db.rom_paths = NULL; }
    for (int t = 0; t < GAME_TYPE_COUNT; t++) {
        if (s_db.type_lists[t]) { heap_caps_free(s_db.type_lists[t]);
                                  s_db.type_lists[t] = NULL; }
    }
    s_db.count = 0;
    s_db.initialized = false;
    memset(s_db.type_counts, 0, sizeof(s_db.type_counts));
}

/* ── 查询 API ── */

int game_db_get_type_count(void)
{
    return GAME_TYPE_COUNT;
}

const char *game_db_get_type_name(uint8_t type)
{
    if (type >= GAME_TYPE_COUNT) return NULL;
    return s_type_names[type];
}

int game_db_get_games_by_type(uint8_t type, game_entry_t **list)
{
    if (!s_db.initialized || type >= GAME_TYPE_COUNT) {
        if (list) *list = NULL;
        return 0;
    }
    if (list) *list = s_db.type_lists[type];
    return (int)s_db.type_counts[type];
}

esp_err_t game_db_get_rom_path(uint16_t id, char *path, size_t len)
{
    if (!s_db.initialized) return ESP_ERR_INVALID_STATE;
    if (!path || len == 0) return ESP_ERR_INVALID_ARG;

    /* id 从 1 开始; entries 按 id 升序存储 (id==idx+1) */
    int idx = (int)id - 1;
    if (idx < 0 || idx >= s_db.count) {
        return ESP_ERR_NOT_FOUND;
    }

    /* 优先取缓存的 ROM 路径 */
    if (s_db.rom_paths) {
        const char *rp = &s_db.rom_paths[idx * ROM_PATH_MAX];
        if (rp[0] != '\0') {
            strncpy(path, rp, len - 1);
            path[len - 1] = '\0';
            return ESP_OK;
        }
    }

    /* 回退: 由 folder + name 拼接 (可靠但缺少完整文件名) */
    const game_entry_t *e = &s_db.entries[idx];
    const char *mp = sd_card_get_mount_point();
    snprintf(path, len, "%s/ROM/%s/", mp, e->folder);
    /* 调用方应自己拼接文件名; 此处仅返回目录前缀 */
    ESP_LOGW(TAG, "ROM path fallback for id=%d: %s (no default_path cached)", id, path);
    return ESP_OK;
}

int game_db_get_total_count(void)
{
    return s_db.initialized ? (int)s_db.count : 0;
}