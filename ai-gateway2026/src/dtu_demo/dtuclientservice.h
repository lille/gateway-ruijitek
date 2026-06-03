#ifndef DTUCLIENTSERVICE_H
#define DTUCLIENTSERVICE_H

#include <QObject>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QTimer>

class QModbusClient;

class DtuClientService : public QObject
{
    Q_OBJECT

public:
    explicit DtuClientService(const QString &configPath, QObject *parent = nullptr);

    bool load(QString *errorMessage = nullptr);
    void start();
    QString summary() const;

private slots:
    void pollAndForward();

private:
    QJsonObject readSensorValues();
    QJsonObject simulateValues() const;
    QJsonObject readModbusRtuValues();
    QString resolveSerialPortName(const QJsonObject &serialConfig) const;
    QJsonObject decodeZhQ006Registers(const QVector<quint16> &registers) const;

    QString m_configPath;
    QJsonObject m_config;
    QTimer m_timer;
    QPointer<QModbusClient> m_modbusClient;
};

#endif // DTUCLIENTSERVICE_H
