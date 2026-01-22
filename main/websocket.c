#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

#include "pwm_led.h"
#include "websocket.h"

static const char *TAG_WS = "websocket";

static esp_err_t ws_send_state(httpd_req_t *req, const char *status, const char *message)
{
    pwm_led_state_t state = pwm_led_get_state();
    char payload[192];
    int len;

    if (message) {
        len = snprintf(payload, sizeof(payload),
                       "{\"type\":\"pwm_state\",\"status\":\"%s\",\"frequency\":%" PRIu32 ",\"duty\":%" PRIu32 ",\"message\":\"%s\"}",
                       status ? status : "ok",
                       state.frequency_hz,
                       state.duty_percent,
                       message);
    } else {
        len = snprintf(payload, sizeof(payload),
                       "{\"type\":\"pwm_state\",\"status\":\"%s\",\"frequency\":%" PRIu32 ",\"duty\":%" PRIu32 "}",
                       status ? status : "ok",
                       state.frequency_hz,
                       state.duty_percent);
    }

    if (len < 0 || len >= (int)sizeof(payload)) {
        ESP_LOGE(TAG_WS, "WS response truncated");
        return ESP_ERR_NO_MEM;
    }

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)payload,
        .len = len,
    };
    return httpd_ws_send_frame(req, &ws_pkt);
}

static esp_err_t handle_pwm_update(httpd_req_t *req, const cJSON *root)
{
    const cJSON *freq = cJSON_GetObjectItemCaseSensitive(root, "frequency");
    const cJSON *duty = cJSON_GetObjectItemCaseSensitive(root, "duty");
    if (!cJSON_IsNumber(freq) || !cJSON_IsNumber(duty)) {
        return ws_send_state(req, "error", "missing pwm fields");
    }

    uint32_t freq_val = (uint32_t)freq->valuedouble;
    uint32_t duty_val = (uint32_t)duty->valuedouble;

    esp_err_t err = pwm_led_set(freq_val, duty_val);
    if (err != ESP_OK) {
        return ws_send_state(req, "error", esp_err_to_name(err));
    }

    return ws_send_state(req, "ok", NULL);
}

static esp_err_t handle_ws_payload(httpd_req_t *req, const char *payload, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(payload, len);
    if (!root) {
        ESP_LOGE(TAG_WS, "Invalid JSON payload");
        return ws_send_state(req, "error", "invalid_json");
    }

    esp_err_t res = ESP_OK;
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == NULL) {
        res = ws_send_state(req, "error", "missing_type");
        goto cleanup;
    }

    if (strcmp(type->valuestring, "pwm_update") == 0) {
        res = handle_pwm_update(req, root);
    } else if (strcmp(type->valuestring, "pwm_get") == 0) {
        res = ws_send_state(req, "ok", NULL);
    } else {
        ESP_LOGW(TAG_WS, "Unknown WS type: %s", type->valuestring);
        res = ws_send_state(req, "error", "unknown_type");
    }

cleanup:
    cJSON_Delete(root);
    return res;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG_WS, "WebSocket handshake complete");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {0};
    uint8_t *buf = NULL;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WS, "Failed to get WS frame len: %s", esp_err_to_name(ret));
        return ret;
    }

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG_WS, "Failed to alloc %zu bytes for WS payload", ws_pkt.len);
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_WS, "Failed to read WS frame: %s", esp_err_to_name(ret));
            free(buf);
            return ret;
        }
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len && ws_pkt.payload) {
        ESP_LOGI(TAG_WS, "WS message: %s", ws_pkt.payload);
        ret = handle_ws_payload(req, (const char *)ws_pkt.payload, ws_pkt.len);
    } else {
        ESP_LOGW(TAG_WS, "Unsupported WS packet type %d", ws_pkt.type);
        ret = ws_send_state(req, "error", "unsupported_type");
    }

    free(buf);
    return ret;
}

static const httpd_uri_t ws_uri = {
    .uri        = "/ws",
    .method     = HTTP_GET,
    .handler    = ws_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

static const httpd_uri_t ws_auth_uri = {
    .uri        = "/auth",
    .method     = HTTP_GET,
    .handler    = ws_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

esp_err_t websocket_register_handlers(httpd_handle_t server)
{
    esp_err_t err;

    err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WS, "Failed to register /ws WebSocket handler (%s)", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(server, &ws_auth_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WS, "Failed to register /auth WebSocket handler (%s)", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG_WS, "Registered WebSocket handlers at /ws and /auth");
    return ESP_OK;
}
