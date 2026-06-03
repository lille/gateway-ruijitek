#include "dtuclientservice.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QVector>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QModbusDataUnit>
#include <QModbusDevice>
#include <QModbusReply>
#include <QModbusRtuSerialMaster>
#include <QRandomGenerator>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTcpSocket>

namespace {
double dtuJitter(double min, double max)
{
    return min + (max - min) * QRandomGenerator::global()->generateDouble();
}
}

DtuClientService::DtuClientService(const QString &configPath, QObject *parent)
    : QObject(parent)
    , m_configPath(configPath)
{
    connect(&m_timer, &QTimer::timeout, this, &DtuClientService::pollAndForward);
}

bool DtuClientService::load(QString *errorMessage)
{
    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open config: %1").arg(m_configPath);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid DTU demo config: %1").arg(parseError.errorString());
        }
        return false;
    }

    m_config = document.object();
    m_timer.setInterval(m_config.value(QStringLiteral("intervalMs")).toInt(1000));
    return true;
}

void DtuClientService::start()
{
    pollAndForward();
    m_timer.start();
}

QString DtuClientService::summary() const
{
    const QJsonObject server = m_config.value(QStringLiteral("server")).toObject();
    const QJsonObject serial = m_config.value(QStringLiteral("serial")).toObject();
    return QStringLiteral("device=%1, target=%2:%3, serial=%4, baud=%5, slave=%6, register=%7+%8")
        .arg(m_config.value(QStringLiteral("deviceId")).toString(QStringLiteral("DEV-DTU-01")))
        .arg(server.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1")))
        .arg(server.value(QStringLiteral("port")).toInt(15020))
        .arg(resolveSerialPortName(serial))
        .arg(serial.value(QStringLiteral("baudRate")).toInt(9600))
        .arg(m_config.value(QStringLiteral("slaveId")).toInt(1))
        .arg(m_config.value(QStringLiteral("startAddress")).toInt(0))
        .arg(m_config.value(QStringLiteral("registerCount")).toInt(2));
}

void DtuClientService::pollAndForward()
{
    const QJsonObject values = readSensorValues();
    QJsonObject payload;
    payload.insert(QStringLiteral("gatewayId"), m_config.value(QStringLiteral("gatewayId")).toString(QStringLiteral("GW-DTU-01")));
    payload.insert(QStringLiteral("gatewayName"), m_config.value(QStringLiteral("gatewayName")).toString(QStringLiteral("DTU 透传终端")));
    payload.insert(QStringLiteral("deviceId"), m_config.value(QStringLiteral("deviceId")).toString(QStringLiteral("DEV-DTU-01")));
    payload.insert(QStringLiteral("deviceName"), m_config.value(QStringLiteral("deviceName")).toString(QStringLiteral("RS485 温湿度传感器")));
    payload.insert(QStringLiteral("location"), m_config.value(QStringLiteral("location")).toString(QStringLiteral("RK3568 现场终端")));
    payload.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));
    payload.insert(QStringLiteral("measurements"), values);
    payload.insert(QStringLiteral("temperature"), values.value(QStringLiteral("temperature")).toDouble());
    payload.insert(QStringLiteral("humidity"), values.value(QStringLiteral("humidity")).toDouble());
    payload.insert(QStringLiteral("pm25"), values.value(QStringLiteral("pm25")).toDouble());
    payload.insert(QStringLiteral("pm10"), values.value(QStringLiteral("pm10")).toDouble());
    payload.insert(QStringLiteral("pm1_0"), values.value(QStringLiteral("pm1_0")).toDouble());
    payload.insert(QStringLiteral("tvoc"), values.value(QStringLiteral("tvoc")).toDouble());
    payload.insert(QStringLiteral("co2"), values.value(QStringLiteral("co2")).toDouble());
    payload.insert(QStringLiteral("formaldehyde"), values.value(QStringLiteral("formaldehyde")).toDouble());
    payload.insert(QStringLiteral("sf6"), values.value(QStringLiteral("sf6")).toDouble());

    const QJsonObject server = m_config.value(QStringLiteral("server")).toObject();
    QTcpSocket socket;
    socket.connectToHost(server.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1")),
                         static_cast<quint16>(server.value(QStringLiteral("port")).toInt(15020)));
    if (!socket.waitForConnected(800)) {
        return;
    }

    socket.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    socket.waitForBytesWritten(500);
    socket.disconnectFromHost();
}

QJsonObject DtuClientService::readSensorValues()
{
    const QString mode = m_config.value(QStringLiteral("mode")).toString(QStringLiteral("simulate")).toLower();
    if (mode == QStringLiteral("modbus-rtu")) {
        return readModbusRtuValues();
    }
    return simulateValues();
}

QJsonObject DtuClientService::simulateValues() const
{
    const double baseTemp = m_config.value(QStringLiteral("baseTemperature")).toDouble(25.0);
    const double baseHumidity = m_config.value(QStringLiteral("baseHumidity")).toDouble(60.0);
    QJsonObject values;
    values.insert(QStringLiteral("formaldehyde"), 12 + dtuJitter(0.0, 6.0));
    values.insert(QStringLiteral("pm25"), 18 + dtuJitter(0.0, 8.0));
    values.insert(QStringLiteral("tvoc"), 55 + dtuJitter(0.0, 20.0));
    values.insert(QStringLiteral("co2"), 520 + dtuJitter(0.0, 120.0));
    values.insert(QStringLiteral("temperature"), baseTemp + dtuJitter(0.0, 2.5));
    values.insert(QStringLiteral("humidity"), baseHumidity + dtuJitter(0.0, 5.0));
    values.insert(QStringLiteral("pm1_0"), 12 + dtuJitter(0.0, 5.0));
    values.insert(QStringLiteral("pm10"), 26 + dtuJitter(0.0, 10.0));
    values.insert(QStringLiteral("sf6"), dtuJitter(0.0, 0.02));
    return values;
}

QJsonObject DtuClientService::readModbusRtuValues()
{
    const QJsonObject serial = m_config.value(QStringLiteral("serial")).toObject();
    if (serial.isEmpty() || serial.value(QStringLiteral("simulate")).toBool(false)) {
        return simulateValues();
    }

    if (!m_modbusClient) {
        QModbusRtuSerialMaster *client = new QModbusRtuSerialMaster(this);
        client->setConnectionParameter(QModbusDevice::SerialPortNameParameter, resolveSerialPortName(serial));
        client->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, serial.value(QStringLiteral("baudRate")).toInt(9600));
        client->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, serial.value(QStringLiteral("dataBits")).toInt(8));
        client->setConnectionParameter(QModbusDevice::SerialStopBitsParameter, serial.value(QStringLiteral("stopBits")).toInt(1) == 2
                                       ? QSerialPort::TwoStop : QSerialPort::OneStop);
        client->setConnectionParameter(QModbusDevice::SerialParityParameter, serial.value(QStringLiteral("parity")).toString(QStringLiteral("N")) == QStringLiteral("E")
                                       ? QSerialPort::EvenParity : QSerialPort::NoParity);
        client->setTimeout(1000);
        client->setNumberOfRetries(1);
        m_modbusClient = client;
    }

    if (m_modbusClient->state() != QModbusDevice::ConnectedState && !m_modbusClient->connectDevice()) {
        return simulateValues();
    }

    const int slaveId = m_config.value(QStringLiteral("slaveId")).toInt(1);
    const int startAddress = m_config.value(QStringLiteral("startAddress")).toInt(0);
    const int registerCount = m_config.value(QStringLiteral("registerCount")).toInt(2);
    QModbusReply *reply = m_modbusClient->sendReadRequest(QModbusDataUnit(QModbusDataUnit::HoldingRegisters, startAddress, registerCount), slaveId);
    if (!reply) {
        return simulateValues();
    }

    QEventLoop loop;
    connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QJsonObject values;
    if (reply->error() == QModbusDevice::NoError) {
        const QModbusDataUnit unit = reply->result();
        QVector<quint16> registers;
        registers.reserve(static_cast<int>(unit.valueCount()));
        for (uint i = 0; i < unit.valueCount(); ++i) {
            registers.append(unit.value(i));
        }
        values = decodeZhQ006Registers(registers);
    } else {
        values = simulateValues();
    }

    reply->deleteLater();
    return values;
}

QString DtuClientService::resolveSerialPortName(const QJsonObject &serialConfig) const
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

QJsonObject DtuClientService::decodeZhQ006Registers(const QVector<quint16> &registers) const
{
    auto readAt = [&registers](int index) -> quint16 {
        return (index >= 0 && index < registers.size()) ? registers.at(index) : 0;
    };

    QJsonObject values;
    values.insert(QStringLiteral("formaldehyde"), static_cast<double>(readAt(0)));
    values.insert(QStringLiteral("pm25"), static_cast<double>(readAt(1)));
    values.insert(QStringLiteral("tvoc"), static_cast<double>(readAt(2)));
    values.insert(QStringLiteral("co2"), static_cast<double>(readAt(3)));
    values.insert(QStringLiteral("temperature"), (static_cast<double>(readAt(4)) - 500.0) / 10.0);
    values.insert(QStringLiteral("humidity"), static_cast<double>(readAt(5)) / 10.0);
    values.insert(QStringLiteral("pm1_0"), static_cast<double>(readAt(6)));
    values.insert(QStringLiteral("pm10"), static_cast<double>(readAt(7)));
    values.insert(QStringLiteral("sf6"), static_cast<double>(readAt(8)) / 1000.0);
    return values;
}
