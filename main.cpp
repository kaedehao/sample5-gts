#include "mainwindow.h"
#include <QApplication>
#include "thread.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
//    qDebug()<<"From main thread: "<<Thread::currentThreadId();
//    qDebug()<<"a: "<<&a;
    MainWindow w;
    w.set_QApplication(&a);
    //w.setWindowState(Qt::WindowFullScreen);
    //w.setWindowState(w.windowState() ^ Qt::WindowFullScreen);
    w.show();

    return a.exec();
}

