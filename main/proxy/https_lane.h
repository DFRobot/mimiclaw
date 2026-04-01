#pragma once

#include "esp_err.h"

/**
 * 串行化所有走 mbedTLS / esp_http_client / esp_tls 的 HTTPS 会话。
 * 避免飞书发消息（outbound 任务）与 LLM（agent 任务）并行两套 TLS 挤爆内部 DRAM（esp-aes 等）。
 */
esp_err_t https_lane_init(void);
void https_lane_lock(void);
void https_lane_unlock(void);
