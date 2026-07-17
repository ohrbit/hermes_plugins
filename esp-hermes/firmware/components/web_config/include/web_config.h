/*
 * web_config.h — internal WiFi config web server for esp-hermes.
 *
 * Adapted in spirit from esp-claw's http_server (SoftAP + captive portal +
 * config/status JSON APIs + embedded UI), but trimmed to what esp-hermes
 * needs: set WiFi + Gateway credentials, read status, toggle backend.
 *
 * The server runs on the SoftAP interface (default 192.168.4.1) while STA is
 * not yet configured, and is reachable on the STA interface afterwards too.
 */
#ifndef EH_WEB_CONFIG_H
#define EH_WEB_CONFIG_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the internal HTTP config server (idempotent). Safe to call once. */
esp_err_t eh_web_config_start(void);

/* Stop the server (e.g. on entering normal operation). */
esp_err_t eh_web_config_stop(void);

/* True while the config server is running. */
bool eh_web_config_running(void);

#ifdef __cplusplus
}
#endif
#endif /* EH_WEB_CONFIG_H */
