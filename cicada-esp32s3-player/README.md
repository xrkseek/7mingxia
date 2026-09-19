# 7鸣匣 —— ESP32-S3 昆虫叫声播放器

按 **播放键** 或 **手机网页** 顺序播七段叫声；播放中再按一次 / 点「停止」结束。空闲时舵机不摆、灯全灭。

播放顺序（每段约 **5 秒**，段间静音 0.8s）：

**白天：蝉 → 蝗虫 → 天牛 → 蝈螽；夜里：蟋蟀 → 蝼蛄 → 纺织娘**

| 文件 | 说明 | 来源（约） |
|---|---|---|
| `cicada.wav` | 蝉 | 抖音「山海自然观」素材，截 5s |
| `cricket.wav` | 蟋蟀 | 抖音油葫芦鸣叫，截 5s |
| `mole_cricket.wav` | 蝼蛄 | Wikimedia *Gryllotalpa gryllotalpa*，CC BY，响度窗口截 5s |
| `katydid_day.wav` | 螽斯白天 | 抖音纺织娘相关素材，截 5s |
| `katydid_night.wav` | 螽斯黑夜 | 抖音素材，截 5s |
| `longhorn.wav` | 天牛 | 抖音 `7652307906738585521`，高通后截响度最大 5s |
| `locust.wav` | 蝗虫 | 抖音 `7401524677300030783`，截响度最大 5s |

规格统一：**PCM WAV / 16 kHz / mono / 16-bit**，每段约 **160 KB**，七段合计约 **1.1 MB**。构建时经 `EMBED_FILES` 嵌入固件。

## SoftAP 科普网页

上电后设备开热点，手机连上即可打开科普页并遥控播音：

| 项 | 值 |
|----|----|
| SSID | `7MingXia`（开放网络，无密码） |
| 地址 | 连上后手机通常自动弹出；也可手动打开 http://192.168.4.1 |
| 接口 | `POST /api/play`、`/api/play?track=0..6`、`/api/stop` |

页面源码在 `web/index.html`（无图、紧凑播控首页），构建时嵌入固件。

独立科普站（Cloudflare Pages，含照片）：仓库根目录 → https://github.com/xrkseek/7mingxia

科普站首页 https://7mingxia.pages.dev ：先看七种虫。白天蝉、蝗虫、天牛、蝈螽，夜里蟋蟀、蝼蛄、纺织娘。页底再点名播放。点某一只就单独亮灯、放这一声；正在播时后来的人自动排上。曲号 0–6 与这个顺序一致。需安卓 Chrome 或电脑 Edge。iPhone 自带浏览器不支持网页蓝牙。

## 音频格式与换轨

固件要求 **PCM / 16-bit**。用 `tools/mp3_to_mcu_wav.py` 转换：

```powershell
python cicada-esp32s3-player\tools\mp3_to_mcu_wav.py 输入.mp3 cicada-esp32s3-player\spiffs\cricket.wav
```

改完 `spiffs/*.wav` 后需重新 `build`/`flash`（中文路径请先同步到英文副本，见下文）。

## 分区表

`partitions.csv`：factory **0x3F0000**。`sdkconfig.defaults` 已启用自定义分区表。改分区后需删旧 `sdkconfig` 或 menuconfig 手动改。

## 硬件

- **ESP32-S3-DevKitC-1**（本工程按 **16MB Flash + 8MB Octal PSRAM**）
- **MAX98357A** I2S 功放 + 小喇叭（4Ω/8Ω，2–3W）
- **3× 9g 舵机**（仅播对应轨时给 SIG）
- **播放按键**（OUT → GPIO40，按下 **HIGH**）
- **7× 轨道灯**（GPIO **HIGH = 亮**）

**本板禁用脚：** GPIO26–32（Flash）、GPIO35–37（PSRAM）、GPIO19/20（USB）、GPIO0/45/46（strapping）、GPIO43/44（UART0）。GPIO3 空闲。

### 供电（推荐）

三节干电池约 **4.5V**：

| 接到 | 说明 |
|------|------|
| 板子 **5V / VIN** | 经板载稳压出 3.3V；**勿直接灌 3V3** |
| 功放 VIN、舵机红线 | **单独粗线**接电池正极（不要只经面包板细线） |
| 全部 GND | **共地** |

灯脚仍是 **GPIO ≈ 3.3V**，电池电压加不到灯上。

**USB 正常、电池只亮灯/舵机不动：** 多半是 SoftAP + 舵机瞬间掉压导致芯片反复复位。固件已降低 WiFi 发射功率；硬件上请：电池要够新、正极分路给功放/舵机、靠近板子加 **470µF～1000µF** 电解电容（正极→VIN/电池+，负极→GND）。

## 引脚总表（与 `main/board_pins.h` 一致）

对照 [DevKitC-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide.html) 丝印 **J1 / J3**：

| 功能 | GPIO | 备注 |
|------|------|------|
| I2S BCLK / WS / DOUT | **4 / 5 / 6** | MAX98357A |
| 蝉灯 | **1** | 无舵机 |
| **蟋蟀灯** | **11** | |
| **螽斯白天灯** | **14** | |
| **蝗虫灯** | **2** | 只接灯 |
| **蝗虫舵机 SIG** | **13** | 只接舵机黄线 |
| 天牛舵机 SIG | **15** | 仅播天牛 |
| 螽斯夜舵机 SIG | **16** | 仅播螽斯夜 |
| 蝼蛄灯 | **17** | 无舵机 |
| 播放键 | **40** | 按下 HIGH |
| 天牛灯 | **41** | |
| 螽斯晚上灯 | **42** | |

```
 J1 │  4  BCLK ──► MAX98357A                  │ J3
    │  5  WS                                  │
    │  6  DIN                                 │
    │  2  灯·蝗虫                             │
    │ 11  灯·蟋蟀                             │
    │ 13  舵机·蝗虫                           │
    │ 14  灯·螽斯白天                         │
    │ 15  舵机·天牛                           │
    │ 16  舵机·螽斯夜                         │
    │ 17  灯·蝼蛄                             │
    │  1  灯·蝉                               │
    │ 40  播放键                              │
    │ 41  灯·天牛                             │
    │ 42  灯·螽斯晚上                         │
```

**接线：** 蟋蟀 **11**，螽斯白天 **14**，蝗虫灯 **2**；蝗虫舵机 **13**。GPIO8 空闲。

改脚只改 `main/board_pins.h`。

### 9g 舵机（×3）

| # | 角色 | SIG | 摆幅 |
|---|------|-----|------|
| 1 | 天牛 | GPIO15 | ±4° |
| 2 | 螽斯晚上 | GPIO16 | ±30° |
| 3 | 蝗虫 | GPIO13 | ±4° |

黄→SIG，红→**~4.5–5V**，棕/黑→**GND（共地）**。空闲停 PWM 并拉低 SIG。

## 接线（音频）

```
ESP32-S3          MAX98357A         喇叭
---------         ---------         ----
GPIO 4  --------> BCLK
GPIO 5  --------> LRC / WS
GPIO 6  --------> DIN / DATA
~4.5–5V --------> VIN
GND     --------> GND
                  GAIN → VIN（约 15dB）或悬空（约 9dB）
                  SD   悬空或接 3V3
                  + / - ------------> 喇叭
```

## 行为

1. **上电空闲：** 灯全灭；三路舵机 SIG 停 PWM（idle 低）；音频关；SoftAP 已开  
2. **按 GPIO40 或网页「播放全部」：** 从蝉播到蝗虫；中途再按 / 「停止」结束  
3. **网页单曲：** `/api/play?track=0..6` 只播一轨  
4. **每轨时序：** 亮灯 → `LAMP_LEAD_MS`(1000ms) → 灭灯 → 再 I2S/舵机播音  
   - 七路灯统一：**高电平亮、低电平灭**  
5. 段间静音后结束关 I2S，回到空闲  

时序：`LAMP_LEAD_MS`、`TRACK_GAP_MS`、`DRAIN_FRAMES`。

## 刷机（ESP-IDF v6.1）

中文路径会让 cmake 出问题，用英文副本：`C:\esp\projects\cicada-esp32s3-player`

```powershell
chcp 65001
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
# robocopy '...\主仓库\开始\cicada-esp32s3-player' C:\esp\projects\cicada-esp32s3-player /E /XD build
Set-Location C:\esp\projects\cicada-esp32s3-player
idf.py set-target esp32s3
idf.py build
idf.py -p COM6 flash monitor
```

把 `COM6` 换成实际串口。

## 目录

```
cicada-esp32s3-player/
  CMakeLists.txt
  partitions.csv
  sdkconfig.defaults
  spiffs/                 # 七段 5s WAV（构建嵌入）
  web/index.html          # 科普+播控页（构建嵌入）
  components/dns_server/  # 强制门户 DNS 劫持（来自 ESP-IDF 示例）
  tools/mp3_to_mcu_wav.py
  main/
    main.c                # 按键、网页指令、列表、灯时序
    web_portal.c / .h     # SoftAP + HTTP
    board_pins.h          # 引脚总表
    servo_wiggle.c / .h   # 仅播对应轨时摆
    CMakeLists.txt        # EMBED_FILES
  README.md
```
