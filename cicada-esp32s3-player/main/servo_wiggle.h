#pragma once

#include "esp_err.h"
#include "board_pins.h"

/** 启动 LEDC 与摆动任务；空闲全停 PWM（idle=0）。 */
esp_err_t servo_wiggle_start(void);

/**
 * 指定一路摆动，其余停。
 * SERVO_IDX_LONGHORN / KATYDID_NIGHT / LOCUST，或 SERVO_IDX_NONE。
 */
void servo_wiggle_set_active(int index);
