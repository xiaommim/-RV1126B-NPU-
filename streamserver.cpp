#include "streamserver.h"
#include "clientworker.h"
#include <QDebug>
#include <QThread>

StreamServer::StreamServer(QObject *parent) : QTcpServer(parent)
{
    // 干净的构造函数，不再绑定老槽函数
}

StreamServer::~StreamServer()
{
    // 大管家销毁时，名册列表由各自绑定的 deleteLater 自动处理，也可以在此进行强行清空
    m_workers.clear();
}

bool StreamServer::startServer(quint16 port)
{
    if (this->listen(QHostAddress::Any, port)) {
        qDebug() << "✅ 高性能多线程Web推流服务器已启动，监听端口:" << port;
        return true;
    }
    return false;
}

void StreamServer::broadcastFrame(const QByteArray &jpegData)
{
    // 一行 C++11 基于范围的循环，让数据秒扔秒走
    for(ClientWorker *w : m_workers)
    {
        w->enqueueFrame(jpegData);
    }
}

void StreamServer::incomingConnection(qintptr socketDescriptor)
{
    QThread *thread = new QThread();
    ClientWorker *worker = new ClientWorker(socketDescriptor);

    worker->moveToThread(thread);
    m_workers.append(worker);

    // 四连常规销毁回收连接
    connect(thread, &QThread::started, worker, &ClientWorker::startWork);
    connect(worker, &ClientWorker::finished, thread, &QThread::quit);
    connect(worker, &ClientWorker::finished, worker, &ClientWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    // 方案 A 资源清理锁：完美预防野指针崩溃
    connect(worker, &ClientWorker::finished, this, [=](){
        ClientWorker *disconnectedWorker = qobject_cast<ClientWorker*>(sender());
        if (disconnectedWorker) {
            m_workers.removeOne(disconnectedWorker);
            qDebug() << "客户端安全退出，移出名册。在线客户端数:" << m_workers.size();
        }
    });

    thread->start();
}
