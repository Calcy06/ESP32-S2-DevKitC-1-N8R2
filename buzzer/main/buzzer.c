#include "buzzer.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "buzzer";

/* 模块内部状态：记住引脚号和有效电平，buzzer_set() 才能换算真实电平 */
static gpio_num_t s_gpio        = GPIO_NUM_NC;
static int        s_active_lvl  = 1;

esp_err_t buzzer_init(gpio_num_t gpio, int active_level)
{
    /* 防御性检查：不是所有的 GPIO 号都能作为输出。
       GPIO46 在 ESP32-S2 上是「仅输入」，会被这个宏挡掉。 */
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(gpio),
                        ESP_ERR_INVALID_ARG, TAG, "GPIO%d 不能作为输出使用", (int)gpio);

    s_gpio       = gpio;
    s_active_lvl = active_level ? 1 : 0;

    gpio_config_t io_conf = {
        /* 位掩码：一个 gpio_config() 可以一次配多个引脚，这里只配一个。
           注意必须用 1ULL << gpio，用 1U 会在 gpio >= 32 时溢出（S2 最高到 46）。 */
        .pin_bit_mask   = (1ULL << gpio),

        /* 推挽输出：高电平由内部上拉管主动驱动到 3.3V，低电平主动拉到 GND。
           有源高电平触发蜂鸣器就用推挽。
           只有在「低电平有效 + 外部上拉到 5V」时才需要 GPIO_MODE_OUTPUT_OD（开漏）。 */
        .mode           = GPIO_MODE_OUTPUT,

        /* 推挽输出模式下，内部上下拉对输出电平没有意义，一律关掉。
           - 上拉：若开启，会在输出高电平时并联一个 45kΩ 上拉，毫无作用；
           - 下拉：输出模式下也没有意义，且会额外增加静态电流。
           真正要解决「上电瞬间误鸣」，请看 README 里的硬件 10kΩ 下拉方案。 */
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,

        /* 蜂鸣器是纯输出负载，不需要中断，必须显式关闭，
           否则残留配置可能导致定时器/中断资源被无意义占用。 */
        .intr_type      = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }

    /* 提高驱动能力档位（ESP32-S2 共 4 档）：
       CAP_0 ≈ 5mA / CAP_1 ≈ 10mA / CAP_2 ≈ 20mA / CAP_3 ≈ 40mA。
       - 若 GPIO 直接带小功率 3.3V 蜂鸣器，用 CAP_1（10mA）足够；
       - 若 GPIO 只驱动三极管/MOS 的栅极/基极（本例推荐做法），电流极小，
         CAP_0 即可，这里用 CAP_1 是给直驱方案留余量。
       注意：这只是「驱动强度」，不是限流保护，直驱蜂鸣器仍必须串限流电阻！ */
    gpio_set_drive_capability(gpio, GPIO_DRIVE_CAP_1);

    /* 关键：先把输出寄存器设成「关闭电平」，再让引脚变输出，
       避免配置完成的瞬间出现一次误鸣。
       有源高电平触发蜂鸣器 -> 关闭电平 = 0（低）。 */
    gpio_set_level(gpio, !s_active_lvl);

    ESP_LOGI(TAG, "buzzer init: GPIO%d, active_level=%d (%s)",
             (int)gpio, s_active_lvl,
             s_active_lvl ? "高电平触发" : "低电平触发");
    return ESP_OK;
}

void buzzer_set(bool on)
{
    if (s_gpio == GPIO_NUM_NC) {
        return;  /* 未初始化，安全返回 */
    }
    /* 有效电平换算：高电平触发 -> on 时输出 1；低电平触发 -> on 时输出 0 */
    gpio_set_level(s_gpio, on ? s_active_lvl : !s_active_lvl);
}

void buzzer_on(void)
{
    buzzer_set(true);
}

void buzzer_off(void)
{
    buzzer_set(false);
}
