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
#include <QHash>
#include <QMutex>
#include <QThread>
#include <QVector>
#include <QFile>

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

QString normalizeSerialPortName(const QString &configured)
{
    QString port = configured.trimmed();
    if (port.isEmpty()) {
        return port;
    }

#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
    if (!port.startsWith(QStringLiteral("/dev/"))
        && (port.startsWith(QStringLiteral("tty"), Qt::CaseInsensitive)
            || port.startsWith(QStringLiteral("ttyUSB"), Qt::CaseInsensitive)
            || port.startsWith(QStringLiteral("ttyACM"), Qt::CaseInsensitive))) {
        port.prepend(QStringLiteral("/dev/"));
    }
#endif

    return port;
}

QString resolveSerialPortName(const QJsonObject &serialConfig)
{
    const QString configured = serialConfig.value(QStringLiteral("port")).toString().trimmed();
    if (!configured.isEmpty() && configured.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0) {
        return normalizeSerialPortName(configured);
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

QString rtuPortKey(const QJsonObject &serialConfig)
{
    const QString port = resolveSerialPortName(serialConfig);
    if (port.isEmpty()) {
        return QString();
    }

    return QStringLiteral("%1|%2|%3|%4")
        .arg(port)
        .arg(serialConfig.value(QStringLiteral("baudRate")).toInt(9600))
        .arg(serialConfig.value(QStringLiteral("dataBits")).toInt(8))
        .arg(serialConfig.value(QStringLiteral("parity")).toString(QStringLiteral("N")));
}

struct SharedRtuBus {
    QModbusRtuSerialMaster *client = nullptr;
    QMutex mutex;
};

QHash<QString, SharedRtuBus *> g_rtuBuses;

QModbusRtuSerialMaster *createRtuClient(const QJsonObject &serialConfig)
{
    auto *client = new QModbusRtuSerialMaster();
    client->setConnectionParameter(QModbusDevice::SerialPortNameParameter,
                                   resolveSerialPortName(serialConfig));
    client->setConnectionParameter(QModbusDevice::SerialParityParameter,
                                   serialConfig.value(QStringLiteral("parity")).toString(QStringLiteral("N")) == QStringLiteral("E")
                                       ? QSerialPort::EvenParity
                                       : QSerialPort::NoParity);
    client->setConnectionParameter(QModbusDevice::SerialBaudRateParameter,
                                   serialConfig.value(QStringLiteral("baudRate")).toInt(9600));
    client->setConnectionParameter(QModbusDevice::SerialDataBitsParameter,
                                   serialConfig.value(QStringLiteral("dataBits")).toInt(8));
    client->setConnectionParameter(QModbusDevice::SerialStopBitsParameter,
                                   serialConfig.value(QStringLiteral("stopBits")).toInt(1) == 2
                                       ? QSerialPort::TwoStop
                                       : QSerialPort::OneStop);
    client->setTimeout(2000);
    client->setNumberOfRetries(2);
    return client;
}

SharedRtuBus *busForPort(const QJsonObject &serialConfig)
{
    const QString key = rtuPortKey(serialConfig);
    if (key.isEmpty()) {
        return nullptr;
    }

    SharedRtuBus *&bus = g_rtuBuses[key];
    if (!bus) {
        bus = new SharedRtuBus();
    }
    return bus;
}

QModbusRtuSerialMaster *ensureRtuClient(SharedRtuBus *bus, const QJsonObject &serialConfig)
{
    if (!bus) {
        return nullptr;
    }

    if (!bus->client) {
        bus->client = createRtuClient(serialConfig);
    }

    if (bus->client->state() != QModbusDevice::ConnectedState && !bus->client->connectDevice()) {
        bus->client->disconnectDevice();
        delete bus->client;
        bus->client = createRtuClient(serialConfig);
        if (!bus->client->connectDevice()) {
            delete bus->client;
            bus->client = nullptr;
            return nullptr;
        }
    }

    return bus->client;
}

QVector<quint16> readHoldingRegisters(QModbusRtuSerialMaster *client,
                                      int slaveId,
                                      int startAddress,
                                      int registerCount,
                                      QString *errorMessage)
{
    QModbusReply *reply = client->sendReadRequest(
        QModbusDataUnit(QModbusDataUnit::HoldingRegisters, startAddress, registerCount),
        static_cast<quint8>(slaveId));
    if (!reply) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("rtu-read-failed");
        }
        return {};
    }

    QEventLoop loop;
    connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QVector<quint16> registers;
    if (reply->error() == QModbusDevice::NoError) {
        const QModbusDataUnit unit = reply->result();
        registers.reserve(static_cast<int>(unit.valueCount()));
        for (uint i = 0; i < unit.valueCount(); ++i) {
            registers.append(unit.value(i));
        }
    } else if (errorMessage) {
        *errorMessage = reply->errorString();
    }

    reply->deleteLater();
    return registers;
}

QVector<quint16> readHoldingRegistersOnBus(const QJsonObject &serialConfig,
                                           int slaveId,
                                           int startAddress,
                                           int registerCount,
                                           QString *errorMessage)
{
    SharedRtuBus *bus = busForPort(serialConfig);
    if (!bus) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("rtu-no-port");
        }
        return {};
    }

    QMutexLocker locker(&bus->mutex);
    QModbusRtuSerialMaster *client = ensureRtuClient(bus, serialConfig);
    if (!client) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("rtu-disconnected");
        }
        return {};
    }

    QThread::msleep(30);
    return readHoldingRegisters(client, slaveId, startAddress, registerCount, errorMessage);
}

QVector<quint16> mergeExTh01Registers(quint16 slaveId,
                                      const QVector<quint16> &addressReg,
                                      const QVector<quint16> &statusReg,
                                      const QVector<quint16> &dataRegs)
{
    if (statusReg.isEmpty() || dataRegs.size() < 5) {
        return {};
    }

    QVector<quint16> merged(7);
    merged[0] = addressReg.isEmpty() ? slaveId : addressReg.at(0);
    merged[1] = statusReg.at(0);
    for (int i = 0; i < 5; ++i) {
        merged[2 + i] = dataRegs.at(i);
    }
    return merged;
}

QVector<quint16> readExTh01Registers(const QJsonObject &serialConfig, int slaveId, QString *errorMessage)
{
    QString lastError;
    for (const int startAddress : {1, 0}) {
        const QVector<quint16> block = readHoldingRegistersOnBus(serialConfig, slaveId, startAddress, 7, &lastError);
        if (block.size() >= 7) {
            return block;
        }
    }

    QString splitError;
    for (const int baseAddress : {1, 0}) {
        const QVector<quint16> addressReg = readHoldingRegistersOnBus(serialConfig, slaveId, baseAddress, 1, &splitError);
        const QVector<quint16> statusReg = readHoldingRegistersOnBus(serialConfig, slaveId, baseAddress + 1, 1, &splitError);
        const QVector<quint16> dataRegs = readHoldingRegistersOnBus(serialConfig, slaveId, baseAddress + 2, 5, &splitError);
        const QVector<quint16> merged = mergeExTh01Registers(static_cast<quint16>(slaveId), addressReg, statusReg, dataRegs);
        if (!merged.isEmpty()) {
            return merged;
        }
    }

    if (errorMessage) {
        *errorMessage = lastError.isEmpty() ? splitError : lastError;
        if (errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("rtu-read-failed");
        }
    }
    return {};
}
}

void ProtocolCollector::resetSharedRtuBuses()
{
    for (SharedRtuBus *bus : g_rtuBuses) {
        if (!bus) {
            continue;
        }
        if (bus->client) {
            if (bus->client->state() != QModbusDevice::UnconnectedState) {
                bus->client->disconnectDevice();
            }
            delete bus->client;
            bus->client = nullptr;
        }
        delete bus;
    }
    g_rtuBuses.clear();
}

void ProtocolCollector::start()
{
    const QString deviceId = m_deviceConfig.value(QStringLiteral("id")).toString();
    const int phaseMs = static_cast<int>(qHash(deviceId) % 300U);
    QTimer::singleShot(phaseMs, this, [this]() {
        poll();
        m_timer.start();
    });
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

    if (useExTh01Template()) {
        return {
            {QStringLiteral("device_address"), QStringLiteral("设备地址"), QStringLiteral(""), 3.0, QStringLiteral("good"), now},
            {QStringLiteral("alarm_status"), QStringLiteral("报警器状态"), QStringLiteral(""), 0.0, QStringLiteral("good"), now},
            {QStringLiteral("concentration"), QStringLiteral("浓度实时值"), QStringLiteral("PPM"), 12.5 + jitter(0.0, 3.0), QStringLiteral("good"), now},
            {QStringLiteral("precision"), QStringLiteral("精度"), QStringLiteral(""), 1.0, QStringLiteral("good"), now},
            {QStringLiteral("gas_type"), QStringLiteral("气体类型"), QStringLiteral(""), 0.0, QStringLiteral("good"), now}
        };
    }

    if (useWaterDetectTemplate()) {
        const bool alarm = m_deviceConfig.value(QStringLiteral("simulateWaterAlarm")).toBool(false);
        const double value = alarm ? 257.0 : 0.0;
        return {
            {QStringLiteral("water_level"), QStringLiteral("水侵检测"), QStringLiteral(""), value, QStringLiteral("good"), now}
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

    const int slaveId = m_deviceConfig.value(QStringLiteral("slaveId")).toInt(1);
    const int startAddress = m_deviceConfig.value(QStringLiteral("startAddress")).toInt(0);
    const int registerCount = m_deviceConfig.value(QStringLiteral("registerCount")).toInt(2);

    QString readError;
    QVector<quint16> registers;
    if (useExTh01Template()) {
        registers = readExTh01Registers(serial, slaveId, &readError);
    } else {
        registers = readHoldingRegistersOnBus(serial, slaveId, startAddress, registerCount, &readError);
    }

    if (registers.isEmpty()) {
        return fallbackPoints(readError.isEmpty() ? QStringLiteral("rtu-read-failed") : readError);
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (useZhQ006Template()) {
        return decodeZhQ006Points(registers, now);
    }
    if (useExTh01Template()) {
        return decodeExTh01Points(registers, now);
    }
    if (useWaterDetectTemplate()) {
        return decodeWaterDetectPoints(registers, now);
    }

    const double temperature = registers.size() > 0 ? registers.at(0) / 10.0 : 0.0;
    const double humidity = registers.size() > 1 ? registers.at(1) / 10.0 : 0.0;
    return {
        {QStringLiteral("temperature"), QStringLiteral("温度"), QStringLiteral("℃"), temperature, QStringLiteral("good"), now},
        {QStringLiteral("humidity"), QStringLiteral("湿度"), QStringLiteral("%RH"), humidity, QStringLiteral("good"), now}
    };
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
        QVector<quint16> registers;
        registers.reserve(static_cast<int>(unit.valueCount()));
        for (uint i = 0; i < unit.valueCount(); ++i) {
            registers.append(unit.value(i));
        }
        
        if (useZhQ006Template()) {
            points = decodeZhQ006Points(registers, now);
        } else if (useExTh01Template()) {
            points = decodeExTh01Points(registers, now);
        } else if (useWaterDetectTemplate()) {
            points = decodeWaterDetectPoints(registers, now);
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

namespace {
void controlRelay(const QString &relayId, const QString &action)
{
    const QString cmd = QStringLiteral("%1 %2\n").arg(relayId).arg(action == QStringLiteral("on") ? 1 : 0);
    QFile f(QStringLiteral("/proc/proembed/gpio"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(cmd.toUtf8());
        f.close();
    }
}
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

    const DataPoint *co2Point = findPoint(points, QStringLiteral("co2"));
    static bool co2RelayClosed = false;
    if (co2Point && co2Point->value > 800.0) {
        if (!co2RelayClosed) {
            controlRelay(QStringLiteral("500"), QStringLiteral("off"));
            co2RelayClosed = true;
        }
    } else {
        co2RelayClosed = false;
    }

    if (deviceId == QStringLiteral("DEV-2005")) {
        static bool waterRelayClosed = false;
        const DataPoint *waterPoint = points.isEmpty() ? nullptr : &points.first();
        if (waterPoint && static_cast<int>(waterPoint->value) > 256) {
            if (!waterRelayClosed) {
                controlRelay(QStringLiteral("500"), QStringLiteral("off"));
                waterRelayClosed = true;
            }
        } else {
            waterRelayClosed = false;
        }
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
    const QString deviceId = m_deviceConfig.value(QStringLiteral("id")).toString();
    return templateName.contains(QStringLiteral("ZH-Q006"), Qt::CaseInsensitive)
        || deviceId == QStringLiteral("DEV-2001");
}

bool ProtocolCollector::useExTh01Template() const
{
    const QString templateName = m_deviceConfig.value(QStringLiteral("templateName")).toString();
    return templateName.contains(QStringLiteral("EX-TH-01"), Qt::CaseInsensitive)
        || m_deviceConfig.value(QStringLiteral("id")).toString() == QStringLiteral("DEV-2004");
}

bool ProtocolCollector::useWaterDetectTemplate() const
{
    const QString templateName = m_deviceConfig.value(QStringLiteral("templateName")).toString();
    return templateName.contains(QStringLiteral("WATER-DETECT"), Qt::CaseInsensitive)
        || m_deviceConfig.value(QStringLiteral("id")).toString() == QStringLiteral("DEV-2005");
}

namespace {
QString exTh01UnitText(quint16 code)
{
    switch (code) {
    case 0: return QStringLiteral("%VOL");
    case 1: return QStringLiteral("%LEL");
    case 2: return QStringLiteral("PPM");
    default: return QStringLiteral("");
    }
}

double exTh01PrecisionDivisor(quint16 code)
{
    switch (code) {
    case 1: return 10.0;
    case 2: return 100.0;
    case 3: return 1000.0;
    default: return 1.0;
    }
}
}

QList<DataPoint> ProtocolCollector::decodeExTh01Points(const QVector<quint16> &registers, const QDateTime &timestamp) const
{
    auto readAt = [&registers](int index) -> quint16 {
        return (index >= 0 && index < registers.size()) ? registers.at(index) : 0;
    };

    const quint16 highReg = readAt(2);
    const quint16 lowReg = readAt(3);
    quint32 rawConcentration = 0;
    if (highReg <= 0xFF) {
        rawConcentration = static_cast<quint32>(highReg) * 256U + static_cast<quint32>(lowReg & 0xFF);
    } else {
        rawConcentration = (static_cast<quint32>(highReg) << 16) | static_cast<quint32>(lowReg);
    }
    const quint16 precisionCode = readAt(4);
    const double concentration = static_cast<double>(rawConcentration) / exTh01PrecisionDivisor(precisionCode);

    return {
        {QStringLiteral("device_address"), QStringLiteral("设备地址"), QStringLiteral(""), static_cast<double>(readAt(0)), QStringLiteral("good"), timestamp},
        {QStringLiteral("alarm_status"), QStringLiteral("报警器状态"), QStringLiteral(""), static_cast<double>(readAt(1)), QStringLiteral("good"), timestamp},
        {QStringLiteral("concentration"), QStringLiteral("浓度实时值"), exTh01UnitText(readAt(5)), concentration, QStringLiteral("good"), timestamp},
        {QStringLiteral("precision"), QStringLiteral("精度"), QStringLiteral(""), static_cast<double>(precisionCode), QStringLiteral("good"), timestamp},
        {QStringLiteral("gas_type"), QStringLiteral("气体类型"), QStringLiteral(""), static_cast<double>(readAt(6)), QStringLiteral("good"), timestamp}
    };
}

QList<DataPoint> ProtocolCollector::decodeWaterDetectPoints(const QVector<quint16> &registers, const QDateTime &timestamp) const
{
    auto readAt = [&registers](int index) -> quint16 {
        return (index >= 0 && index < registers.size()) ? registers.at(index) : 0;
    };

    return {
        {QStringLiteral("water_level"), QStringLiteral("水侵检测"), QStringLiteral(""), static_cast<double>(readAt(0)), QStringLiteral("good"), timestamp}
    };
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
