#ifndef DATASTORE_H
#define DATASTORE_H

#include "types.h"

#include <QObject>
#include <QHash>
#include <QReadWriteLock>
#include <QVector>

class DataStore : public QObject
{
    Q_OBJECT

public:
    explicit DataStore(QObject *parent = nullptr);

    void reset();
    void upsertGateway(const GatewaySnapshot &gateway);
    void upsertDevice(const DeviceSnapshot &device);
    void updateDevicePoints(const QString &deviceId, const QList<DataPoint> &points);
    void addAlarm(const AlarmSnapshot &alarm);
    void appendHistory(const QString &pointId, double value, const QDateTime &timestamp);

    GatewaySnapshot gateway(const QString &id) const;
    QList<GatewaySnapshot> gateways() const;
    QList<DeviceSnapshot> devices() const;
    QList<AlarmSnapshot> alarms() const;
    QList<DataPoint> realtime(const QString &deviceId) const;
    QList<HistoryPoint> history(const QString &pointId) const;
    QJsonObject dashboardJson() const;
    QJsonArray gatewaysJson() const;
    QJsonArray realtimeJson(const QString &deviceId) const;
    QJsonArray historyJson(const QString &pointId) const;

private:
    int deviceIndex(const QString &id) const;
    int gatewayIndex(const QString &id) const;
    void updateDeviceStatusLocked(DeviceSnapshot &device) const;

    mutable QReadWriteLock m_lock;
    QList<GatewaySnapshot> m_gateways;
    QList<DeviceSnapshot> m_devices;
    QList<AlarmSnapshot> m_alarms;
    QHash<QString, QList<HistoryPoint>> m_history;
};

#endif // DATASTORE_H
