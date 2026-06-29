#include "clientworker.h"
#include <QDebug>

ClientWorker::ClientWorker(qintptr socketDescriptor, QObject *parent)
    : QObject(parent), m_socketDescriptor(socketDescriptor) {}

ClientWorker::~ClientWorker() {
    if (m_socket) m_socket->deleteLater();
}

void ClientWorker::startWork() {
    m_socket = new QTcpSocket();
    if (!m_socket->setSocketDescriptor(m_socketDescriptor)) {
        emit finished();
        return;
    }

    // 当浏览器断开时，踩下队列的刹车，打破 sendLoop 里的 pop 阻塞
    connect(m_socket, &QTcpSocket::disconnected, this, [=](){
        m_frameQueue.stop();
    });

    /* 任务：拼接 HTTP/1.0 200 OK 且包含 boundary=myboundary 的头部，
    并写入 m_socket 发送出去 */
    QString header = "HTTP/1.0 200 OK\r\n"                     // 状态码 200，表示请求成功
                     "Connection: close\r\n"                   // TCP 连接状态
                     "Cache-Control: no-cache\r\n"             // 告诉浏览器不要缓存这些数据，保证实时性
                     "Content-Type: multipart/x-mixed-replace; boundary=myboundary\r\n\r\n";
    m_socket->write(header.toUtf8());
    // 握手成功后，立刻进入发送死循环
    sendLoop();
}

void ClientWorker::enqueueFrame(const QByteArray &jpegData) {
    m_frameQueue.push(jpegData); // 供大管家丢图片进来
}

void ClientWorker::sendLoop() {
    QByteArray jpegData;

    // 📢【核心空白 2：请在这里写下你的发送死循环】
    // 提示：
    // 1. 写一个 while 循环不停地从 m_frameQueue.pop(jpegData) 取图片
    // 2. 在循环内，拼装单张图片的头部（包含 --myboundary 和当前 jpegData.size()）
    // 3. 用 m_socket->write() 依次写入头部和图片数据，最后 flush()

    while(m_frameQueue.pop(jpegData))
    {

        // 准备存放这一帧数据的 "子头部" 信息
        QByteArray frameHeader;
        // 写入分割线，告诉浏览器："下面要开始一张新图片了"
        frameHeader.append("--myboundary\r\n");
        // 声明接下来的数据类型是一张 JPEG 图片
        frameHeader.append("Content-Type: image/jpeg\r\n");
        // 告诉浏览器这张图片的实际大小（字节数），\r\n\r\n 表示这个子头部结束，后面紧跟纯图像数据
        frameHeader.append("Content-Length: " + QByteArray::number(jpegData.size()) + "\r\n\r\n");

        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        {
            m_socket->write(frameHeader);

            m_socket->write(jpegData);

            // 发送换行符，代表这一张图片的数据彻底结束
            m_socket->write("\r\n");
            // 强制将底层缓冲区的数据立刻发送出去，避免数据在操作系统底层堆积导致视频流延迟
            m_socket->flush();
        }

    }



    // --- 善后工作 ---
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->close();
    }
    emit finished(); // 罢工通知
}
