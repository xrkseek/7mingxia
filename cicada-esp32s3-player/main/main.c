/*
 * 7鸣匣 — ESP32-S3 + MAX98357A
 *
 * 空闲：灯灭、舵机 PWM 停、音频关。SoftAP 热点提供科普网页与播控。
 * 按键 GPIO40（按下 HIGH）或网页：播列表 / 单曲；播放中再按或网页停止。
 * 每轨：亮灯 → 短亮 → 灭灯 → 再 I2S/舵机/播音。
 *
 * 引脚见 board_pins.h；蝗虫灯=2 / 舵机=13；蟋蟀=11；螽斯白天=14。无独角仙。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "board_pins.h"
#include "servo_wiggle.h"
#include "web_portal.h"
#include "ble_ctrl.h"

static const char *TAG = "chirp";

#define CHUNK_FRAMES    256
#define TRACK_GAP_MS    800
#define LAMP_LEAD_MS    1000
#define DRAIN_FRAMES    2048
#define BTN_DEBOUNCE_MS 40

extern const uint8_t cicada_wav_start[]        asm("_binary_cicada_wav_start");
extern const uint8_t cicada_wav_end[]          asm("_binary_cicada_wav_end");
extern const uint8_t cricket_wav_start[]       asm("_binary_cricket_wav_start");
extern const uint8_t cricket_wav_end[]         asm("_binary_cricket_wav_end");
extern const uint8_t mole_cricket_wav_start[]  asm("_binary_mole_cricket_wav_start");
extern const uint8_t mole_cricket_wav_end[]    asm("_binary_mole_cricket_wav_end");
extern const uint8_t katydid_day_wav_start[]   asm("_binary_katydid_day_wav_start");
extern const uint8_t katydid_day_wav_end[]     asm("_binary_katydid_day_wav_end");
extern const uint8_t katydid_night_wav_start[] asm("_binary_katydid_night_wav_start");
extern const uint8_t katydid_night_wav_end[]   asm("_binary_katydid_night_wav_end");
extern const uint8_t longhorn_wav_start[]      asm("_binary_longhorn_wav_start");
extern const uint8_t longhorn_wav_end[]        asm("_binary_longhorn_wav_end");
extern const uint8_t locust_wav_start[]        asm("_binary_locust_wav_start");
extern const uint8_t locust_wav_end[]          asm("_binary_locust_wav_end");
extern const uint8_t egg_chou_xuhang_wav_start[] asm("_binary_egg_chou_xuhang_wav_start");
extern const uint8_t egg_chou_xuhang_wav_end[]   asm("_binary_egg_chou_xuhang_wav_end");

typedef struct {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    const uint8_t *pcm;
    size_t pcm_bytes;
} wav_info_t;

typedef struct {
    const char *name;
    const uint8_t *start;
    const uint8_t *end;
    gpio_num_t lamp;
    int servo_idx;
} track_t;

static bool parse_wav(const uint8_t *data, size_t size, wav_info_t *out)
{
    if (size < 44 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }

    size_t offset = 12;
    const uint8_t *fmt = NULL;
    uint32_t fmt_size = 0;
    const uint8_t *pcm = NULL;
    uint32_t pcm_size = 0;

    while (offset + 8 <= size) {
        const uint8_t *chunk = data + offset;
        uint32_t chunk_size = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (chunk[7] << 24);
        offset += 8;
        if (offset + chunk_size > size) {
            return false;
        }
        if (memcmp(chunk, "fmt ", 4) == 0) {
            fmt = data + offset;
            fmt_size = chunk_size;
        } else if (memcmp(chunk, "data", 4) == 0) {
            pcm = data + offset;
            pcm_size = chunk_size;
            break;
        }
        offset += chunk_size + (chunk_size & 1);
    }

    if (!fmt || fmt_size < 16 || !pcm || pcm_size == 0) {
        return false;
    }

    out->audio_format = fmt[0] | (fmt[1] << 8);
    out->num_channels = fmt[2] | (fmt[3] << 8);
    out->sample_rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
    out->bits_per_sample = fmt[14] | (fmt[15] << 8);
    out->pcm = pcm;
    out->pcm_bytes = pcm_size;
    return out->audio_format == 1 && out->bits_per_sample == 16;
}

static i2s_chan_handle_t s_tx;
static uint32_t s_rate;
static volatile bool s_stop_play;

/* 灯调试顺序与播放顺序一致：昼蝉蝗天牛蝈螽，夜蟋蟀蝼蛄纺织娘 */
static const gpio_num_t s_lamps[PIN_LAMP_COUNT] = {
    PIN_LAMP_CICADA,
    PIN_LAMP_LOCUST,
    PIN_LAMP_LONGHORN,
    PIN_LAMP_KATYDID_DAY,
    PIN_LAMP_CRICKET,
    PIN_LAMP_MOLE,
    PIN_LAMP_KATYDID_NIGHT,
};

static const gpio_num_t s_servos[PIN_SERVO_COUNT] = {
    PIN_SERVO_LONGHORN,
    PIN_SERVO_KATYDID_NIGHT,
    PIN_SERVO_LOCUST,
};

static void btn_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_BTN_PLAY,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
}

static void track_lamps_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < PIN_LAMP_COUNT; i++) {
        if (rtc_gpio_is_valid_gpio(s_lamps[i])) {
            rtc_gpio_deinit(s_lamps[i]);
        }
        mask |= 1ULL << s_lamps[i];
    }
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    for (int i = 0; i < PIN_LAMP_COUNT; i++) {
        gpio_set_drive_capability(s_lamps[i], GPIO_DRIVE_CAP_3);
        gpio_set_level(s_lamps[i], !LAMP_ACTIVE_LEVEL); /* idle 灭 */
        ESP_LOGI(TAG, "lamp GPIO%d idle OFF", (int)s_lamps[i]);
    }
}

static void track_lamp_all_off(void)
{
    for (int i = 0; i < PIN_LAMP_COUNT; i++) {
        gpio_set_level(s_lamps[i], !LAMP_ACTIVE_LEVEL);
    }
}

static void track_lamp_set(gpio_num_t pin, bool on)
{
    if (pin == GPIO_NUM_NC) {
        return;
    }
    if (on) {
        gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_3);
    }
    gpio_set_level(pin, on ? LAMP_ACTIVE_LEVEL : !LAMP_ACTIVE_LEVEL);
    ESP_LOGI(TAG, "lamp GPIO%d -> %s", (int)pin, on ? "ON" : "OFF");
}

static void pins_audit(void)
{
    for (int i = 0; i < PIN_LAMP_COUNT; i++) {
        for (int j = 0; j < PIN_SERVO_COUNT; j++) {
            if (s_lamps[i] == s_servos[j]) {
                ESP_LOGE(TAG, "PIN CONFLICT lamp GPIO%d == servo GPIO%d",
                         (int)s_lamps[i], (int)s_servos[j]);
            }
        }
    }
    ESP_LOGI(TAG,
             "pins: I2S %d/%d/%d | lamps 1,11,17,14,42,41,2 | servos 15,16,13 | btn %d",
             (int)PIN_I2S_BCLK, (int)PIN_I2S_WS, (int)PIN_I2S_DOUT, (int)PIN_BTN_PLAY);
}

static bool btn_click_edge(void)
{
    static bool armed = true;
    static bool last = false;
    static TickType_t last_change = 0;

    const bool now = gpio_get_level(PIN_BTN_PLAY) != 0;
    const TickType_t t = xTaskGetTickCount();
    if (now != last) {
        last = now;
        last_change = t;
    }
    if ((t - last_change) < pdMS_TO_TICKS(BTN_DEBOUNCE_MS)) {
        return false;
    }
    if (now && armed) {
        armed = false;
        return true;
    }
    if (!now) {
        armed = true;
    }
    return false;
}

/** 空闲等待：实体键或网页。返回单曲序号，-1 播全部，-2 彩蛋，-3 灯泡调试。 */
static int wait_play_trigger(void)
{
    while (1) {
        if (btn_click_edge()) {
            return -1;
        }
        int cmd = web_portal_take_cmd();
        if (cmd == WEB_CMD_NONE) {
            cmd = ble_ctrl_take_cmd();
        }
        if (cmd == WEB_CMD_PLAY_ALL) {
            return -1;
        }
        if (cmd == WEB_CMD_PLAY_EGG) {
            return -2;
        }
        if (cmd == WEB_CMD_LAMP_DEBUG) {
            return -3;
        }
        if (cmd >= WEB_CMD_PLAY_TRACK_BASE &&
            cmd < WEB_CMD_PLAY_TRACK_BASE + PIN_LAMP_COUNT) {
            return cmd - WEB_CMD_PLAY_TRACK_BASE;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static bool want_stop(void)
{
    if (s_stop_play) {
        return true;
    }
    if (btn_click_edge()) {
        s_stop_play = true;
        return true;
    }
    return false;
}

static esp_err_t i2s_ensure(uint32_t sample_rate)
{
    if (s_tx == NULL) {
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
        chan_cfg.auto_clear = true;
        ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "new channel");

        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = PIN_I2S_BCLK,
                .ws = PIN_I2S_WS,
                .dout = PIN_I2S_DOUT,
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };

        ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "init std");
        ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable");
        s_rate = sample_rate;
        return ESP_OK;
    }

    if (s_rate == sample_rate) {
        return ESP_OK;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx), TAG, "disable for retune");
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_tx, &clk_cfg), TAG, "reconfig clock");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "re-enable");
    s_rate = sample_rate;
    ESP_LOGI(TAG, "I2S retuned to %u Hz", (unsigned)sample_rate);
    return ESP_OK;
}

static esp_err_t write_silence(size_t frames)
{
    static const int16_t zero[CHUNK_FRAMES * 2] = {0};

    while (frames > 0) {
        if (want_stop()) {
            return ESP_OK;
        }
        size_t n = frames > CHUNK_FRAMES ? CHUNK_FRAMES : frames;
        size_t written = 0;
        ESP_RETURN_ON_ERROR(i2s_channel_write(s_tx, zero, n * 2 * sizeof(int16_t), &written, portMAX_DELAY),
                            TAG, "silence write");
        frames -= n;
    }
    return ESP_OK;
}

static void i2s_teardown(void)
{
    if (s_tx) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
        s_rate = 0;
    }
}

/**
 * 每轨：亮灯 → LAMP_LEAD_MS → 灭灯 → 再 I2S/舵机/播音。
 */
static void play_wav(const track_t *tr)
{
    wav_info_t wav = {0};
    const size_t size = (size_t)(tr->end - tr->start);
    if (!parse_wav(tr->start, size, &wav)) {
        ESP_LOGE(TAG, "%s: need 16-bit PCM WAV", tr->name);
        return;
    }

    const size_t channels = wav.num_channels ? wav.num_channels : 1;
    if (channels > 2) {
        ESP_LOGE(TAG, "%s: unsupported channels %u", tr->name, (unsigned)channels);
        return;
    }

    track_lamp_set(tr->lamp, true);
    vTaskDelay(pdMS_TO_TICKS(LAMP_LEAD_MS));
    track_lamp_set(tr->lamp, false);

    if (i2s_ensure(wav.sample_rate) != ESP_OK) {
        ESP_LOGE(TAG, "%s: I2S setup failed", tr->name);
        return;
    }

    int16_t *stereo = malloc(CHUNK_FRAMES * 2 * sizeof(int16_t));
    if (!stereo) {
        ESP_LOGE(TAG, "oom");
        return;
    }

    const int16_t *src = (const int16_t *)wav.pcm;
    const size_t total_frames = wav.pcm_bytes / sizeof(int16_t) / channels;

    ESP_LOGI(TAG, "%s: %u Hz %uch %.1fs lamp=%d servo=%d",
             tr->name, (unsigned)wav.sample_rate, (unsigned)channels,
             (double)total_frames / wav.sample_rate, (int)tr->lamp, tr->servo_idx);

    servo_wiggle_set_active(tr->servo_idx);

    size_t frame = 0;
    while (frame < total_frames) {
        if (want_stop()) {
            ESP_LOGI(TAG, "%s: stop", tr->name);
            break;
        }

        size_t n = total_frames - frame;
        if (n > CHUNK_FRAMES) {
            n = CHUNK_FRAMES;
        }

        if (channels == 1) {
            for (size_t i = 0; i < n; i++) {
                int16_t s = src[frame + i];
                stereo[i * 2] = s;
                stereo[i * 2 + 1] = s;
            }
        } else {
            memcpy(stereo, &src[frame * channels], n * 2 * sizeof(int16_t));
        }

        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, stereo, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: i2s write: %s", tr->name, esp_err_to_name(err));
            break;
        }
        frame += n;
    }

    servo_wiggle_set_active(SERVO_IDX_NONE);
    track_lamp_all_off();
    free(stereo);
    ESP_LOGI(TAG, "%s: %s", tr->name, s_stop_play ? "stopped" : "done");
}

/** 七路灯依次点亮；再点网页「灯泡调试」或停止/实体键退出 */
static void run_lamp_debug(void)
{
    ESP_LOGI(TAG, "lamp debug start");
    int idx = 0;
    while (!s_stop_play) {
        int cmd = web_portal_take_cmd();
        if (cmd == WEB_CMD_LAMP_DEBUG || cmd == WEB_CMD_STOP) {
            break;
        }
        if (btn_click_edge()) {
            break;
        }

        track_lamp_all_off();
        track_lamp_set(s_lamps[idx], true);
        idx = (idx + 1) % PIN_LAMP_COUNT;

        for (int t = 0; t < 50 && !s_stop_play; t++) { /* 每灯约 500ms */
            cmd = web_portal_take_cmd();
            if (cmd == WEB_CMD_LAMP_DEBUG || cmd == WEB_CMD_STOP) {
                s_stop_play = true;
                break;
            }
            if (btn_click_edge()) {
                s_stop_play = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    track_lamp_all_off();
    ESP_LOGI(TAG, "lamp debug end");
}

void app_main(void)
{
    /* 下标即网页/蓝牙曲号：0蝉 1蝗虫 2天牛 3蝈螽 4蟋蟀 5蝼蛄 6纺织娘 */
    static const track_t tracks[] = {
        { "cicada",        cicada_wav_start,        cicada_wav_end,        PIN_LAMP_CICADA,        SERVO_IDX_NONE },
        { "locust",        locust_wav_start,        locust_wav_end,        PIN_LAMP_LOCUST,        SERVO_IDX_LOCUST },
        { "longhorn",      longhorn_wav_start,      longhorn_wav_end,      PIN_LAMP_LONGHORN,      SERVO_IDX_LONGHORN },
        { "katydid_day",   katydid_day_wav_start,   katydid_day_wav_end,   PIN_LAMP_KATYDID_DAY,   SERVO_IDX_NONE },
        { "cricket",       cricket_wav_start,       cricket_wav_end,       PIN_LAMP_CRICKET,       SERVO_IDX_NONE },
        { "mole_cricket",  mole_cricket_wav_start,  mole_cricket_wav_end,  PIN_LAMP_MOLE,          SERVO_IDX_NONE },
        { "katydid_night", katydid_night_wav_start, katydid_night_wav_end, PIN_LAMP_KATYDID_NIGHT, SERVO_IDX_KATYDID_NIGHT },
    };
    const size_t track_count = sizeof(tracks) / sizeof(tracks[0]);

    size_t total = 0;
    for (size_t i = 0; i < track_count; i++) {
        total += (size_t)(tracks[i].end - tracks[i].start);
    }
    ESP_LOGI(TAG, "7鸣匣: %u tracks, %u bytes embedded",
             (unsigned)track_count, (unsigned)total);

    pins_audit();
    btn_init();
    /* 电池上电先稳住，再开舵机/灯/WiFi，减轻 SoftAP 瞬间掉压复位 */
    vTaskDelay(pdMS_TO_TICKS(300));
    /* 与上次能亮时一致：先舵机(15/16/13)再灯 gpio_config */
    esp_err_t servo_err = servo_wiggle_start();
    if (servo_err != ESP_OK) {
        ESP_LOGW(TAG, "servo start failed: %s", esp_err_to_name(servo_err));
    }
    track_lamps_init();

    web_portal_bind_stop_flag(&s_stop_play);
    ble_ctrl_bind_stop_flag(&s_stop_play);
    if (web_portal_start() != ESP_OK) {
        ESP_LOGW(TAG, "web portal failed — button-only mode");
    }
    if (ble_ctrl_start() != ESP_OK) {
        ESP_LOGW(TAG, "BLE queue failed — hotspot/button only");
    }

    while (1) {
        ESP_LOGI(TAG, "idle — GPIO%d or SoftAP http://192.168.4.1", (int)PIN_BTN_PLAY);
        int only = wait_play_trigger();
        s_stop_play = false;
        ble_ctrl_mark_busy();

        if (only == -3) {
            ESP_LOGI(TAG, "lamp debug");
            run_lamp_debug();
        } else if (only == -2) {
            static const track_t egg = {
                "chou_xuhang",
                egg_chou_xuhang_wav_start,
                egg_chou_xuhang_wav_end,
                GPIO_NUM_NC,
                SERVO_IDX_NONE,
            };
            ESP_LOGI(TAG, "play egg");
            play_wav(&egg);
        } else if (only >= 0 && (size_t)only < track_count) {
            ESP_LOGI(TAG, "play start (track %d)", only);
            play_wav(&tracks[only]);
        } else {
            ESP_LOGI(TAG, "play start (all)");
            for (size_t i = 0; i < track_count; i++) {
                if (s_stop_play) {
                    break;
                }
                if (i > 0 && s_tx) {
                    write_silence((size_t)TRACK_GAP_MS * s_rate / 1000);
                    if (s_stop_play) {
                        break;
                    }
                }
                play_wav(&tracks[i]);
            }
        }

        if (s_tx) {
            write_silence(DRAIN_FRAMES);
        }
        servo_wiggle_set_active(SERVO_IDX_NONE);
        track_lamp_all_off();
        i2s_teardown();
        ESP_LOGI(TAG, "play end%s", s_stop_play ? " (stopped)" : "");
        ble_ctrl_play_finished();

        while (gpio_get_level(PIN_BTN_PLAY) != 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));
    }
}
