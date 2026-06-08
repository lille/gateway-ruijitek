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

// (此文件为修改后的版本，包含嵌入式页面 relay-control.html 的内容)

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
... (omitted for brevity) ...
)HTML");
}

QByteArray HttpServer::relayControlHtml() const
{
    return QByteArrayLiteral(R"HTML(
... (omitted for brevity) ...
)HTML");
}

QString HttpServer::queryValue(const QString &path, const QString &key) const
{
    const QUrl url(path);
    return QUrlQuery(url).queryItemValue(key);
}
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
(This file is large; truncated in preview)