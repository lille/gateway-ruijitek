#ifndef DTUPASSTHROUGHSERVER_H
#define DTUPASSTHROUGHSERVER_H

#include <QObject>
#include <QHostAddress>
#include <QTcpServer>

class DataStore;
class QTcpSocket;

class DtuPassthroughServer : public QObject
{
    Q_OBJECT

public:
    explicit DtuPassthroughServer(DataStore *store, QObject *parent = nullptr);

    bool start(const QHostAddress &address, quint16 port);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void handlePayload(QTcpSocket *socket, const QByteArray &payload);

    DataStore *m_store = nullptr;
    QTcpServer m_server;
};

#endif // DTUPASSTHROUGHSERVER_H
