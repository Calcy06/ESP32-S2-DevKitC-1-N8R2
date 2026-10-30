#pragma once
#include <stdio.h>
#define ESP_LOGE(tag, fmt, ...) do { fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, fmt, ...) do { fprintf(stderr, "[W][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGI(tag, fmt, ...) do { fprintf(stderr, "[I][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGD(tag, fmt, ...) do { } while (0)
#define ESP_ERROR_CHECK(x) do { if ((x) != ESP_OK) { fprintf(stderr, "ESP_ERROR_CHECK failed at %s:%d\n", __FILE__, __LINE__); } } while (0)
