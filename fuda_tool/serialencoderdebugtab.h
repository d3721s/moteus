#ifndef SERIALENCODERDEBUGTAB_H
#define SERIALENCODERDEBUGTAB_H

#include <QByteArray>
#include <QWidget>

#include <initializer_list>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSerialPort;
class QSpinBox;
class QTimer;

class AngleChartWidget;

class SerialEncoderDebugTab : public QWidget
{
    Q_OBJECT

public:
    explicit SerialEncoderDebugTab(QWidget *parent = nullptr);
    ~SerialEncoderDebugTab() override;

signals:
    void encoderPositionSampled(double timestampSec, double positionTurns, double angleDeg, bool continuousPosition);

private:
    struct EncoderData
    {
        bool ok = false;
        quint8 command = 0;
        quint8 status = 0;
        quint8 encoderId = 0;
        quint8 almc = 0;
        int resolutionBits = 0;
        quint64 raw = 0;
        quint32 turns = 0;
        quint64 totalRaw = 0;
        double angleDeg = 0.0;
        bool hasRaw = false;
        bool hasTurns = false;
        bool hasTotal = false;
        bool hasId = false;
        bool hasAlmc = false;
        QByteArray response;
        QString summary;
        QString errorText;
    };

    QWidget *createConnectionGroup();
    QWidget *createDataGroup();
    QWidget *createEepromGroup();
    QWidget *createResetGroup();
    QWidget *createBottomArea();

    void refreshPorts();
    void setSerialOpened(bool opened, const QString &state);
    void updateSamplingButtons();
    bool openSerial(QString *error);
    void closeSerial();

    bool identifyResolution(QString *error);
    int selectedResolutionBits(QString *error);
    double softwareReductionRatio() const;
    EncoderData readDataCommand(quint8 command);
    EncoderData parseDataResponse(quint8 command, const QByteArray &response, int resolutionBits);
    bool readEeprom(quint8 address, quint8 *value, bool *busy, QString *error);
    bool writeEeprom(quint8 address, quint8 value, bool *busy, QString *error);
    bool sendRepeatedResetCommand(quint8 command, QByteArray *response, QString *error);
    bool confirmOperation(const QString &title, const QString &message);

    bool transact(quint8 command, int expectedLength, QByteArray *response, int timeoutMs, QString *error);
    bool transactFrame(const QByteArray &request,
                       quint8 expectedCommand,
                       int expectedLength,
                       QByteArray *response,
                       int timeoutMs,
                       QString *error);
    bool writeRequest(const QByteArray &request, int timeoutMs, QString *error);
    bool readResponse(quint8 expectedCommand, int expectedLength, QByteArray *response, int timeoutMs, QString *error);

    void setDataStatus(const EncoderData &data);
    void publishMotionSample(const EncoderData &data);
    void appendMessage(const QString &message);
    void appendFrameLog(const QString &prefix, const QByteArray &frame);

    void onIdentifyClicked();
    void onReadAbsoluteClicked();
    void onReadMultiTurnClicked();
    void onReadIdClicked();
    void onReadAllClicked();
    void onReadPositionClicked();
    void onEepromReadClicked();
    void onEepromWriteClicked();
    void onResetFaultClicked();
    void onZeroSingleTurnClicked();
    void onResetMultiTurnClicked();
    void onSamplingToggled(bool running);
    void onSamplingTick();

    static QByteArray buildFrame(std::initializer_list<quint8> bytesWithoutCrc);
    static QString bytesToHex(const QByteArray &bytes);
    static bool checkXor(const QByteArray &bytes);
    static int encoderIdToResolutionBits(quint8 encoderId);
    static quint64 resolutionValue(int bits);
    static quint64 parseUnsignedLe(const QByteArray &bytes, int offset, int count);
    static int expectedLengthForCommand(quint8 command, int resolutionBits);
    static QString commandName(quint8 command);
    static QString statusText(quint8 status);
    static QString almcText(quint8 almc);
    static QString encoderIdText(quint8 encoderId);

    QSerialPort *m_serial = nullptr;
    QByteArray m_rxBuffer;

    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QPushButton *m_openButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QPushButton *m_identifyButton = nullptr;
    QLineEdit *m_connectionStateEdit = nullptr;

    QComboBox *m_resolutionCombo = nullptr;
    QPushButton *m_readAbsoluteButton = nullptr;
    QPushButton *m_readMultiTurnButton = nullptr;
    QPushButton *m_readIdButton = nullptr;
    QPushButton *m_readAllButton = nullptr;
    QPushButton *m_readPositionButton = nullptr;
    QPushButton *m_startSamplingButton = nullptr;
    QPushButton *m_stopSamplingButton = nullptr;
    QSpinBox *m_samplingIntervalSpin = nullptr;
    QDoubleSpinBox *m_reductionRatioSpin = nullptr;
    QLineEdit *m_idStateEdit = nullptr;
    QLineEdit *m_statusEdit = nullptr;
    QLineEdit *m_almcEdit = nullptr;
    QLineEdit *m_rawEdit = nullptr;
    QLineEdit *m_angleEdit = nullptr;
    QLineEdit *m_turnsEdit = nullptr;
    QLineEdit *m_totalEdit = nullptr;
    QLineEdit *m_dataSummaryEdit = nullptr;

    QSpinBox *m_eepromAddrSpin = nullptr;
    QSpinBox *m_eepromValueSpin = nullptr;
    QPushButton *m_eepromReadButton = nullptr;
    QPushButton *m_eepromWriteButton = nullptr;
    QLineEdit *m_eepromStateEdit = nullptr;

    QPushButton *m_resetFaultButton = nullptr;
    QPushButton *m_zeroSingleTurnButton = nullptr;
    QPushButton *m_resetMultiTurnButton = nullptr;
    QLineEdit *m_resetStateEdit = nullptr;

    AngleChartWidget *m_chart = nullptr;
    QPlainTextEdit *m_protocolLogEdit = nullptr;
    QPushButton *m_clearLogButton = nullptr;

    QTimer *m_samplingTimer = nullptr;
    int m_detectedResolutionBits = 0;
    quint8 m_detectedEncoderId = 0;
    int m_sampleIndex = 0;
};

#endif // SERIALENCODERDEBUGTAB_H
