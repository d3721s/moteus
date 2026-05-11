#ifndef SPIENCODERDEBUGTAB_H
#define SPIENCODERDEBUGTAB_H

#include <QByteArray>
#include <QMap>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSerialPort;
class QSpinBox;
class QTimer;

class MultiSeriesChartWidget;

class SpiEncoderDebugTab : public QWidget
{
    Q_OBJECT

public:
    explicit SpiEncoderDebugTab(QWidget *parent = nullptr);
    ~SpiEncoderDebugTab() override;

private:
    enum class EncoderModel
    {
        Mt6826 = 0,
        Mt6835 = 1,
        Ma600 = 2,
        Kth7812N = 3,
        Kth7812C = 4
    };

    enum class EncoderSlot
    {
        Axis,
        OffAxis
    };

    struct DebugFrame
    {
        quint8 isCheck = 0;
        quint8 cmd = 0;
        QByteArray payload;
        QByteArray raw;
    };

    struct EncoderReadResult
    {
        bool ok = false;
        double angleDeg = 0.0;
        quint32 raw = 0;
        QString statusText;
        QString errorText;
        QString rxHex;
    };

    QWidget *createConnectionGroup();
    QWidget *createSpiGroup();
    QWidget *createEncoderGroup();
    QWidget *createRegisterGroup();
    QWidget *createBottomArea();

    void refreshPorts();
    void setSerialOpened(bool opened, const QString &state);
    bool openSerial(QString *error);
    void closeSerial();
    bool probeDebugger(QString *error);

    bool applySpiConfig(int masterSlave, int mode, int dividerCode, int bitOrderValue, QString *error);
    bool querySpiConfig(QString *summary, QString *error);
    bool setCsLevel(bool high, QString *error);
    bool spiTransfer(const QByteArray &tx, QByteArray *rx, QString *error);
    bool performSpiTransaction(const QByteArray &tx,
                               QByteArray *rx,
                               int timeoutMs,
                               bool autoToggleCs,
                               QString *error);

    bool sendCommand(quint8 cmd,
                     const QByteArray &payload,
                     quint8 expectedCmd,
                     int timeoutMs,
                     DebugFrame *response,
                     QString *error);
    bool readFrame(DebugFrame *frame, int timeoutMs, QString *error);
    bool tryExtractFrame(DebugFrame *frame, QString *error);

    static QByteArray buildFrame(quint8 cmd, const QByteArray &payload);
    static quint8 xorChecksum(const QByteArray &frameWithoutChecksum);
    static QString bytesToHex(const QByteArray &bytes);
    static bool parseHexText(const QString &text, QByteArray *out, QString *error);
    static bool allBytesEqual(const QByteArray &data, char value);

    void appendProtoLog(const QString &prefix, const QByteArray &frame);
    void appendMessage(const QString &message);

    void onApplySpiClicked();
    void onQuerySpiClicked();
    void onRawTransferClicked();
    void onConfigureEncoderClicked();
    void onReadOnceClicked();
    void onSamplingToggled(bool running);
    void onSamplingTick();
    void onReadRegisterClicked();
    void onWriteRegisterClicked();
    void onSetBctClicked();

    EncoderModel axisModel() const;
    EncoderModel offAxisModel() const;
    EncoderModel slotModel(EncoderSlot slot) const;
    QString slotName(EncoderSlot slot) const;
    bool configureForModel(EncoderModel model, QString *error);
    EncoderReadResult readEncoder(EncoderModel model);
    EncoderReadResult readMt6826();
    EncoderReadResult readMt6835();
    EncoderReadResult readMa600();
    EncoderReadResult readKth7812(bool crc4);
    bool readRegister(EncoderModel model, quint16 addr, quint16 *value, QString *error);
    bool writeRegister(EncoderModel model, quint16 addr, quint16 value, QString *error);
    static quint8 crc8_07(const QByteArray &bytes);
    static quint8 crc4_3(const QByteArray &bytes, int bitCount);
    void setSlotStatus(EncoderSlot slot, const EncoderReadResult &result);

    QSerialPort *m_serial = nullptr;
    QByteArray m_rxBuffer;

    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QPushButton *m_openButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QPushButton *m_probeButton = nullptr;
    QLineEdit *m_debuggerStateEdit = nullptr;

    QComboBox *m_masterSlaveCombo = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_bitOrderCombo = nullptr;
    QComboBox *m_dividerCombo = nullptr;
    QPushButton *m_applySpiButton = nullptr;
    QPushButton *m_querySpiButton = nullptr;
    QPushButton *m_csLowButton = nullptr;
    QPushButton *m_csHighButton = nullptr;
    QLineEdit *m_txHexEdit = nullptr;
    QLineEdit *m_rxHexEdit = nullptr;
    QPushButton *m_rawTransferButton = nullptr;

    QComboBox *m_axisEncoderCombo = nullptr;
    QComboBox *m_offAxisEncoderCombo = nullptr;
    QPushButton *m_configureEncoderButton = nullptr;
    QPushButton *m_readOnceButton = nullptr;
    QPushButton *m_startSamplingButton = nullptr;
    QPushButton *m_stopSamplingButton = nullptr;
    QSpinBox *m_samplingIntervalSpin = nullptr;
    QCheckBox *m_axisEnabledCheck = nullptr;
    QCheckBox *m_offAxisEnabledCheck = nullptr;
    QLineEdit *m_bctEdit = nullptr;
    QPushButton *m_setBctButton = nullptr;
    QLineEdit *m_axisStateEdit = nullptr;
    QLineEdit *m_offAxisStateEdit = nullptr;

    QComboBox *m_registerTargetCombo = nullptr;
    QLineEdit *m_registerAddrEdit = nullptr;
    QLineEdit *m_registerValueEdit = nullptr;
    QPushButton *m_readRegisterButton = nullptr;
    QPushButton *m_writeRegisterButton = nullptr;

    MultiSeriesChartWidget *m_chart = nullptr;
    QPlainTextEdit *m_protocolLogEdit = nullptr;
    QPushButton *m_clearLogButton = nullptr;

    QTimer *m_samplingTimer = nullptr;
    int m_sampleIndex = 0;
};

#endif // SPIENCODERDEBUGTAB_H

