#include "achievement.h"
#include "achievement_ui.h"
#include "esp_log.h"
#include "storage.h"

static const char *TAG = "ACHIEVEMENT";

void achievement_init(void)
{
    ESP_LOGI(TAG, "achievement_init()");
    achievement_ui_init();
}

int achievement_update(int score)
{
    ESP_LOGI(TAG, "achievement_update(%d)", score);

    if (score <= 0) {
        return achievement_ui_get_exp();
    }

    /* 增加经验，自动处理升级和持久化 */
    achievement_ui_add_exp(score);

    return achievement_ui_get_exp();
}

void achievement_show(void)
{
    ESP_LOGI(TAG, "achievement_show()");
    achievement_ui_show();
}

int achievement_complete_task(achievement_task_t task, int count)
{
    ESP_LOGI(TAG, "achievement_complete_task(%d, %d)", task, count);
    return achievement_ui_complete_task(task, count);
}

int achievement_get_level(void)
{
    return achievement_ui_get_level();
}

int achievement_get_exp(void)
{
    return achievement_ui_get_exp();
}

int achievement_get_unlocked_badge_count(void)
{
    return achievement_ui_get_unlocked_badge_count();
}