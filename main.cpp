#include "mainwindow.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFont>
#include <QDebug>
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    int fontId = QFontDatabase::addApplicationFont("/userdata/ywy_test/fonts/NotoSansCJK-Regular.ttc");
    if (fontId < 0) {
        fontId = QFontDatabase::addApplicationFont("/userdata/ywy_test/fonts/msyh.ttc");
    }

    if (fontId >= 0) {
        QString family = QFontDatabase::applicationFontFamilies(fontId).at(0);
        qDebug() << "load font:" << family;
        a.setFont(QFont(family, 12));
    } else {
        qDebug() << "Chinese font load failed";
    }
    MainWindow w;
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.showFullScreen();
    return a.exec();
}
