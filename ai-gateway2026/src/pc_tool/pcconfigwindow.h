#ifndef PCCONFIGWINDOW_H
#define PCCONFIGWINDOW_H

#include <QJsonObject>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QComboBox;
class QSpinBox;
class QTextEdit;
class QCheckBox;
class QLabel;

class PcConfigWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PcConfigWindow(QWidget *parent = nullptr);

private slots:
    void saveConfig();
    void applyTemplate(int index);

private:
    QJsonObject buildConfigObject() const;
    bool writeConfigPackage(const QJsonObject &object, QString *message);

    QLineEdit *m_serverHostEdit = nullptr;
    QSpinBox *m_serverPortSpin = nullptr;
    QLineEdit *m_gatewayIdEdit = nullptr;
    QLineEdit *m_deviceIdEdit = nullptr;
    QLineEdit *m_deviceNameEdit = nullptr;
    QLineEdit *m_locationEdit = nullptr;
    QComboBox *m_templateCombo = nullptr;
    QLineEdit *m_serialPortEdit = nullptr;
    QSpinBox *m_baudRateSpin = nullptr;
    QSpinBox *m_slaveIdSpin = nullptr;
    QSpinBox *m_startAddressSpin = nullptr;
    QSpinBox *m_registerCountSpin = nullptr;
    QSpinBox *m_intervalSpin = nullptr;
    QCheckBox *m_simulateCheck = nullptr;
    QTextEdit *m_hintText = nullptr;
    QLabel *m_platformHintLabel = nullptr;
    QPushButton *m_saveButton = nullptr;
};

#endif // PCCONFIGWINDOW_H
