#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "simple_wifi_sta.h"

static const char *TAG = "main";

#define MQTT_ADDRESS "mqtt://14fbd9e5ff.st1.iotda-device.cn-north-4.myhuaweicloud.com:1883"
#define MQTT_CLIENT "6a82d28b7f2e6c302f902097_esp32_connect_0_0_2026082210"
#define MQTT_USERNAME "6a82d28b7f2e6c302f902097_esp32_connect"
#define MQTT_PASSWORD "YOUR_MQTT_PASSWORD" // MQTT密码（勿提交真实Token）
// 华为云 IoTDA 的其它参数是设备标识，可保留；真实密钥请放在不提交的文件里
// #define PUSHTOPIC "$oc/devices/你的设备ID/sys/properties/report"
#define MQTT_PUBLIC_TOPIC "$oc/devices/6a82d28b7f2e6c302f902097_esp32_connect/sys/properties/report" // 测试用的,推送消息主题
// 订阅平台下发命令（物模型命令、属性设置）
#define MQTT_SUBSCRIBE_TOPIC "$oc/devices/6a82d28b7f2e6c302f902097_esp32_connect/sys/commands/#" // 测试用的,需要订阅的主题

// 定义一个事件组，用于通知main函数WIFI连接成功
#define WIFI_CONNECT_BIT BIT0
static EventGroupHandle_t s_wifi_ev = NULL;

// MQTT客户端操作句柄
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

// MQTT连接标志
static bool s_is_mqtt_connected = false;

/**
 * mqtt连接事件处理函数
 * @param event 事件参数
 * @return 无
 */
static void aliot_mqtt_event_handler(void *event_handler_arg,
                                     esp_event_base_t event_base,
                                     int32_t event_id,
                                     void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    // your_context_t *context = event->context;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED: // 连接成功
        ESP_LOGI(TAG, "mqtt connected");
        s_is_mqtt_connected = true;
        // 连接成功后，订阅测试主题
        esp_mqtt_client_subscribe_single(s_mqtt_client, MQTT_SUBSCRIBE_TOPIC, 1);
        break;
    case MQTT_EVENT_DISCONNECTED: // 连接断开
        ESP_LOGI(TAG, "mqtt disconnected");
        s_is_mqtt_connected = false;
        break;
    case MQTT_EVENT_SUBSCRIBED: // 收到订阅消息ACK
        ESP_LOGI(TAG, " mqtt subscribed ack, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED: // 收到解订阅消息ACK
        break;
    case MQTT_EVENT_PUBLISHED: // 收到发布消息ACK
        ESP_LOGI(TAG, "mqtt publish ack, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic); // 收到Pub消息直接打印出来
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        break;
    }
}

/** 启动mqtt连接
 * @param 无
 * @return 无
 */
void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_cfg.broker.address.uri = MQTT_ADDRESS;
    // mqtt_cfg.broker.address.port = MQTT_PORT;
    // Client ID
    mqtt_cfg.credentials.client_id = MQTT_CLIENT;
    // 用户名
    mqtt_cfg.credentials.username = MQTT_USERNAME;
    // 密码
    mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
    ESP_LOGI(TAG, "mqtt connect->clientId:%s,username:%s,password:%s", mqtt_cfg.credentials.client_id,
             mqtt_cfg.credentials.username, mqtt_cfg.credentials.authentication.password);
    // 设置mqtt配置，返回mqtt操作句柄
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    // 注册mqtt事件回调函数
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, aliot_mqtt_event_handler, s_mqtt_client);
    // 启动mqtt连接
    esp_mqtt_client_start(s_mqtt_client);
}

/** wifi事件通知
 * @param 无
 * @return 无
 */
void wifi_event_handler(WIFI_EV_e ev)
{
    if (ev == WIFI_CONNECTED)
    {
        xEventGroupSetBits(s_wifi_ev, WIFI_CONNECT_BIT);
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS出现错误，执行擦除
        ESP_ERROR_CHECK(nvs_flash_erase());
        // 重新尝试初始化
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    s_wifi_ev = xEventGroupCreate();
    EventBits_t ev = 0;

    // 初始化WIFI，传入回调函数，用于通知连接成功事件
    wifi_sta_init(wifi_event_handler);

    // 一直监听WIFI连接事件，直到WiFi连接成功后，才启动MQTT连接
    ev = xEventGroupWaitBits(s_wifi_ev, WIFI_CONNECT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if (ev & WIFI_CONNECT_BIT)
    {
        mqtt_start();
    }
    static char mqtt_pub_buff[64];
    int count = 0;
    while (1)
    {

        // 延时2秒发布一条消息到/test/topic1主题
        if (s_is_mqtt_connected)
        {
            snprintf(mqtt_pub_buff, 64, "{\"count\":\"%d\"}", count);
            esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLIC_TOPIC,
                                    mqtt_pub_buff, strlen(mqtt_pub_buff), 1, 0);
            count++;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    return;
}
