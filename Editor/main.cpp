#include "GnMainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    GnMainWindow w;
    w.show();
    return a.exec();
}
