/**
 * @file ai_llm_parser.c
 * @brief LLM SSE 解析实现
 */
#include "ai_llm_parser.h"
#include "cJSON.h"
#include <string.h>

sse_result_t llm_parser_next(const char *line, char *content_out, size_t content_max)
{
    if (!line || !content_out) return SSE_ERR;
    content_out[0] = '\0';

    if (strncmp(line, "data: ", 6) != 0) {
        return SSE_ERR;
    }

    const char *json_start = line + 6;

    /* [DONE] 标记 */
    if (strcmp(json_start, "[DONE]") == 0) {
        return SSE_DONE;
    }

    cJSON *root = cJSON_Parse(json_start);
    if (!root) return SSE_ERR;

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || cJSON_GetArraySize(choices) == 0) {
        cJSON_Delete(root);
        return SSE_ERR;
    }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *delta  = cJSON_GetObjectItem(choice, "delta");
    if (!delta) {
        cJSON_Delete(root);
        return SSE_ERR;
    }

    cJSON *content = cJSON_GetObjectItem(delta, "content");
    sse_result_t result = SSE_ERR;
    if (content && cJSON_IsString(content) && content->valuestring[0]) {
        strncpy(content_out, content->valuestring, content_max - 1);
        content_out[content_max - 1] = '\0';
        result = SSE_DATA;
    }

    cJSON_Delete(root);
    return result;
}
