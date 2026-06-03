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

    if (method == "GET" && (path == QStringLiteral("/") || path.startsWith(QStringLiteral("/config")))) {
        return okHtml(configPageHtml());
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
    return notFound();
}

QByteArray HttpServer::okJson(const QByteArray &json) const
{
    QByteArray response;
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: application/json; charset=utf-8\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
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
            </select>
          </div>
          <div>
            <label>设备 ID</label>
            <input id="deviceId" readonly>
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

    function byId(id) { return document.getElementById(id); }
    function setLog(text, bad) {
      const el = byId('saveLog');
      el.textContent = text;
      el.className = bad ? 'log status-bad' : 'log status-ok';
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
          renderDeviceList();
          fillForm();
          renderRealtime().catch(() => {});
        });
      });
    }

    async function renderRealtime() {
      const device = configState.devices[activeIndex] || {};
      const container = byId('livePoints');
      if (!device.id) {
        container.innerHTML = '';
        return;
      }
      const response = await fetch(`/api/realtime?deviceId=${encodeURIComponent(device.id)}`);
      if (!response.ok) {
        throw new Error('实时点位读取失败');
      }
      const points = await response.json();
      container.innerHTML = points.map((point) => `
        <div class="point-card">
          <strong>${point.name}</strong>
          <div>${point.value} ${point.unit || ''}</div>
          <div class="device-meta">${point.quality || 'good'}</div>
        </div>
      `).join('');
    }

    function fillForm() {
      const device = configState.devices[activeIndex] || {};
      const serial = device.serial || {};
      const tcp = device.tcp || {};
      const alarmThresholds = device.alarmThresholds || {};
      const threshold = (key, field, fallback) => ((alarmThresholds[key] || {})[field] ?? fallback);
      byId('httpPort').value = configState.httpPort ?? 8090;
      byId('dtuPort').value = configState.dtuListenPort ?? 15020;
      byId('templateName').value = device.templateName || '';
      byId('deviceId').value = device.id || '';
      byId('deviceName').value = device.name || '';
      byId('gatewayId').value = device.gatewayId || '';
      byId('protocol').value = device.protocol || 'modbus-rtu';
      byId('location').value = device.location || '';
      byId('intervalMs').value = device.intervalMs ?? 1000;
      byId('slaveId').value = device.slaveId ?? 1;
      byId('startAddress').value = device.startAddress ?? 0;
      byId('registerCount').value = device.registerCount ?? 2;
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

    loadConfig().catch((error) => setLog(`加载失败: ${error.message}`, true));
    setInterval(() => {
      renderRealtime().catch(() => {});
    }, 3000);
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
