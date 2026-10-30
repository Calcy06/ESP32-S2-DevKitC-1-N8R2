#include "simple_wifi_sta.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

// 需要把这两个修改成你家WIFI，测试是否连接成功
// 注意：不要提交真实密码，建议改用 menuconfig / Kconfig 配置
#define DEFAULT_WIFI_SSID "YOUR_WIFI_SSID"
#define DEFAULT_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

static const char *TAG = "wifi";

// 事件通知回调函数
static wifi_event_cb wifi_cb = NULL;

/** 事件回调函数
 * @param arg   用户传递的参数
 * @param event_base    事件类别
 * @param event_id      事件ID
 * @param event_data    事件携带的数据
 * @return 无
 */
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START: // WIFI以STA模式启动后触发此事件
            esp_wifi_connect();    // 启动WIFI连接
            break;
        case WIFI_EVENT_STA_CONNECTED: // WIFI连上路由器后，触发此事件
            ESP_LOGI(TAG, "connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: // WIFI从路由器断开连接后触发此事件
            esp_wifi_connect();           // 继续重连
            ESP_LOGI(TAG, "connect to the AP fail,retry now");
            break;
        default:
            break;
        }
    }
    if (event_base == IP_EVENT) // IP相关事件
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP: // 只有获取到路由器分配的IP，才认为是连上了路由器
            if (wifi_cb)
                wifi_cb(WIFI_CONNECTED);
            ESP_LOGI(TAG, "get ip address");
            break;
        }
    }
}

// WIFI STA初始化
esp_err_t wifi_sta_init(wifi_event_cb f)
{
    ESP_ERROR_CHECK(esp_netif_init());                // 用于初始化tcpip协议栈
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // 创建一个默认系统事件调度循环，之后可以注册回调函数来处理系统的一些事件
    esp_netif_create_default_wifi_sta();              // 使用默认配置创建STA对象

    // 初始化WIFI
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // WIFI配置
    wifi_config_t wifi_config =
        {
            .sta =
                {
                    .ssid = DEFAULT_WIFI_SSID,                // WIFI的SSID
                    .password = DEFAULT_WIFI_PASSWORD,        // WIFI密码
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK, // 加密方式

                    .pmf_cfg =
                        {
                            .capable = true,
                            .required = false},
                },
        };
    wifi_cb = f;
    // 启动WIFI
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));               // 设置工作模式为STA
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config)); // 设置wifi配置
    ESP_ERROR_CHECK(esp_wifi_start());                               // 启动WIFI

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    return ESP_OK;
}
