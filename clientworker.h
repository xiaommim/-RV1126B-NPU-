#ifndef CLIENTWORKER_H
#define CLIENTWORKER_H

#include <QObject>
#include <QTcpSocket>
#include <QThread>
#include "rknnworker.h" // 引入我们之前提到的 DropQueue

class ClientWorker : public QObject
{
    Q_OBJECT
public:
    // 构造时，把网络套接字描述符（socketDescriptor）传进来
    explicit ClientWorker(qintptr socketDescriptor, QObject *parent = nullptr);
    ~ClientWorker();

signals:
    void finished(); // 当浏览器断开，通知外面销毁线程

public slots:
    void startWork();                     // 线程启动后的入口槽函数
    void enqueueFrame(const QByteArray &jpegData); // 专门供外部往这个客户端的私有队列里塞入最新一帧图片

private slots:
    void sendLoop(); // 内部的发送死循环

private:
    qintptr m_socketDescriptor;
    QTcpSocket *m_socket = nullptr;

    //进阶核心：每个客户端都有一个自己的专属“小垃圾桶队列”
    //容量设为 2，如果网络卡了发不完，新来的帧会自动把老帧顶掉（丢帧保护）
    DropQueue<QByteArray> m_frameQueue{2};
};
#endif // CLIENTWORKER_H
