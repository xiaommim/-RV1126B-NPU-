#ifndef STREAMSERVER_H
#define STREAMSERVER_H
#include "clientworker.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QByteArray>

class StreamServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit StreamServer(QObject *parent = nullptr);
    ~StreamServer();
    bool startServer(quint16 port);

public slots:
    void broadcastFrame(const QByteArray &jpegData);

private slots:

    void incomingConnection(qintptr socketDescriptor);

private:

    // 存储所有正在运行的推流客户端
    QList<ClientWorker*> m_workers;
};

#endif // STREAMSERVER_H
