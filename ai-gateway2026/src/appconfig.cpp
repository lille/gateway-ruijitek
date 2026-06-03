#include "appconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

bool AppConfig::save(const QString &path, const QJsonObject &object, QString *errorMessage)
{
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write config: %1").arg(path);
        }
        return false;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

AppConfig AppConfig::load(const QString &path, QString *errorMessage)
{
    AppConfig config;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open config: %1").arg(path);
        }
        return config;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Config parse failed: %1").arg(parseError.errorString());
        }
        return config;
    }

    if (!document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Config root must be a JSON object.");
        }
        return config;
    }

    config.raw = document.object();
    config.configPath = QFileInfo(path).absoluteFilePath();
    config.httpPort = config.raw.value(QStringLiteral("httpPort")).toInt(8090);
    config.dtuListenPort = config.raw.value(QStringLiteral("dtuListenPort")).toInt(15020);
    config.refreshIntervalMs = config.raw.value(QStringLiteral("refreshIntervalMs")).toInt(1000);
    config.bindAddress = config.raw.value(QStringLiteral("bindAddress")).toString(QStringLiteral("0.0.0.0"));
    config.gateways = config.raw.value(QStringLiteral("gateways")).toArray();
    config.devices = config.raw.value(QStringLiteral("devices")).toArray();
    config.m_valid = true;
    return config;
}

bool AppConfig::isValid() const
{
    return m_valid;
}
