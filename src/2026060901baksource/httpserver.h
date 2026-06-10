#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>
#include <QHostAddress>
#include <QTcpServer>

class DataStore;
class GatewayService;

class HttpServer : public QObject
{
    Q_OBJECT

public:
    explicit HttpServer(DataStore *store, GatewayService *service, QObject *parent = nullptr);

    bool start(const QHostAddress &address, quint16 port);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QByteArray buildResponse(const QByteArray &requestData) const;
    QByteArray okJson(const QByteArray &json) const;
    QByteArray okHtml(const QByteArray &html) const;
    QByteArray badRequest(const QByteArray &message) const;
    QByteArray notFound() const;
    QByteArray configPageHtml() const;
    QByteArray relayControlHtml() const;
    QString queryValue(const QString &path, const QString &key) const;

    DataStore *m_store = nullptr;
    GatewayService *m_service = nullptr;
    QTcpServer m_server;
};

#endif // HTTPSERVER_H
