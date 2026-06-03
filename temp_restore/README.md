# 数据采集云平台 DEMO

> **版本：v0.0 初始发布**  
> 适用于本地及其他机器快速演示

## 快速部署与演示

1. 安装 Node.js 18 或更高版本。
2. 克隆或拷贝本项目到目标机器。
3. 在项目根目录执行：
	```bash
	npm install
	npm start
	```
4. 启动后访问：
	- Web 端：http://localhost:3000/web-demo/index.html
	- Android 预览页：http://localhost:3000/android-app/index.html
	- 小程序预览页：http://localhost:3000/mini-program/index.html

如遇端口占用或访问异常，请检查 3000 端口是否被占用，或将 `server.js` 中端口号修改为其他未被占用端口。

---

# 数据采集云平台 DEMO

这是一个面向工业数据采集场景的 DEMO 方案，覆盖两条设备接入路线：

1. DTU 硬件网关透传方案
2. 前端网关协议采集方案，支持 MODBUS 485、TCP Socket、PLC、串口通信

同时提供三类展示端：

- B/S Web 管理端
- Android App
- 微信小程序

## DEMO 目标

- 统一设备、采集点、告警、实时数据模型
- 展示云端统一接入、规则处理、存储和可视化流程
- 给硬件网关与软件网关两种接入方式留出清晰边界
- 兼顾浏览器、手机和小程序的演示一致性

## 目录说明

- [docs/architecture.md](docs/architecture.md): 双技术方案架构说明
- [docs/api-model.md](docs/api-model.md): 统一数据模型与接口草案
- [web-demo/index.html](web-demo/index.html): Web 原型页
- [android-app/index.html](android-app/index.html): Android App 预览页
- [mini-program/index.html](mini-program/index.html): 小程序预览页
- [android-app/README.md](android-app/README.md): Android App 骨架说明
- [mini-program/README.md](mini-program/README.md): 小程序骨架说明

## 推荐实现路线

如果你希望这个 DEMO 继续落成可运行项目，建议采用以下组合：

- Web 端：Vue 3 + Vite + ECharts
- Android 端：UniApp 或 Flutter
- 小程序端：UniApp 或 Taro
- 云端接口：Node.js / Java / .NET 均可

其中 UniApp 适合同时覆盖 Android App 和小程序，减少重复开发。

## 运行方式

如果你想先看一个可运行的版本，直接启动本地 mock 服务即可：

1. 安装 Node.js 18 或更高版本。
2. 在项目根目录执行 `npm start`。
3. 打开 `http://localhost:3000/web-demo/index.html` 查看 Web 端。
4. 打开 `http://localhost:3000/android-app/index.html` 查看 Android 预览页。
5. 打开 `http://localhost:3000/mini-program/index.html` 查看小程序预览页。

## Mock 接口

- GET /api/summary
- GET /api/gateways
- GET /api/devices
- GET /api/realtime?deviceId=DEV-2001
- GET /api/history?pointId=temp
- GET /api/alarms
- GET /api/dashboard
