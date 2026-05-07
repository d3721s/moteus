#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "protocol.h"

#include <QCanBusFrame>
#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QVector>

class CalibrationRunner;
class CanService;
class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QThread;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void connectCanInterfaceRequested(const QString &interfaceName, int bitrate, int dataBitrate);
    void disconnectCanInterfaceRequested();
    void sendCanCommandRequested(quint8 nodeId, quint8 commandId, const QByteArray &payload);

private:
    QWidget *createConnectionPanel();
    QWidget *createCommandPanel();
    QWidget *createConfigPanel();
    QWidget *createCalibrationPanel();
    QWidget *createAnticoggingPanel();
    QWidget *createDfuPanel();
    QWidget *createStatusPanel();
    QWidget *createLogPanel();

    void setupCanConnections();
    void applyVisualStyle();

    void sendProtocolCommand(quint8 commandId);
    void sendRawProtocolCommand(quint8 commandId, const QByteArray &payload);
    void sendConfigRead(quint32 index);
    void sendConfigWrite(quint32 index);
    void startOneClickCalibration();
    void appendCalibrationOutput(const QString &text);
    void finishOneClickCalibration(const QString &message);
    void stopOneClickCalibrationProcess();
    bool buildCommandPayload(const CommandDef &command, QByteArray *payload, QString *error) const;
    bool encodeConfigValue(const ConfigDef &config, const QString &text, QByteArray *payload, QString *error) const;

    void handleReceivedFrame(const QCanBusFrame &frame);
    void handleTransmittedFrame(const QCanBusFrame &frame);
    QString describeFrame(const QCanBusFrame &frame, bool updateUi);
    void addLogFrame(const QString &direction, const QCanBusFrame &frame, const QString &parsed);
    void appendSystemLog(const QString &message);

    void updateStatusWords(quint32 status, quint32 errors);
    void updateConfigCurrentValue(quint32 index, quint32 rawValue);
    void setFlagLabel(QLabel *label, bool active);
    void setConnectionState(bool connected, const QString &message);

    quint8 currentNodeId() const;
    QString commandInputText(quint8 commandId) const;

    QThread *m_canThread = nullptr;
    CanService *m_canService = nullptr;
    QWidget *m_canContentArea = nullptr;

    QLineEdit *m_interfaceEdit = nullptr;
    QSpinBox *m_nodeSpin = nullptr;
    QSpinBox *m_bitrateSpin = nullptr;
    QSpinBox *m_dataBitrateSpin = nullptr;
    QLabel *m_connectionStateLabel = nullptr;

    QTableWidget *m_commandTable = nullptr;
    QTableWidget *m_configTable = nullptr;
    QTableWidget *m_logTable = nullptr;
    QPushButton *m_oneClickCalibrateButton = nullptr;
    QPushButton *m_stopOneClickCalibrateButton = nullptr;
    QPlainTextEdit *m_calibrationOutputEdit = nullptr;
    QThread *m_calibrationThread = nullptr;
    QPointer<CalibrationRunner> m_calibrationRunner;
    QString m_calibrationCurrentLine;
    bool m_calibrationRunning = false;
    bool m_calibrationLiveLineVisible = false;

    QLabel *m_lastNodeLabel = nullptr;
    QLabel *m_lastCommandLabel = nullptr;
    QLabel *m_statusWordLabel = nullptr;
    QLabel *m_errorsWordLabel = nullptr;
    QLabel *m_switchedOnLabel = nullptr;
    QLabel *m_targetReachedLabel = nullptr;
    QLabel *m_currentLimitLabel = nullptr;
    QLabel *m_adcSelftestLabel = nullptr;
    QLabel *m_encoderOfflineLabel = nullptr;
    QLabel *m_overVoltageLabel = nullptr;
    QLabel *m_underVoltageLabel = nullptr;
    QLabel *m_overCurrentLabel = nullptr;
    QLabel *m_fwVersionLabel = nullptr;
    QLabel *m_calibLabel = nullptr;
    QLabel *m_anticoggingLabel = nullptr;
    QVector<QLabel *> m_value1Labels;

    QMap<quint8, QLineEdit *> m_commandInputs;
    QMap<quint32, QLineEdit *> m_configEditors;
    QMap<quint32, QTableWidgetItem *> m_configCurrentItems;
};
#endif // MAINWINDOW_H
