#include "httpserver.h"

#include "datastore.h"
#include "gatewayservice.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QFile>

HttpServer::HttpServer(DataStore *store, GatewayService *service, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_service(service)
{
    connect(&m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

bool HttpServer::start(const QHostAddress &address, quint16 port)
{
    return m_server.listen(address, port);
}

void HttpServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, &HttpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void HttpServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    const QByteArray requestData = socket->readAll();
    socket->write(buildResponse(requestData));
    socket->disconnectFromHost();
}

QByteArray HttpServer::buildResponse(const QByteArray &requestData) const
{
    const QList<QByteArray> lines = requestData.split('\n');
    if (lines.isEmpty()) {
        return notFound();
    }

    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() < 2) {
        return notFound();
    }

    const QByteArray method = requestLine.first();
    const QString path = QString::fromUtf8(requestLine.at(1));
    const int bodyOffset = requestData.indexOf("\r\n\r\n");
    const QByteArray body = bodyOffset >= 0 ? requestData.mid(bodyOffset + 4) : QByteArray();

    // 处理 CORS 预检请求
    if (method == "OPTIONS") {
      // 返回 204 并允许常用头
      QByteArray response;
      response.append("HTTP/1.1 204 No Content\r\n");
      response.append("Access-Control-Allow-Origin: *\r\n");
      response.append("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
      response.append("Access-Control-Allow-Headers: Content-Type, Authorization\r\n");
      response.append("Access-Control-Max-Age: 3600\r\n");
      response.append("Connection: close\r\n\r\n");
      return response;
    }

    if (method == "GET" && (path == QStringLiteral("/") || path.startsWith(QStringLiteral("/config")))) {
        return okHtml(configPageHtml());
    }
    if (method == "GET" && (path == QStringLiteral("/web-demo/relay-control.html") || path == QStringLiteral("/relay-control.html"))) {
      return okHtml(relayControlHtml());
    }
    if (method == "GET" && path.startsWith(QStringLiteral("/api/config"))) {
        QJsonObject payload = m_service ? m_service->runtimeConfigJson() : QJsonObject();
        return okJson(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }
    if (method == "POST" && path.startsWith(QStringLiteral("/api/config"))) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return badRequest("{\"error\":\"invalid json body\"}");
        }

        QString errorMessage;
        if (!m_service || !m_service->saveRuntimeConfig(document.object(), &errorMessage)) {
            const QByteArray payload = QJsonDocument(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), errorMessage.isEmpty() ? QStringLiteral("save failed") : errorMessage}
            }).toJson(QJsonDocument::Compact);
            return badRequest(payload);
        }

        const QByteArray payload = QJsonDocument(QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("message"), QStringLiteral("config saved and reloaded")}
        }).toJson(QJsonDocument::Compact);
        return okJson(payload);
    }
    if (method == "GET" && path.startsWith(QStringLiteral("/api/dashboard"))) {
        return okJson(QJsonDocument(m_store->dashboardJson()).toJson(QJsonDocument::Compact));
    }
    if (method == "GET" && path.startsWith(QStringLiteral("/api/gateways"))) {
        return okJson(QJsonDocument(m_store->gatewaysJson()).toJson(QJsonDocument::Compact));
    }
    if (method == "GET" && path.startsWith(QStringLiteral("/api/realtime"))) {
        return okJson(QJsonDocument(m_store->realtimeJson(queryValue(path, QStringLiteral("deviceId")))).toJson(QJsonDocument::Compact));
    }
    if (method == "GET" && path.startsWith(QStringLiteral("/api/history"))) {
        return okJson(QJsonDocument(m_store->historyJson(queryValue(path, QStringLiteral("pointId")))).toJson(QJsonDocument::Compact));
    }

    // 继电器控制：POST /api/relay  { "id": "500", "action": "on" }
    if (method == "POST" && path.startsWith(QStringLiteral("/api/relay"))) {
      QJsonParseError parseError;
      const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
      if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return badRequest("{\"ok\":false,\"error\":\"invalid json body\"}");
      }

      const QJsonObject obj = document.object();
      const QString id = obj.value(QStringLiteral("id")).toString();
      const QString action = obj.value(QStringLiteral("action")).toString();
      if (id.isEmpty() || !(action == QStringLiteral("on") || action == QStringLiteral("off"))) {
        return badRequest("{\"ok\":false,\"error\":\"invalid payload\"}");
      }

      const QString cmd = QStringLiteral("%1 %2\n").arg(id).arg(action == QStringLiteral("on") ? 1 : 0);
      QFile f(QStringLiteral("/proc/proembed/gpio"));
      if (!f.open(QIODevice::WriteOnly)) {
        const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("cannot open device")}}).toJson(QJsonDocument::Compact);
        return badRequest(payload);
      }
      f.write(cmd.toUtf8());
      f.close();

      const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("id"), id}, {QStringLiteral("action"), action}}).toJson(QJsonDocument::Compact);
      return okJson(payload);
    }
    return notFound();
}

QByteArray HttpServer::okJson(const QByteArray &json) const
{
    QByteArray response;
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: application/json; charset=utf-8\r\n");
  response.append("Access-Control-Allow-Origin: *\r\n");
  response.append("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
  response.append("Access-Control-Allow-Headers: Content-Type, Authorization\r\n");
    response.append(QStringLiteral("Content-Length: %1\r\n").arg(json.size()).toUtf8());
    response.append("Connection: close\r\n\r\n");
    response.append(json);
    return response;
}

QByteArray HttpServer::okHtml(const QByteArray &html) const
{
    QByteArray response;
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: text/html; charset=utf-8\r\n");
  response.append("Access-Control-Allow-Origin: *\r\n");
  response.append("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
  response.append("Access-Control-Allow-Headers: Content-Type, Authorization\r\n");
    response.append(QStringLiteral("Content-Length: %1\r\n").arg(html.size()).toUtf8());
    response.append("Connection: close\r\n\r\n");
    response.append(html);
    return response;
}

QByteArray HttpServer::badRequest(const QByteArray &message) const
{
    QByteArray response;
    response.append("HTTP/1.1 400 Bad Request\r\n");
    response.append("Content-Type: application/json; charset=utf-8\r\n");
  response.append("Access-Control-Allow-Origin: *\r\n");
  response.append("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
  response.append("Access-Control-Allow-Headers: Content-Type, Authorization\r\n");
    response.append(QStringLiteral("Content-Length: %1\r\n").arg(message.size()).toUtf8());
    response.append("Connection: close\r\n\r\n");
    response.append(message);
    return response;
}

QByteArray HttpServer::notFound() const
{
    const QByteArray body = "{\"error\":\"not found\"}";
    QByteArray response;
    response.append("HTTP/1.1 404 Not Found\r\n");
    response.append("Content-Type: application/json; charset=utf-8\r\n");
  response.append("Access-Control-Allow-Origin: *\r\n");
  response.append("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
  response.append("Access-Control-Allow-Headers: Content-Type, Authorization\r\n");
    response.append(QStringLiteral("Content-Length: %1\r\n").arg(body.size()).toUtf8());
    response.append("Connection: close\r\n\r\n");
    response.append(body);
    return response;
}

QByteArray HttpServer::configPageHtml() const
{
    return QByteArrayLiteral(R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>RK3568 Edge Gateway Config</title>
  <style>
    :root {
      --bg: #091622;
      --card: rgba(13, 27, 43, 0.9);
      --line: rgba(108, 174, 221, 0.18);
      --text: #e8f1fb;
      --muted: #87a2bf;
      --brand: #35d0ba;
      --accent: #f7b04b;
      --danger: #ff7070;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
      color: var(--text);
      background:
        radial-gradient(circle at top left, rgba(53,208,186,0.15), transparent 24%),
        radial-gradient(circle at top right, rgba(247,176,75,0.12), transparent 20%),
        linear-gradient(180deg, #07111b, #0b1b2d 55%, #091622);
    }
    .shell { max-width: 1280px; margin: 0 auto; padding: 24px; }
    .hero, .card { border: 1px solid var(--line); background: var(--card); border-radius: 24px; box-shadow: 0 18px 50px rgba(0,0,0,0.25); }
    .hero { padding: 24px; margin-bottom: 18px; }
    h1 { margin: 0 0 12px; font-size: 34px; }
    .lead { color: var(--muted); line-height: 1.8; }
    .grid { display: grid; grid-template-columns: 1.1fr 0.9fr; gap: 18px; }
    .card { padding: 22px; }
    .card h2 { margin-top: 0; }
    label { display: block; margin-bottom: 6px; color: var(--muted); font-size: 13px; }
    input, select, textarea {
      width: 100%;
      border: 1px solid var(--line);
      background: rgba(255,255,255,0.03);
      color: var(--text);
      border-radius: 14px;
      padding: 12px 14px;
      margin-bottom: 14px;
      font: inherit;
    }
    textarea { min-height: 110px; resize: vertical; }
    .row2 { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    .btns { display: flex; gap: 12px; flex-wrap: wrap; }
    button {
      border: 0;
      border-radius: 14px;
      padding: 12px 18px;
      font-weight: 700;
      cursor: pointer;
      color: #06211d;
      background: linear-gradient(135deg, var(--brand), #9cf4de);
    }
    button.secondary {
      color: var(--text);
      background: rgba(255,255,255,0.05);
      border: 1px solid var(--line);
    }
    .hint, .device-meta, .log { color: var(--muted); }
    .device-list { display: grid; gap: 12px; }
    .device-item {
      border: 1px solid var(--line);
      border-radius: 18px;
      padding: 14px;
      background: rgba(255,255,255,0.02);
      cursor: pointer;
    }
    .device-item.active { border-color: rgba(53,208,186,0.72); box-shadow: inset 0 0 0 1px rgba(53,208,186,0.4); }
    .tag { display: inline-block; padding: 4px 10px; border-radius: 999px; background: rgba(247,176,75,0.12); color: #ffd18c; font-size: 12px; }
    .status-ok { color: #71ebb6; }
    .status-bad { color: #ff9d9d; }
    .log { white-space: pre-wrap; min-height: 72px; }
    .live-grid { display:grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap:10px; margin-top: 14px; }
    .point-card { border:1px solid var(--line); border-radius:16px; padding:12px; background:rgba(255,255,255,0.03); }
    .point-card strong { display:block; margin-bottom:6px; }
    @media (max-width: 980px) {
      .grid, .row2 { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="shell">
    <div class="hero">
      <h1>RK3568 工业网关 Web 配置台</h1>
        <div style="margin-top:8px"><a href="/web-demo/relay-control.html" target="_blank" style="color:var(--brand);text-decoration:none;font-weight:700">继电器控制面板</a></div>
        <div class="lead">这个页面用于第二套方案演示：传感器接入 RK3568 网关后，通过浏览器配置协议、串口、寄存器和目标连接参数，保存后立刻重载采集逻辑。</div>
    </div>
    <div class="grid">
      <div class="card">
        <h2>设备配置</h2>
        <div class="hint">左侧选择设备，右侧修改参数。当前 DEMO 重点覆盖 Modbus RTU、Modbus TCP、PLC、TCP Socket、串口采集。HTTP 端口和 DTU 端口显示当前值，如需修改建议保存后重启网关进程。</div>
        <div class="row2">
          <div>
            <label>HTTP 端口</label>
            <input id="httpPort" type="number" readonly>
          </div>
          <div>
            <label>DTU 透传端口</label>
            <input id="dtuPort" type="number" readonly>
          </div>
        </div>
        <div class="row2">
          <div>
            <label>设备模板</label>
            <select id="templateName">
              <option value="">默认</option>
              <option value="ZH-Q006">ZH-Q006 空气质量传感器</option>
              <option value="EX-TH-01">防爆环境温湿度传感器 (DEV-2004)</option>
              <option value="WATER-DETECT">水侵传感器 (DEV-2005)</option>
            </select>
          </div>
          <div>
            <label>设备 ID</label>
            <input id="deviceId">
          </div>
          <div>
            <label>设备名称</label>
            <input id="deviceName">
          </div>
        </div>
        <div class="row2">
          <div>
            <label>所属网关 ID</label>
            <input id="gatewayId">
          </div>
          <div>
            <label>采集协议</label>
            <select id="protocol">
              <option value="modbus-rtu">MODBUS RTU / RS485</option>
              <option value="modbus-tcp">MODBUS TCP</option>
              <option value="plc">PLC</option>
              <option value="tcp-socket">TCP Socket</option>
              <option value="serial">Serial ASCII</option>
              <option value="simulate">模拟数据</option>
            </select>
          </div>
        </div>
        <div class="row2">
          <div>
            <label>安装位置</label>
            <input id="location">
          </div>
          <div>
            <label>采样周期 ms</label>
            <input id="intervalMs" type="number">
          </div>
        </div>
        <div class="row2">
          <div>
            <label>Modbus 从站地址</label>
            <input id="slaveId" type="number">
          </div>
          <div>
            <label>起始寄存器</label>
            <input id="startAddress" type="number">
          </div>
        </div>
        <div class="row2">
          <div>
            <label>寄存器数量</label>
            <input id="registerCount" type="number">
          </div>
          <div>
            <label>串口</label>
            <input id="serialPort" placeholder="auto / /dev/ttyS4 / COM3">
          </div>
        </div>
        <div class="row2">
          <div>
            <label>波特率</label>
            <input id="baudRate" type="number">
          </div>
          <div>
            <label>串口模拟采集</label>
            <select id="serialSimulate">
              <option value="true">true</option>
              <option value="false">false</option>
            </select>
          </div>
        </div>
        <div class="row2">
          <div>
            <label>TCP/PLC 主机</label>
            <input id="tcpHost">
          </div>
          <div>
            <label>TCP/PLC 端口</label>
            <input id="tcpPort" type="number">
          </div>
        </div>
        <div class="row2">
          <div>
            <label>PM2.5 告警阈值</label>
            <input id="pm25Warning" type="number" placeholder="warning">
          </div>
          <div>
            <label>PM2.5 严重阈值</label>
            <input id="pm25Critical" type="number" placeholder="critical">
          </div>
        </div>
        <div class="row2">
          <div>
            <label>CO2 告警阈值</label>
            <input id="co2Warning" type="number" placeholder="warning">
          </div>
          <div>
            <label>CO2 严重阈值</label>
            <input id="co2Critical" type="number" placeholder="critical">
          </div>
        </div>
        <div class="row2">
          <div>
            <label>TVOC 告警阈值</label>
            <input id="tvocWarning" type="number" placeholder="warning">
          </div>
          <div>
            <label>TVOC 严重阈值</label>
            <input id="tvocCritical" type="number" placeholder="critical">
          </div>
        </div>
        <div class="row2">
          <div>
            <label>甲醛 告警阈值</label>
            <input id="hchoWarning" type="number" placeholder="warning">
          </div>
          <div>
            <label>甲醛 严重阈值</label>
            <input id="hchoCritical" type="number" placeholder="critical">
          </div>
        </div>
        <div class="row2">
          <div>
            <label>温度 告警阈值</label>
            <input id="tempWarning" type="number" step="0.1" placeholder="warning">
          </div>
          <div>
            <label>温度 严重阈值</label>
            <input id="tempCritical" type="number" step="0.1" placeholder="critical">
          </div>
        </div>
        <label>Socket/Serial 请求报文</label>
        <textarea id="requestText"></textarea>
        <div class="btns">
          <button id="saveBtn">保存并重载</button>
          <button class="secondary" id="reloadBtn">重新读取配置</button>
        </div>
        <div id="saveLog" class="log"></div>
      </div>
      <div class="card">
        <h2>设备清单</h2>
        <div id="deviceList" class="device-list"></div>
        <h2 style="margin-top:20px;">实时点位</h2>
        <div id="livePoints" class="live-grid"></div>
      </div>
    </div>
  </div>
  <script>
    let configState = null;
    let activeIndex = 0;
    let previewTemplate = '';

    function byId(id) { return document.getElementById(id); }
    function setLog(text, bad) {
      const el = byId('saveLog');
      el.textContent = text;
      el.className = bad ? 'log status-bad' : 'log status-ok';
    }

    function templateDefaults(templateName) {
      const map = {
        'EX-TH-01': { id: 'DEV-2004', name: '防爆环境温湿度传感器', startAddress: 1, registerCount: 7, slaveId: 3 },
        'WATER-DETECT': { id: 'DEV-2005', name: '水侵传感器', startAddress: 0, registerCount: 1, slaveId: 1 },
        'ZH-Q006': { id: 'DEV-2001', name: 'ZH-Q006 空气质量传感器', startAddress: 0, registerCount: 9, slaveId: 2 }
      };
      return map[templateName] || null;
    }

    function applyTemplateFields(templateName) {
      const info = templateDefaults(templateName);
      if (!info) {
        return null;
      }
      byId('templateName').value = templateName;
      byId('deviceId').value = info.id;
      byId('deviceName').value = info.name;
      byId('protocol').value = 'modbus-rtu';
      byId('startAddress').value = info.startAddress;
      byId('registerCount').value = info.registerCount;
      byId('slaveId').value = info.slaveId;
      return info;
    }

    function currentDeviceId() {
      const info = templateDefaults(previewTemplate || byId('templateName').value || '');
      if (info) {
        return info.id;
      }
      return (byId('deviceId').value || '').trim()
        || (configState.devices[activeIndex] || {}).id
        || '';
    }

    function loadDeviceContext(index) {
      const device = (configState.devices || [])[index] || {};
      const serial = device.serial || {};
      byId('gatewayId').value = device.gatewayId || '';
      byId('location').value = device.location || '';
      byId('intervalMs').value = device.intervalMs ?? 1000;
      byId('serialPort').value = serial.port || 'auto';
      byId('baudRate').value = serial.baudRate ?? 9600;
      byId('serialSimulate').value = String(serial.simulate ?? true);
    }

    function applyTemplateSelection(templateName) {
      previewTemplate = templateName || '';
      const info = applyTemplateFields(templateName);
      if (!info) {
        renderRealtime().catch(() => {});
        return;
      }

      if (Array.isArray(configState.devices)) {
        let idx = configState.devices.findIndex((d) => d.id === info.id);
        if (idx < 0) {
          idx = configState.devices.findIndex((d) => d.templateName === templateName);
        }
        if (idx >= 0) {
          activeIndex = idx;
          renderDeviceList();
          loadDeviceContext(idx);
          applyTemplateFields(templateName);
        }
      }

      renderRealtime().catch(() => {});
    }

    function renderDeviceList() {
      const list = byId('deviceList');
      const devices = Array.isArray(configState.devices) ? configState.devices : [];
      list.innerHTML = devices.map((device, index) => `
        <div class="device-item ${index === activeIndex ? 'active' : ''}" data-index="${index}">
          <div><strong>${device.name || '-'}</strong></div>
          <div class="device-meta">${device.id || '-'} | ${device.protocol || '-'} | ${device.location || '-'}</div>
          <div style="margin-top:8px;"><span class="tag">${device.gatewayId || 'GW'}</span></div>
        </div>
      `).join('');
      Array.from(document.querySelectorAll('.device-item')).forEach((item) => {
        item.addEventListener('click', () => {
          activeIndex = Number(item.dataset.index || 0);
          previewTemplate = '';
          renderDeviceList();
          fillForm();
          previewTemplate = byId('templateName').value || '';
          renderRealtime().catch(() => {});
        });
      });
    }

    async function renderRealtime() {
      const deviceId = currentDeviceId();
      const templateName = byId('templateName').value || '';
      const container = byId('livePoints');
      if (!deviceId) {
        container.innerHTML = '';
        return;
      }
      const response = await fetch(`/api/realtime?deviceId=${encodeURIComponent(deviceId)}`);
      if (!response.ok) {
        throw new Error('实时点位读取失败');
      }
      const points = await response.json();
      
      const isWater = deviceId === 'DEV-2005' || templateName === 'WATER-DETECT';
      const isExTh = deviceId === 'DEV-2004' || templateName === 'EX-TH-01';

      if (isWater) {
        const rawValue = points[0]?.value || 0;
        const value = Math.round(rawValue);
        const isWaterDetected = value > 256;
        if (isWaterDetected) {
          fetch('/api/relay', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id: '500', action: 'off' })
          }).catch(() => {});
        }
        container.innerHTML = `
          <div class="point-card" style="grid-column: span 2; text-align: center; padding: 20px;">
            <label style="font-size: 16px; color: var(--muted);">水侵检测</label>
            <div style="font-size: 28px; margin-top: 10px; font-weight: 700; color: ${isWaterDetected ? '#ff7070' : '#71ebb6'};">
              ${value}
            </div>
            <div class="device-meta" style="margin-top: 8px;">
              ${isWaterDetected ? '检测到水侵，已关闭报警灯(继电器500)' : '正常 (阈值 256)'}
            </div>
          </div>
        `;
      } else if (isExTh) {
        const statusMap = { 0: '正常', 1: '低报', 2: '高报', 3: '故障' };
        const precisionMap = { 0: '无小数', 1: '1位小数', 2: '2位小数', 3: '3位小数' };
        const gasMap = {
          0: '甲烷(CH4)', 1: '一氧化碳(CO)', 2: '硫化氢(H2S)', 3: '氯气(CL2)',
          4: '二氧化硫(SO2)', 5: '氧气(O2)', 6: '氨气(NH3)', 7: '二氧化氮(NO2)',
          8: '氯化氢(HCL)', 9: '磷化氢(PH3)', 10: '可燃(EX)', 11: '氢气(H2)',
          12: '有毒(TOX)', 13: '臭氧(O3)', 14: '氟化氢(HF)', 15: '氮气(N2)'
        };
        const formatValue = (point) => {
          if (point.id === 'alarm_status') {
            return statusMap[Math.round(point.value)] || point.value;
          }
          if (point.id === 'precision') {
            return precisionMap[Math.round(point.value)] || point.value;
          }
          if (point.id === 'gas_type') {
            return gasMap[Math.round(point.value)] || point.value;
          }
          if (point.id === 'concentration') {
            return `${point.value} ${point.unit || ''}`.trim();
          }
          return `${point.value}${point.unit ? ' ' + point.unit : ''}`;
        };
        container.innerHTML = points.map((point) => `
          <div class="point-card">
            <strong>${point.name}</strong>
            <div>${formatValue(point)}</div>
            <div class="device-meta">${point.quality || 'good'}</div>
          </div>
        `).join('');
      } else {
        container.innerHTML = points.map((point) => `
          <div class="point-card">
            <strong>${point.name}</strong>
            <div>${point.value} ${point.unit || ''}</div>
            <div class="device-meta">${point.quality || 'good'}</div>
          </div>
        `).join('');
      }
    }

    function fillForm() {
      const device = configState.devices[activeIndex] || {};
      const serial = device.serial || {};
      const tcp = device.tcp || {};
      const alarmThresholds = device.alarmThresholds || {};
      const threshold = (key, field, fallback) => ((alarmThresholds[key] || {})[field] ?? fallback);
      byId('httpPort').value = configState.httpPort ?? 8090;
      byId('dtuPort').value = configState.dtuListenPort ?? 15020;

      const templateName = device.templateName || '';
      byId('templateName').value = templateName;

      const defaults = templateDefaults(templateName);
      const deviceId = defaults ? defaults.id : (device.id || '');
      const deviceName = defaults ? defaults.name : (device.name || (templateName === 'ZH-Q006' ? 'ZH-Q006 空气质量传感器' : ''));
      
      byId('deviceId').value = deviceId;
      byId('deviceName').value = deviceName;
      byId('gatewayId').value = device.gatewayId || '';
      byId('protocol').value = device.protocol || 'modbus-rtu';
      byId('location').value = device.location || '';
      byId('intervalMs').value = device.intervalMs ?? 1000;
      byId('slaveId').value = defaults ? defaults.slaveId : (device.slaveId ?? 1);
      if (templateName === 'EX-TH-01') {
        byId('startAddress').value = device.startAddress ?? 1;
        byId('registerCount').value = device.registerCount ?? 7;
      } else if (templateName === 'WATER-DETECT') {
        byId('startAddress').value = device.startAddress ?? 0;
        byId('registerCount').value = device.registerCount ?? 1;
      } else if (templateName === 'ZH-Q006') {
        byId('startAddress').value = device.startAddress ?? 0;
        byId('registerCount').value = device.registerCount ?? 9;
      } else {
        byId('startAddress').value = device.startAddress ?? 0;
        byId('registerCount').value = device.registerCount ?? 2;
      }
      byId('serialPort').value = serial.port || 'auto';
      byId('baudRate').value = serial.baudRate ?? 9600;
      byId('serialSimulate').value = String(serial.simulate ?? true);
      byId('tcpHost').value = tcp.host || '';
      byId('tcpPort').value = tcp.port ?? 502;
      byId('requestText').value = tcp.request || serial.request || 'READ\\n';
      byId('pm25Warning').value = threshold('pm25', 'warning', 75);
      byId('pm25Critical').value = threshold('pm25', 'critical', 150);
      byId('co2Warning').value = threshold('co2', 'warning', 1000);
      byId('co2Critical').value = threshold('co2', 'critical', 2000);
      byId('tvocWarning').value = threshold('tvoc', 'warning', 600);
      byId('tvocCritical').value = threshold('tvoc', 'critical', 1000);
      byId('hchoWarning').value = threshold('formaldehyde', 'warning', 100);
      byId('hchoCritical').value = threshold('formaldehyde', 'critical', 200);
      byId('tempWarning').value = threshold('temperature', 'warning', 32);
      byId('tempCritical').value = threshold('temperature', 'critical', 38);
    }

    function collectForm() {
      const next = JSON.parse(JSON.stringify(configState));
      next.httpPort = Number(byId('httpPort').value || 8090);
      next.dtuListenPort = Number(byId('dtuPort').value || 15020);
      const device = next.devices[activeIndex];
      device.templateName = byId('templateName').value;
      device.id = byId('deviceId').value.trim();
      device.name = byId('deviceName').value.trim();
      device.gatewayId = byId('gatewayId').value.trim();
      device.protocol = byId('protocol').value;
      device.location = byId('location').value.trim();
      device.intervalMs = Number(byId('intervalMs').value || 1000);
      device.slaveId = Number(byId('slaveId').value || 1);
      device.startAddress = Number(byId('startAddress').value || 0);
      device.registerCount = Number(byId('registerCount').value || 2);
      device.serial = device.serial || {};
      device.serial.port = byId('serialPort').value.trim() || 'auto';
      device.serial.baudRate = Number(byId('baudRate').value || 9600);
      device.serial.dataBits = 8;
      device.serial.stopBits = 1;
      device.serial.parity = 'N';
      device.serial.simulate = byId('serialSimulate').value === 'true';
      device.serial.request = byId('requestText').value;
      device.tcp = device.tcp || {};
      device.tcp.host = byId('tcpHost').value.trim();
      device.tcp.port = Number(byId('tcpPort').value || 502);
      device.tcp.request = byId('requestText').value;
      device.alarmThresholds = {
        pm25: {
          warning: Number(byId('pm25Warning').value || 75),
          critical: Number(byId('pm25Critical').value || 150)
        },
        co2: {
          warning: Number(byId('co2Warning').value || 1000),
          critical: Number(byId('co2Critical').value || 2000)
        },
        tvoc: {
          warning: Number(byId('tvocWarning').value || 600),
          critical: Number(byId('tvocCritical').value || 1000)
        },
        formaldehyde: {
          warning: Number(byId('hchoWarning').value || 100),
          critical: Number(byId('hchoCritical').value || 200)
        },
        temperature: {
          warning: Number(byId('tempWarning').value || 32),
          critical: Number(byId('tempCritical').value || 38)
        }
      };
      if (device.templateName === 'ZH-Q006') {
        device.protocol = 'modbus-rtu';
        device.startAddress = 0;
        device.registerCount = 9;
        device.slaveId = Number(byId('slaveId').value || 2);
        device.serial.baudRate = 9600;
        device.alarmThresholds = device.alarmThresholds || {};
      } else if (device.templateName === 'EX-TH-01') {
        device.id = 'DEV-2004';
        device.name = '防爆环境温湿度传感器';
        device.protocol = 'modbus-rtu';
        device.startAddress = 1;
        device.registerCount = 7;
        device.slaveId = Number(byId('slaveId').value || 3);
        device.serial.baudRate = 9600;
        device.alarmThresholds = device.alarmThresholds || {};
      } else if (device.templateName === 'WATER-DETECT') {
        device.id = 'DEV-2005';
        device.name = '水侵传感器';
        device.protocol = 'modbus-rtu';
        device.startAddress = 0;
        device.registerCount = 1;
        device.slaveId = Number(byId('slaveId').value || 1);
        device.serial.baudRate = 9600;
        device.alarmThresholds = device.alarmThresholds || {};
      }
      return next;
    }

    async function loadConfig() {
      setLog('正在读取网关配置...', false);
      const response = await fetch('/api/config');
      if (!response.ok) {
        throw new Error('加载配置失败');
      }
      configState = await response.json();
      if (!Array.isArray(configState.devices) || !configState.devices.length) {
        configState.devices = [];
      }
      activeIndex = Math.min(activeIndex, Math.max(configState.devices.length - 1, 0));
      renderDeviceList();
      fillForm();
      previewTemplate = byId('templateName').value || '';
      await renderRealtime();
      setLog('配置已加载，可以直接修改并保存。', false);
    }

    async function saveConfig() {
      try {
        const next = collectForm();
        setLog('正在保存并重载采集任务...', false);
        const response = await fetch('/api/config', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(next)
        });
        const data = await response.json();
        if (!response.ok || data.ok === false) {
          throw new Error(data.error || '保存失败');
        }
        configState = next;
        renderDeviceList();
        fillForm();
        previewTemplate = byId('templateName').value || '';
        await renderRealtime();
        setLog('保存成功，网关采集器已按新配置重载。', false);
      } catch (error) {
        setLog(`保存失败: ${error.message}`, true);
      }
    }

    byId('saveBtn').addEventListener('click', saveConfig);
    byId('reloadBtn').addEventListener('click', () => {
      loadConfig().catch((error) => setLog(`加载失败: ${error.message}`, true));
    });

    byId('templateName').addEventListener('change', (e) => {
      applyTemplateSelection(e.target.value);
    });

    loadConfig().catch((error) => setLog(`加载失败: ${error.message}`, true));
    setInterval(() => {
      renderRealtime().catch(() => {});
    }, 3000);
  </script>
</body>
</html>
)HTML");
}

QByteArray HttpServer::relayControlHtml() const
{
    return QByteArrayLiteral(R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>继电器控制</title>
  <style>
    body{font-family:Segoe UI,system-ui,Microsoft YaHei,sans-serif;margin:20px;background:#f6f9fc;color:#0b2635}
    h1{margin-bottom:12px}
    .list{max-width:920px}
    .row{display:flex;gap:8px;align-items:center;padding:8px;border:1px solid #e3edf3;border-radius:8px;margin-bottom:8px;background:#fff}
    .id{width:220px;font-weight:700}
    select{padding:6px;border-radius:6px}
    button{padding:8px 12px;border-radius:8px;border:0;background:#0b84ff;color:#fff;cursor:pointer}
    .small{padding:6px 8px;background:#e9f2ff;border-radius:6px;color:#0b2635}
    #msg{margin-top:12px;color:#0b6b2e}
  </style>
</head>
<body>
  <h1>继电器控制面板</h1>
  <p>从设备列表加载可用 ID，或在下方手动输入自定义指示灯/继电器 ID。</p>

  <div style="margin-bottom:12px">
    <label style="display:block;margin-bottom:6px;color:#6b8290">网关地址（示例 http://192.168.10.200:3010），留空使用本地 demo:</label>
    <input id="gatewayUrl" placeholder="http://<gateway-ip>:<port>" style="padding:8px;border-radius:6px;border:1px solid #ccdbe6;width:420px;margin-right:8px" />
    <button id="setGateway">连接</button>
    <span id="currentTarget" style="margin-left:12px;color:#4b6572"></span>
    <div style="margin-top:8px;display:flex;gap:8px;align-items:center">
      <label style="color:#6b8290">访问模式：</label>
      <select id="accessMode" style="padding:6px;border-radius:6px">
        <option value="demo">本地 demo</option>
        <option value="direct">直接访问网关</option>
        <option value="proxy">走上位机代理（proxy）</option>
      </select>
      <input id="proxyToken" placeholder="Proxy token (用于 proxy 模式)" style="padding:6px;border-radius:6px;border:1px solid #ccdbe6;width:260px;display:none;margin-left:8px" />
      <input id="proxySshKey" placeholder="SSH 私钥路径 (可选)" style="padding:6px;border-radius:6px;border:1px solid #ccdbe6;width:260px;display:none;margin-left:8px" />
    </div>
  </div>

  <div style="margin-bottom:12px">
    <input id="customId" placeholder="输入自定义 ID，例如 LED-1" style="padding:8px;border-radius:6px;border:1px solid #ccdbe6;width:260px;margin-right:8px" />
    <select id="customAction" style="padding:6px;border-radius:6px">
      <option value="on">打开</option>
      <option value="off">关闭</option>
    </select>
    <button id="customConfirm">确认</button>
  </div>

  <div class="list" id="list"></div>

  <div id="msg"></div>

  <script>
    let gatewayBase = '';
    let accessMode = 'demo'; // 'demo' | 'direct' | 'proxy'

    function buildUrl(path){
      if (accessMode === 'proxy') {
        return '/api/relay/proxy';
      }
      if (accessMode === 'direct' && gatewayBase && gatewayBase.trim()){
        return gatewayBase.replace(/\/$/, '') + path;
      }
      return path; // relative to current host (demo server)
    }

    function buildHeaders(){
      const headers = { 'Content-Type': 'application/json' };
      if (accessMode === 'proxy'){
        const t = document.getElementById('proxyToken').value.trim();
        if (t) headers['Authorization'] = 'Bearer ' + t;
      }
      return headers;
    }

    async function postRelay(id, action){
      const target = buildUrl('/api/relay');
      const res = await fetch(target, {
        method: 'POST',
        headers: buildHeaders(),
        body: JSON.stringify({ id, action })
      });
      return res.json();
    }

    function mkRow(id){
      const row = document.createElement('div'); row.className='row';
      const idSpan = document.createElement('div'); idSpan.className='id'; idSpan.textContent = id;
      const sel = document.createElement('select');
      const onOpt = document.createElement('option'); onOpt.value='on'; onOpt.textContent='打开';
      const offOpt = document.createElement('option'); offOpt.value='off'; offOpt.textContent='关闭';
      sel.appendChild(onOpt); sel.appendChild(offOpt);
      const btn = document.createElement('button'); btn.textContent='确认';
      const stateSpan = document.createElement('div'); stateSpan.className='small'; stateSpan.textContent='状态：未知';
      btn.addEventListener('click', async ()=>{
        btn.disabled = true; btn.textContent='处理中...';
        try{
          const r = await postRelay(id, sel.value);
          stateSpan.textContent = '状态：' + (r.state ? '打开' : '关闭');
          showMsg(`操作成功：${id} -> ${r.action}`);
        }catch(e){
          showMsg('请求失败：' + (e && e.message));
        }finally{ btn.disabled=false; btn.textContent='确认'; }
      });

      row.appendChild(idSpan); row.appendChild(sel); row.appendChild(btn); row.appendChild(stateSpan);
      return row;
    }

    function showMsg(text){ const m = document.getElementById('msg'); m.textContent = text; setTimeout(()=>{ m.textContent = '' }, 4000); }

    async function load(){
      const listEl = document.getElementById('list'); listEl.innerHTML='';
      try{
        // 板载继电器（来自设备手册截图）
        const onboard = [
          { id: '500', label: '12V_OUT4 (继电器4, 引脚1)' },
          { id: '502', label: '12V_OUT5 (继电器5, 引脚3)' },
          { id: '503', label: '12V_OUT6 (继电器6, 引脚5)' },
          { id: '501', label: '12V_OUT7 (继电器7, 引脚7)' }
        ];

        // 12V控制输出（2路）
        const power12v = [
          { id: '507', label: '12V_OUT1 (12V控制输出1)' },
          { id: '508', label: '12V_OUT2 (12V控制输出2)' }
        ];

        const h = document.createElement('h3'); h.textContent = '板载继电器'; h.style.margin='8px 0';
        listEl.appendChild(h);
        onboard.forEach(r => {
          const row = mkRow(r.id);
          row.querySelector('.id').textContent = `${r.label} — ID: ${r.id}`;
          listEl.appendChild(row);
        });

        const sep0 = document.createElement('hr'); sep0.style.margin='12px 0'; listEl.appendChild(sep0);
        // 12V控制输出
        const hPower = document.createElement('h3'); hPower.textContent = '12V控制输出'; hPower.style.margin='8px 0';
        listEl.appendChild(hPower);
        power12v.forEach(r => {
          const row = mkRow(r.id);
          row.querySelector('.id').textContent = `${r.label} — ID: ${r.id}`;
          listEl.appendChild(row);
        });

        const sep = document.createElement('hr'); sep.style.margin='12px 0'; listEl.appendChild(sep);
        // 干节点继电器（按产品手册映射）
        const dryOnboard = [
          { id: '496', label: '继电器0 (NO_OUT0 / ID: 496)' },
          { id: '498', label: '继电器1 (NO_OUT1 / ID: 498)' },
          { id: '497', label: '继电器2 (NO_OUT2 / ID: 497)' },
          { id: '499', label: '继电器3 (NO_OUT3 / ID: 499)' }
        ];
        const hDry = document.createElement('h3'); hDry.textContent = '干节点继电器'; hDry.style.margin='8px 0';
        listEl.appendChild(hDry);
        dryOnboard.forEach(r => {
          const row = mkRow(r.id);
          row.querySelector('.id').textContent = `${r.label} — ID: ${r.id}`;
          listEl.appendChild(row);
        });
        const sep2 = document.createElement('hr'); sep2.style.margin='12px 0'; listEl.appendChild(sep2);

        // DI 输入（16 路）
        const diList = [
          { id: '480', label: 'IN0' },{ id: '481', label: 'IN1' },{ id: '482', label: 'IN2' },{ id: '483', label: 'IN3' },
          { id: '484', label: 'IN4' },{ id: '485', label: 'IN5' },{ id: '486', label: 'IN6' },{ id: '487', label: 'IN7' },
          { id: '488', label: 'IN8' },{ id: '489', label: 'IN9' },{ id: '490', label: 'IN10' },{ id: '491', label: 'IN11' },
          { id: '492', label: 'IN12' },{ id: '493', label: 'IN13' },{ id: '494', label: 'IN14' },{ id: '495', label: 'IN15' }
        ];
        const hDi = document.createElement('h3'); hDi.textContent = '数字输入 DI'; hDi.style.margin='8px 0';
        listEl.appendChild(hDi);
        diList.forEach(d => {
          const row = document.createElement('div'); row.className='row';
          const idSpan = document.createElement('div'); idSpan.className='id'; idSpan.textContent = `${d.label} — ID: ${d.id}`;
          const btn = document.createElement('button'); btn.textContent = '读取';
          const stateSpan = document.createElement('div'); stateSpan.className='small'; stateSpan.textContent = '状态：未知';
          btn.addEventListener('click', async ()=>{
            btn.disabled = true; btn.textContent = '读取中...';
            try{
              const res = await fetch('/api/debug-gateway-latest');
              const js = await res.json();
              const s = JSON.stringify(js || {});
              if (s.indexOf(d.id) !== -1) stateSpan.textContent = '状态：触发';
              else stateSpan.textContent = '状态：未触发';
            }catch(e){ stateSpan.textContent = '状态：读取失败'; }
            finally{ btn.disabled = false; btn.textContent = '读取'; }
          });
          row.appendChild(idSpan); row.appendChild(btn); row.appendChild(stateSpan);
          listEl.appendChild(row);
        });

        // 继续加载已知设备作为示例；尝试从网关（如果设置了 gatewayBase），否则使用本地 demo
        const devicesUrl = buildUrl('/api/devices');
        const res = await fetch(devicesUrl);
        const devices = await res.json();
        const ids = devices.map(d=>d.id).slice(0,20);
        // 同时加入几个示例 LED id
        ids.push('LED-1','LED-2','LED-3');
        const h2 = document.createElement('h3'); h2.textContent='设备示例 ID'; h2.style.margin='8px 0';
        listEl.appendChild(h2);
        ids.forEach(id => listEl.appendChild(mkRow(id)));
      }catch(e){ showMsg('加载设备失败：' + (e && e.message)); }
    }

    document.getElementById('customConfirm').addEventListener('click', async ()=>{
      const id = document.getElementById('customId').value.trim();
      const action = document.getElementById('customAction').value;
      if(!id){ showMsg('请输入 ID'); return; }
      try{
        const r = await postRelay(id, action);
        showMsg(`操作成功：${id} -> ${r.action}`);
      }catch(e){ showMsg('请求失败'); }
    });

    document.getElementById('setGateway').addEventListener('click', ()=>{
      const v = document.getElementById('gatewayUrl').value.trim();
      gatewayBase = v;
      document.getElementById('currentTarget').textContent = gatewayBase ? ('目标：' + gatewayBase) : '目标：本地 demo';
      // 如果选择 proxy 模式，保存运行时配置到上位机服务
      if (accessMode === 'proxy'){
        const config = {
          remoteRelayUrl: gatewayBase && gatewayBase.startsWith('http') ? gatewayBase : '',
          remoteRelaySsh: gatewayBase && !gatewayBase.startsWith('http') ? gatewayBase : '',
          remoteRelaySshKey: document.getElementById('proxySshKey').value.trim(),
          relayProxyToken: document.getElementById('proxyToken').value.trim()
        };
        fetch('/api/relay/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(config) })
          .then(()=> load())
          .catch(()=> load());
        return;
      }
      load();
    });

    document.getElementById('accessMode').addEventListener('change', (e)=>{
      accessMode = e.target.value;
      document.getElementById('proxyToken').style.display = accessMode === 'proxy' ? 'inline-block' : 'none';
      document.getElementById('proxySshKey').style.display = accessMode === 'proxy' ? 'inline-block' : 'none';
      if (accessMode === 'demo') document.getElementById('currentTarget').textContent = '目标：本地 demo';
      else if (accessMode === 'direct' && gatewayBase) document.getElementById('currentTarget').textContent = '目标：' + gatewayBase;
      else if (accessMode === 'proxy') document.getElementById('currentTarget').textContent = '目标：proxy (/api/relay/proxy)';
      load();
    });

    // 初始化：尝试读取运行时配置
    fetch('/api/relay/config').then(r=>r.json()).then(cfg=>{
      if (cfg){
        if (cfg.remoteRelayUrl) { document.getElementById('gatewayUrl').value = cfg.remoteRelayUrl; gatewayBase = cfg.remoteRelayUrl; accessMode = 'proxy'; document.getElementById('accessMode').value = 'proxy'; }
        else if (cfg.remoteRelaySsh) { document.getElementById('gatewayUrl').value = cfg.remoteRelaySsh; gatewayBase = cfg.remoteRelaySsh; accessMode = 'proxy'; document.getElementById('accessMode').value = 'proxy'; }
        if (cfg.relayProxyToken) document.getElementById('proxyToken').value = cfg.relayProxyToken;
        if (cfg.remoteRelaySshKey) document.getElementById('proxySshKey').value = cfg.remoteRelaySshKey;
      }
    }).catch(()=>{}).finally(()=>{ load(); });
  </script>
</body>
</html>
)HTML");
}

QString HttpServer::queryValue(const QString &path, const QString &key) const
{
    const QUrl url(path);
    return QUrlQuery(url).queryItemValue(key);
}
