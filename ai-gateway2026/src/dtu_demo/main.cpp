#include "dtuclientservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

static QString resolveConfigPath(const QStringList &arguments)
{
    if (arguments.size() > 1) {
        return arguments.at(1);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().filePath(QStringLiteral("deploy/rk3568-dtu-package/config/rk3568-dtu-demo.json")),
        QDir::current().filePath(QStringLiteral("config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../deploy/rk3568-dtu-package/config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../../config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../../../ai-gateway2026/config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../../../ai-gateway2026/deploy/rk3568-dtu-package/config/rk3568-dtu-demo.json"))
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return QDir::current().filePath(QStringLiteral("config/rk3568-dtu-demo.json"));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString configPath = resolveConfigPath(app.arguments());

    QString errorMessage;
    DtuClientService service(configPath);
    if (!service.load(&errorMessage)) {
        qCritical().noquote() << errorMessage;
        return 1;
    }

    service.start();
    qInfo().noquote() << "RK3568 DTU terminal demo started with config:" << configPath;
    qInfo().noquote() << "Runtime summary:" << service.summary();
    return app.exec();
}
