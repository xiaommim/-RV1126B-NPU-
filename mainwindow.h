#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "rknnworker.h"
#include "gimbalcontroller.h"
namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void on_pushButton_clicked();
    void updateStatistics(int safe, int alarm);// 更新人数标签
    void updateAlarmLog(QString msg); // 更新报警日志
    bool eventFilter(QObject *obj, QEvent *event) override;

    void on_pushButton_2_clicked();

    void on_pushButton_3_clicked();

private:
    Ui::MainWindow *ui;
    bool m_isDetecting = false;
    rknnworker *worker;
    QRect m_alarmRect;   // 存储报警区域
    bool m_isDrawing = false;// 是否在画框
    QPoint m_startPoint;// 鼠标按下的起点
    bool m_isSettingRect = false;
    GimbalController *m_gimbal;

};

#endif // MAINWINDOW_H
