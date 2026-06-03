#include "pcconfigwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QFont>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
void prepareSpinBox(QSpinBox *spinBox, int minimumWidth = 180)
{
    QFont font(QStringLiteral("Microsoft YaHei UI"), 11);
    font.setStyleStrategy(QFont::PreferAntialias);
    spinBox->setFont(font);
    spinBox->setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    spinBox->setMinimumWidth(minimumWidth);
    spinBox->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    spinBox->setAlignment(Qt::AlignLeft);
    if (QLineEdit *edit = spinBox->findChild<QLineEdit *>()) {
        edit->setFont(font);
        edit->setAlignment(Qt::AlignLeft);
    }
}
}

PcConfigWindow::PcConfigWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("DTU 方案 PC 上位机配置工具"));
    resize(760, 620);

    m_serverHostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), this);
    m_serverPortSpin = new QSpinBox(this);
    m_serverPortSpin->setRange(1, 65535);
    m_serverPortSpin->setValue(15020);
    prepareSpinBox(m_serverPortSpin);

    m_templateCombo = new QComboBox(this);
    m_templateCombo->addItems(QStringList()
                              << QStringLiteral("温湿度传感器(默认)")
                              << QStringLiteral("ZH-Q006 空气质量传感器")
                              << QStringLiteral("压力传感器")
                              << QStringLiteral("通用 Modbus 设备"));
    m_gatewayIdEdit = new QLineEdit(QStringLiteral("GW-DTU-01"), this);
    m_deviceIdEdit = new QLineEdit(QStringLiteral("DEV-DTU-01"), this);
    m_deviceNameEdit = new QLineEdit(QStringLiteral("RS485 温湿度传感器"), this);
    m_locationEdit = new QLineEdit(QStringLiteral("RK3568 现场终端"), this);
    m_serialPortEdit = new QLineEdit(QStringLiteral("auto"), this);
    m_baudRateSpin = new QSpinBox(this);
    m_baudRateSpin->setRange(1200, 115200);
    m_baudRateSpin->setValue(9600);
    prepareSpinBox(m_baudRateSpin);
    m_slaveIdSpin = new QSpinBox(this);
    m_slaveIdSpin->setRange(1, 247);
    m_slaveIdSpin->setValue(1);
    prepareSpinBox(m_slaveIdSpin);
    m_startAddressSpin = new QSpinBox(this);
    m_startAddressSpin->setRange(0, 65535);
    prepareSpinBox(m_startAddressSpin);
    m_registerCountSpin = new QSpinBox(this);
    m_registerCountSpin->setRange(1, 32);
    m_registerCountSpin->setValue(2);
    prepareSpinBox(m_registerCountSpin);
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(200, 60000);
    m_intervalSpin->setValue(1000);
    prepareSpinBox(m_intervalSpin);
    m_simulateCheck = new QCheckBox(QStringLiteral("生成模拟采集配置"), this);
    m_simulateCheck->setChecked(false);
    m_platformHintLabel = new QLabel(QStringLiteral("平台 IP 指智慧管廊环境监测平台接收服务所在主机 IP。第一套方案里，DTU 终端会把采集到的传感器数据发到这个地址，而不是发给传感器本身。"), this);
    m_platformHintLabel->setWordWrap(true);
    m_platformHintLabel->setStyleSheet(QStringLiteral("color:#4f657a;"));

    QGroupBox *networkBox = new QGroupBox(QStringLiteral("DTU 目标平台参数"), this);
    QFormLayout *networkForm = new QFormLayout(networkBox);
    networkForm->addRow(QStringLiteral("设备模板"), m_templateCombo);
    networkForm->addRow(QStringLiteral("平台接收服务 IP"), m_serverHostEdit);
    networkForm->addRow(QStringLiteral("平台接收端口"), m_serverPortSpin);
    networkForm->addRow(QStringLiteral("网关 ID"), m_gatewayIdEdit);
    networkForm->addRow(QStringLiteral("设备 ID"), m_deviceIdEdit);
    networkForm->addRow(QStringLiteral("说明"), m_platformHintLabel);

    QGroupBox *sensorBox = new QGroupBox(QStringLiteral("RK3568 终端采集参数"), this);
    QFormLayout *sensorForm = new QFormLayout(sensorBox);
    sensorForm->addRow(QStringLiteral("设备名称"), m_deviceNameEdit);
    sensorForm->addRow(QStringLiteral("安装位置"), m_locationEdit);
    sensorForm->addRow(QStringLiteral("串口"), m_serialPortEdit);
    sensorForm->addRow(QStringLiteral("波特率"), m_baudRateSpin);
    sensorForm->addRow(QStringLiteral("Modbus 从站地址"), m_slaveIdSpin);
    sensorForm->addRow(QStringLiteral("起始寄存器"), m_startAddressSpin);
    sensorForm->addRow(QStringLiteral("寄存器数量"), m_registerCountSpin);
    sensorForm->addRow(QStringLiteral("采样周期(ms)"), m_intervalSpin);
    sensorForm->addRow(QStringLiteral("模拟模式"), m_simulateCheck);

    m_hintText = new QTextEdit(this);
    m_hintText->setReadOnly(true);
    m_hintText->setPlainText(QStringLiteral(
        "作用说明:\n"
        "1. 这个工具用于第一套方案: 传感器接 DTU/终端, 由 PC 上位机下发平台接收服务 IP 和串口采集参数。\n"
        "2. 点击保存后会一键生成 RK3568 可用配置包: deploy/rk3568-dtu-package/。\n"
        "3. 包内会包含 config/rk3568-dtu-demo.json 和部署说明，可直接拷到板子。\n"
        "4. 如果要在智慧管廊环境监测平台展示，平台 IP 应填写部署 web-demo/API 接收服务的主机地址。\n"
        "5. 第二套方案不依赖这个工具, 而是在网关 WebServer 页面上配置采集参数。"));

    m_saveButton = new QPushButton(QStringLiteral("一键生成 RK3568 配置包"), this);
    connect(m_saveButton, &QPushButton::clicked, this, &PcConfigWindow::saveConfig);
    connect(m_templateCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &PcConfigWindow::applyTemplate);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(networkBox);
    layout->addWidget(sensorBox);
    layout->addWidget(m_hintText);
    layout->addWidget(m_saveButton);

    applyTemplate(0);
}

static QString resolveDtuConfigOutputPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().filePath(QStringLiteral("config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../../config/rk3568-dtu-demo.json")),
        QDir(appDir).filePath(QStringLiteral("../../../../ai-gateway2026/config/rk3568-dtu-demo.json"))
    };

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() || info.absolutePath().contains(QStringLiteral("ai-gateway2026"))) {
            return QDir::cleanPath(candidate);
        }
    }

    return QDir::current().filePath(QStringLiteral("config/rk3568-dtu-demo.json"));
}

static QString resolveRkPackageDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().filePath(QStringLiteral("deploy/rk3568-dtu-package")),
        QDir(appDir).filePath(QStringLiteral("../deploy/rk3568-dtu-package")),
        QDir(appDir).filePath(QStringLiteral("../../deploy/rk3568-dtu-package")),
        QDir(appDir).filePath(QStringLiteral("../../../deploy/rk3568-dtu-package")),
        QDir(appDir).filePath(QStringLiteral("../../../../ai-gateway2026/deploy/rk3568-dtu-package"))
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).absolutePath().contains(QStringLiteral("ai-gateway2026"))) {
            return QDir::cleanPath(candidate);
        }
    }

    return QDir::current().filePath(QStringLiteral("deploy/rk3568-dtu-package"));
}

void PcConfigWindow::applyTemplate(int index)
{
    if (index == 1) {
        m_deviceNameEdit->setText(QStringLiteral("ZH-Q006 空气质量传感器"));
        m_startAddressSpin->setValue(0);
        m_registerCountSpin->setValue(9);
        m_baudRateSpin->setValue(9600);
        m_slaveIdSpin->setValue(1);
    } else if (index == 2) {
        m_deviceNameEdit->setText(QStringLiteral("RS485 压力传感器"));
        m_startAddressSpin->setValue(2);
        m_registerCountSpin->setValue(1);
    } else if (index == 3) {
        m_deviceNameEdit->setText(QStringLiteral("通用 Modbus 终端"));
        m_startAddressSpin->setValue(0);
        m_registerCountSpin->setValue(4);
    } else {
        m_deviceNameEdit->setText(QStringLiteral("RS485 温湿度传感器"));
        m_startAddressSpin->setValue(0);
        m_registerCountSpin->setValue(2);
    }
}

QJsonObject PcConfigWindow::buildConfigObject() const
{
    QJsonObject object;
    object.insert(QStringLiteral("gatewayId"), m_gatewayIdEdit->text().trimmed());
    object.insert(QStringLiteral("gatewayName"), QStringLiteral("DTU 透传终端"));
    object.insert(QStringLiteral("deviceId"), m_deviceIdEdit->text().trimmed());
    object.insert(QStringLiteral("deviceName"), m_deviceNameEdit->text().trimmed());
    object.insert(QStringLiteral("location"), m_locationEdit->text().trimmed());
    object.insert(QStringLiteral("mode"), QStringLiteral("modbus-rtu"));
    object.insert(QStringLiteral("intervalMs"), m_intervalSpin->value());
    object.insert(QStringLiteral("slaveId"), m_slaveIdSpin->value());
    object.insert(QStringLiteral("startAddress"), m_startAddressSpin->value());
    object.insert(QStringLiteral("registerCount"), m_registerCountSpin->value());
    object.insert(QStringLiteral("baseTemperature"), 25.0);
    object.insert(QStringLiteral("baseHumidity"), 60.0);
    object.insert(QStringLiteral("server"), QJsonObject{
        {QStringLiteral("host"), m_serverHostEdit->text().trimmed()},
        {QStringLiteral("port"), m_serverPortSpin->value()}
    });
    object.insert(QStringLiteral("serial"), QJsonObject{
        {QStringLiteral("simulate"), m_simulateCheck->isChecked()},
        {QStringLiteral("port"), m_serialPortEdit->text().trimmed()},
        {QStringLiteral("baudRate"), m_baudRateSpin->value()},
        {QStringLiteral("dataBits"), 8},
        {QStringLiteral("stopBits"), 1},
        {QStringLiteral("parity"), QStringLiteral("N")}
    });
    object.insert(QStringLiteral("deploy"), QJsonObject{
        {QStringLiteral("targetBoard"), QStringLiteral("RK3568")},
        {QStringLiteral("generatedBy"), QStringLiteral("pc-dtu-config-tool")},
        {QStringLiteral("packageDir"), resolveRkPackageDir()},
        {QStringLiteral("templateName"), m_templateCombo->currentText()}
    });
    return object;
}

bool PcConfigWindow::writeConfigPackage(const QJsonObject &object, QString *message)
{
    const QString configPath = resolveDtuConfigOutputPath();
    const QString packageDir = resolveRkPackageDir();
    const QString packageConfigDir = QDir(packageDir).filePath(QStringLiteral("config"));
    const QString packageScriptsDir = QDir(packageDir).filePath(QStringLiteral("scripts"));
    const QString packageSystemdDir = QDir(packageDir).filePath(QStringLiteral("systemd"));
    const QString packageConfigPath = QDir(packageConfigDir).filePath(QStringLiteral("rk3568-dtu-demo.json"));
    const QString readmePath = QDir(packageDir).filePath(QStringLiteral("README.txt"));
    const QString startScriptPath = QDir(packageScriptsDir).filePath(QStringLiteral("start.sh"));
    const QString installScriptPath = QDir(packageScriptsDir).filePath(QStringLiteral("install-service.sh"));
    const QString servicePath = QDir(packageSystemdDir).filePath(QStringLiteral("rk3568-dtu-demo.service"));

    QDir().mkpath(QFileInfo(configPath).absolutePath());
    QDir().mkpath(packageConfigDir);
    QDir().mkpath(packageScriptsDir);
    QDir().mkpath(packageSystemdDir);

    QFile repoConfig(configPath);
    if (!repoConfig.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (message) {
            *message = QStringLiteral("无法写入仓库配置文件:\n%1").arg(configPath);
        }
        return false;
    }
    repoConfig.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    repoConfig.close();

    QFile packageConfig(packageConfigPath);
    if (!packageConfig.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (message) {
            *message = QStringLiteral("无法写入 RK3568 配置文件:\n%1").arg(packageConfigPath);
        }
        return false;
    }
    packageConfig.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    packageConfig.close();

    const QString startScript = QStringLiteral(
        "#!/bin/sh\n"
        "set -eu\n\n"
        "APP_DIR=\"$(CDPATH= cd -- \"$(dirname -- \"$0\")/..\" && pwd)\"\n"
        "BIN_PATH=\"${APP_DIR}/bin/rk3568-dtu-terminal-demo\"\n"
        "CONFIG_PATH=\"${APP_DIR}/config/rk3568-dtu-demo.json\"\n\n"
        "if [ ! -x \"$BIN_PATH\" ]; then\n"
        "  echo \"binary not found: $BIN_PATH\"\n"
        "  echo \"please copy rk3568-dtu-terminal-demo to ${APP_DIR}/bin/ first\"\n"
        "  exit 1\n"
        "fi\n\n"
        "exec \"$BIN_PATH\" \"$CONFIG_PATH\"\n");
    QFile startScriptFile(startScriptPath);
    if (startScriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        startScriptFile.write(startScript.toUtf8());
        startScriptFile.close();
        startScriptFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                       | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                       | QFileDevice::ReadOther | QFileDevice::ExeOther);
    }

    const QString serviceText = QStringLiteral(
        "[Unit]\n"
        "Description=RK3568 DTU Demo Service\n"
        "After=network.target\n\n"
        "[Service]\n"
        "Type=simple\n"
        "WorkingDirectory=/opt/rk3568-dtu-demo\n"
        "ExecStart=/opt/rk3568-dtu-demo/scripts/start.sh\n"
        "Restart=always\n"
        "RestartSec=3\n"
        "User=%1\n\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n").arg(QStringLiteral("root"));
    QFile serviceFile(servicePath);
    if (serviceFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        serviceFile.write(serviceText.toUtf8());
        serviceFile.close();
    }

    const QString installScript = QStringLiteral(
        "#!/bin/sh\n"
        "set -eu\n\n"
        "APP_ROOT=/opt/rk3568-dtu-demo\n"
        "SERVICE_NAME=rk3568-dtu-demo.service\n"
        "PKG_DIR=\"$(CDPATH= cd -- \"$(dirname -- \"$0\")/..\" && pwd)\"\n\n"
        "mkdir -p \"$APP_ROOT/config\" \"$APP_ROOT/bin\" \"$APP_ROOT/scripts\"\n"
        "cp \"$PKG_DIR/config/rk3568-dtu-demo.json\" \"$APP_ROOT/config/\"\n"
        "cp \"$PKG_DIR/scripts/start.sh\" \"$APP_ROOT/scripts/\"\n"
        "chmod +x \"$APP_ROOT/scripts/start.sh\"\n\n"
        "if [ -f \"$PKG_DIR/bin/rk3568-dtu-terminal-demo\" ]; then\n"
        "  cp \"$PKG_DIR/bin/rk3568-dtu-terminal-demo\" \"$APP_ROOT/bin/\"\n"
        "  chmod +x \"$APP_ROOT/bin/rk3568-dtu-terminal-demo\"\n"
        "else\n"
        "  echo \"warning: binary not found in package bin/, copy it manually later\"\n"
        "fi\n\n"
        "cp \"$PKG_DIR/systemd/$SERVICE_NAME\" \"/etc/systemd/system/$SERVICE_NAME\"\n"
        "systemctl daemon-reload\n"
        "systemctl enable \"$SERVICE_NAME\"\n"
        "echo \"installed. start with: sudo systemctl start $SERVICE_NAME\"\n");
    QFile installScriptFile(installScriptPath);
    if (installScriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        installScriptFile.write(installScript.toUtf8());
        installScriptFile.close();
        installScriptFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                         | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                         | QFileDevice::ReadOther | QFileDevice::ExeOther);
    }

    const QString readmeText = QStringLiteral(
        "RK3568 DTU Demo 配置包\n"
        "适用系统: 银河麒麟 / Ubuntu (systemd)\n\n"
        "目录说明:\n"
        "1. config/rk3568-dtu-demo.json   板端运行配置\n"
        "2. scripts/start.sh              启动脚本\n"
        "3. scripts/install-service.sh    一键安装 systemd 服务\n"
        "4. systemd/rk3568-dtu-demo.service 服务模板\n"
        "5. bin/                          放置 rk3568-dtu-terminal-demo 二进制\n\n"
        "部署步骤:\n"
        "1. 把交叉编译后的 rk3568-dtu-terminal-demo 复制到 bin/ 目录\n"
        "2. 整个 deploy/rk3568-dtu-package 拷到板子\n"
        "3. 在板子上执行: sudo ./scripts/install-service.sh\n"
        "4. 启动服务: sudo systemctl start rk3568-dtu-demo.service\n"
        "5. 查看日志: sudo journalctl -u rk3568-dtu-demo.service -f\n\n"
        "当前目标平台: %1:%2\n"
        "当前串口: %3, 波特率: %4, 从站: %5\n")
        .arg(object.value(QStringLiteral("server")).toObject().value(QStringLiteral("host")).toString())
        .arg(object.value(QStringLiteral("server")).toObject().value(QStringLiteral("port")).toInt())
        .arg(object.value(QStringLiteral("serial")).toObject().value(QStringLiteral("port")).toString())
        .arg(object.value(QStringLiteral("serial")).toObject().value(QStringLiteral("baudRate")).toInt())
        .arg(object.value(QStringLiteral("slaveId")).toInt());

    QFile readmeFile(readmePath);
    if (readmeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        readmeFile.write(readmeText.toUtf8());
        readmeFile.close();
    }

    if (message) {
        *message = QStringLiteral(
            "已生成 RK3568 配置与部署包:\n"
            "1. 仓库配置: %1\n"
            "2. 板端配置: %2\n"
            "3. 启动脚本: %3\n"
            "4. 服务脚本: %4\n"
            "5. 服务模板: %5\n\n"
            "部署目录:\n%6\n\n"
            "适用系统: 银河麒麟 / Ubuntu")
            .arg(QDir::cleanPath(configPath))
            .arg(QDir::cleanPath(packageConfigPath))
            .arg(QDir::cleanPath(startScriptPath))
            .arg(QDir::cleanPath(installScriptPath))
            .arg(QDir::cleanPath(servicePath))
            .arg(QDir::cleanPath(packageDir));
    }
    return true;
}

void PcConfigWindow::saveConfig()
{
    const QJsonObject object = buildConfigObject();
    QString message;
    if (!writeConfigPackage(object, &message)) {
        QMessageBox::critical(this, QStringLiteral("生成失败"), message);
        return;
    }
    QMessageBox::information(this, QStringLiteral("生成成功"), message);
}
