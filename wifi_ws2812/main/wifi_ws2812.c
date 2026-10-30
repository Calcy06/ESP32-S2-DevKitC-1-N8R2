#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "rgb_web";

// 改成你自己的 Wi-Fi（不要提交真实密码到公开仓库！建议改用 menuconfig / Kconfig）
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#define WS2812_GPIO GPIO_NUM_18
#define LED_NUM 1

static led_strip_handle_t led_strip;
static uint8_t g_r = 0, g_g = 0, g_b = 0;
static bool breath_en = false;
static SemaphoreHandle_t led_sem;

// html页面
static const char *HTML_PAGE =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>S2 RGB控制</title>"
    "<style>body{text-align:center;font-size:20px;margin-top:30px;}"
    "input{width:70px;font-size:18px;}"
    "button{padding:8px 16px;font-size:18px;margin:4px;}</style>"
    "</head><body>"
    "<h3>ESP32-S2 板载彩灯</h3>"
    "<form action='/set'>R:<input name='r' value='0'> G:<input name='g' value='0'> B:<input name='b' value='0'><br><br>"
    "<button type='submit'>设置颜色</button></form><br>"
    "<a href='/breath/on'><button>开启呼吸</button></a>"
    "<a href='/breath/off'><button>关闭呼吸</button></a>"
    "<a href='/off'><button>关灯</button>"
    "</body></html>";

// 初始化WS2812
static void led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = WS2812_GPIO,
        .max_leds = LED_NUM,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led_strip));
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
}

// 设置rgb
static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (xSemaphoreTake(led_sem, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
        xSemaphoreGive(led_sem);
    }
}

// ===== HTTP回调 =====
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t set_color_handler(httpd_req_t *req)
{
    char buf[128];
    httpd_req_get_url_query_str(req, buf, sizeof(buf));
    char r_str[8], g_str[8], b_str[8];
    httpd_query_key_value(buf, "r", r_str, sizeof(r_str));
    httpd_query_key_value(buf, "g", g_str, sizeof(g_str));
    httpd_query_key_value(buf, "b", b_str, sizeof(b_str));

    g_r = atoi(r_str);
    g_g = atoi(g_str);
    g_b = atoi(b_str);
    breath_en = false;
    set_rgb(g_r, g_g, g_b);
    httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t breath_on_handler(httpd_req_t *req)
{
    breath_en = true;
    httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
static esp_err_t breath_off_handler(httpd_req_t *req)
{
    breath_en = false;
    set_rgb(g_r, g_g, g_b);
    httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
static esp_err_t off_handler(httpd_req_t *req)
{
    breath_en = false;
    g_r = g_g = g_b = 0;
    set_rgb(0, 0, 0);
    httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// 呼吸任务
static void breath_task(void *arg)
{
    uint8_t brt = 0;
    int8_t dir = 1;
    for (;;)
    {
        if (breath_en)
        {
            brt += dir;
            if (brt >= 255)
                dir = -1;
            if (brt <= 0)
                dir = 1;
            uint8_t rr = (uint16_t)g_r * brt / 255;
            uint8_t gg = (uint16_t)g_g * brt / 255;
            uint8_t bb = (uint16_t)g_b * brt / 255;
            set_rgb(rr, gg, bb);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// web服务器启动
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    if (httpd_start(&server, &cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "web start fail");
        return NULL;
    }
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    httpd_uri_t seturi = {.uri = "/set", .method = HTTP_GET, .handler = set_color_handler};
    httpd_uri_t bon = {.uri = "/breath/on", .method = HTTP_GET, .handler = breath_on_handler};
    httpd_uri_t boff = {.uri = "/breath/off", .method = HTTP_GET, .handler = breath_off_handler};
    httpd_uri_t offuri = {.uri = "/off", .method = HTTP_GET, .handler = off_handler};
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &seturi);
    httpd_register_uri_handler(server, &bon);
    httpd_register_uri_handler(server, &boff);
    httpd_register_uri_handler(server, &offuri);
    ESP_LOGI(TAG, "web server ok");
    return server;
}

// wifi事件回调
static void wifi_event_cb(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "wifi disconnect, retry");
        esp_wifi_connect();
    }
    else if (event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "✅ 连上wifi! IP:" IPSTR, IP2STR(&event->ip_info.ip));
        start_webserver();
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_wifi, instance_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_cb, NULL, &instance_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_cb, NULL, &instance_ip));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    led_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(led_sem);
    led_init();
    wifi_init_sta();
    xTaskCreate(breath_task, "breath", 2048, NULL, 5, NULL);
}
