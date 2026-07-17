/*
 * web_config.c — internal config web server (SoftAP + captive portal +
 * embedded UI). See web_config.h for the design note.
 */
#include "web_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_manager.h"
#include "nvs_config.h"

static const char *TAG = "eh_web_cfg";
static httpd_handle_t s_server = NULL;

/* ---------------- embedded UI (single page, no build step) ---------------- */
static const char *UI_HTML =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<title>ESP-Hermes Setup</title>"
"<style>body{font-family:system-ui,sans-serif;max-width:420px;margin:2rem auto;padding:0 1rem}"
"h1{font-size:1.4rem}label{display:block;margin-top:1rem;font-weight:600}"
"input,select{width:100%;padding:.5rem;margin-top:.3rem;box-sizing:border-box}"
"button{margin-top:1.5rem;width:100%;padding:.7rem;background:#0a7;color:#fff;border:0;border-radius:.4rem;font-size:1rem}"
"pre{background:#f0f0f0;padding:.6rem;border-radius:.4rem;white-space:pre-wrap}</style>"
"</head><body><h1>ESP-Hermes Setup</h1>"
"<form id=f>"
"<label>WiFi SSID<input name=sta_ssid value=\"\"></label>"
"<label>WiFi Password<input name=sta_password type=password></label>"
"<hr><label>Backend"
"<select name=backend><option value=claw>Claw (local)</option><option value=hermes>Hermes Gateway</option></select></label>"
"<label>Gateway Host<input name=gateway_host placeholder=\"gateway.example.com\"></label>"
"<label>Device Token<input name=token></label>"
"<label>Device ID<input name=device_id></label>"
"<button type=submit>Save</button></form>"
"<p id=status></p><script>"
"fetch('/api/config').then(r=>r.json()).then(c=>{"
"f.sta_ssid.value=c.sta_ssid||'';f.gateway_host.value=c.gateway_host||'';"
"f.token.value=c.token||'';f.device_id.value=c.device_id||'';"
"f.backend.value=c.backend||'claw';});"
"f.onsubmit=async e=>{e.preventDefault();const d=Object.fromEntries(new FormData(f));"
"const r=await fetch('/api/config',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(d)});"
"document.getElementById('status').textContent=r.ok?'Saved — device will restart & connect.':'Save failed';"
"if(r.ok)setTimeout(()=>location.reload(),1500);};"
"</script></body></html>";

/* ---------------- handlers ---------------- */
static esp_err_t handler_root(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, UI_HTML, HTTPD_RESP_USE_STRLEN);
}

/* captive portal: redirect any unknown GET to our UI */
static esp_err_t handler_catchall(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    httpd_resp_send_404(req);
    return ESP_OK;
}

static esp_err_t handler_config_get(httpd_req_t *req) {
    eh_config_t cfg;
    eh_nvs_load(&cfg);
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "{\"sta_ssid\":\"%s\",\"gateway_host\":\"%s\",\"token\":\"%s\","
        "\"device_id\":\"%s\",\"backend\":\"%s\"}",
        cfg.wifi_ssid, cfg.gateway_host, cfg.token, cfg.device_id,
        cfg.backend[0] ? cfg.backend : "claw");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t handler_config_post(httpd_req_t *req) {
    char body[512];
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) { httpd_resp_send_err(req, 400, "bad body"); return ESP_FAIL; }
    int r = httpd_req_recv(req, body, len);
    if (r <= 0) { httpd_resp_send_err(req, 400, "recv"); return ESP_FAIL; }
    body[r] = '\0';

    /* minimal JSON field extraction (no cJSON needed for config) */
    eh_config_t cfg;
    eh_nvs_load(&cfg);
    #define EXTRACT(field, dst, max) do { \
        const char *p = strstr(body, "\"" #field "\""); \
        if (p) { const char *q = strchr(p + strlen("\"" #field "\""), '"'); \
                 if (q) { const char *e = strchr(q + 1, '"'); \
                         int l = e ? (int)(e - (q + 1)) : 0; \
                         if (l > 0 && l < (int)(max)) { strncpy(dst, q + 1, l); dst[l] = '\0'; } } } } while (0)
    EXTRACT(sta_ssid, cfg.wifi_ssid, sizeof(cfg.wifi_ssid));
    EXTRACT(sta_password, cfg.wifi_pass, sizeof(cfg.wifi_pass));
    EXTRACT(gateway_host, cfg.gateway_host, sizeof(cfg.gateway_host));
    EXTRACT(token, cfg.token, sizeof(cfg.token));
    EXTRACT(device_id, cfg.device_id, sizeof(cfg.device_id));
    EXTRACT(backend, cfg.backend, sizeof(cfg.backend));
    #undef EXTRACT

    esp_err_t e = eh_nvs_save(&cfg);
    if (e != ESP_OK) { httpd_resp_send_err(req, 500, "save failed"); return ESP_FAIL; }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 9);

    /* apply WiFi immediately + schedule restart so new backend takes effect */
    wifi_manager_config_t wcfg = {
        .sta_ssid = cfg.wifi_ssid[0] ? cfg.wifi_ssid : NULL,
        .sta_password = cfg.wifi_pass[0] ? cfg.wifi_pass : NULL,
        .ap_ssid_prefix = "esp-hermes",
    };
    wifi_manager_apply_sta_config(&wcfg);
    esp_restart();
    return ESP_OK;
}

static esp_err_t handler_status(httpd_req_t *req) {
    wifi_manager_status_t st;
    wifi_manager_get_status(&st);
    eh_config_t cfg;
    eh_nvs_load(&cfg);
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "{\"sta_connected\":%s,\"ap_active\":%s,\"sta_ip\":\"%s\",\"ap_ip\":\"%s\","
        "\"backend\":\"%s\",\"mode\":\"%s\"}",
        st.sta_connected ? "true" : "false",
        st.ap_active ? "true" : "false",
        st.sta_ip ? st.sta_ip : "",
        st.ap_ip ? st.ap_ip : "",
        cfg.backend[0] ? cfg.backend : "claw",
        st.mode ? st.mode : "");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

/* ---------------- server lifecycle ---------------- */
static const httpd_uri_t routes[] = {
    { .uri = "/", .method = HTTP_GET, .handler = handler_root, .user_ctx = NULL },
    { .uri = "/api/config", .method = HTTP_GET, .handler = handler_config_get, .user_ctx = NULL },
    { .uri = "/api/config", .method = HTTP_POST, .handler = handler_config_post, .user_ctx = NULL },
    { .uri = "/api/status", .method = HTTP_GET, .handler = handler_status, .user_ctx = NULL },
    { .uri = "/*", .method = HTTP_ANY, .handler = handler_catchall, .user_ctx = NULL },
};

esp_err_t eh_web_config_start(void) {
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 8;
    config.stack_size = 8 * 1024;

    esp_err_t e = httpd_start(&s_server, &config);
    if (e != ESP_OK) { ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(e)); return e; }

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    ESP_LOGI(TAG, "config web server up on :80");
    return ESP_OK;
}

esp_err_t eh_web_config_stop(void) {
    if (!s_server) return ESP_OK;
    esp_err_t e = httpd_stop(s_server);
    s_server = NULL;
    return e;
}

bool eh_web_config_running(void) { return s_server != NULL; }
