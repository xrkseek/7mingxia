# 7鸣匣

声光机电联动的发声昆虫科普展品。源码公开：科普站和 ESP32-S3 固件都在这个仓库里。

- 科普站：https://7mingxia.pages.dev
- 展览说明：https://7mingxia.pages.dev/credits
- 仓库：https://github.com/xrkseek/7mingxia

白天先蝉，再蝗虫、天牛、蝈螽；夜里先蟋蟀，再蝼蛄、纺织娘。灯亮的是正在叫的那一只。

## 目录

| 路径 | 内容 |
|---|---|
| `index.html`、`styles.css`、`credits.html` | Cloudflare Pages 科普站，仓库根目录即站点 |
| `images/` | 七种虫的对照照，以及展柜远景、近景、侧景、俯视 |
| `audio/` | 网页彩蛋音 |
| `cicada-esp32s3-player/` | ESP32-S3 固件、接线、嵌入网页与叫声。说明见该目录 `README.md` |

原始候选音频在 `cricket-sounds/`、`insect-sounds-candidates/`，不入库。固件 `build/` 也不入库。

## 本地预览

```powershell
npx --yes serve .
```

浏览器打开提示的地址。点「匣上操控 → 播放全部」可听彩蛋。本站网页本身不播虫鸣；连上展柜热点 `7MingXia` 后，在设备页点标题「7鸣匣」，喇叭才放彩蛋。

## 部署

Cloudflare Pages 连接本仓库时，**Root directory 留空**，构建命令留空，输出目录 `/`。或：

```powershell
npx --yes wrangler pages deploy . --project-name=7mingxia
```

## 固件

硬件、引脚、刷机见 [`cicada-esp32s3-player/README.md`](cicada-esp32s3-player/README.md)。板子是 ESP32-S3，16MB Flash、8MB PSRAM。
