# 工业数据采集双方案 DEMO 联调说明

这份文档用于现场向客户演示两种工业数据采集接入路线：

1. 第一套方案：`DTU 透传方案`
2. 第二套方案：`RK3568 网关 WebServer 配置方案`

目标设备场景：

- `RK3568`
- `ZH-Q006 多参数空气质量传感器`
- 操作系统：`银河麒麟 / Ubuntu`
- 采集协议：`RS485 Modbus RTU`

目标展示平台：

- [D:/project/gateway-dev/plat2026/web-demo/index.html](D:/project/gateway-dev/plat2026/web-demo/index.html)

---

## 一句话区别

第一套方案：

- 传感器接到 `RK3568/DTU 终端`
- 通过 `PC 上位机` 配置目标平台 IP 和采集参数
- 终端把数据直接透传到平台接收端

第二套方案：

- 传感器直接接到 `RK3568 网关`
- 通过 `网关自带 Web 配置页` 配置采集参数和阈值
- 网关直接输出平台接口和本地可视化数据

---

## 现场准备

硬件准备：

- `RK3568` 板卡 1 套
- `ZH-Q006` 空气质量传感器 1 台
- `RS485` 接线
- `24V` 供电
- PC 笔记本 1 台
- 路由器或交换机 1 台

软件准备：

- `Qt Creator + Qt 5.14`
- 已编译好的：
  - `pc-dtu-config-tool`
  - `rk3568-dtu-terminal-demo`
  - `rk3568-edge-gateway-demo`
- 浏览器

建议网络：

- PC、RK3568、平台演示机放在同一网段
- 示例：
  - 平台机：`192.168.10.100`
  - RK3568：`192.168.10.30`

---

## 传感器接线

以 `ZH-Q006` 为例：

- `VCC` 接 `24V`
- `GND` 接地
- `A` 接 `485A`
- `B` 接 `485B`

协议参数：

- 波特率：`9600`
- 数据位：`8`
- 停止位：`1`
- 校验位：`无`
- 从站地址：`1`
- 起始寄存器：`0`
- 寄存器数量：`9`

ZH-Q006 主要点位：

- `0x00` 甲醛
- `0x01` PM2.5
- `0x02` TVOC
- `0x03` CO2
- `0x04` 温度
- `0x05` 湿度
- `0x06` PM1.0
- `0x07` PM10
- `0x08` SF6

---

## 第一套方案演示步骤

### 方案定位

适合讲给客户的说法：

`如果现场已经有 DTU 或透传终端，不希望在边缘侧做复杂界面和管理，就可以采用这条路线。PC 上位机只负责下发目标平台地址和串口采集参数，终端负责稳定采集和上送。`

### 需要启动的程序

- PC 端：
  [apps/pc_config_tool/pc_config_tool.pro](D:/project/gateway-dev/ai-gateway2026/apps/pc_config_tool/pc_config_tool.pro)
- RK3568 终端：
  [apps/rk3568_dtu_demo/rk3568_dtu_demo.pro](D:/project/gateway-dev/ai-gateway2026/apps/rk3568_dtu_demo/rk3568_dtu_demo.pro)
- 平台接收端：
  [apps/rk3568_edge_gateway_demo/rk3568_edge_gateway_demo.pro](D:/project/gateway-dev/ai-gateway2026/apps/rk3568_edge_gateway_demo/rk3568_edge_gateway_demo.pro)
  说明：
  这里它承担“平台接收服务”的角色，监听 `15020` 端口接收 DTU 数据。

### 启动顺序

1. 先启动 `rk3568-edge-gateway-demo`
2. 确认监听正常：
   - Web 配置页：`http://平台机IP:8090/`
   - DTU 接收端口：`15020`
3. 打开 `pc-dtu-config-tool`
4. 选择模板：`ZH-Q006 空气质量传感器`
5. 填写：
   - 平台接收服务 IP：平台机 IP
   - 平台接收端口：`15020`
   - 串口：`auto` 或 `/dev/ttyS4`
   - 波特率：`9600`
   - 从站地址：`1`
   - 起始寄存器：`0`
   - 寄存器数量：`9`
6. 点击“一键生成 RK3568 配置包”
7. 将生成的部署包复制到 RK3568
8. 在 RK3568 上运行 `rk3568-dtu-terminal-demo`
9. 打开平台展示页，观察空气质量数据变化

### 客户面前的讲解重点

可以这样讲：

- `这条路线强调“终端即插即采，平台统一接收”。`
- `现场只需要配置一次平台 IP、串口和 Modbus 参数，终端就能持续把 PM2.5、CO2、TVOC、甲醛等数据透传到平台。`
- `后续如果换平台地址，重新在上位机里下发即可，不需要改边缘侧复杂逻辑。`

### 现场应看到的效果

- 平台页出现 `ZH-Q006` 设备
- 实时显示：
  - `PM2.5`
  - `CO2`
  - `TVOC`
  - `甲醛`
  - `温度`
  - `湿度`
- 历史趋势显示：
  - `PM2.5`
  - `CO2`

### 建议演示动作

可以做这些动作增强说服力：

- 把传感器靠近污染源或模拟通风变化
- 观察 `PM2.5 / CO2 / TVOC / 甲醛` 数值变化
- 演示告警阈值触发后的平台变化

---

## 第二套方案演示步骤

### 方案定位

适合讲给客户的说法：

`如果客户希望设备侧具备本地可配置能力、边缘自治能力和更清晰的运维入口，就推荐这条路线。RK3568 网关本身带 WebServer，可直接在浏览器配置采集设备、阈值和协议参数。`

### 需要启动的程序

- 网关程序：
  [apps/rk3568_edge_gateway_demo/rk3568_edge_gateway_demo.pro](D:/project/gateway-dev/ai-gateway2026/apps/rk3568_edge_gateway_demo/rk3568_edge_gateway_demo.pro)
- 平台展示页：
  [D:/project/gateway-dev/plat2026/web-demo/index.html](D:/project/gateway-dev/plat2026/web-demo/index.html)

### 启动顺序

1. 启动 `rk3568-edge-gateway-demo`
2. 浏览器打开：
   - 网关配置页：`http://网关IP:8090/`
3. 选择设备模板：`ZH-Q006`
4. 填写采集参数：
   - 协议：`modbus-rtu`
   - 串口：`auto` 或 `/dev/ttyS4`
   - 波特率：`9600`
   - 从站地址：`1`
   - 起始寄存器：`0`
   - 寄存器数量：`9`
5. 根据现场需要调整阈值：
   - PM2.5
   - CO2
   - TVOC
   - 甲醛
   - 温度
6. 点击“保存并重载”
7. 打开平台展示页
8. 观察实时点位、告警和历史曲线变化

### 客户面前的讲解重点

可以这样讲：

- `这条路线强调“网关自治和可运维”。`
- `现场工程人员不需要单独上位机，直接浏览器登录网关即可完成参数配置。`
- `设备模板、寄存器、串口和阈值都可以在线调整，保存后立即生效。`
- `更适合后续规模化部署和运维。`

### 现场应看到的效果

- 网关 Web 配置页能看到：
  - `ZH-Q006` 模板
  - 多参数实时点位
  - 告警阈值设置
- 平台页能看到：
  - `PM2.5 / CO2 / TVOC / 甲醛`
  - `PM2.5 / CO2` 历史曲线
  - 超限告警

### 建议演示动作

- 在 Web 配置页把 `PM2.5` 告警阈值临时调低
- 立即触发平台侧“空气告警”
- 再恢复正常阈值，展示配置生效和恢复过程

---

## 推荐讲解结构

给客户演示时建议按这个顺序说：

1. `同一套传感器、同一块 RK3568，我们提供两种接入路线。`
2. `第一套偏轻量和透传，适合已有 DTU 场景。`
3. `第二套偏自治和运维，适合客户希望本地可配置、可扩展的网关场景。`
4. `两条路线最终都能把 ZH-Q006 的 PM2.5、CO2、TVOC、甲醛、温湿度数据展示到智慧管廊平台。`

---

## 常见问答

### Q1：第一套方案里的平台 IP 指什么？

指的是：

- 平台接收服务所在主机 IP

不是：

- 传感器 IP
- RK3568 本机 IP

### Q2：`web-demo` 本身能直接接收 DTU 数据吗？

不能单独接收。

需要一个后端接收服务。
当前 DEMO 里由 `rk3568-edge-gateway-demo` 负责接收 DTU 数据并提供 `/api/*`。

### Q3：银河麒麟和 Ubuntu 都能演示吗？

可以。

当前部署方式按 `systemd + /opt` 目录组织，适合这两类系统。

### Q4：ZH-Q006 是真实寄存器解析吗？

是。

当前 DEMO 已按你提供的协议文档实现：

- `0x00 ~ 0x08`
- `PM2.5 / CO2 / TVOC / 甲醛 / 温湿度 / PM1.0 / PM10 / SF6`

---

## 现场建议

- 第一套方案重点讲“快速接入、平台统一接收”
- 第二套方案重点讲“本地可配、运维友好、阈值在线调整”
- 客户如果更关注“上云和平台接入”，先演示第一套
- 客户如果更关注“后续部署和维护”，重点演示第二套

---

## 相关文件

- [README.md](D:/project/gateway-dev/ai-gateway2026/README.md)
- [docs/rk3568-deploy.md](D:/project/gateway-dev/ai-gateway2026/docs/rk3568-deploy.md)
- [docs/solution-architecture.md](D:/project/gateway-dev/ai-gateway2026/docs/solution-architecture.md)
- [docs/demo-playbook.md](D:/project/gateway-dev/ai-gateway2026/docs/demo-playbook.md)
- [docs/demo-checklist.md](D:/project/gateway-dev/ai-gateway2026/docs/demo-checklist.md)
