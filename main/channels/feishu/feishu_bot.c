#include "feishu_bot.h"
#include "mimi_config.h"
#include "bus/message_bus.h"
#include "proxy/http_proxy.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "feishu";

/* ── Feishu API endpoints ──────────────────────────────────── */
#define FEISHU_API_BASE         "https://open.feishu.cn/open-apis"
#define FEISHU_AUTH_URL         FEISHU_API_BASE "/auth/v3/tenant_access_token/internal"
#define FEISHU_SEND_MSG_URL     FEISHU_API_BASE "/im/v1/messages"
#define FEISHU_REPLY_MSG_URL    FEISHU_API_BASE "/im/v1/messages/%s/reply"

/* ── Credentials & token state ─────────────────────────────── */
static char s_app_id[64] = MIMI_SECRET_FEISHU_APP_ID;
static char s_app_secret[128] = MIMI_SECRET_FEISHU_APP_SECRET;
static char s_tenant_token[512] = {0};
static int64_t s_token_expire_time = 0;

/* ── Message deduplication ─────────────────────────────────── */
#define FEISHU_DEDUP_CACHE_SIZE 64

static uint64_t s_seen_msg_keys[FEISHU_DEDUP_CACHE_SIZE] = {0};
static size_t s_seen_msg_idx = 0;

static uint64_t fnv1a64(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    if (!s) return h;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static bool dedup_check_and_record(const char *message_id)
{
    uint64_t key = fnv1a64(message_id);
    for (size_t i = 0; i < FEISHU_DEDUP_CACHE_SIZE; i++) {
        if (s_seen_msg_keys[i] == key) return true;
    }
    s_seen_msg_keys[s_seen_msg_idx] = key;
    s_seen_msg_idx = (s_seen_msg_idx + 1) % FEISHU_DEDUP_CACHE_SIZE;
    return false;
}

/* ── HTTP response accumulator ─────────────────────────────── */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} http_resp_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_resp_t *resp = (http_resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (resp->len + evt->data_len >= resp->cap) {
            size_t new_cap = resp->cap * 2;
            if (new_cap < resp->len + evt->data_len + 1) {
                new_cap = resp->len + evt->data_len + 1;
            }
            char *tmp = realloc(resp->buf, new_cap);
            if (!tmp) return ESP_ERR_NO_MEM;
            resp->buf = tmp;
            resp->cap = new_cap;
        }
        memcpy(resp->buf + resp->len, evt->data, evt->data_len);
        resp->len += evt->data_len;
        resp->buf[resp->len] = '\0';
    }
    return ESP_OK;
}

/* ── Get / refresh tenant access token ─────────────────────── */
static esp_err_t feishu_get_tenant_token(void)
{
    if (s_app_id[0] == '\0' || s_app_secret[0] == '\0') {
        ESP_LOGW(TAG, "No Feishu credentials configured");
        return ESP_ERR_INVALID_STATE;
    }

    int64_t now = esp_timer_get_time() / 1000000LL;
    if (s_tenant_token[0] != '\0' && s_token_expire_time > now + 300) {
        return ESP_OK;
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "app_id", s_app_id);
    cJSON_AddStringToObject(body, "app_secret", s_app_secret);
    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) return ESP_ERR_NO_MEM;

    http_resp_t resp = { .buf = calloc(1, 2048), .len = 0, .cap = 2048 };
    if (!resp.buf) { free(json_str); return ESP_ERR_NO_MEM; }

    esp_http_client_config_t config = {
        .url = FEISHU_AUTH_URL,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(json_str); free(resp.buf); return ESP_FAIL; }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, strlen(json_str));

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    free(json_str);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Token request HTTP failed: %s", esp_err_to_name(err));
        free(resp.buf);
        return err;
    }

    cJSON *root = cJSON_Parse(resp.buf);
    free(resp.buf);
    if (!root) { ESP_LOGE(TAG, "Failed to parse token response"); return ESP_FAIL; }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!code || code->valueint != 0) {
        ESP_LOGE(TAG, "Token request failed: code=%d", code ? code->valueint : -1);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *token = cJSON_GetObjectItem(root, "tenant_access_token");
    cJSON *expire = cJSON_GetObjectItem(root, "expire");

    if (token && cJSON_IsString(token)) {
        strncpy(s_tenant_token, token->valuestring, sizeof(s_tenant_token) - 1);
        s_token_expire_time = now + (expire ? expire->valueint : 7200) - 300;
        ESP_LOGI(TAG, "Got tenant access token (expires in %ds)",
                 expire ? expire->valueint : 7200);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── Feishu API call helper ────────────────────────────────── */
static char *feishu_api_call(const char *url, const char *method, const char *post_data)
{
    if (feishu_get_tenant_token() != ESP_OK) return NULL;

    http_resp_t resp = { .buf = calloc(1, 4096), .len = 0, .cap = 4096 };
    if (!resp.buf) return NULL;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(resp.buf); return NULL; }

    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_tenant_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json; charset=utf-8");

    if (strcmp(method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        if (post_data) {
            esp_http_client_set_post_field(client, post_data, strlen(post_data));
        }
    }

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "API call failed: %s", esp_err_to_name(err));
        free(resp.buf);
        return NULL;
    }

    return resp.buf;
}

/* ── Webhook event handler ─────────────────────────────────── */

/*
 * Feishu Event Callback v2 schema:
 * {
 *   "schema": "2.0",
 *   "header": { "event_type": "im.message.receive_v1", "event_id": "...", ... },
 *   "event": {
 *     "sender": { "sender_id": { "open_id": "ou_xxx" }, "sender_type": "user" },
 *     "message": {
 *       "message_id": "om_xxx",
 *       "chat_id": "oc_xxx",
 *       "chat_type": "p2p" | "group",
 *       "message_type": "text",
 *       "content": "{\"text\":\"hello\"}"
 *     }
 *   }
 * }
 *
 * URL verification challenge:
 * { "challenge": "xxx", "token": "yyy", "type": "url_verification" }
 */

static void handle_message_event(cJSON *event)
{
    cJSON *message = cJSON_GetObjectItem(event, "message");
    if (!message) return;

    cJSON *message_id_j = cJSON_GetObjectItem(message, "message_id");
    cJSON *chat_id_j    = cJSON_GetObjectItem(message, "chat_id");
    cJSON *chat_type_j  = cJSON_GetObjectItem(message, "chat_type");
    cJSON *msg_type_j   = cJSON_GetObjectItem(message, "message_type");
    cJSON *content_j    = cJSON_GetObjectItem(message, "content");

    if (!chat_id_j || !cJSON_IsString(chat_id_j)) return;
    if (!content_j || !cJSON_IsString(content_j)) return;

    const char *message_id = cJSON_IsString(message_id_j) ? message_id_j->valuestring : "";
    const char *chat_id    = chat_id_j->valuestring;
    const char *chat_type  = cJSON_IsString(chat_type_j) ? chat_type_j->valuestring : "p2p";
    const char *msg_type   = cJSON_IsString(msg_type_j) ? msg_type_j->valuestring : "text";

    /* Deduplication */
    if (message_id[0] && dedup_check_and_record(message_id)) {
        ESP_LOGD(TAG, "Duplicate message %s, skipping", message_id);
        return;
    }

    /* Only handle text messages for now */
    if (strcmp(msg_type, "text") != 0) {
        ESP_LOGI(TAG, "Ignoring non-text message type: %s", msg_type);
        return;
    }

    /* Parse the content JSON to extract text */
    cJSON *content_obj = cJSON_Parse(content_j->valuestring);
    if (!content_obj) {
        ESP_LOGW(TAG, "Failed to parse message content JSON");
        return;
    }

    cJSON *text_j = cJSON_GetObjectItem(content_obj, "text");
    if (!text_j || !cJSON_IsString(text_j)) {
        cJSON_Delete(content_obj);
        return;
    }

    const char *text = text_j->valuestring;

    /* Strip @bot mention prefix if present (Feishu adds @_user_1 for mentions) */
    const char *cleaned = text;
    if (strncmp(cleaned, "@_user_1 ", 9) == 0) {
        cleaned += 9;
    }
    /* Skip leading whitespace */
    while (*cleaned == ' ' || *cleaned == '\n') cleaned++;

    if (cleaned[0] == '\0') {
        cJSON_Delete(content_obj);
        return;
    }

    /* Get sender info */
    const char *sender_id = "";
    cJSON *sender = cJSON_GetObjectItem(event, "sender");
    if (sender) {
        cJSON *sender_id_obj = cJSON_GetObjectItem(sender, "sender_id");
        if (sender_id_obj) {
            cJSON *open_id = cJSON_GetObjectItem(sender_id_obj, "open_id");
            if (open_id && cJSON_IsString(open_id)) {
                sender_id = open_id->valuestring;
            }
        }
    }

    ESP_LOGI(TAG, "Message from %s in %s(%s): %.60s%s",
             sender_id, chat_id, chat_type, cleaned,
             strlen(cleaned) > 60 ? "..." : "");

    /* For p2p (DM) chats, use sender open_id as chat_id for session routing.
     * For group chats, use the chat_id (group ID).
     * This matches the moltbot reference pattern where DMs route by sender. */
    const char *route_id = chat_id;
    if (strcmp(chat_type, "p2p") == 0 && sender_id[0]) {
        route_id = sender_id;
    }

    /* Push to inbound message bus */
    mimi_msg_t msg = {0};
    strncpy(msg.channel, MIMI_CHAN_FEISHU, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, route_id, sizeof(msg.chat_id) - 1);
    msg.content = strdup(cleaned);

    if (msg.content) {
        if (message_bus_push_inbound(&msg) != ESP_OK) {
            ESP_LOGW(TAG, "Inbound queue full, dropping feishu message");
            free(msg.content);
        }
    }

    cJSON_Delete(content_obj);
}

static esp_err_t feishu_webhook_handler(httpd_req_t *req)
{
    /* Read request body */
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > MIMI_FEISHU_WEBHOOK_MAX_BODY) {
        ESP_LOGW(TAG, "Invalid content length: %d", content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    char *body = calloc(1, content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_ERR_NO_MEM;
    }

    int received = 0;
    while (received < content_len) {
        int ret = httpd_req_recv(req, body + received, content_len - received);
        if (ret <= 0) {
            free(body);
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Timeout");
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    body[content_len] = '\0';

    ESP_LOGD(TAG, "Webhook body: %.200s", body);

    cJSON *root = cJSON_Parse(body);
    free(body);

    if (!root) {
        ESP_LOGW(TAG, "Failed to parse webhook JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
        return ESP_FAIL;
    }

    /* URL verification challenge */
    cJSON *type_j = cJSON_GetObjectItem(root, "type");
    if (type_j && cJSON_IsString(type_j) &&
        strcmp(type_j->valuestring, "url_verification") == 0) {
        cJSON *challenge = cJSON_GetObjectItem(root, "challenge");
        if (challenge && cJSON_IsString(challenge)) {
            ESP_LOGI(TAG, "URL verification challenge received");
            cJSON *resp_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(resp_obj, "challenge", challenge->valuestring);
            char *resp_str = cJSON_PrintUnformatted(resp_obj);
            cJSON_Delete(resp_obj);
            if (resp_str) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, resp_str);
                free(resp_str);
            }
            cJSON_Delete(root);
            return ESP_OK;
        }
    }

    /* Event callback v2 */
    cJSON *header = cJSON_GetObjectItem(root, "header");
    cJSON *event  = cJSON_GetObjectItem(root, "event");

    if (header && event) {
        /* Check for duplicate event_id */
        cJSON *event_id = cJSON_GetObjectItem(header, "event_id");
        if (event_id && cJSON_IsString(event_id)) {
            if (dedup_check_and_record(event_id->valuestring)) {
                ESP_LOGD(TAG, "Duplicate event %s, skipping", event_id->valuestring);
                httpd_resp_sendstr(req, "{\"code\":0}");
                cJSON_Delete(root);
                return ESP_OK;
            }
        }

        cJSON *event_type = cJSON_GetObjectItem(header, "event_type");
        if (event_type && cJSON_IsString(event_type)) {
            const char *et = event_type->valuestring;
            if (strcmp(et, "im.message.receive_v1") == 0) {
                handle_message_event(event);
            } else {
                ESP_LOGI(TAG, "Unhandled event type: %s", et);
            }
        }
    }

    /* Always respond 200 OK to Feishu to prevent retries */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"code\":0}");

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── Webhook HTTP server ───────────────────────────────────── */
static httpd_handle_t s_webhook_server = NULL;

static esp_err_t feishu_start_webhook_server(void)
{
    if (s_webhook_server) {
        ESP_LOGW(TAG, "Webhook server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = MIMI_FEISHU_WEBHOOK_PORT;
    config.stack_size = MIMI_FEISHU_POLL_STACK;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_webhook_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start webhook server: %s", esp_err_to_name(err));
        return err;
    }

    /* Register the webhook endpoint */
    httpd_uri_t webhook_uri = {
        .uri = MIMI_FEISHU_WEBHOOK_PATH,
        .method = HTTP_POST,
        .handler = feishu_webhook_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_webhook_server, &webhook_uri);

    ESP_LOGI(TAG, "Feishu webhook server started on port %d, path: %s",
             MIMI_FEISHU_WEBHOOK_PORT, MIMI_FEISHU_WEBHOOK_PATH);

    return ESP_OK;
}

/* ── Public API ────────────────────────────────────────────── */

esp_err_t feishu_bot_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_FEISHU, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp_id[64] = {0};
        char tmp_secret[128] = {0};
        size_t len_id = sizeof(tmp_id);
        size_t len_secret = sizeof(tmp_secret);

        if (nvs_get_str(nvs, MIMI_NVS_KEY_FEISHU_APP_ID, tmp_id, &len_id) == ESP_OK && tmp_id[0]) {
            strncpy(s_app_id, tmp_id, sizeof(s_app_id) - 1);
        }
        if (nvs_get_str(nvs, MIMI_NVS_KEY_FEISHU_APP_SECRET, tmp_secret, &len_secret) == ESP_OK && tmp_secret[0]) {
            strncpy(s_app_secret, tmp_secret, sizeof(s_app_secret) - 1);
        }
        nvs_close(nvs);
    }

    if (s_app_id[0] && s_app_secret[0]) {
        ESP_LOGI(TAG, "Feishu credentials loaded (app_id=%.8s...)", s_app_id);
    } else {
        ESP_LOGW(TAG, "No Feishu credentials. Use CLI: set_feishu_creds <APP_ID> <APP_SECRET>");
    }

    return ESP_OK;
}

esp_err_t feishu_bot_start(void)
{
    if (s_app_id[0] == '\0' || s_app_secret[0] == '\0') {
        ESP_LOGW(TAG, "Feishu not configured, skipping webhook server start");
        return ESP_OK;
    }

    /* Pre-fetch tenant token so we're ready to send immediately */
    esp_err_t token_err = feishu_get_tenant_token();
    if (token_err != ESP_OK) {
        ESP_LOGW(TAG, "Initial token fetch failed (will retry on first API call)");
    }

    return feishu_start_webhook_server();
}

esp_err_t feishu_send_message(const char *chat_id, const char *text)
{
    if (s_app_id[0] == '\0' || s_app_secret[0] == '\0') {
        ESP_LOGW(TAG, "Cannot send: no credentials configured");
        return ESP_ERR_INVALID_STATE;
    }

    /* Determine receive_id_type based on ID prefix */
    const char *id_type = "chat_id";
    if (strncmp(chat_id, "ou_", 3) == 0) {
        id_type = "open_id";
    }

    char url[256];
    snprintf(url, sizeof(url), "%s?receive_id_type=%s", FEISHU_SEND_MSG_URL, id_type);

    size_t text_len = strlen(text);
    size_t offset = 0;
    int all_ok = 1;

    while (offset < text_len) {
        size_t chunk = text_len - offset;
        if (chunk > MIMI_FEISHU_MAX_MSG_LEN) {
            chunk = MIMI_FEISHU_MAX_MSG_LEN;
        }

        char *segment = malloc(chunk + 1);
        if (!segment) return ESP_ERR_NO_MEM;
        memcpy(segment, text + offset, chunk);
        segment[chunk] = '\0';

        /* Build content JSON: {"text":"..."} */
        cJSON *content = cJSON_CreateObject();
        cJSON_AddStringToObject(content, "text", segment);
        char *content_str = cJSON_PrintUnformatted(content);
        cJSON_Delete(content);
        free(segment);

        if (!content_str) { offset += chunk; all_ok = 0; continue; }

        /* Build message body */
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "receive_id", chat_id);
        cJSON_AddStringToObject(body, "msg_type", "text");
        cJSON_AddStringToObject(body, "content", content_str);
        free(content_str);

        char *json_str = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);

        if (json_str) {
            char *resp = feishu_api_call(url, "POST", json_str);
            free(json_str);

            if (resp) {
                cJSON *root = cJSON_Parse(resp);
                if (root) {
                    cJSON *code = cJSON_GetObjectItem(root, "code");
                    if (code && code->valueint != 0) {
                        cJSON *msg = cJSON_GetObjectItem(root, "msg");
                        ESP_LOGW(TAG, "Send failed: code=%d, msg=%s",
                                 code->valueint, msg ? msg->valuestring : "unknown");
                        all_ok = 0;
                    } else {
                        ESP_LOGI(TAG, "Sent to %s (%d bytes)", chat_id, (int)chunk);
                    }
                    cJSON_Delete(root);
                }
                free(resp);
            } else {
                ESP_LOGE(TAG, "Failed to send message chunk");
                all_ok = 0;
            }
        }

        offset += chunk;
    }

    return all_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t feishu_reply_message(const char *message_id, const char *text)
{
    if (s_app_id[0] == '\0' || s_app_secret[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char url[256];
    snprintf(url, sizeof(url), FEISHU_REPLY_MSG_URL, message_id);

    cJSON *content = cJSON_CreateObject();
    cJSON_AddStringToObject(content, "text", text);
    char *content_str = cJSON_PrintUnformatted(content);
    cJSON_Delete(content);
    if (!content_str) return ESP_ERR_NO_MEM;

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "msg_type", "text");
    cJSON_AddStringToObject(body, "content", content_str);
    free(content_str);

    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) return ESP_ERR_NO_MEM;

    char *resp = feishu_api_call(url, "POST", json_str);
    free(json_str);

    esp_err_t ret = ESP_FAIL;
    if (resp) {
        cJSON *root = cJSON_Parse(resp);
        if (root) {
            cJSON *code = cJSON_GetObjectItem(root, "code");
            if (code && code->valueint == 0) {
                ret = ESP_OK;
            } else {
                cJSON *msg = cJSON_GetObjectItem(root, "msg");
                ESP_LOGW(TAG, "Reply failed: code=%d, msg=%s",
                         code ? code->valueint : -1, msg ? msg->valuestring : "unknown");
            }
            cJSON_Delete(root);
        }
        free(resp);
    }

    return ret;
}

esp_err_t feishu_set_credentials(const char *app_id, const char *app_secret)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_FEISHU, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_FEISHU_APP_ID, app_id));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_FEISHU_APP_SECRET, app_secret));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    strncpy(s_app_id, app_id, sizeof(s_app_id) - 1);
    strncpy(s_app_secret, app_secret, sizeof(s_app_secret) - 1);

    /* Clear cached token to force re-auth */
    s_tenant_token[0] = '\0';
    s_token_expire_time = 0;

    ESP_LOGI(TAG, "Feishu credentials saved");
    return ESP_OK;
}
