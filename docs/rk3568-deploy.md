# RK3568 部署说明

适用目标系统：

- 银河麒麟
- Ubuntu

两者都按 `systemd + /opt` 目录结构部署。

## 目标

把 Qt DEMO 网关部署到 RK3568 板卡，连接 RS485 温湿度传感器，实现即插即用采集并把数据提供给平台展示。

## 建议运行方式

- 网关程序以守护进程方式运行。
- 通过配置文件切换模拟采集和真实采集。
- 使用 `systemd` 自启动。

## RS485 接线建议

- `A` 接传感器 `485+`
- `B` 接传感器 `485-`
- `GND` 共地
- 如果现场线长较长，建议增加终端电阻和防浪涌保护

## Linux 侧准备

先确认串口节点：

```bash
ls /dev/ttyS*
ls /dev/ttyUSB*
dmesg | grep tty
```

确认串口权限：

```bash
sudo usermod -a -G dialout $USER
```

## 典型配置修改

把 [config/gateway-demo.json](D:/project/gateway-dev/ai-gateway2026/config/gateway-demo.json) 中这段改成真实采集：

```json
"serial": {
  "simulate": false,
  "port": "auto",
  "baudRate": 9600,
  "dataBits": 8,
  "stopBits": 1,
  "parity": "N"
}
```

如果传感器协议是标准 Modbus RTU，通常还要确认：

- 从站地址 `slaveId`
- 温度寄存器地址
- 湿度寄存器地址
- 数据缩放系数，常见是 `寄存器值 / 10`

## 建议的部署目录

```text
/opt/rk3568-dtu-demo/
  bin/rk3568-dtu-terminal-demo
  config/rk3568-dtu-demo.json
  scripts/start.sh
```

## DTU Demo 推荐部署方式

通过 PC 配置工具生成的 [deploy/rk3568-dtu-package](D:/project/gateway-dev/ai-gateway2026/deploy/rk3568-dtu-package) 会自带：

- `config/rk3568-dtu-demo.json`
- `scripts/start.sh`
- `scripts/install-service.sh`
- `systemd/rk3568-dtu-demo.service`

建议流程：

1. 在 PC 上生成配置包
2. 把交叉编译得到的 `rk3568-dtu-terminal-demo` 放进部署包 `bin/`
3. 将整个部署包复制到 RK3568
4. 在板子上执行 `sudo ./scripts/install-service.sh`

## systemd 服务示例

```ini
[Unit]
Description=RK3568 DTU Demo Service
After=network.target

[Service]
WorkingDirectory=/opt/rk3568-dtu-demo
ExecStart=/opt/rk3568-dtu-demo/scripts/start.sh
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

## 银河麒麟 / Ubuntu 注意点

- 两个系统通常都使用 `systemd`，服务安装方式一致
- 串口权限通常归 `dialout` 组，必要时把运行用户加入该组
- 如果是桌面版银河麒麟，首次部署建议先手工执行 `scripts/start.sh` 验证串口和依赖
- 如果是 Ubuntu Server，推荐直接用 `systemd` 托管

## 即插即用建议

为了更接近即插即用，建议下一步增加这两项：

1. 串口自动探测：扫描 `/dev/ttyS*`、`/dev/ttyUSB*` 并尝试多个波特率。
2. 设备模板库：预置温湿度、压力、气体等寄存器映射模板，插入后自动识别。

## 当前 DEMO 边界

当前版本已经适合做：

- 平台联调
- 现场单点传感器演示
- DTU 与前端采集双路线展示

如果要进一步上板量产，建议继续补：

- 断线重连状态机
- 串口热插拔探测
- 本地 SQLite 缓存
- MQTT/HTTP 上云转发
- 完整的 PLC 驱动适配层
