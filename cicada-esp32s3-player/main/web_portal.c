/*
 * 7鸣匣 SoftAP 网页门户：开放热点 + 强制门户跳转 + 播控 API
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "nvs_flash.h"
#include "dns_server.h"
#include "web_portal.h"

static const char *TAG = "web";

#define WEB_AP_SSID     "7MingXia"
#define WEB_AP_CHANNEL  1
#define WEB_AP_MAX_CONN 4

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

static volatile int s_cmd;
static volatile bool *s_stop_flag;
static char s_captive_uri[32] = "http://192.168.4.1";

void web_portal_bind_stop_flag(volatile bool *stop_flag)
{
    s_stop_flag = stop_flag;
}

int web_portal_take_cmd(void)
{
    int c = s_cmd;
    s_cmd = WEB_CMD_NONE;
    return c;
}

static void set_cmd(int c)
{
    s_cmd = c;
    if (c == WEB_CMD_STOP && s_stop_flag) {
        *s_stop_flag = true;
    }
}

static esp_err_t root_get(httpd_req_t *req)
{
    const size_t len = (size_t)(index_html_end - index_html_start);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)index_html_start, len);
}

static esp_err_t api_play(httpd_req_t *req)
{
    int track = -1;
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1) {
        char *q = malloc(qlen);
        if (q && httpd_req_get_url_query_str(req, q, qlen) == ESP_OK) {
            char val[8];
            if (httpd_query_key_value(q, "egg", val, sizeof(val)) == ESP_OK && val[0] == '1') {
                free(q);
                set_cmd(WEB_CMD_PLAY_EGG);
                httpd_resp_sendstr(req, "egg");
                return ESP_OK;
            }
            if (httpd_query_key_value(q, "track", val, sizeof(val)) == ESP_OK) {
                track = atoi(val);
            }
        }
        free(q);
    }

    if (track >= 0 && track < 7) {
        set_cmd(WEB_CMD_PLAY_TRACK_BASE + track);
        httpd_resp_sendstr(req, "playing track");
    } else {
        set_cmd(WEB_CMD_PLAY_ALL);
        httpd_resp_sendstr(req, "playing all");
    }
    return ESP_OK;
}

static esp_err_t api_stop(httpd_req_t *req)
{
    set_cmd(WEB_CMD_STOP);
    httpd_resp_sendstr(req, "stopped");
    return ESP_OK;
}

static esp_err_t api_lamp_debug(httpd_req_t *req)
{
    set_cmd(WEB_CMD_LAMP_DEBUG);
    httpd_resp_sendstr(req, "lamp debug toggle");
    return ESP_OK;
}

/* 强制门户：未注册路径 → 跳首页（iOS 需带正文） */
static esp_err_t http_404_redirect(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_httpd(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 7;
    cfg.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return NULL;
    }

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get,
    };
    const httpd_uri_t play = {
        .uri = "/api/play",
        .method = HTTP_POST,
        .handler = api_play,
    };
    const httpd_uri_t play_get = {
        .uri = "/api/play",
        .method = HTTP_GET,
        .handler = api_play,
    };
    const httpd_uri_t stop = {
        .uri = "/api/stop",
        .method = HTTP_POST,
        .handler = api_stop,
    };
    const httpd_uri_t stop_get = {
        .uri = "/api/stop",
        .method = HTTP_GET,
        .handler = api_stop,
    };
    const httpd_uri_t lamp_dbg = {
        .uri = "/api/lamp-debug",
        .method = HTTP_POST,
        .handler = api_lamp_debug,
    };
    const httpd_uri_t lamp_dbg_get = {
        .uri = "/api/lamp-debug",
        .method = HTTP_GET,
        .handler = api_lamp_debug,
    };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &play);
    httpd_register_uri_handler(server, &play_get);
    httpd_register_uri_handler(server, &stop);
    httpd_register_uri_handler(server, &stop_get);
    httpd_register_uri_handler(server, &lamp_dbg);
    httpd_register_uri_handler(server, &lamp_dbg_get);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_redirect);
    return server;
}

static void dhcp_set_captiveportal_url(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!netif) {
        return;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        return;
    }

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, sizeof(ip_addr));
    snprintf(s_captive_uri, sizeof(s_captive_uri), "http://%s", ip_addr);

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(
        netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
        s_captive_uri, strlen(s_captive_uri)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
    ESP_LOGI(TAG, "DHCP captive URI %s", s_captive_uri);
}

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, WEB_AP_SSID, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(WEB_AP_SSID);
    wifi_config.ap.password[0] = '\0';
    wifi_config.ap.channel = WEB_AP_CHANNEL;
    wifi_config.ap.max_connection = WEB_AP_MAX_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* 单位 0.25dBm；压低发射功率，4.5V 电池+舵机时不易掉压卡死 */
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_max_tx_power(40)); /* ~10 dBm */

    dhcp_set_captiveportal_url();
    ESP_LOGI(TAG, "SoftAP open SSID=%s → %s (tx~10dBm)", WEB_AP_SSID, s_captive_uri);
}

esp_err_t web_portal_start(void)
{
    /* 强制门户探测会产生大量无效请求，压低 httpd 噪音 */
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    wifi_init_softap();
    if (!start_httpd()) {
        return ESP_FAIL;
    }

    dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    if (!start_dns_server(&dns_cfg)) {
        ESP_LOGW(TAG, "DNS captive redirect failed");
    }
    return ESP_OK;
}
