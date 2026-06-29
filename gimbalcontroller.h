#ifndef GIMBALCONTROLLER_H
#define GIMBALCONTROLLER_H

#include <QObject>

class GimbalController : public QObject
{
    Q_OBJECT
public:
    explicit GimbalController(QObject *parent = nullptr);

signals:

public slots:
    void onGimbalMoveRequested(long pan_delta, long tilt_delta);

private:

    bool writeSysfs(QString path,QString value);
    long m_currentPanNs = 1500000;
    long m_currentTiltNs = 1500000;
};

#endif // GIMBALCONTROLLER_H
