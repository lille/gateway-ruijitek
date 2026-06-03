#include "datastore.h"

#include <QJsonArray>

namespace {
double pointValueById(const QList<DataPoint> &points, const QString &id)
{
    for (const DataPoint &point : points) {
        if (point.id == id) {
            return point.value;
        }
    }
    return 0.0;
}

bool isAirQualityAlarm(const QList<DataPoint> &points)
{
    return pointValueById(points, QStringLiteral("pm25")) >= 75.0
        || pointValueById(points, QStringLiteral("co2")) >= 1000.0
        || pointValueById(points, QStringLiteral("tvoc")) >= 600.0
        || pointValueById(points, QStringLiteral("formaldehyde")) >= 100.0
        || pointValueById(points, QStringLiteral("temperature")) >= 32.0;
}
}

DataStore::DataStore(QObject *parent)
    : QObject(parent)
{
}

void DataStore::reset()
{
    QWriteLocker locker(&m_lock);
    m_gateways.clear();
    m_devices.clear();
    m_alarms.clear();
    m_history.clear();
}

void DataStore::upsertGateway(const GatewaySnapshot &gateway)
{
    QWriteLocker locker(&m_lock);
    const int index = gatewayIndex(gateway.id);
    if (index >= 0) {
        m_gateways[index] = gateway;
    } else {
        m_gateways.append(gateway);
    }
}

void DataStore::upsertDevice(const DeviceSnapshot &device)
{
    QWriteLocker locker(&m_lock);
    DeviceSnapshot copy = device;
    updateDeviceStatusLocked(copy);
    const int index = deviceIndex(copy.id);
    if (index >= 0) {
        m_devices[index] = copy;
    } else {
        m_devices.append(copy);
    }
}

void DataStore::updateDevicePoints(const QString &deviceId, const QList<DataPoint> &points)
{
    QWriteLocker locker(&m_lock);
    const int index = deviceIndex(deviceId);
    if (index < 0) {
        return;
    }

    m_devices[index].realtimePoints = points;
    m_devices[index].lastSeenAt = QDateTime::currentDateTime();

    m_devices[index].status = isAirQualityAlarm(points) ? QStringLiteral("alarm") : QStringLiteral("online");
}

void DataStore::addAlarm(const AlarmSnapshot &alarm)
{
    QWriteLocker locker(&m_lock);
    for (const AlarmSnapshot &existing : qAsConst(m_alarms)) {
        if (existing.sourceId == alarm.sourceId
            && existing.message == alarm.message
            && existing.createdAt.secsTo(alarm.createdAt) <= 300) {
            return;
        }
    }
    m_alarms.prepend(alarm);
    while (m_alarms.size() > 20) {
        m_alarms.removeLast();
    }
}

void DataStore::appendHistory(const QString &pointId, double value, const QDateTime &timestamp)
{
    QWriteLocker locker(&m_lock);
    QList<HistoryPoint> &points = m_history[pointId];
    points.append({pointId, timestamp.toString(QStringLiteral("HH:mm:ss")), value});
    while (points.size() > 30) {
        points.removeFirst();
    }
}

GatewaySnapshot DataStore::gateway(const QString &id) const
{
    QReadLocker locker(&m_lock);
    const int index = gatewayIndex(id);
    return index >= 0 ? m_gateways.at(index) : GatewaySnapshot();
}

QList<GatewaySnapshot> DataStore::gateways() const
{
    QReadLocker locker(&m_lock);
    return m_gateways;
}

QList<DeviceSnapshot> DataStore::devices() const
{
    QReadLocker locker(&m_lock);
    return m_devices;
}

QList<AlarmSnapshot> DataStore::alarms() const
{
    QReadLocker locker(&m_lock);
    return m_alarms;
}

QList<DataPoint> DataStore::realtime(const QString &deviceId) const
{
    QReadLocker locker(&m_lock);
    const int index = deviceIndex(deviceId);
    return index >= 0 ? m_devices.at(index).realtimePoints : QList<DataPoint>();
}

QList<HistoryPoint> DataStore::history(const QString &pointId) const
{
    QReadLocker locker(&m_lock);
    return m_history.value(pointId);
}

QJsonObject DataStore::dashboardJson() const
{
    QReadLocker locker(&m_lock);

    QJsonArray devicesArray;
    int onlineCount = 0;
    int alarmCount = 0;
    int pointCount = 0;
    for (const DeviceSnapshot &device : m_devices) {
        devicesArray.append(device.toJson());
        pointCount += device.realtimePoints.size();
        if (device.status == QStringLiteral("online")) {
            ++onlineCount;
        } else if (device.status == QStringLiteral("alarm")) {
            ++alarmCount;
        }
    }

    QJsonArray alarmsArray;
    for (const AlarmSnapshot &alarm : m_alarms) {
        alarmsArray.append(alarm.toJson());
    }

    QJsonArray categories;
    categories.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("空气质量")}, {QStringLiteral("count"), m_devices.size()}});
    categories.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("颗粒物")}, {QStringLiteral("count"), m_devices.size()}});
    categories.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("气体浓度")}, {QStringLiteral("count"), m_devices.size()}});
    categories.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("环境舒适度")}, {QStringLiteral("count"), onlineCount}});

    QJsonArray rows;
    for (const DeviceSnapshot &device : m_devices) {
        double temperature = 0.0;
        double humidity = 0.0;
        double pm25 = 0.0;
        double pm10 = 0.0;
        double co2 = 0.0;
        double tvoc = 0.0;
        double formaldehyde = 0.0;
        for (const DataPoint &point : device.realtimePoints) {
            if (point.id == QStringLiteral("temperature")) {
                temperature = point.value;
            } else if (point.id == QStringLiteral("humidity")) {
                humidity = point.value;
            } else if (point.id == QStringLiteral("pm25")) {
                pm25 = point.value;
            } else if (point.id == QStringLiteral("pm10")) {
                pm10 = point.value;
            } else if (point.id == QStringLiteral("co2")) {
                co2 = point.value;
            } else if (point.id == QStringLiteral("tvoc")) {
                tvoc = point.value;
            } else if (point.id == QStringLiteral("formaldehyde")) {
                formaldehyde = point.value;
            }
        }

        rows.append(QJsonObject{
            {QStringLiteral("device"), device.name},
            {QStringLiteral("status"), device.status == QStringLiteral("alarm") ? QStringLiteral("预警") : QStringLiteral("正常")},
            {QStringLiteral("temperature"), temperature},
            {QStringLiteral("humidity"), humidity},
            {QStringLiteral("pm25"), pm25},
            {QStringLiteral("pm10"), pm10},
            {QStringLiteral("co2"), co2},
            {QStringLiteral("tvoc"), tvoc},
            {QStringLiteral("formaldehyde"), formaldehyde}
        });
    }

    QJsonObject summary;
    summary.insert(QStringLiteral("gatewayCount"), m_gateways.size());
    summary.insert(QStringLiteral("deviceCount"), m_devices.size());
    summary.insert(QStringLiteral("pointCount"), pointCount);
    summary.insert(QStringLiteral("onlineCount"), onlineCount);
    summary.insert(QStringLiteral("alarmCount"), alarmCount);
    summary.insert(QStringLiteral("reportInterval"), QStringLiteral("1s"));
    summary.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));

    QJsonObject object;
    object.insert(QStringLiteral("summary"), summary);
    object.insert(QStringLiteral("devices"), devicesArray);
    object.insert(QStringLiteral("alarms"), alarmsArray);
    object.insert(QStringLiteral("monitorCategories"), categories);
    object.insert(QStringLiteral("monitorRows"), rows);
    return object;
}

QJsonArray DataStore::gatewaysJson() const
{
    QReadLocker locker(&m_lock);
    QJsonArray array;
    for (const GatewaySnapshot &gateway : m_gateways) {
        array.append(gateway.toJson());
    }
    return array;
}

QJsonArray DataStore::realtimeJson(const QString &deviceId) const
{
    QReadLocker locker(&m_lock);
    QJsonArray array;
    const int index = deviceIndex(deviceId);
    if (index < 0) {
        return array;
    }

    for (const DataPoint &point : m_devices.at(index).realtimePoints) {
        array.append(point.toJson());
    }
    return array;
}

QJsonArray DataStore::historyJson(const QString &pointId) const
{
    QReadLocker locker(&m_lock);
    QJsonArray array;
    for (const HistoryPoint &point : m_history.value(pointId)) {
        array.append(point.toJson());
    }
    return array;
}

int DataStore::deviceIndex(const QString &id) const
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

int DataStore::gatewayIndex(const QString &id) const
{
    for (int i = 0; i < m_gateways.size(); ++i) {
        if (m_gateways.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void DataStore::updateDeviceStatusLocked(DeviceSnapshot &device) const
{
    if (!device.lastSeenAt.isValid()) {
        device.lastSeenAt = QDateTime::currentDateTime();
    }
}
