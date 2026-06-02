# 统一数据模型与接口草案

## 1. 核心实体

### 1.1 设备 Device

- id: 设备唯一标识
- name: 设备名称
- type: 设备类型
- location: 安装位置
- status: 在线、离线、故障
- gatewayId: 所属网关

### 1.2 网关 Gateway

- id: 网关唯一标识
- name: 网关名称
- mode: dtu-pass-through / protocol-collect
- protocolList: 支持协议列表
- status: 在线、离线、升级中
- lastSeenAt: 最近心跳时间

### 1.3 点位 Point

- id: 点位唯一标识
- deviceId: 所属设备
- code: 点位编码
- name: 点位名称
- unit: 单位
- value: 当前值
- quality: 数据质量

### 1.4 告警 Alarm

- id: 告警唯一标识
- level: 提示、一般、严重
- sourceId: 设备或点位来源
- message: 告警内容
- status: 新建、已确认、已恢复
- createdAt: 创建时间

## 2. 建议接口

### 2.1 网关状态

GET /api/gateways

返回网关列表、在线状态、接入方式和最近心跳时间。

### 2.2 设备列表

GET /api/devices

返回设备档案、所属网关、实时状态和最近上报值。

### 2.3 实时数据

GET /api/realtime?deviceId=xxx

返回指定设备当前点位值和质量状态。

### 2.4 历史曲线

GET /api/history?pointId=xxx&range=24h

返回时序曲线数据，用于折线图展示。

### 2.5 告警列表

GET /api/alarms

返回当前未恢复和最近恢复的告警记录。

## 3. DEMO 统一上报消息

```json
{
  "gatewayId": "GW-1001",
  "deviceId": "DEV-2001",
  "mode": "protocol-collect",
  "protocol": "modbus-485",
  "timestamp": "2026-04-23T10:12:30+08:00",
  "points": [
    { "code": "temp", "value": 27.4, "unit": "C" },
    { "code": "pressure", "value": 1.62, "unit": "MPa" }
  ]
}
```

## 4. 说明

该模型同时适用于 DTU 透传和前端网关采集：

- DTU 透传时，protocol 字段可以保留原始协议或报文类型
- 前端网关采集时，points 字段应为标准化后的点位集合
- Web、Android、小程序都可直接消费同一套接口
