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
class QCheckBox;
class QCloseEvent;
class QDialog;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QThread;
class QWidget;
class SerialEncoderDebugTab;
class UniformMotionWindow;

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

protected:
    void closeEvent(QCloseEvent *event) override;

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
    void shutdownWorkers();
    void shutdownCanThread();
    bool confirmAction(const QString &windowTitle,
                       const QString &message,
                       const QString &detail,
                       const QString &confirmText);
    bool confirmProtocolCommand(const CommandDef &command, const QByteArray &payload);
    bool confirmConfigWritePayload(const QByteArray &payload);
    bool confirmFactoryReset();

    void sendProtocolCommand(quint8 commandId, bool requireConfirmation = true);
    void sendRawProtocolCommand(quint8 commandId, const QByteArray &payload, bool requireConfirmation = true);
    void sendConfigRead(quint32 index);
    void sendConfigWrite(quint32 index);
    void startOneClickCalibration();
    void chooseAnticoggingFile();
    void startOneClickAnticogging();
    void chooseElfFile();
    void startOneClickDfuFlash();
    void stopOneClickCalibrationProcess();
    void stopOneClickAnticoggingProcess();
    bool buildCommandPayload(const CommandDef &command, QByteArray *payload, QString *error) const;
    bool encodeConfigValue(const ConfigDef &config, const QString &text, QByteArray *payload, QString *error) const;

    void handleReceivedFrame(const QCanBusFrame &frame);
    void handleTransmittedFrame(const QCanBusFrame &frame);
    QString describeFrame(const QCanBusFrame &frame, bool updateUi);
    void addLogFrame(const QString &direction, const QCanBusFrame &frame, const QString &parsed);
    void appendSystemLog(const QString &message);

    void updateStatusWords(quint32 status, quint32 errors);
    void updateConfigCurrentValue(quint32 index, quint32 rawValue);
    QString anticoggingEstimateLine() const;
    void setAnticoggingEstimateLine(const QString &line);
    void updateAnticoggingEstimate();
    void setFlagLabel(QLabel *label, bool active);
    void setConnectionState(bool connected, const QString &message);
    void showSpiDebugDialog();
    void showSerialEncoderDebugDialog();

    quint8 currentNodeId() const;
    QString commandInputText(quint8 commandId) const;

    struct ProcessPanel
    {
        QPushButton *startButton = nullptr;
        QPushButton *stopButton = nullptr;
        QPlainTextEdit *outputEdit = nullptr;
        QThread *thread = nullptr;
        QPointer<CalibrationRunner> runner;
        QString currentLine;
        bool running = false;
        bool liveLineVisible = false;
    };

    void startProcessPanel(ProcessPanel *panel, const QString &command, const QString &initialText = QString());
    void stopProcessPanel(ProcessPanel *panel, bool waitForThread = false);
    void appendProcessOutput(ProcessPanel *panel, const QString &text);
    void finishProcessPanel(ProcessPanel *panel, const QString &message);

    QThread *m_canThread = nullptr;
    CanService *m_canService = nullptr;
    QWidget *m_canContentArea = nullptr;
    QPointer<QDialog> m_spiDebugDialog;
    QPointer<QDialog> m_serialEncoderDebugDialog;
    QPointer<SerialEncoderDebugTab> m_serialEncoderDebugTab;
    QPointer<UniformMotionWindow> m_uniformMotionWindow;

    QLineEdit *m_interfaceEdit = nullptr;
    QSpinBox *m_nodeSpin = nullptr;
    QSpinBox *m_bitrateSpin = nullptr;
    QSpinBox *m_dataBitrateSpin = nullptr;
    QLabel *m_connectionStateLabel = nullptr;

    QTableWidget *m_commandTable = nullptr;
    QTableWidget *m_configTable = nullptr;
    QTableWidget *m_logTable = nullptr;
    QCheckBox *m_filterCanLogCheck = nullptr;
    ProcessPanel m_calibrationProcess;
    ProcessPanel m_anticoggingProcess;
    ProcessPanel m_dfuProcess;
    QCheckBox *m_bootloaderActiveCheck = nullptr;
    QSpinBox *m_anticoggingAverageSpin = nullptr;
    QSpinBox *m_anticoggingSplitSpin = nullptr;
    QDoubleSpinBox *m_anticoggingSpeedSpin = nullptr;
    QSpinBox *m_anticoggingPolesSpin = nullptr;
    QString m_selectedAnticoggingScriptPath;
    QString m_selectedElfPath;
    bool m_shuttingDown = false;

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
