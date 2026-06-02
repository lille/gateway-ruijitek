# Android App 骨架

## 定位

Android App 主要用于巡检、告警查看、设备概览和轻量操作，强调移动端快速访问。

## 页面建议

- 首页总览
- 设备列表
- 实时点位页
- 告警中心
- 巡检任务
- 扫码绑定设备

## 预览页

- [index.html](index.html): 当前 Android App 静态预览页

## 推荐技术

- 优先建议使用 UniApp 或 Flutter
- 如果项目偏向一套代码同时覆盖小程序和 Android，建议使用 UniApp
- 如果更重视原生手感和动画表现，可以选 Flutter

## 页面风格

- 深色工业风
- 卡片化指标展示
- 告警红黄绿分级
- 支持离线与弱网提示

## DEMO 可接接口

- GET /api/devices
- GET /api/realtime?deviceId=xxx
- GET /api/alarms

## 后续落地

如果你要继续把这个 DEMO 做成可运行 Android 工程，我可以下一步直接补出 UniApp 或 Flutter 的页面代码结构。
