/*
 * ESP32-S3-DevKitC-1 / WROOM-1（16MB Flash + 8MB Octal PSRAM）引脚总表。
 * 对照乐鑫官方 Header Block 丝印数字（J1 / J3）。
 *
 * 官方勿用：GPIO26–32 Flash；35–37 PSRAM；19/20 USB；0/45/46 strapping；
 * GPIO3 strapping（空闲）；43/44 UART0；38 板载 RGB。
 *
 * 蝗虫：灯=2，舵机=13。无独角仙。
 */
#pragma once

#include "driver/gpio.h"

#define PIN_I2S_BCLK        GPIO_NUM_4
#define PIN_I2S_WS          GPIO_NUM_5
#define PIN_I2S_DOUT        GPIO_NUM_6

#define PIN_SERVO_1                 GPIO_NUM_15  /* 天牛 */
#define PIN_SERVO_LONGHORN          PIN_SERVO_1
#define PIN_SERVO_2                 GPIO_NUM_16  /* 螽斯晚上 */
#define PIN_SERVO_KATYDID_NIGHT     PIN_SERVO_2
#define PIN_SERVO_3                 GPIO_NUM_13  /* 蝗虫 */
#define PIN_SERVO_LOCUST            PIN_SERVO_3
#define PIN_SERVO_COUNT             3

#define SERVO_IDX_NONE              (-1)
#define SERVO_IDX_LONGHORN          0
#define SERVO_IDX_KATYDID_NIGHT     1
#define SERVO_IDX_LOCUST            2

#define PIN_BTN_PLAY        GPIO_NUM_40

/* 轨道灯：一律高电平亮、低电平灭 */
#define LAMP_ACTIVE_LEVEL       1

#define PIN_LAMP_CICADA         GPIO_NUM_1
#define PIN_LAMP_CRICKET        GPIO_NUM_11
#define PIN_LAMP_MOLE           GPIO_NUM_17
#define PIN_LAMP_KATYDID_DAY    GPIO_NUM_14
#define PIN_LAMP_LONGHORN       GPIO_NUM_41
#define PIN_LAMP_KATYDID_NIGHT  GPIO_NUM_42
#define PIN_LAMP_LOCUST         GPIO_NUM_2
#define PIN_LAMP_COUNT          7
