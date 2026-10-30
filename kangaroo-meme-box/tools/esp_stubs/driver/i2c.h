#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum { I2C_MODE_MASTER = 1 } i2c_mode_t;
typedef enum { I2C_NUM_0 = 0, I2C_NUM_1 } i2c_port_t;
typedef enum { GPIO_PULLUP_ENABLE_STUB = 1 } i2c_pullup_t;
#define I2C_MASTER_WRITE 0
#ifndef GPIO_PULLUP_ENABLE
#define GPIO_PULLUP_ENABLE 1
#endif

typedef struct {
    i2c_mode_t mode;
    int sda_io_num;
    int scl_io_num;
    int sda_pullup_en;
    int scl_pullup_en;
    struct { uint32_t clk_speed; } master;
} i2c_config_t;

typedef void *i2c_cmd_handle_t;

esp_err_t i2c_param_config(i2c_port_t port, const i2c_config_t *conf);
esp_err_t i2c_driver_install(i2c_port_t port, i2c_mode_t mode, size_t slv_rx, size_t slv_tx, int flags);
i2c_cmd_handle_t i2c_cmd_link_create(void);
void i2c_cmd_link_delete(i2c_cmd_handle_t cmd);
esp_err_t i2c_master_start(i2c_cmd_handle_t cmd);
esp_err_t i2c_master_stop(i2c_cmd_handle_t cmd);
esp_err_t i2c_master_write_byte(i2c_cmd_handle_t cmd, uint8_t data, bool ack);
esp_err_t i2c_master_write(i2c_cmd_handle_t cmd, uint8_t *data, size_t len, bool ack);
esp_err_t i2c_master_cmd_begin(i2c_port_t port, i2c_cmd_handle_t cmd, uint32_t ticks);
