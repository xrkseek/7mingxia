#pragma once

#include "esp_err.h"
#include <stdbool.h>

/** NimBLE 外设。广播名 7MingXia。点某一轨：空闲立刻播，忙则排队。 */
esp_err_t ble_ctrl_start(void);

/** 与 web_portal_take_cmd 相同的 WEB_CMD_*。 */
int ble_ctrl_take_cmd(void);

void ble_ctrl_bind_stop_flag(volatile bool *stop_flag);

/** 任意播放开始时调用，避免蓝牙请求叠在正在播的声音上。 */
void ble_ctrl_mark_busy(void);

/** 一段播放结束。下一位若在排队，会自动开始。 */
void ble_ctrl_play_finished(void);
