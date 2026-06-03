#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class AppConfig
{
public:
    static AppConfig load(const QString &path, QString *errorMessage = nullptr);
    static bool save(const QString &path, const QJsonObject &object, QString *errorMessage = nullptr);

    bool isValid() const;

    int httpPort = 8090;
    int dtuListenPort = 15020;
    int refreshIntervalMs = 1000;
    QString bindAddress = QStringLiteral("0.0.0.0");
    QString configPath;
    QJsonArray gateways;
    QJsonArray devices;
    QJsonObject raw;

private:
    bool m_valid = false;
};

#endif // APPCONFIG_H
