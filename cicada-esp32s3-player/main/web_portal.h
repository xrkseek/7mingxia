#pragma once

#include "esp_err.h"
#include <stdbool.h>

/** SoftAP + 内嵌科普网页。开放热点 7MingXia → 连上自动跳 http://192.168.4.1 */
esp_err_t web_portal_start(void);

/** 取出并清除一条网页指令。返回值见 WEB_CMD_* */
int web_portal_take_cmd(void);

/** 网页请求停止时置位；与实体键共用 stop 语义 */
void web_portal_bind_stop_flag(volatile bool *stop_flag);

#define WEB_CMD_NONE            0
#define WEB_CMD_PLAY_ALL        1
#define WEB_CMD_STOP            2
#define WEB_CMD_PLAY_EGG        3
#define WEB_CMD_LAMP_DEBUG      4  /* 灯泡依次点亮调试（再发一次退出） */
#define WEB_CMD_PLAY_TRACK_BASE 10  /* 10 + track_index */
