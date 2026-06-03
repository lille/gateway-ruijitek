#include "dtupassthroughserver.h"

#include "datastore.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTcpSocket>

DtuPassthroughServer::DtuPassthroughServer(DataStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    connect(&m_server, &QTcpServer::newConnection, this, &DtuPassthroughServer::onNewConnection);
}

bool DtuPassthroughServer::start(const QHostAddress &address, quint16 port)
{
    return m_server.listen(address, port);
}

void DtuPassthroughServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, &DtuPassthroughServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &DtuPassthroughServer::onDisconnected);
    }
}

void DtuPassthroughServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    handlePayload(socket, socket->readAll());
}

void DtuPassthroughServer::onDisconnected()
{
    if (QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender())) {
        socket->deleteLater();
    }
}

void DtuPassthroughServer::handlePayload(QTcpSocket *socket, const QByteArray &payload)
{
    Q_UNUSED(socket);
    if (!m_store) {
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }

    const QJsonObject object = document.object();
    const QString gatewayId = object.value(QStringLiteral("gatewayId")).toString(QStringLiteral("GW-DTU-01"));
    const QString deviceId = object.value(QStringLiteral("deviceId")).toString(QStringLiteral("DEV-DTU-01"));
    const QDateTime now = QDateTime::currentDateTime();

    GatewaySnapshot gateway;
    gateway.id = gatewayId;
    gateway.name = object.value(QStringLiteral("gatewayName")).toString(QStringLiteral("DTU硬件网关"));
    gateway.mode = QStringLiteral("DTU透传");
    gateway.status = QStringLiteral("online");
    gateway.protocolList = QStringList{QStringLiteral("DTU"), QStringLiteral("TCP Socket")};
    gateway.lastSeenAt = now;
    m_store->upsertGateway(gateway);

    DeviceSnapshot device;
    device.id = deviceId;
    device.name = object.value(QStringLiteral("deviceName")).toString(QStringLiteral("DTU温湿度传感器"));
    device.gatewayId = gatewayId;
    device.protocol = QStringLiteral("DTU");
    device.location = object.value(QStringLiteral("location")).toString(QStringLiteral("现场采集点"));
    device.lastSeenAt = now;
    m_store->upsertDevice(device);

    QList<DataPoint> points;
    const QJsonObject measurements = object.value(QStringLiteral("measurements")).toObject();
    const QMap<QString, QPair<QString, QString>> pointMeta = {
        {QStringLiteral("temperature"), qMakePair(QStringLiteral("温度"), QStringLiteral("℃"))},
        {QStringLiteral("humidity"), qMakePair(QStringLiteral("湿度"), QStringLiteral("%RH"))},
        {QStringLiteral("pm25"), qMakePair(QStringLiteral("PM2.5"), QStringLiteral("ug/m3"))},
        {QStringLiteral("pm10"), qMakePair(QStringLiteral("PM10"), QStringLiteral("ug/m3"))},
        {QStringLiteral("pm1_0"), qMakePair(QStringLiteral("PM1.0"), QStringLiteral("ug/m3"))},
        {QStringLiteral("tvoc"), qMakePair(QStringLiteral("TVOC"), QStringLiteral("ug/m3"))},
        {QStringLiteral("co2"), qMakePair(QStringLiteral("CO2"), QStringLiteral("ppm"))},
        {QStringLiteral("formaldehyde"), qMakePair(QStringLiteral("甲醛"), QStringLiteral("ug/m3"))},
        {QStringLiteral("sf6"), qMakePair(QStringLiteral("SF6"), QStringLiteral("ppm"))}
    };

    if (!measurements.isEmpty()) {
        for (auto it = pointMeta.constBegin(); it != pointMeta.constEnd(); ++it) {
            if (!measurements.contains(it.key())) {
                continue;
            }
            points.append({it.key(), it.value().first, it.value().second, measurements.value(it.key()).toDouble(), QStringLiteral("good"), now});
        }
    }

    if (points.isEmpty()) {
        const double temperature = object.value(QStringLiteral("temperature")).toDouble();
        const double humidity = object.value(QStringLiteral("humidity")).toDouble();
        points = {
            {QStringLiteral("temperature"), QStringLiteral("温度"), QStringLiteral("℃"), temperature, QStringLiteral("good"), now},
            {QStringLiteral("humidity"), QStringLiteral("湿度"), QStringLiteral("%RH"), humidity, QStringLiteral("good"), now}
        };
    }

    m_store->updateDevicePoints(deviceId, points);
    for (const DataPoint &point : points) {
        if (point.id == QStringLiteral("temperature")) {
            m_store->appendHistory(QStringLiteral("temp"), point.value, now);
        } else if (point.id == QStringLiteral("humidity")) {
            m_store->appendHistory(QStringLiteral("pressure"), point.value, now);
        } else {
            m_store->appendHistory(point.id, point.value, now);
        }
    }
}
