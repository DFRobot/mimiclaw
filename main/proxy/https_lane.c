#include "https_lane.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_https_lane;

esp_err_t https_lane_init(void)
{
    if (s_https_lane) {
        return ESP_OK;
    }
    s_https_lane = xSemaphoreCreateRecursiveMutex();
    return s_https_lane ? ESP_OK : ESP_ERR_NO_MEM;
}

void https_lane_lock(void)
{
    if (s_https_lane) {
        xSemaphoreTakeRecursive(s_https_lane, portMAX_DELAY);
    }
}

void https_lane_unlock(void)
{
    if (s_https_lane) {
        xSemaphoreGiveRecursive(s_https_lane);
    }
}
