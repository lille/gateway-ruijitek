#ifndef TYPES_H
#define TYPES_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

struct DataPoint
{
    QString id;
    QString name;
    QString unit;
    double value = 0.0;
    QString quality = QStringLiteral("good");
    QDateTime timestamp;

    QJsonObject toJson() const
    {
        QJsonObject object;
        object.insert(QStringLiteral("id"), id);
        object.insert(QStringLiteral("name"), name);
        object.insert(QStringLiteral("unit"), unit);
        object.insert(QStringLiteral("value"), value);
        object.insert(QStringLiteral("quality"), quality);
        object.insert(QStringLiteral("timestamp"), timestamp.toString(Qt::ISODate));
        return object;
    }
};

struct DeviceSnapshot
{
    QString id;
    QString name;
    QString gatewayId;
    QString protocol;
    QString location;
    QString status = QStringLiteral("online");
    QDateTime lastSeenAt;
    QList<DataPoint> realtimePoints;

    QJsonObject toJson() const
    {
        QJsonObject object;
        object.insert(QStringLiteral("id"), id);
        object.insert(QStringLiteral("name"), name);
        object.insert(QStringLiteral("gatewayId"), gatewayId);
        object.insert(QStringLiteral("protocol"), protocol);
        object.insert(QStringLiteral("location"), location);
        object.insert(QStringLiteral("status"), status);
        object.insert(QStringLiteral("lastSeenAt"), lastSeenAt.toString(Qt::ISODate));
        return object;
    }
};

struct GatewaySnapshot
{
    QString id;
    QString name;
    QString mode;
    QString status = QStringLiteral("online");
    QStringList protocolList;
    QDateTime lastSeenAt;

    QJsonObject toJson() const
    {
        QJsonObject object;
        object.insert(QStringLiteral("id"), id);
        object.insert(QStringLiteral("name"), name);
        object.insert(QStringLiteral("mode"), mode);
        object.insert(QStringLiteral("status"), status);
        object.insert(QStringLiteral("lastSeenAt"), lastSeenAt.toString(Qt::ISODate));

        QJsonArray protocols;
        for (const QString &protocol : protocolList) {
            protocols.append(protocol);
        }
        object.insert(QStringLiteral("protocolList"), protocols);
        return object;
    }
};

struct AlarmSnapshot
{
    QString id;
    QString sourceId;
    QString message;
    QString level = QStringLiteral("warning");
    QString status = QStringLiteral("new");
    QDateTime createdAt;

    QJsonObject toJson() const
    {
        QJsonObject object;
        object.insert(QStringLiteral("id"), id);
        object.insert(QStringLiteral("sourceId"), sourceId);
        object.insert(QStringLiteral("message"), message);
        object.insert(QStringLiteral("level"), level);
        object.insert(QStringLiteral("status"), status);
        object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODate));
        return object;
    }
};

struct HistoryPoint
{
    QString pointId;
    QString timeLabel;
    double value = 0.0;

    QJsonObject toJson() const
    {
        QJsonObject object;
        object.insert(QStringLiteral("time"), timeLabel);
        object.insert(QStringLiteral("value"), value);
        return object;
    }
};

#endif // TYPES_H
