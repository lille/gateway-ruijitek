# 双方案 DEMO 框架说明

## 方案一: DTU 透传方案

链路：

`RS485/串口传感器 -> RK3568 终端采集程序 -> DTU/透传链路 -> 智慧管廊平台`

建议演示程序：

- `pc-dtu-config-tool`
  作用：PC 上位机下发目标平台 IP、端口、从站地址、串口参数。
- `rk3568-dtu-terminal-demo`
  作用：运行在 RK3568 上，读取 RS485 Modbus 传感器，然后把数据透传到平台。

说明：

- 如果现场真的是现成 DTU 硬件，一般需要一个 PC 上位机工具配置 `目标 IP/端口/串口参数`，这就是第一套方案里上位机的价值。
- 这个上位机并不是配置“传感器内部参数”本身，而是配置 `终端/DTU 的联网参数和采集参数`。

## 方案二: 网关 WebServer 配置方案

链路：

`RS485/串口传感器 -> RK3568 网关程序 -> 本地 WebServer 配置 -> 平台展示接口`

建议演示程序：

- `rk3568-edge-gateway-demo`
  作用：运行在 RK3568 或 PC 上，内置协议采集和平台 API，适合直接对接前端展示。

说明：

- 第二套方案不强调 PC 上位机。
- 重点是网关本地提供 WebServer 页面或接口，在浏览器中配置传感器、协议、串口和采集点。

## 当前代码框架映射

- [apps/rk3568_dtu_demo/rk3568_dtu_demo.pro](D:/project/gateway-dev/ai-gateway2026/apps/rk3568_dtu_demo/rk3568_dtu_demo.pro)
  第一套方案的 RK3568 终端采集 Demo
- [apps/pc_config_tool/pc_config_tool.pro](D:/project/gateway-dev/ai-gateway2026/apps/pc_config_tool/pc_config_tool.pro)
  第一套方案的 PC 配置工具 Demo
- [apps/rk3568_edge_gateway_demo/rk3568_edge_gateway_demo.pro](D:/project/gateway-dev/ai-gateway2026/apps/rk3568_edge_gateway_demo/rk3568_edge_gateway_demo.pro)
  第二套方案的网关 Demo
