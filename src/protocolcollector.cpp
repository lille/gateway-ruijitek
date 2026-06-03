#include "protocolcollector.h"

#include "datastore.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QModbusClient>
#include <QModbusDataUnit>
#include <QModbusReply>
#include <QModbusRtuSerialMaster>
#include <QModbusTcpClient>
#include <QRandomGenerator>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTcpSocket>
#include <QVector>

ProtocolCollector::ProtocolCollector(const QJsonObject &gatewayConfig,
                                     const QJsonObject &deviceConfig,
                                     DataStore *store,
                                     QObject *parent)
    : QObject(parent)
    , m_gatewayConfig(gatewayConfig)
    , m_deviceConfig(deviceConfig)
    , m_store(store)
{
    const int interval = deviceConfig.value(QStringLiteral("intervalMs")).toInt(1000);
    m_timer.setInterval(interval);
    connect(&m_timer, &QTimer::timeout, this, &ProtocolCollector::poll);
}

namespace {
double jitter(double min, double max)
{
    return min + (max - min) * QRandomGenerator::global()->generateDouble();
}

const DataPoint *findPoint(const QList<DataPoint> &points, const QString &id)
{
    for (const DataPoint &point : points) {
        if (point.id == id) {
            return &point;
        }
    }
    return nullptr;
}

QString resolveSerialPortName(const QJsonObject &serialConfig)
{
    const QString configured = serialConfig.value(QStringLiteral("port")).toString().trimmed();
    if (!configured.isEmpty() && configured.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0) {
        return configured;
    }

    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        const QString name = info.systemLocation();
        if (name.contains(QStringLiteral("ttyUSB"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("ttyS"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("COM"), Qt::CaseInsensitive)) {
            return name;
        }
    }

    return ports.isEmpty() ? QString() : ports.first().systemLocation();
}
}

void ProtocolCollector::start()
{
    poll();
    m_timer.start();
}

void ProtocolCollector::poll()
{
    const QString protocol = m_deviceConfig.value(QStringLiteral("protocol")).toString().toLower();
    QList<DataPoint> points;

    if (protocol == QStringLiteral("modbus-rtu")) {
        points = readModbusRtuPoints();
    } else if (protocol == QStringLiteral("modbus-tcp") || protocol == QStringLiteral("plc")) {
        points = readModbusTcpPoints();
    } else if (protocol == QStringLiteral("tcp-socket")) {
        points = readTcpSocketPoints();
    } else if (protocol == QStringLiteral("serial")) {
        points = readSerialAsciiPoints();
    } else {
        points = simulatePoints();
    }

    publishPoints(points);
}

QList<DataPoint> ProtocolCollector::simulatePoints() const
{
    const double baseTemp = m_deviceConfig.value(QStringLiteral("baseTemperature")).toDouble(24.0);
    const double baseHumidity = m_deviceConfig.value(QStringLiteral("baseHumidity")).toDouble(56.0);
    const QDateTime now = QDateTime::currentDateTime();

    if (useZhQ006Template()) {
        return {
            {QStringLiteral("formaldehyde"), QStringLiteral("甲醛"), QStringLiteral("ug/m3"), 12 + jitter(0.0, 6.0), QStringLiteral("good"), now},
            {QStringLiteral("pm25"), QStringLiteral("PM2.5"), QStringLiteral("ug/m3"), 18 + jitter(0.0, 8.0), QStringLiteral("good"), now},
            {QStringLiteral("tvoc"), QStringLiteral("TVOC"), QStringLiteral("ug/m3"), 55 + jitter(0.0, 20.0), QStringLiteral("good"), now},
            {QStringLiteral("co2"), QStringLiteral("CO2"), QStringLiteral("ppm"), 520 + jitter(0.0, 120.0), QStringLiteral("good"), now},
            {QStringLiteral("temperature"), QStringLiteral("温度"), QStringLiteral("℃"), baseTemp + jitter(0.0, 3.5), QStringLiteral("good"), now},
            {QStringLiteral("humidity"), QStringLiteral("湿度"), QStringLiteral("%RH"), baseHumidity + jitter(0.0, 8.0), QStringLiteral("good"), now},
            {QStringLiteral("pm1_0"), QStringLiteral("PM1.0"), QStringLiteral("ug/m3"), 12 + jitter(0.0, 5.0), QStringLiteral("good"), now},
            {QStringLiteral("pm10"), QStringLiteral("PM10"), QStringLiteral("ug/m3"), 26 + jitter(0.0, 10.0), QStringLiteral("good"), now},
            {QStringLiteral("sf6"), QStringLiteral("SF6"), QStringLiteral("ppm"), jitter(0.0, 0.02), QStringLiteral("good"), now}
        };
    }

    return {
        {QStringLiteral("temperature"), QStringLiteral("温度"), QStringLiteral("℃"),
         baseTemp + jitter(0.0, 3.5), QStringLiteral("good"), now},
        {QStringLiteral("humidity"), QStringLiteral("湿度"), QStringLiteral("%RH"),
         baseHumidity + jitter(0.0, 8.0), QStringLiteral("good"), now},
        {QStringLiteral("pressure"), QStringLiteral("压力"), QStringLiteral("kPa"),
         101.0 + jitter(-1.2, 1.2), QStringLiteral("good"), now}
    };
}

QList<DataPoint> ProtocolCollector::readModbusRtuPoints()
{
    QJsonObject serial = m_deviceConfig.value(QStringLiteral("serial")).toObject();
    if (serial.isEmpty() || serial.value(QStringLiteral("simulate")).toBool(false)) {
        return simulatePoints();
    }

    if (!m_modbusClient) {
        QModbusRtuSerialMaster *client = new QModbusRtuSerialMaster(this);
        client->setConnectionParameter(QModbusDevice::SerialPortNameParameter, resolveSerialPortName(serial));
        client->setConnectionParameter(QModbusDevice::SerialParityParameter, serial.value(QStringLiteral("parity")).toString(QStringLiteral("N")) == QStringLiteral("E")
                                       ? QSerialPort::EvenParity : QSerialPort::NoParity);
        client->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, serial.value(QStringLiteral("baudRate")).toInt(9600));
        client->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, serial.value(QStringLiteral("dataBits")).toInt(8));
        client->setConnectionParameter(QModbusDevice::SerialStopBitsParameter, serial.value(QStringLiteral("stopBits")).toInt(1) == 2
                                       ? QSerialPort::TwoStop : QSerialPort::OneStop);
        client->setTimeout(800);
        client->setNumberOfRetries(1);
        m_modbusClient = client;
    }

    if (m_modbusClient->state() != QModbusDevice::ConnectedState && !m_modbusClient->connectDevice()) {
        return fallbackPoints(QStringLiteral("rtu-disconnected"));
    }

    const int slaveId = m_deviceConfig.value(QStringLiteral("slaveId")).toInt(1);
    const int startAddress = m_deviceConfig.value(QStringLiteral("startAddress")).toInt(0);
    const int registerCount = m_deviceConfig.value(QStringLiteral("registerCount")).toInt(2);
    QModbusReply *reply = m_modbusClient->sendReadRequest(QModbusDataUnit(QModbusDataUnit::HoldingRegisters, startAddress, registerCount), slaveId);
    if (!reply) {
        return fallbackPoints(QStringLiteral("rtu-read-failed"));
    }

    QEventLoop loop;
    connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QList<DataPoint> points;
    const QDateTime now = QDateTime::currentDateTime();
    if (reply->error() == QModbusDevice::NoError) {
        const QModbusDataUnit unit = reply->result();
        if (useZhQ006Template()) {
            QVector<quint16> registers;
            registers.reserve(static_cast<int>(unit.valueCount()));
            for (uint i = 0; i < unit.valueCount(); ++i) {
                registers.append(unit.value(i));
            }
            points = decodeZhQ006Points(registers, now);
        } else {
            const double temperature = unit.valueCount() > 0 ? unit.value(0) / 10.0 : 0.0;
            const double humidity = unit.valueCount() > 1 ? unit.value(1) / 10.0 : 0.0;
            points = {
                {QStringLiteral("temperature"), QStringLiteral("温度"), QStringLiteral("℃"), temperature, QStringLiteral("good"), now},
                {QStringLiteral("humidity"), QStringLiteral("湿度"), QStringLiteral("%RH"), humidity, QStringLiteral("good"), now}
            };
        }
    } else {
        points = fallbackPoints(reply->errorString());
    }

    reply->deleteLater();
    return points;
}

QList<DataPoint> ProtocolCollector::readModbusTcpPoints()
{
    QJsonObject tcp = m_deviceConfig.value(QStringLiteral("tcp")).toObject();
    if (tcp.isEmpty() || tcp.value(QStringLiteral("simulate")).toBool(false)) {
        return simulatePoints();
    }

    if (!m_modbusClient) {
        QModbusTcpClient *client = new QModbusTcpClient(this);
        client->setConnectionParameter(QModbusDevice::NetworkAddressParameter, tcp.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1")));
        client->setConnectionParameter(QModbusDevice::NetworkPortParameter, tcp.value(QStringLiteral("port")).toInt(502));
        client->setTimeout(800);
        client->setNumberOfRetries(1);
        m_modbusClient = client;
    }

    if (m_modbusClient->state() != QModbusDevice::ConnectedState && !m_modbusClient->connectDevice()) {
        return fallbackPoints(QStringLiteral("tcp-disconnected"));
    }

    const int slaveId = m_deviceConfig.value(QStringLiteral("slaveId")).toInt(1);
    const int startAddress = m_deviceConfig.value(QStringLiteral("startAddress")).toInt(0);
    const int registerCount = m_deviceConfig.value(QStringLiteral("registerCount")).toInt(2);
    QModbusReply *reply = m_modbusClient->sendReadRequest(QModbusDataUnit(QModbusDataUnit::HoldingRegisters, startAddress, registerCount), slaveId);
    if (!reply) {
        return fallbackPoints(QStringLiteral("tcp-read-failed"));
    }

    QEventLoop loop;
    connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QList<DataPoint> points;
    const QDateTime now = QDateTime::currentDateTime();
    if (reply->error() == QModbusDevice::NoError) {
        const QModbusDataUnit unit = reply->result();
        if (useZhQ006Template()) {
            QVector<quint16> registers;
            registers.reserve(static_cast<int>(unit.valueCount()));
            for (uint i = 0; i < unit.valueCount(); ++i) {
                registers.append(unit.value(i));
            }
            points = decodeZhQ006Points(registers, now);
        } else {
            const double temperature = unit.valueCount() > 0 ? unit.value(0) / 10.0 : 0.0;
            const double humidity = unit.valueCount() > 1 ? unit.value(1) / 10.0 : 0.0;
            points = {
                {QStringLiteral("temperature"), QStringLiteral("温度"), QStringLiteral("℃"), temperature, QStringLiteral("good"), now},
                {QStringLiteral("humidity"), QStringLiteral("湿度"), QStringLiteral("%RH"), humidity, QStringLiteral("good"), now}
            };
        }
    } else {
        points = fallbackPoints(reply->errorString());
    }

    reply->deleteLater();
    return points;
}

QList<DataPoint> ProtocolCollector::readTcpSocketPoints()
{
    QJsonObject tcp = m_deviceConfig.value(QStringLiteral("tcp")).toObject();
    if (tcp.isEmpty() || tcp.value(QStringLiteral("simulate")).toBool(false)) {
        return simulatePoints();
    }

    QTcpSocket socket;
    socket.connectToHost(tcp.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1")),
                         static_cast<quint16>(tcp.value(QStringLiteral("port")).toInt(9000)));
    if (!socket.waitForConnected(500)) {
        return fallbackPoints(QStringLiteral("socket-connect-failed"));
    }

    socket.write(tcp.value(QStringLiteral("request")).toString(QStringLiteral("READ\n")).toUtf8());
    if (!socket.waitForReadyRead(500)) {
        return fallbackPoints(QStringLiteral("socket-timeout"));
    }

    const QString payload = QString::fromUtf8(socket.readAll()).trimmed();
    const QStringList pairs = payload.split(QLatin1Char(','), Qt::SkipEmptyParts);
    double temperature = 0.0;
    double humidity = 0.0;
    for (const QString &pair : pairs) {
        const QStringList kv = pair.split(QLatin1Char('='));
        if (kv.size() != 2) {
            continue;
        }
        if (kv.at(0).trimmed().compare(QStringLiteral("temp"), Qt::CaseInsensitive) == 0) {
            temperature = kv.at(1).toDouble();
        } else if (kv.at(0).trimmed().compare(QStringLiteral("humidity"), Qt::CaseInsensitive) == 0) {
            humidity = kv.at(1).toDouble();
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    return {
        {QStringLiteral("temperature"), QStringLiteral("温度"), QStringLiteral("℃"), temperature, QStringLiteral("good"), now},
        {QStringLiteral("humidity"), QStringLiteral("湿度"), QStringLiteral("%RH"), humidity, QStringLiteral("good"), now}
    };
}

QList<DataPoint> ProtocolCollector::readSerialAsciiPoints()
{
    QJsonObject serial = m_deviceConfig.value(QStringLiteral("serial")).toObject();
    if (serial.isEmpty() || serial.value(QStringLiteral("simulate")).toBool(false)) {
        return simulatePoints();
    }

    QSerialPort port;
    port.setPortName(resolveSerialPortName(serial));
    port.setBaudRate(serial.value(QStringLiteral("baudRate")).toInt(9600));
    port.setDataBits(QSerialPort::Data8);
    port.setParity(QSerialPort::NoParity);
    port.setStopBits(QSerialPort::OneStop);
    if (!port.open(QIODevice::ReadWrite)) {
        return fallbackPoints(QStringLiteral("serial-open-failed"));
    }

    port.write(serial.value(QStringLiteral("request")).toString(QStringLiteral("READ\r\n")).toUtf8());
    if (!port.waitForReadyRead(500)) {
        return fallbackPoints(QStringLiteral("serial-timeout"));
    }

    const QString line = QString::fromUtf8(port.readAll()).trimmed();
    const QStringList pairs = line.split(QLatin1Char(','), Qt::SkipEmptyParts);
    double temperature = 0.0;
    double humidity = 0.0;
    for (const QString &pair : pairs) {
        const QStringList kv = pair.split(QLatin1Char(':'));
        if (kv.size() != 2) {
            continue;
        }
        if (kv.at(0).trimmed() == QStringLiteral("T")) {
            temperature = kv.at(1).toDouble();
        } else if (kv.at(0).trimmed() == QStringLiteral("H")) {
            humidity = kv.at(1).toDouble();
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    return {
        {QStringLiteral("temperature"), QStringLiteral("温度"), QStringLiteral("℃"), temperature, QStringLiteral("good"), now},
        {QStringLiteral("humidity"), QStringLiteral("湿度"), QStringLiteral("%RH"), humidity, QStringLiteral("good"), now}
    };
}

QList<DataPoint> ProtocolCollector::fallbackPoints(const QString &quality) const
{
    QList<DataPoint> points = simulatePoints();
    for (DataPoint &point : points) {
        point.quality = quality;
    }
    return points;
}

void ProtocolCollector::publishPoints(const QList<DataPoint> &points)
{
    if (!m_store) {
        return;
    }

    const QString deviceId = m_deviceConfig.value(QStringLiteral("id")).toString();
    m_store->updateDevicePoints(deviceId, points);

    for (const DataPoint &point : points) {
        if (point.id == QStringLiteral("temperature")) {
            m_store->appendHistory(QStringLiteral("temp"), point.value, point.timestamp);
        } else if (point.id == QStringLiteral("humidity")) {
            m_store->appendHistory(QStringLiteral("pressure"), point.value, point.timestamp);
        }
        m_store->appendHistory(point.id, point.value, point.timestamp);
    }

    const QString deviceName = m_deviceConfig.value(QStringLiteral("name")).toString();
    for (const AlarmRule &rule : currentAlarmRules()) {
        const DataPoint *point = findPoint(points, rule.id);
        if (!point) {
            continue;
        }

        QString level;
        if (point->value >= rule.criticalThreshold) {
            level = QStringLiteral("critical");
        } else if (point->value >= rule.warningThreshold) {
            level = QStringLiteral("warning");
        } else {
            continue;
        }

        m_store->addAlarm({
            QStringLiteral("ALM-%1-%2").arg(deviceId, rule.id),
            deviceId,
            QStringLiteral("%1 %2超限：%3 %4")
                .arg(deviceName,
                     rule.name)
                .arg(point->value, 0, 'f', rule.precision)
                .arg(rule.unit),
            level,
            QStringLiteral("new"),
            point->timestamp
        });
    }

    emit deviceSampled(deviceId);
}

QList<DataPoint> ProtocolCollector::decodeZhQ006Points(const QVector<quint16> &registers, const QDateTime &timestamp) const
{
    auto readAt = [&registers](int index) -> quint16 {
        return (index >= 0 && index < registers.size()) ? registers.at(index) : 0;
    };

    return {
        {QStringLiteral("formaldehyde"), QStringLiteral("甲醛"), QStringLiteral("ug/m3"), static_cast<double>(readAt(0)), QStringLiteral("good"), timestamp},
        {QStringLiteral("pm25"), QStringLiteral("PM2.5"), QStringLiteral("ug/m3"), static_cast<double>(readAt(1)), QStringLiteral("good"), timestamp},
        {QStringLiteral("tvoc"), QStringLiteral("TVOC"), QStringLiteral("ug/m3"), static_cast<double>(readAt(2)), QStringLiteral("good"), timestamp},
        {QStringLiteral("co2"), QStringLiteral("CO2"), QStringLiteral("ppm"), static_cast<double>(readAt(3)), QStringLiteral("good"), timestamp},
        {QStringLiteral("temperature"), QStringLiteral("温度"), QStringLiteral("℃"), (static_cast<double>(readAt(4)) - 500.0) / 10.0, QStringLiteral("good"), timestamp},
        {QStringLiteral("humidity"), QStringLiteral("湿度"), QStringLiteral("%RH"), static_cast<double>(readAt(5)) / 10.0, QStringLiteral("good"), timestamp},
        {QStringLiteral("pm1_0"), QStringLiteral("PM1.0"), QStringLiteral("ug/m3"), static_cast<double>(readAt(6)), QStringLiteral("good"), timestamp},
        {QStringLiteral("pm10"), QStringLiteral("PM10"), QStringLiteral("ug/m3"), static_cast<double>(readAt(7)), QStringLiteral("good"), timestamp},
        {QStringLiteral("sf6"), QStringLiteral("SF6"), QStringLiteral("ppm"), static_cast<double>(readAt(8)) / 1000.0, QStringLiteral("good"), timestamp}
    };
}

bool ProtocolCollector::useZhQ006Template() const
{
    const QString templateName = m_deviceConfig.value(QStringLiteral("templateName")).toString();
    return templateName.contains(QStringLiteral("ZH-Q006"), Qt::CaseInsensitive)
        || m_deviceConfig.value(QStringLiteral("registerCount")).toInt() >= 9;
}

QList<ProtocolCollector::AlarmRule> ProtocolCollector::currentAlarmRules() const
{
    QList<AlarmRule> rules = {
        {QStringLiteral("pm25"), QStringLiteral("PM2.5"), 75.0, 150.0, QStringLiteral("ug/m3"), 0},
        {QStringLiteral("co2"), QStringLiteral("CO2"), 1000.0, 2000.0, QStringLiteral("ppm"), 0},
        {QStringLiteral("tvoc"), QStringLiteral("TVOC"), 600.0, 1000.0, QStringLiteral("ug/m3"), 0},
        {QStringLiteral("formaldehyde"), QStringLiteral("甲醛"), 100.0, 200.0, QStringLiteral("ug/m3"), 0},
        {QStringLiteral("temperature"), QStringLiteral("温度"), 32.0, 38.0, QStringLiteral("℃"), 1}
    };

    const QJsonObject thresholds = m_deviceConfig.value(QStringLiteral("alarmThresholds")).toObject();
    if (thresholds.isEmpty()) {
        return rules;
    }

    for (AlarmRule &rule : rules) {
        const QJsonObject overrideRule = thresholds.value(rule.id).toObject();
        if (overrideRule.isEmpty()) {
            continue;
        }
        rule.warningThreshold = overrideRule.value(QStringLiteral("warning")).toDouble(rule.warningThreshold);
        rule.criticalThreshold = overrideRule.value(QStringLiteral("critical")).toDouble(rule.criticalThreshold);
    }
    return rules;
}
