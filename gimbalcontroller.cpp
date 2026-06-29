#include <QFile>
#include <QDebug>
#include <QThread>
#include "gimbalcontroller.h"
#include "rknnworker.h"

// ===============================
// 云台 PWM 参数
// ===============================

// 水平轴：GPIO3_B4 -> pwmchip2/pwm0
static const QString PAN_PWM_CHIP = "/sys/class/pwm/pwmchip2";
static const QString PAN_PWM_PATH = "/sys/class/pwm/pwmchip2/pwm0";

// 垂直轴：GPIO3_B5 -> pwmchip3/pwm0
static const QString TILT_PWM_CHIP = "/sys/class/pwm/pwmchip3";
static const QString TILT_PWM_PATH = "/sys/class/pwm/pwmchip3/pwm0";

// 舵机周期：20ms
static const long SERVO_PERIOD_NS = 20000000;

// 舵机常用脉宽范围
static const long SERVO_MIN_NS = 500000;    // 0.5ms
static const long SERVO_MID_NS = 1500000;   // 1.5ms
static const long SERVO_MAX_NS = 2500000;   // 2.5ms

GimbalController::GimbalController(QObject *parent)
    : QObject{parent}
{
    // ===============================
    // 1. 申请 PWM 通道控制权
    // ===============================
    // 水平轴 GPIO3_B4 -> pwmchip2/pwm0
    writeSysfs(PAN_PWM_CHIP + "/export", "0");

    // 垂直轴 GPIO3_B5 -> pwmchip3/pwm0
    writeSysfs(TILT_PWM_CHIP + "/export", "0");

    QThread::msleep(50);

    // ===============================
    // 2. 先关闭 PWM，防止修改 period / polarity 时报错
    // ===============================
    writeSysfs(PAN_PWM_PATH + "/enable", "0");
    writeSysfs(TILT_PWM_PATH + "/enable", "0");

    // ===============================
    // 3. 设置 PWM 周期 20ms
    // ===============================
    writeSysfs(PAN_PWM_PATH + "/period", QString::number(SERVO_PERIOD_NS));
    writeSysfs(TILT_PWM_PATH + "/period", QString::number(SERVO_PERIOD_NS));

    // ===============================
    // 4. 设置极性
    // ===============================
    // 水平轴实测 polarity = normal，可以正常写
    writeSysfs(PAN_PWM_PATH + "/polarity", "normal");

    // 垂直轴实测 polarity = inverse，而且写 normal 会 Invalid argument
    // 所以这里不要强行改 polarity，保持驱动当前 inverse 状态
    // 后面通过 duty 反向换算解决
    // writeSysfs(TILT_PWM_PATH + "/polarity", "normal");

    // ===============================
    // 5. 初始化云台逻辑位置：居中
    // ===============================
    m_currentPanNs = SERVO_MID_NS;
    m_currentTiltNs = SERVO_MID_NS;

    // 水平轴 normal：直接写真实脉宽 1.5ms
    writeSysfs(PAN_PWM_PATH + "/duty_cycle",
               QString::number(m_currentPanNs));

    // 垂直轴 inverse：
    // 想让真实高电平为 1.5ms，就要写 20ms - 1.5ms = 18.5ms
    long tiltWriteNs = SERVO_PERIOD_NS - m_currentTiltNs;
    writeSysfs(TILT_PWM_PATH + "/duty_cycle",
               QString::number(tiltWriteNs));

    // ===============================
    // 6. 打开 PWM 输出
    // ===============================
    writeSysfs(PAN_PWM_PATH + "/enable", "1");
    writeSysfs(TILT_PWM_PATH + "/enable", "1");

    qDebug() << "云台初始化完成：";
    qDebug() << "水平轴 pwmchip2/pwm0 normal duty =" << m_currentPanNs;
        qDebug() << "垂直轴 pwmchip3/pwm0 inverse write duty =" << tiltWriteNs
             << "实际逻辑 duty =" << m_currentTiltNs;


}

void GimbalController::onGimbalMoveRequested(long pan_delta, long tilt_delta)
{
    // ===============================
    // 1. 水平轴 Pan 逻辑脉宽更新
    // ===============================
    m_currentPanNs = m_currentPanNs + pan_delta;


    if (m_currentPanNs > SERVO_MAX_NS)
        m_currentPanNs = SERVO_MAX_NS;

    if (m_currentPanNs < SERVO_MIN_NS)
        m_currentPanNs = SERVO_MIN_NS;


    // ===============================
    // 2. 垂直轴 Tilt 逻辑脉宽更新
    // ===============================
    m_currentTiltNs = m_currentTiltNs + tilt_delta;

    if (m_currentTiltNs > SERVO_MAX_NS)
        m_currentTiltNs = SERVO_MAX_NS;

    if (m_currentTiltNs < SERVO_MIN_NS)
        m_currentTiltNs = SERVO_MIN_NS;


    // ===============================
    // 3. 写入硬件
    // ===============================

    // 水平轴 normal：直接写真实脉宽
    writeSysfs(PAN_PWM_PATH + "/duty_cycle",
               QString::number(m_currentPanNs));

    // 垂直轴 inverse：反向换算
    // 例如：
    // 真实想要 1.0ms -> 写 19.0ms
    // 真实想要 1.5ms -> 写 18.5ms
    // 真实想要 2.0ms -> 写 18.0ms
    long tiltWriteNs = SERVO_PERIOD_NS - m_currentTiltNs;

    writeSysfs(TILT_PWM_PATH + "/duty_cycle",
               QString::number(tiltWriteNs));

    qDebug() << "云台移动："
             << "pan logic =" << m_currentPanNs
             << "tilt logic =" << m_currentTiltNs
             << "tilt write =" << tiltWriteNs;


}

bool GimbalController::writeSysfs(QString path, QString value)
{
    QFile file(path);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qDebug() << "节点打开失败:" << path << "写入值:" << value;
        return false;
    }

    qint64 ret = file.write(value.toUtf8());
    file.close();

    if (ret < 0)
    {
        qDebug() << "节点写入失败:" << path << "写入值:" << value;
        return false;
    }

    return true;

}
