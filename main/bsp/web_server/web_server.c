/**
 * @file web_server.c
 * @brief 家长端网页 HTTP Server 实现
 *
 * 提供以下端点:
 *   GET /dashboard        - 家长端网页主页
 *   GET /api/stats        - 学习统计数据
 *   GET /api/wrong_book   - 错题本列表
 *   GET /api/photos       - 拍照记录列表
 *   GET /api/achievements - 成就数据
 */

#include "web_server.h"
#include "app/achievement/achievement.h"
#include "app/achievement/achievement_ui.h"
#include "app/wrong_book/wrong_book.h"
#include "app/photos/photo_history.h"
#include "bsp/wifi/wifi.h"
#include "bsp/storage/storage.h"
#include "bsp/storage/sd_card.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "WEB_SERVER";

static httpd_handle_t s_server = NULL;
static bool s_running = false;

/* ═══════════════════════════════════════════════
   HTML 页面模板
   ═══════════════════════════════════════════════ */

static const char *DASHBOARD_HTML = 
"<!DOCTYPE html>"
"<html lang='zh-CN'>"
"<head>"
"  <meta charset='UTF-8'>"
"  <meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"  <title>智能学习盒 - 家长端</title>"
"  <style>"
"    * { margin: 0; padding: 0; box-sizing: border-box; }"
"    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background: #0F172A; color: #E2E8F0; }"
"    .header { background: linear-gradient(135deg, #1E293B 0%, #334155 100%); padding: 20px; text-align: center; }"
"    .header h1 { color: #60A5FA; font-size: 24px; margin-bottom: 8px; }"
"    .header p { color: #94A3B8; font-size: 14px; }"
"    .container { max-width: 1200px; margin: 0 auto; padding: 20px; }"
"    .stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; margin-bottom: 24px; }"
"    .stat-card { background: #1E293B; border-radius: 16px; padding: 20px; text-align: center; }"
"    .stat-value { font-size: 36px; font-weight: bold; color: #60A5FA; margin: 8px 0; }"
"    .stat-label { color: #94A3B8; font-size: 14px; }"
"    .section { background: #1E293B; border-radius: 16px; padding: 20px; margin-bottom: 20px; }"
"    .section h2 { color: #60A5FA; margin-bottom: 16px; font-size: 18px; }"
"    .wrong-item { background: #334155; border-radius: 12px; padding: 16px; margin-bottom: 12px; }"
"    .wrong-item h3 { color: #E2E8F0; margin-bottom: 8px; }"
"    .wrong-item p { color: #94A3B8; font-size: 14px; }"
"    .badge { display: inline-block; background: #3B82F6; color: white; padding: 4px 12px; border-radius: 20px; font-size: 12px; margin: 4px; }"
"    .photo-item { display: inline-block; width: 150px; margin: 8px; text-align: center; }"
"    .photo-item img { width: 100%; border-radius: 12px; }"
"    .photo-item p { color: #94A3B8; font-size: 12px; margin-top: 4px; }"
"    .refresh-btn { background: #3B82F6; color: white; border: none; padding: 12px 24px; border-radius: 12px; cursor: pointer; font-size: 16px; margin: 20px 0; }"
"    .refresh-btn:hover { background: #2563EB; }"
"    .loading { text-align: center; padding: 40px; color: #94A3B8; }"
"  </style>"
"</head>"
"<body>"
"  <div class='header'>"
"    <h1>📚 智能学习盒 - 家长端</h1>"
"    <p>实时查看孩子学习进度</p>"
"  </div>"
"  <div class='container'>"
"    <button class='refresh-btn' onclick='loadData()'>🔄 刷新数据</button>"
"    <div id='stats-section' class='stats-grid'></div>"
"    <div id='wrong-section' class='section' style='display:none'>"
"      <h2>📝 错题本</h2>"
"      <div id='wrong-list'></div>"
"    </div>"
"    <div id='photo-section' class='section' style='display:none'>"
"      <h2>📸 拍照记录</h2>"
"      <div id='photo-list'></div>"
"    </div>"
"  </div>"
"  <script>"
"    async function loadData() {"
"      try {"
"        const statsRes = await fetch('/api/stats');"
"        const stats = await statsRes.json();"
"        renderStats(stats);"
"        const wrongRes = await fetch('/api/wrong_book');"
"        const wrong = await wrongRes.json();"
"        renderWrongBook(wrong);"
"        const photoRes = await fetch('/api/photos');"
"        const photos = await photoRes.json();"
"        renderPhotos(photos);"
"      } catch(e) { console.error('加载失败:', e); }"
"    }"
"    function renderStats(s) {"
"      const html = `"
"        <div class='stat-card'><div class='stat-label'>等级</div><div class='stat-value'>Lv.${s.level}</div></div>"
"        <div class='stat-card'><div class='stat-label'>经验值</div><div class='stat-value'>${s.exp}/${s.exp_max}</div></div>"
"        <div class='stat-card'><div class='stat-label'>连续学习</div><div class='stat-value'>${s.streak}天</div></div>"
"        <div class='stat-card'><div class='stat-label'>完成题目</div><div class='stat-value'>${s.questions}</div></div>"
"        <div class='stat-card'><div class='stat-label'>AI对话</div><div class='stat-value'>${s.ai_chats}次</div></div>"
"        <div class='stat-card'><div class='stat-label'>学习时长</div><div class='stat-value'>${s.study_hours}h</div></div>"
"      `;"
"      document.getElementById('stats-section').innerHTML = html;"
"    }"
"    function renderWrongBook(w) {"
"      if (!w || w.length === 0) {"
"        document.getElementById('wrong-section').style.display = 'none';"
"        return;"
"      }"
"      document.getElementById('wrong-section').style.display = 'block';"
"      const html = w.map(item => `"
"        <div class='wrong-item'>"
"          <h3>${item.question.substring(0, 50)}${item.question.length > 50 ? '...' : ''}</h3>"
"          <p>学科: ${item.subject} | 时间: ${item.timestamp} | 复习: ${item.review_count}次</p>"
"          <span class='badge'>${item.tags}</span>"
"        </div>"
"      `).join('');"
"      document.getElementById('wrong-list').innerHTML = html;"
"    }"
"    function renderPhotos(p) {"
"      if (!p || p.length === 0) {"
"        document.getElementById('photo-section').style.display = 'none';"
"        return;"
"      }"
"      document.getElementById('photo-section').style.display = 'block';"
"      const html = p.map(item => `"
"        <div class='photo-item'>"
"          <p style='font-size:14px; color:#E2E8F0; margin-bottom:4px'>${item.question.substring(0, 30)}${item.question.length > 30 ? '...' : ''}</p>"
"          <p style='font-size:12px; color:#94A3B8'>${item.timestamp}</p>"
"          <p style='font-size:11px; color:#64748B'>${(item.jpeg_size / 1024).toFixed(1)} KB</p>"
"        </div>"
"      `).join('');"
"      document.getElementById('photo-list').innerHTML = html;"
"    }"
"    loadData();"
"  </script>"
"</body>"
"</html>";

/* ═══════════════════════════════════════════════
   HTTP Handler: 主页
   ═══════════════════════════════════════════════ */

static esp_err_t handler_dashboard(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, DASHBOARD_HTML, strlen(DASHBOARD_HTML));
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   HTTP Handler: 学习统计 API
   ═══════════════════════════════════════════════ */

static esp_err_t handler_api_stats(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    
    /* 直接使用achievement_ui的getter接口（已封装NVS读取） */
    cJSON_AddNumberToObject(root, "level", achievement_ui_get_level());
    cJSON_AddNumberToObject(root, "exp", achievement_ui_get_exp());
    cJSON_AddNumberToObject(root, "exp_max", achievement_ui_get_exp_max());
    cJSON_AddNumberToObject(root, "streak", achievement_ui_get_streak());
    cJSON_AddNumberToObject(root, "questions", achievement_ui_get_questions());
    cJSON_AddNumberToObject(root, "ai_chats", achievement_ui_get_ai_chats());
    cJSON_AddNumberToObject(root, "study_hours", achievement_ui_get_study_hours());
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   HTTP Handler: 错题本 API
   ═══════════════════════════════════════════════ */

static esp_err_t handler_api_wrong_book(httpd_req_t *req)
{
    if (!sd_card_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }
    
    /* 堆分配避免栈溢出（50个wrong_entry_t约60KB） */
    wrong_entry_t *entries = malloc(sizeof(wrong_entry_t) * 20);
    if (!entries) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }
    
    int count = 0;
    esp_err_t ret = wrong_book_list(entries, 20, &count);
    
    if (ret != ESP_OK) {
        free(entries);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to load wrong book");
        return ESP_FAIL;
    }
    
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", entries[i].id);
        cJSON_AddStringToObject(item, "question", entries[i].question);
        cJSON_AddStringToObject(item, "subject", entries[i].subject);
        cJSON_AddStringToObject(item, "tags", entries[i].tags);
        cJSON_AddStringToObject(item, "timestamp", entries[i].timestamp);
        cJSON_AddNumberToObject(item, "review_count", entries[i].review_count);
        cJSON_AddItemToArray(arr, item);
    }
    
    free(entries);
    
    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   HTTP Handler: 拍照记录 API
   ═══════════════════════════════════════════════ */

static esp_err_t handler_api_photos(httpd_req_t *req)
{
    if (!sd_card_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }
    
    /* 堆分配避免栈溢出 */
    photo_entry_t *entries = malloc(sizeof(photo_entry_t) * 20);
    if (!entries) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }
    
    int count = 0;
    esp_err_t ret = photo_history_list(entries, 20, &count);
    
    if (ret != ESP_OK) {
        free(entries);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to load photos");
        return ESP_FAIL;
    }
    
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "timestamp", entries[i].timestamp);
        cJSON_AddStringToObject(item, "question", entries[i].question);
        cJSON_AddNumberToObject(item, "jpeg_size", (int)entries[i].jpeg_size);
        cJSON_AddItemToArray(arr, item);
    }
    
    free(entries);
    
    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   路由注册
   ═══════════════════════════════════════════════ */

static const httpd_uri_t uri_handlers[] = {
    {
        .uri      = "/dashboard",
        .method   = HTTP_GET,
        .handler  = handler_dashboard,
        .user_ctx = NULL
    },
    {
        .uri      = "/api/stats",
        .method   = HTTP_GET,
        .handler  = handler_api_stats,
        .user_ctx = NULL
    },
    {
        .uri      = "/api/wrong_book",
        .method   = HTTP_GET,
        .handler  = handler_api_wrong_book,
        .user_ctx = NULL
    },
    {
        .uri      = "/api/photos",
        .method   = HTTP_GET,
        .handler  = handler_api_photos,
        .user_ctx = NULL
    },
};

#define HANDLER_COUNT (sizeof(uri_handlers) / sizeof(uri_handlers[0]))

/* ═══════════════════════════════════════════════
   公开 API
   ═══════════════════════════════════════════════ */

esp_err_t web_server_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "Web server already running");
        return ESP_OK;
    }
    
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, cannot start web server");
        return ESP_ERR_INVALID_STATE;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = HANDLER_COUNT;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Starting web server on port %d...", config.server_port);
    
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    for (int i = 0; i < HANDLER_COUNT; i++) {
        httpd_register_uri_handler(s_server, &uri_handlers[i]);
    }
    
    /* 获取并打印设备IP地址 */
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            char ip_str[16];
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            ESP_LOGI(TAG, "Web server started successfully");
            ESP_LOGI(TAG, "Access dashboard at: http://%s/dashboard", ip_str);
        }
    }
    
    s_running = true;
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    
    s_running = false;
    ESP_LOGI(TAG, "Web server stopped");
    return ESP_OK;
}

bool web_server_is_running(void)
{
    return s_running;
}