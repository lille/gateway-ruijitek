#include "gatewayservice.h"

#include "datastore.h"
#include "dtupassthroughserver.h"
#include "httpserver.h"
#include "protocolcollector.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>

GatewayService::GatewayService(const AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_store(new DataStore(this))
    , m_httpServer(new HttpServer(m_store, this, this))
    , m_dtuServer(new DtuPassthroughServer(m_store, this))
{
}

bool GatewayService::start(QString *errorMessage)
{
    bootstrapSnapshots();

    const QHostAddress address(m_config.bindAddress);
    if (!m_httpServer->start(address, static_cast<quint16>(m_config.httpPort))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("HTTP server listen failed on port %1").arg(m_config.httpPort);
        }
        return false;
    }

    if (!m_dtuServer->start(address, static_cast<quint16>(m_config.dtuListenPort))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("DTU server listen failed on port %1").arg(m_config.dtuListenPort);
        }
        return false;
    }

    createCollectors();

    return true;
}

QJsonObject GatewayService::runtimeConfigJson() const
{
    return m_config.raw;
}

bool GatewayService::saveRuntimeConfig(const QJsonObject &configObject, QString *errorMessage)
{
    if (!AppConfig::save(m_config.configPath, configObject, errorMessage)) {
        return false;
    }

    AppConfig nextConfig = AppConfig::load(m_config.configPath, errorMessage);
    if (!nextConfig.isValid()) {
        return false;
    }

    m_config = nextConfig;
    stopCollectors();
    m_store->reset();
    bootstrapSnapshots();
    createCollectors();
    return true;
}

void GatewayService::bootstrapSnapshots()
{
    for (const QJsonValue &value : m_config.gateways) {
        const QJsonObject object = value.toObject();
        GatewaySnapshot gateway;
        gateway.id = object.value(QStringLiteral("id")).toString();
        gateway.name = object.value(QStringLiteral("name")).toString();
        gateway.mode = object.value(QStringLiteral("mode")).toString();
        gateway.status = QStringLiteral("online");
        gateway.lastSeenAt = QDateTime::currentDateTime();
        const QJsonArray protocolList = object.value(QStringLiteral("protocolList")).toArray();
        for (const QJsonValue &protocol : protocolList) {
            gateway.protocolList.append(protocol.toString());
        }
        m_store->upsertGateway(gateway);
    }

    for (const QJsonValue &value : m_config.devices) {
        const QJsonObject object = value.toObject();
        DeviceSnapshot device;
        device.id = object.value(QStringLiteral("id")).toString();
        device.name = object.value(QStringLiteral("name")).toString();
        device.gatewayId = object.value(QStringLiteral("gatewayId")).toString();
        device.protocol = object.value(QStringLiteral("protocol")).toString();
        device.location = object.value(QStringLiteral("location")).toString();
        device.lastSeenAt = QDateTime::currentDateTime();
        m_store->upsertDevice(device);
    }
}

void GatewayService::createCollectors()
{
    for (const QJsonValue &value : m_config.devices) {
        const QJsonObject deviceConfig = value.toObject();
        ProtocolCollector *collector = new ProtocolCollector(findGatewayConfig(deviceConfig.value(QStringLiteral("gatewayId")).toString()),
                                                             deviceConfig,
                                                             m_store,
                                                             this);
        collector->start();
        m_collectors.append(collector);
    }
}

void GatewayService::stopCollectors()
{
    qDeleteAll(m_collectors);
    m_collectors.clear();
}

QJsonObject GatewayService::findGatewayConfig(const QString &gatewayId) const
{
    for (const QJsonValue &value : m_config.gateways) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("id")).toString() == gatewayId) {
            return object;
        }
    }
    return QJsonObject();
}
