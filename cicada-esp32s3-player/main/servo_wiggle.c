/*
 * 3× 180° 9g 舵机，LEDC 50Hz。
 * 仅 set_active(idx) 时对该路出 PWM；空闲 ledc_stop(idle=0)。
 * 勿对灯脚做 gpio 抢占：停 PWM 只走 ledc_stop，保持 LEDC 矩阵，下次可直接 duty。
 *
 * idx: SERVO_IDX_LONGHORN(15) / KATYDID_NIGHT(16) / LOCUST(13)
 * 不占用 PIN_LAMP_LOCUST(2)。
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "board_pins.h"
#include "servo_wiggle.h"

static const char *TAG = "servo";

#define SERVO_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER    LEDC_TIMER_0
#define SERVO_LEDC_RES      LEDC_TIMER_13_BIT
#define SERVO_LEDC_FREQ_HZ  50
#define SERVO_PERIOD_US     20000
#define SERVO_DUTY_MAX      ((1u << 13) - 1u)

#define SERVO_CENTER_DEG    90
#define SERVO_AMPLITUDE_DEG 4
#define SERVO_NIGHT_AMP_DEG 30
#define SERVO_STEP_DEG      1
#define SERVO_STEP_MS       50
#define SERVO_TICK_MS       20

static const gpio_num_t s_servo_pins[PIN_SERVO_COUNT] = {
    PIN_SERVO_LONGHORN,
    PIN_SERVO_KATYDID_NIGHT,
    PIN_SERVO_LOCUST,
};
static const ledc_channel_t s_ledc_ch[PIN_SERVO_COUNT] = {
    LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2,
};
static const char *s_roles[PIN_SERVO_COUNT] = {
    "天牛", "螽斯晚上", "蝗虫",
};

static bool s_ledc_ready;
static volatile int s_active = SERVO_IDX_NONE;

static uint32_t angle_to_pulse_us(int angle_deg)
{
    if (angle_deg < 0) {
        angle_deg = 0;
    }
    if (angle_deg > 180) {
        angle_deg = 180;
    }
    return 1000 + (uint32_t)angle_deg * 1000 / 180;
}

static void servo_set_one(int idx, int angle_deg)
{
    if (idx < 0 || idx >= PIN_SERVO_COUNT || !s_ledc_ready) {
        return;
    }
    const uint32_t duty = angle_to_pulse_us(angle_deg) * SERVO_DUTY_MAX / SERVO_PERIOD_US;
    ledc_set_duty(SERVO_LEDC_MODE, s_ledc_ch[idx], duty);
    ledc_update_duty(SERVO_LEDC_MODE, s_ledc_ch[idx]);
}

/* 空闲：LEDC 停在低电平。不要 gpio_set_direction，否则拆掉矩阵下次无 PWM。 */
static void servo_signal_off_all(void)
{
    if (!s_ledc_ready) {
        return;
    }
    for (int i = 0; i < PIN_SERVO_COUNT; i++) {
        ledc_stop(SERVO_LEDC_MODE, s_ledc_ch[i], 0);
    }
}

static int servo_amp_for(int idx)
{
    return (idx == SERVO_IDX_KATYDID_NIGHT) ? SERVO_NIGHT_AMP_DEG : SERVO_AMPLITUDE_DEG;
}

void servo_wiggle_set_active(int index)
{
    if (index < SERVO_IDX_NONE || index >= PIN_SERVO_COUNT) {
        index = SERVO_IDX_NONE;
    }
    s_active = index;
    if (index < 0) {
        ESP_LOGI(TAG, "idle (all PWM off)");
        servo_signal_off_all();
    } else {
        ESP_LOGI(TAG, "active=%d %s GPIO%d",
                 index, s_roles[index], (int)s_servo_pins[index]);
        servo_signal_off_all();
        servo_set_one(index, SERVO_CENTER_DEG);
    }
}

static void servo_wiggle_task(void *arg)
{
    (void)arg;
    int angle = SERVO_CENTER_DEG;
    int dir = 1;
    int last_active = -2;
    int accum_ms = 0;

    while (1) {
        const int active = s_active;
        accum_ms += SERVO_TICK_MS;

        if (active < 0) {
            if (last_active != SERVO_IDX_NONE) {
                servo_signal_off_all();
                last_active = SERVO_IDX_NONE;
                angle = SERVO_CENTER_DEG;
                dir = 1;
            }
            accum_ms = 0;
        } else if (accum_ms >= SERVO_STEP_MS) {
            accum_ms = 0;
            if (active != last_active) {
                servo_signal_off_all();
                angle = SERVO_CENTER_DEG;
                dir = 1;
                last_active = active;
                servo_set_one(active, angle);
            }
            const int amp = servo_amp_for(active);
            const int lo = SERVO_CENTER_DEG - amp;
            const int hi = SERVO_CENTER_DEG + amp;
            angle += dir * SERVO_STEP_DEG;
            if (angle >= hi) {
                angle = hi;
                dir = -1;
            } else if (angle <= lo) {
                angle = lo;
                dir = 1;
            }
            servo_set_one(active, angle);
        }

        vTaskDelay(pdMS_TO_TICKS(SERVO_TICK_MS));
    }
}

esp_err_t servo_wiggle_start(void)
{
    ESP_LOGI(TAG, "LEDC 50Hz play-only: GPIO%d/%d/%d (灯脚 GPIO%d 不用)",
             (int)PIN_SERVO_LONGHORN, (int)PIN_SERVO_KATYDID_NIGHT,
             (int)PIN_SERVO_LOCUST, (int)PIN_LAMP_LOCUST);

    ledc_timer_config_t timer_cfg = {
        .speed_mode = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_LEDC_RES,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc timer: %s", esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i < PIN_SERVO_COUNT; i++) {
        ledc_channel_config_t ch_cfg = {
            .gpio_num = s_servo_pins[i],
            .speed_mode = SERVO_LEDC_MODE,
            .channel = s_ledc_ch[i],
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = SERVO_LEDC_TIMER,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&ch_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc ch[%d] GPIO%d: %s", i, (int)s_servo_pins[i],
                     esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "servo%d SIG=GPIO%d (%s)",
                 i + 1, (int)s_servo_pins[i], s_roles[i]);
    }

    s_ledc_ready = true;
    s_active = SERVO_IDX_NONE;
    servo_signal_off_all();

    if (xTaskCreate(servo_wiggle_task, "servo_wiggle", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
