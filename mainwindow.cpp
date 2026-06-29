#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QDialog>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include "rknnworker.h"
#include "streamserver.h"
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    QTimer *heartbeat = new QTimer(this);
    connect(heartbeat, &QTimer::timeout, this, []() {
        qDebug() << "MAIN HEARTBEAT";
    });
    heartbeat->start(1000);

    // 1. 设置整体工业风格和窗口初始大小
    this->setStyleSheet("background-color: #1A1A1D; color: white; "
                        "QPushButton { font-size: 16px; font-weight: bold; padding: 5px; }");
    this->resize(1024, 600);

    // 2. 关键修正：限制 label_video 的缩放行为，防止其无限变大
    ui->label_video->setMinimumSize(640, 480);
    ui->label_video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->label_video->setAlignment(Qt::AlignCenter);
    ui->label_video->setStyleSheet("background-color: #222; border: 1px solid #444;");
    ui->label_video->setText("等待摄像头数据...");

    QThread *workerThread = new QThread(this);
    worker = new rknnworker();
    worker->moveToThread(workerThread);
    connect(workerThread, &QThread::started, this, [=]() {
        qDebug() << "Worker thread started, delay NPU init 5s";

        QTimer::singleShot(5000, this, [=]() {
            qDebug() << "Delay finished, invoke initRknn";
            QMetaObject::invokeMethod(worker, "initRknn", Qt::QueuedConnection);
        });
    });

    m_gimbal = nullptr;

    //new 一个云台控制器
   // m_gimbal = new GimbalController(this);
    //connect(worker,&rknnworker::requestGimbalMove,m_gimbal,&GimbalController::onGimbalMoveRequested);

    // 3. 图像渲染槽：修复语法错误,加入实时动态画框功能
    connect(worker, &rknnworker::frameReady, this, [=](QImage img){
        if (img.isNull()) return;

        //拖拽鼠标，直接在UI层面实时绘制临时框
        if(m_isDrawing)
        {
            QPainter painter(&img);
            painter.setPen(QPen(Qt::yellow,3,Qt::DashLine));

            QPoint currentPos = ui->label_video->mapFromGlobal(QCursor::pos());
            QRect tempRect = QRect(m_startPoint,currentPos).normalized();

            float scale_x = 640.0f / ui->label_video->width();
            float scale_y = 480.0f / ui->label_video->height();
            QRect mappedRect(tempRect.x() * scale_x,tempRect.y() * scale_y,
                             tempRect.width() * scale_x,tempRect.height() * scale_y);
            painter.drawRect(mappedRect);
        }

        ui->label_video->setPixmap(QPixmap::fromImage(img).scaled(
            ui->label_video->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
    });

    connect(worker, &rknnworker::dataResult, this, &MainWindow::updateStatistics);
    connect(worker, &rknnworker::alarmLog, this, &MainWindow::updateAlarmLog);
    connect(worker, &rknnworker::initComplete, this, [=](){
        if(!worker->isCtxValid()){
            qDebug() << "RKNN ctx invalid, pipeline not started";
            ui->textBrowser->append("NPU 初始化失败，请检查模型或硬件。");
            return;
        }

        qDebug() << "NPU init complete, start stream server and pipeline";

        StreamServer *server = new StreamServer(this);
        server->startServer(8000);
        connect(worker, &rknnworker::streamFrameReady,
                server, &StreamServer::broadcastFrame,
                Qt::QueuedConnection);

        qDebug() << "Init gimbal after NPU success";

        if (!m_gimbal) {
            m_gimbal = new GimbalController(this);

            bool ok = connect(worker, &rknnworker::requestGimbalMove,
                              m_gimbal, &GimbalController::onGimbalMoveRequested,
                              Qt::QueuedConnection);

            qDebug() << "gimbal connect ok =" << ok;

            QTimer::singleShot(2000, this, [=]() {
                qDebug() << "TEST GIMBAL MOVE pan";
                m_gimbal->onGimbalMoveRequested(5000, 0);
            });

            QTimer::singleShot(4000, this, [=]() {
                qDebug() << "TEST GIMBAL MOVE tilt";
                m_gimbal->onGimbalMoveRequested(0, 3000);
            });
        }

        worker->process();
    });
    //connect(worker, &rknnworker::initComplete, worker, &rknnworker::process, Qt::QueuedConnection);

    //启动服务器代码
   /* StreamServer *server = new StreamServer(this);
    server->startServer(8000);
    connect(worker,&rknnworker::streamFrameReady,server,&StreamServer::broadcastFrame,Qt::QueuedConnection);
   */
    /*给视频窗口（label_video）安装事件监听器。
     只有在视频窗口上发生的操作，才会触发 eventFilter
     */
   // ui->label_video->installEventFilter(this);
    workerThread->start();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_pushButton_clicked()
{

    ui->pushButton->setStyleSheet("background-color: #AA0000; color: white; font-weight: bold; font-size: 16px;");
    m_isDetecting = !m_isDetecting;
    if (m_isDetecting) {
        ui->pushButton->setText("停止监测");
        ui->pushButton->setStyleSheet("background-color: #AA0000; color: white; font-weight: bold;");
        ui->textBrowser->append("▶️ 系统：AI 监测已开启...");
    } else {
        ui->pushButton->setText("开启监测");
        ui->pushButton->setStyleSheet("");
        ui->textBrowser->append("⏹️ 系统：监测已暂停");
    }


    worker->setDetecting(m_isDetecting);
}

void MainWindow::updateStatistics(int safe, int alarm)
{
    ui->label->setText(QString("安全：%1人").arg(safe));
    ui->label_2->setText(QString("违规：%1人").arg(alarm));
    if (alarm > 0)
        ui->label_2->setStyleSheet("color: #FF3B3F; font-weight: bold;");
    else
        ui->label_2->setStyleSheet("color: #E0E0E0;");
}

void MainWindow::updateAlarmLog(QString msg)
{
    QString timeStr = QDateTime::currentDateTime().toString("[hh:mm:ss] ");
    ui->textBrowser->append("<font color=\"#FF3B3F\">" + timeStr + msg + "</font>");
}

// mainwindow.cpp
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == ui->label_video)
    {
        // 1. 鼠标按下：记录起点
        if(event->type() == QEvent::MouseButtonPress && m_isSettingRect)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            m_startPoint = mouseEvent->pos();
            m_isDrawing = true;
            return true;
        }
        // 2. 鼠标拖拽移动：动态画框
        else if(event->type() == QEvent::MouseMove && m_isDrawing)
        {
            return true;
        }
        // 3. 鼠标松开：区域锁定
        else if(event->type() == QEvent::MouseButtonRelease && m_isDrawing)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            m_isDrawing = false;
            m_isSettingRect = false;
            ui->label_video->setCursor(Qt::ArrowCursor);

            QRect finalRect = QRect(m_startPoint, mouseEvent->pos()).normalized();

            // 【核心】：同样需要坐标映射
            float scale_x = 640.0f / ui->label_video->width();
            float scale_y = 480.0f / ui->label_video->height();
            m_alarmRect = QRect(finalRect.x() * scale_x, finalRect.y() * scale_y,
                                finalRect.width() * scale_x, finalRect.height() * scale_y);

            QMetaObject::invokeMethod(worker, "setAlarmRect", Q_ARG(QRect, m_alarmRect));
            ui->textBrowser->append("✅ 警戒区域设定成功！AI 将只在此区域内预警。");
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}


void MainWindow::on_pushButton_2_clicked()
{
    // 弹出一个独立对话框
    QDialog dialog(this);
    dialog.setWindowTitle("目标检测筛选设置");
    dialog.setFixedSize(350, 250); // 设置弹窗大小
    // 保持工业风 UI
    dialog.setStyleSheet("background-color: #2B2B36; color: white;");

    QVBoxLayout layout(&dialog);

    // 创建复选框
    QCheckBox cbHelmet("检测安全帽 (包含未戴警报)", &dialog);
    QCheckBox cbMask("检测口罩 (包含未戴警报)", &dialog);
    QCheckBox cbVest("检测反光衣 (包含未穿警报)", &dialog);

    QString cbStyle = "QCheckBox { font-size: 16px; padding: 10px; }";
    cbHelmet.setStyleSheet(cbStyle);
    cbMask.setStyleSheet(cbStyle);
    cbVest.setStyleSheet(cbStyle);

    // 从 worker 中读取当前的勾选状态
    cbHelmet.setChecked(worker->isCheckHelmet());
    cbMask.setChecked(worker->isCheckMask());
    cbVest.setChecked(worker->isCheckVest());

    // 创建确认按钮
    QPushButton btnOk("保存并应用", &dialog);
    btnOk.setStyleSheet("QPushButton { background-color: #00A8E8; color: white; font-size: 16px; font-weight: bold; padding: 10px; border-radius: 5px; } "
                        "QPushButton:pressed { background-color: #007bb5; }");

    // 添加到布局
    layout.addWidget(&cbHelmet);
    layout.addWidget(&cbMask);
    layout.addWidget(&cbVest);
    layout.addStretch(); // 加点弹性空间
    layout.addWidget(&btnOk);

    // 绑定按钮点击事件到弹窗关闭
    connect(&btnOk, &QPushButton::clicked, &dialog, &QDialog::accept);

    // 阻塞式运行弹窗，如果用户点击了“保存”
    if (dialog.exec() == QDialog::Accepted) {
        // 将用户的勾选结果发送给底层 AI 线程
        worker->setTargetFilter(cbHelmet.isChecked(), cbMask.isChecked(), cbVest.isChecked());

        // 在日志面板输出一下提示
        ui->textBrowser->append("⚙️ 系统：检测目标配置已更新。");
    }
}
void MainWindow::on_pushButton_3_clicked()
{
    ui->pushButton_3->setStyleSheet("background-color: #AA0000; color: white; font-weight: bold; font-size: 16px;");
    ui->textBrowser->clear();
    ui->label->setText("安全：0人");
    ui->label_2->setText("违规：0人");
    ui->textBrowser->append("🔄 系统统计已重置。");
}
