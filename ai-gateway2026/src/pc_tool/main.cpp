#include "pcconfigwindow.h"

#include <QApplication>
#include <QFont>
#include <QLocale>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11));
    PcConfigWindow window;
    window.show();
    return app.exec();
}
