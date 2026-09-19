# 7鸣匣 · 科普网页

本目录的 `index.html` 会嵌入固件，由 ESP32 SoftAP 在 `http://192.168.4.1` 提供。

- 热点：`7MingXia`（开放网络，无密码）
- 连上后靠 DNS/DHCP 强制门户自动弹出页面；也可手动打开上述地址
- 按钮调用 `/api/play`、`/api/play?track=0..6`、`/api/stop`

本地预览（无设备时）：用浏览器直接打开 `index.html`；播控接口会提示未连上匣子。
