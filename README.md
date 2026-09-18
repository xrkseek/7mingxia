# 7鸣匣 · Cloudflare 科普站

与 ESP 内嵌页（`web/`）分离。本目录可直接挂到 **Cloudflare Pages**。

## 本地预览

```powershell
cd cicada-esp32s3-player\web-cloudflare
npx --yes serve .
```

浏览器打开提示的地址。点「匣上操控 → 播放全部」可听彩蛋。

## 部署到 Cloudflare Pages

1. 把 `web-cloudflare` 推到 GitHub，或用 Wrangler 直传：
   ```powershell
   npm i -g wrangler
   wrangler pages deploy . --project-name=7mingxia
   ```
2. Cloudflare Dashboard → Pages → Create → 连接仓库时，**Root directory** 设为 `web-cloudflare`（若整仓上传），构建命令留空，输出目录 `/`。
3. 自定义域名按需绑定。

无构建步骤：纯静态 HTML/CSS/JS + 图片/音频。

## 彩蛋

本站网页**不播音频**。连实体匣热点后，在 SoftAP 页点击标题「7鸣匣」，由喇叭播放「臭徐航」。
