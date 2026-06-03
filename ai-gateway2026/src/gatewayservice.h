#ifndef GATEWAYSERVICE_H
#define GATEWAYSERVICE_H

#include "appconfig.h"

#include <QObject>

class DataStore;
class DtuPassthroughServer;
class HttpServer;
class ProtocolCollector;

class GatewayService : public QObject
{
    Q_OBJECT

public:
    GatewayService(const AppConfig &config, QObject *parent = nullptr);
    bool start(QString *errorMessage = nullptr);
    QJsonObject runtimeConfigJson() const;
    bool saveRuntimeConfig(const QJsonObject &configObject, QString *errorMessage = nullptr);

private:
    void bootstrapSnapshots();
    void createCollectors();
    void stopCollectors();
    QJsonObject findGatewayConfig(const QString &gatewayId) const;

    AppConfig m_config;
    DataStore *m_store = nullptr;
    HttpServer *m_httpServer = nullptr;
    DtuPassthroughServer *m_dtuServer = nullptr;
    QList<ProtocolCollector *> m_collectors;
};

#endif // GATEWAYSERVICE_H
