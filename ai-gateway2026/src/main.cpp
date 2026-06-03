#include "appconfig.h"
#include "gatewayservice.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

static QString findConfigNearExecutable(const QString &appDir)
{
    QDir dir(appDir);
    for (int depth = 0; depth < 8; ++depth) {
        const QString candidate = dir.filePath(QStringLiteral("config/gateway-demo.json"));
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }

        if (!dir.cdUp()) {
            break;
        }
    }

    return QString();
}

static QString resolveConfigPath(const QStringList &arguments)
{
    if (arguments.size() > 1) {
        return arguments.at(1);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cwd = QDir::currentPath();
    const QString discoveredFromAppDir = findConfigNearExecutable(appDir);
    if (!discoveredFromAppDir.isEmpty()) {
        return discoveredFromAppDir;
    }

    const QStringList candidates = {
        QDir(cwd).filePath(QStringLiteral("config/gateway-demo.json")),
        QDir(appDir).filePath(QStringLiteral("config/gateway-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../config/gateway-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../config/gateway-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../../config/gateway-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../../../ai-gateway2026/config/gateway-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../../../../ai-gateway2026/config/gateway-demo.json"))
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return QDir(cwd).filePath(QStringLiteral("config/gateway-demo.json"));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("gateway-demo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    const QString configPath = resolveConfigPath(app.arguments());

    QString errorMessage;
    const AppConfig config = AppConfig::load(configPath, &errorMessage);
    if (!config.isValid()) {
        qCritical().noquote() << errorMessage;
        return 1;
    }

    GatewayService service(config);
    if (!service.start(&errorMessage)) {
        qCritical().noquote() << errorMessage;
        return 2;
    }

    qInfo().noquote() << "Gateway demo started.";
    qInfo().noquote() << "HTTP API:" << QStringLiteral("http://127.0.0.1:%1/api/dashboard").arg(config.httpPort);
    qInfo().noquote() << "Web config:" << QStringLiteral("http://127.0.0.1:%1/").arg(config.httpPort);
    qInfo().noquote() << "DTU passthrough port:" << config.dtuListenPort;
    return app.exec();
}
