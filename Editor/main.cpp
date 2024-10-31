#include "GnMainWindow.h"

#include <QApplication>

#ifdef QT_NEEDS_QMAIN
#define main qMain
#endif

int main(int argc, char **argv)
{
    QApplication a(argc, argv);
    GnMainWindow w;
    w.show();
    return a.exec();
}
