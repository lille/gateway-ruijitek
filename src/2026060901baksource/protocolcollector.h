#ifndef PROTOCOLCOLLECTOR_H
#define PROTOCOLCOLLECTOR_H

#include "types.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QModbusClient;
class QSerialPort;
class DataStore;

class ProtocolCollector : public QObject
{
    Q_OBJECT

public:
    ProtocolCollector(const QJsonObject &gatewayConfig,
                      const QJsonObject &deviceConfig,
                      DataStore *store,
                      QObject *parent = nullptr);

    void start();

    static void resetSharedRtuBuses();

signals:
    void deviceSampled(const QString &deviceId);

private slots:
    void poll();

private:
    struct AlarmRule {
        QString id;
        QString name;
        double warningThreshold = 0.0;
        double criticalThreshold = 0.0;
        QString unit;
        int precision = 0;
    };

    QList<DataPoint> simulatePoints() const;
    QList<DataPoint> readModbusRtuPoints();
    QList<DataPoint> readModbusTcpPoints();
    QList<DataPoint> readTcpSocketPoints();
    QList<DataPoint> readSerialAsciiPoints();
    QList<DataPoint> fallbackPoints(const QString &quality) const;
    void publishPoints(const QList<DataPoint> &points);
    QList<DataPoint> decodeZhQ006Points(const QVector<quint16> &registers, const QDateTime &timestamp) const;
    QList<DataPoint> decodeExTh01Points(const QVector<quint16> &registers, const QDateTime &timestamp) const;
    QList<DataPoint> decodeWaterDetectPoints(const QVector<quint16> &registers, const QDateTime &timestamp) const;
    bool useZhQ006Template() const;
    bool useExTh01Template() const;
    bool useWaterDetectTemplate() const;
    QList<AlarmRule> currentAlarmRules() const;

    QJsonObject m_gatewayConfig;
    QJsonObject m_deviceConfig;
    DataStore *m_store = nullptr;
    QTimer m_timer;
    QPointer<QModbusClient> m_modbusClient;
    QPointer<QSerialPort> m_serialPort;
};

#endif // PROTOCOLCOLLECTOR_H
