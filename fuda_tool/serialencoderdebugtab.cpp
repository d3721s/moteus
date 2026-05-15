#include "serialencoderdebugtab.h"

#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointF>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>

namespace
{
constexpr int DefaultBaudrate = 2500000;
constexpr int DefaultTimeoutMs = 160;
constexpr int ResetTimeoutMs = 400;
constexpr int MaxLogBlockCount = 3000;
constexpr int MaxChartPoints = 1200;
constexpr quint8 ReadEepromCommand = 0xEA;

QString hexByte(quint8 value)
{
    return QStringLiteral("0x%1").arg(int(value), 2, 16, QLatin1Char('0')).toUpper();
}
}

class AngleChartWidget : public QWidget
{
public:
    explicit AngleChartWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(260);
        setAutoFillBackground(true);
    }

    void clear()
    {
        m_points.clear();
        update();
    }

    void appendPoint(double x, double angleDeg)
    {
        m_points.append(QPointF(x, angleDeg));
        if (m_points.size() > MaxChartPoints) {
            m_points.remove(0, m_points.size() - MaxChartPoints);
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF full = rect().adjusted(6, 6, -6, -6);
        painter.fillRect(full, QColor(18, 22, 26));
        painter.setPen(QPen(QColor(68, 78, 88), 1));
        painter.drawRect(full);

        const QRectF plot = full.adjusted(56, 14, -14, -38);
        painter.setPen(QPen(QColor(45, 54, 62), 1));
        for (int i = 0; i <= 4; ++i) {
            const qreal y = plot.top() + plot.height() * i / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        for (int i = 0; i <= 8; ++i) {
            const qreal x = plot.left() + plot.width() * i / 8.0;
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }

        painter.setPen(QColor(176, 184, 194));
        for (int i = 0; i <= 4; ++i) {
            const int value = 360 - i * 90;
            const qreal y = plot.top() + plot.height() * i / 4.0;
            painter.drawText(QRectF(full.left() + 6, y - 8, 44, 16),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(value));
        }
        painter.drawText(QRectF(full.left() + 6, full.bottom() - 24, 220, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("X: sample"));
        painter.drawText(QRectF(full.right() - 220, full.bottom() - 24, 210, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("Y: angle (deg)"));

        if (m_points.isEmpty()) {
            painter.setPen(QColor(170, 176, 188));
            painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待角度采样数据..."));
            return;
        }

        double minX = m_points.first().x();
        double maxX = m_points.last().x();
        if (qFuzzyCompare(minX, maxX)) {
            maxX += 1.0;
        }

        auto mapToPlot = [&](const QPointF &pt) -> QPointF {
            const double boundedY = std::clamp(pt.y(), 0.0, 360.0);
            const qreal x = plot.left() + (pt.x() - minX) / (maxX - minX) * plot.width();
            const qreal y = plot.bottom() - boundedY / 360.0 * plot.height();
            return QPointF(x, y);
        };

        QPainterPath path;
        path.moveTo(mapToPlot(m_points.first()));
        for (int i = 1; i < m_points.size(); ++i) {
            path.lineTo(mapToPlot(m_points.at(i)));
        }

        painter.setPen(QPen(QColor(84, 204, 255), 2));
        painter.drawPath(path);

        const QPointF last = mapToPlot(m_points.last());
        painter.setBrush(QColor(255, 202, 92));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(last, 4, 4);
        painter.setPen(QColor(218, 226, 236));
        painter.drawText(QRectF(plot.right() - 180, plot.top() + 8, 170, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("angle=%1 deg").arg(m_points.last().y(), 0, 'f', 3));
    }

private:
    QVector<QPointF> m_points;
};

SerialEncoderDebugTab::SerialEncoderDebugTab(QWidget *parent)
    : QWidget(parent)
    , m_serial(new QSerialPort(this))
    , m_samplingTimer(new QTimer(this))
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *top = new QWidget(this);
    auto *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(8);
    topLayout->addWidget(createConnectionGroup(), 0);
    topLayout->addWidget(createDataGroup(), 1);

    auto *writeRow = new QWidget(top);
    auto *writeLayout = new QHBoxLayout(writeRow);
    writeLayout->setContentsMargins(0, 0, 0, 0);
    writeLayout->setSpacing(8);
    writeLayout->addWidget(createEepromGroup(), 2);
    writeLayout->addWidget(createResetGroup(), 3);
    topLayout->addWidget(writeRow, 0);

    auto *topScroll = new QScrollArea(this);
    topScroll->setWidgetResizable(true);
    topScroll->setFrameShape(QFrame::NoFrame);
    topScroll->setWidget(top);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(topScroll);
    splitter->addWidget(createBottomArea());
    splitter->setStretchFactor(0, 5);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({470, 300});
    root->addWidget(splitter);

    m_protocolLogEdit->setMaximumBlockCount(MaxLogBlockCount);
    m_baudCombo->setCurrentText(QString::number(DefaultBaudrate));
    m_samplingIntervalSpin->setValue(20);
    m_samplingTimer->setInterval(m_samplingIntervalSpin->value());

    refreshPorts();
    setSerialOpened(false, QStringLiteral("未连接"));

    connect(m_samplingTimer, &QTimer::timeout, this, &SerialEncoderDebugTab::onSamplingTick);
    connect(m_samplingIntervalSpin, &QSpinBox::valueChanged, this, [this](int value) {
        m_samplingTimer->setInterval(value);
    });
}

SerialEncoderDebugTab::~SerialEncoderDebugTab()
{
    closeSerial();
}

QWidget *SerialEncoderDebugTab::createConnectionGroup()
{
    auto *group = new QGroupBox(QStringLiteral("1. 串口连接 / 通信参数"), this);
    group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    group->setMaximumHeight(78);
    auto *layout = new QGridLayout(group);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(6);

    m_portCombo = new QComboBox(group);
    m_portCombo->setMinimumWidth(230);
    auto *refreshButton = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("刷新"), group);
    m_baudCombo = new QComboBox(group);
    m_baudCombo->setEditable(true);
    m_baudCombo->addItems({QStringLiteral("2500000"),
                           QStringLiteral("5000000"),
                           QStringLiteral("1000000"),
                           QStringLiteral("921600"),
                           QStringLiteral("460800"),
                           QStringLiteral("115200")});
    m_openButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("打开"), group);
    m_closeButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("关闭"), group);
    m_identifyButton = new QPushButton(QStringLiteral("识别分辨率 ID2(0x92)"), group);
    m_connectionStateEdit = new QLineEdit(group);
    m_connectionStateEdit->setReadOnly(true);

    layout->addWidget(new QLabel(QStringLiteral("串口"), group), 0, 0);
    layout->addWidget(m_portCombo, 0, 1);
    layout->addWidget(refreshButton, 0, 2);
    layout->addWidget(new QLabel(QStringLiteral("波特率"), group), 0, 3);
    layout->addWidget(m_baudCombo, 0, 4);
    layout->addWidget(m_openButton, 0, 5);
    layout->addWidget(m_closeButton, 0, 6);
    layout->addWidget(m_identifyButton, 0, 7);
    layout->addWidget(new QLabel(QStringLiteral("状态"), group), 0, 8);
    layout->addWidget(m_connectionStateEdit, 0, 9);
    layout->setColumnStretch(9, 1);

    connect(refreshButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::refreshPorts);
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!openSerial(&error)) {
            setSerialOpened(false, QStringLiteral("打开失败"));
            m_connectionStateEdit->setText(QStringLiteral("打开失败: %1").arg(error));
            return;
        }
        setSerialOpened(true, QStringLiteral("已打开"));
        appendMessage(QStringLiteral("串口已打开"));
    });
    connect(m_closeButton, &QPushButton::clicked, this, [this]() {
        closeSerial();
        appendMessage(QStringLiteral("串口已关闭"));
    });
    connect(m_identifyButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onIdentifyClicked);
    return group;
}

QWidget *SerialEncoderDebugTab::createDataGroup()
{
    auto *group = new QGroupBox(QStringLiteral("2. 位置数据读取 / 连续采样"), this);
    group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto *layout = new QGridLayout(group);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(6);

    m_resolutionCombo = new QComboBox(group);
    m_resolutionCombo->addItem(QStringLiteral("自动识别"), 0);
    m_resolutionCombo->addItem(QStringLiteral("17 bit (0x11)"), 17);
    m_resolutionCombo->addItem(QStringLiteral("21 bit (0x15)"), 21);
    m_resolutionCombo->addItem(QStringLiteral("23 bit (0x17)"), 23);
    m_resolutionCombo->addItem(QStringLiteral("25 bit (0x19)"), 25);

    m_readAbsoluteButton = new QPushButton(QStringLiteral("读绝对角度 ID0/ID4"), group);
    m_readMultiTurnButton = new QPushButton(QStringLiteral("读多圈 ID1(0x8A)"), group);
    m_readIdButton = new QPushButton(QStringLiteral("读 ID ID2(0x92)"), group);
    m_readAllButton = new QPushButton(QStringLiteral("读全部 ID3(0x1A)"), group);
    m_readPositionButton = new QPushButton(QStringLiteral("读位置+多圈 ID4/ID5"), group);
    m_startSamplingButton = new QPushButton(QStringLiteral("开始角度采样"), group);
    m_stopSamplingButton = new QPushButton(QStringLiteral("停止采样"), group);
    m_samplingIntervalSpin = new QSpinBox(group);
    m_samplingIntervalSpin->setRange(5, 2000);
    m_samplingIntervalSpin->setSuffix(QStringLiteral(" ms"));

    m_idStateEdit = new QLineEdit(group);
    m_statusEdit = new QLineEdit(group);
    m_almcEdit = new QLineEdit(group);
    m_rawEdit = new QLineEdit(group);
    m_angleEdit = new QLineEdit(group);
    m_turnsEdit = new QLineEdit(group);
    m_totalEdit = new QLineEdit(group);
    m_dataSummaryEdit = new QLineEdit(group);
    for (QLineEdit *edit : {m_idStateEdit, m_statusEdit, m_almcEdit, m_rawEdit, m_angleEdit, m_turnsEdit, m_totalEdit, m_dataSummaryEdit}) {
        edit->setReadOnly(true);
    }

    layout->addWidget(new QLabel(QStringLiteral("分辨率"), group), 0, 0);
    layout->addWidget(m_resolutionCombo, 0, 1);
    layout->addWidget(new QLabel(QStringLiteral("采样周期"), group), 0, 2);
    layout->addWidget(m_samplingIntervalSpin, 0, 3);
    layout->addWidget(m_startSamplingButton, 0, 4);
    layout->addWidget(m_stopSamplingButton, 0, 5);
    layout->addWidget(m_readAbsoluteButton, 1, 0, 1, 2);
    layout->addWidget(m_readMultiTurnButton, 1, 2, 1, 2);
    layout->addWidget(m_readIdButton, 1, 4);
    layout->addWidget(m_readAllButton, 1, 5);
    layout->addWidget(m_readPositionButton, 1, 6);
    layout->addWidget(new QLabel(QStringLiteral("ID / 分辨率"), group), 2, 0);
    layout->addWidget(m_idStateEdit, 2, 1, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("SF"), group), 2, 3);
    layout->addWidget(m_statusEdit, 2, 4, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("ALMC"), group), 3, 0);
    layout->addWidget(m_almcEdit, 3, 1, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("单圈编码值"), group), 3, 3);
    layout->addWidget(m_rawEdit, 3, 4, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("绝对角度"), group), 4, 0);
    layout->addWidget(m_angleEdit, 4, 1, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("多圈圈数"), group), 4, 3);
    layout->addWidget(m_turnsEdit, 4, 4, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("总编码值"), group), 5, 0);
    layout->addWidget(m_totalEdit, 5, 1, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("解析摘要"), group), 5, 3);
    layout->addWidget(m_dataSummaryEdit, 5, 4, 1, 3);
    layout->setColumnStretch(6, 1);

    connect(m_readAbsoluteButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onReadAbsoluteClicked);
    connect(m_readMultiTurnButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onReadMultiTurnClicked);
    connect(m_readIdButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onReadIdClicked);
    connect(m_readAllButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onReadAllClicked);
    connect(m_readPositionButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onReadPositionClicked);
    connect(m_startSamplingButton, &QPushButton::clicked, this, [this]() { onSamplingToggled(true); });
    connect(m_stopSamplingButton, &QPushButton::clicked, this, [this]() { onSamplingToggled(false); });
    connect(m_resolutionCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_resolutionCombo->currentData().toInt() != 0) {
            m_detectedResolutionBits = 0;
            m_detectedEncoderId = 0;
        }
    });
    return group;
}

QWidget *SerialEncoderDebugTab::createEepromGroup()
{
    auto *group = new QGroupBox(QStringLiteral("3. E2PROM 读写"), this);
    auto *layout = new QGridLayout(group);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(6);

    m_eepromAddrSpin = new QSpinBox(group);
    m_eepromAddrSpin->setRange(0x00, 0x7F);
    m_eepromAddrSpin->setDisplayIntegerBase(16);
    m_eepromAddrSpin->setPrefix(QStringLiteral("0x"));
    m_eepromValueSpin = new QSpinBox(group);
    m_eepromValueSpin->setRange(0x00, 0xFF);
    m_eepromValueSpin->setDisplayIntegerBase(16);
    m_eepromValueSpin->setPrefix(QStringLiteral("0x"));
    m_eepromReadButton = new QPushButton(QStringLiteral("读 IDD(0xEA)"), group);
    m_eepromWriteButton = new QPushButton(QStringLiteral("写 ID6(0x32)"), group);
    m_eepromStateEdit = new QLineEdit(group);
    m_eepromStateEdit->setReadOnly(true);

    layout->addWidget(new QLabel(QStringLiteral("地址 ADF"), group), 0, 0);
    layout->addWidget(m_eepromAddrSpin, 0, 1);
    layout->addWidget(new QLabel(QStringLiteral("数据 EDF"), group), 0, 2);
    layout->addWidget(m_eepromValueSpin, 0, 3);
    layout->addWidget(m_eepromReadButton, 1, 0, 1, 2);
    layout->addWidget(m_eepromWriteButton, 1, 2, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("状态"), group), 2, 0);
    layout->addWidget(m_eepromStateEdit, 2, 1, 1, 3);
    layout->setColumnStretch(3, 1);

    connect(m_eepromReadButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onEepromReadClicked);
    connect(m_eepromWriteButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onEepromWriteClicked);
    return group;
}

QWidget *SerialEncoderDebugTab::createResetGroup()
{
    auto *group = new QGroupBox(QStringLiteral("4. 复位 / 置零"), this);
    auto *layout = new QGridLayout(group);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(6);

    m_resetFaultButton = new QPushButton(QStringLiteral("故障标志复位 ID7(0xBA)"), group);
    m_zeroSingleTurnButton = new QPushButton(QStringLiteral("单圈值置零 ID8(0xC2)"), group);
    m_resetMultiTurnButton = new QPushButton(QStringLiteral("多圈置零+故障复位 IDC(0x62)"), group);
    m_resetStateEdit = new QLineEdit(group);
    m_resetStateEdit->setReadOnly(true);

    auto *note = new QLabel(QStringLiteral("按协议连续发送 10 次，间隔不小于 62.5us；置零类操作会改变编码器记忆位置。"), group);
    note->setWordWrap(true);

    layout->addWidget(m_resetFaultButton, 0, 0);
    layout->addWidget(m_zeroSingleTurnButton, 0, 1);
    layout->addWidget(m_resetMultiTurnButton, 0, 2);
    layout->addWidget(new QLabel(QStringLiteral("状态"), group), 1, 0);
    layout->addWidget(m_resetStateEdit, 1, 1, 1, 2);
    layout->addWidget(note, 2, 0, 1, 3);
    layout->setColumnStretch(2, 1);

    connect(m_resetFaultButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onResetFaultClicked);
    connect(m_zeroSingleTurnButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onZeroSingleTurnClicked);
    connect(m_resetMultiTurnButton, &QPushButton::clicked, this, &SerialEncoderDebugTab::onResetMultiTurnClicked);
    return group;
}

QWidget *SerialEncoderDebugTab::createBottomArea()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_chart = new AngleChartWidget(widget);
    layout->addWidget(m_chart, 3);

    auto *logTitle = new QHBoxLayout;
    auto *label = new QLabel(QStringLiteral("5. 协议日志 (TX/RX 原始帧)"), widget);
    m_clearLogButton = new QPushButton(QStringLiteral("清空"), widget);
    logTitle->addWidget(label);
    logTitle->addStretch(1);
    logTitle->addWidget(m_clearLogButton);
    layout->addLayout(logTitle);

    m_protocolLogEdit = new QPlainTextEdit(widget);
    m_protocolLogEdit->setReadOnly(true);
    m_protocolLogEdit->setMinimumHeight(90);
    m_protocolLogEdit->setMaximumHeight(150);
    layout->addWidget(m_protocolLogEdit, 1);

    connect(m_clearLogButton, &QPushButton::clicked, this, [this]() {
        m_protocolLogEdit->clear();
        m_chart->clear();
        m_sampleIndex = 0;
    });
    return widget;
}

void SerialEncoderDebugTab::refreshPorts()
{
    const QString current = m_portCombo->currentData().toString();
    m_portCombo->clear();
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        QString label = info.portName();
        const QString desc = info.description().trimmed();
        if (!desc.isEmpty()) {
            label += QStringLiteral(" - ") + desc;
        }
        m_portCombo->addItem(label, info.portName());
    }
    if (!current.isEmpty()) {
        const int idx = m_portCombo->findData(current);
        if (idx >= 0) {
            m_portCombo->setCurrentIndex(idx);
        }
    }
}

void SerialEncoderDebugTab::setSerialOpened(bool opened, const QString &state)
{
    if (!opened) {
        m_samplingTimer->stop();
    }

    m_openButton->setEnabled(!opened);
    m_closeButton->setEnabled(opened);
    for (QWidget *widget : {static_cast<QWidget *>(m_identifyButton),
                            static_cast<QWidget *>(m_readAbsoluteButton),
                            static_cast<QWidget *>(m_readMultiTurnButton),
                            static_cast<QWidget *>(m_readIdButton),
                            static_cast<QWidget *>(m_readAllButton),
                            static_cast<QWidget *>(m_readPositionButton),
                            static_cast<QWidget *>(m_eepromReadButton),
                            static_cast<QWidget *>(m_eepromWriteButton),
                            static_cast<QWidget *>(m_resetFaultButton),
                            static_cast<QWidget *>(m_zeroSingleTurnButton),
                            static_cast<QWidget *>(m_resetMultiTurnButton)}) {
        widget->setEnabled(opened);
    }
    m_connectionStateEdit->setText(state);
    updateSamplingButtons();
}

void SerialEncoderDebugTab::updateSamplingButtons()
{
    const bool opened = m_serial && m_serial->isOpen();
    const bool running = m_samplingTimer && m_samplingTimer->isActive();
    m_startSamplingButton->setEnabled(opened && !running);
    m_stopSamplingButton->setEnabled(opened && running);
}

bool SerialEncoderDebugTab::openSerial(QString *error)
{
    if (m_serial->isOpen()) {
        return true;
    }

    const QString portName = m_portCombo->currentData().toString();
    if (portName.isEmpty()) {
        if (error) {
            *error = QStringLiteral("未选择串口");
        }
        return false;
    }

    bool ok = false;
    const int baud = m_baudCombo->currentText().trimmed().toInt(&ok);
    if (!ok || baud <= 0) {
        if (error) {
            *error = QStringLiteral("波特率无效");
        }
        return false;
    }

    m_serial->setPortName(portName);
    m_serial->setBaudRate(baud);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);
    if (!m_serial->open(QIODevice::ReadWrite)) {
        if (error) {
            *error = m_serial->errorString();
        }
        return false;
    }

    m_rxBuffer.clear();
    m_detectedResolutionBits = 0;
    m_detectedEncoderId = 0;
    return true;
}

void SerialEncoderDebugTab::closeSerial()
{
    if (m_serial->isOpen()) {
        m_serial->close();
    }
    setSerialOpened(false, QStringLiteral("未连接"));
}

bool SerialEncoderDebugTab::identifyResolution(QString *error)
{
    const EncoderData data = readDataCommand(0x92);
    if (!data.ok) {
        if (error) {
            *error = data.errorText;
        }
        return false;
    }
    setDataStatus(data);
    return true;
}

int SerialEncoderDebugTab::selectedResolutionBits(QString *error)
{
    const int selected = m_resolutionCombo->currentData().toInt();
    if (selected != 0) {
        return selected;
    }

    if (m_detectedResolutionBits == 0 && !identifyResolution(error)) {
        return 0;
    }
    return m_detectedResolutionBits;
}

SerialEncoderDebugTab::EncoderData SerialEncoderDebugTab::readDataCommand(quint8 command)
{
    EncoderData data;
    data.command = command;

    QString error;
    int bits = command == 0x92 ? (m_detectedResolutionBits != 0 ? m_detectedResolutionBits : 23)
                               : selectedResolutionBits(&error);
    if (bits == 0) {
        data.errorText = error;
        return data;
    }

    const int expectedLength = expectedLengthForCommand(command, bits);
    if (expectedLength <= 0) {
        data.errorText = QStringLiteral("当前分辨率不支持 %1").arg(commandName(command));
        return data;
    }

    QByteArray response;
    if (!transact(command, expectedLength, &response, DefaultTimeoutMs, &error)) {
        data.errorText = error;
        return data;
    }

    data = parseDataResponse(command, response, bits);
    if (!data.ok && data.errorText.isEmpty()) {
        data.errorText = QStringLiteral("解析失败");
    }
    return data;
}

SerialEncoderDebugTab::EncoderData SerialEncoderDebugTab::parseDataResponse(quint8 command,
                                                                            const QByteArray &response,
                                                                            int resolutionBits)
{
    EncoderData data;
    data.ok = true;
    data.command = command;
    data.response = response;
    data.status = response.size() > 1 ? quint8(response.at(1)) : 0;
    data.resolutionBits = resolutionBits;

    auto setAbs = [&](quint64 raw) {
        data.hasRaw = true;
        data.raw = raw;
        const quint64 resolution = resolutionValue(data.resolutionBits);
        if (resolution != 0) {
            data.raw &= resolution - 1;
            data.angleDeg = double(data.raw) * 360.0 / double(resolution);
        }
    };
    auto setTurns = [&](quint64 turns) {
        data.hasTurns = true;
        data.turns = quint32(turns & 0xFFFFU);
        if (data.hasRaw) {
            data.hasTotal = true;
            data.totalRaw = quint64(data.turns) * resolutionValue(data.resolutionBits) + data.raw;
        }
    };

    switch (command) {
    case 0x02:
        setAbs(parseUnsignedLe(response, 2, 3));
        break;
    case 0x8A:
        setTurns(parseUnsignedLe(response, 2, 3));
        break;
    case 0x92:
        data.hasId = true;
        data.encoderId = quint8(response.at(2));
        data.resolutionBits = encoderIdToResolutionBits(data.encoderId);
        if (data.resolutionBits == 0) {
            data.ok = false;
            data.errorText = QStringLiteral("未知编码器 ID: %1").arg(hexByte(data.encoderId));
        }
        break;
    case 0x1A:
        if (resolutionBits == 25) {
            data.hasId = true;
            data.encoderId = quint8(response.at(5));
            data.resolutionBits = encoderIdToResolutionBits(data.encoderId);
            if (data.resolutionBits == 0) {
                data.resolutionBits = 25;
            }
            const quint64 rawLe = parseUnsignedLe(response.mid(2, 3) + response.mid(6, 1), 0, 4);
            setAbs(rawLe >> 7);
            setTurns(parseUnsignedLe(response, 7, 2));
            data.hasAlmc = true;
            data.almc = quint8(response.at(9));
        } else {
            setAbs(parseUnsignedLe(response, 2, 3));
            data.hasId = true;
            data.encoderId = quint8(response.at(5));
            setTurns(parseUnsignedLe(response, 6, 3));
            data.hasAlmc = true;
            data.almc = quint8(response.at(9));
        }
        break;
    case 0x2B:
        setAbs(parseUnsignedLe(response, 2, 3));
        setTurns(parseUnsignedLe(response, 5, 2));
        data.hasAlmc = true;
        data.almc = quint8(response.at(7));
        break;
    case 0xA2:
        setAbs(parseUnsignedLe(response, 2, 4) >> 7);
        break;
    case 0x2A:
        setAbs(parseUnsignedLe(response, 2, 4) >> 7);
        setTurns(parseUnsignedLe(response, 6, 2));
        break;
    case 0xBA:
    case 0xC2:
    case 0x62:
        setAbs(parseUnsignedLe(response, 2, 3));
        break;
    default:
        data.ok = false;
        data.errorText = QStringLiteral("未实现的命令: %1").arg(hexByte(command));
        break;
    }

    if (data.hasId && data.resolutionBits != 0) {
        m_detectedEncoderId = data.encoderId;
        m_detectedResolutionBits = data.resolutionBits;
    }

    QStringList parts;
    parts.append(commandName(command));
    parts.append(statusText(data.status));
    if (data.hasId) {
        parts.append(encoderIdText(data.encoderId));
    }
    if (data.hasRaw) {
        parts.append(QStringLiteral("raw=%1").arg(QString::number(data.raw)));
        parts.append(QStringLiteral("angle=%1 deg").arg(data.angleDeg, 0, 'f', 6));
    }
    if (data.hasTurns) {
        parts.append(QStringLiteral("turns=%1").arg(data.turns));
    }
    if (data.hasTotal) {
        parts.append(QStringLiteral("total=%1").arg(QString::number(data.totalRaw)));
    }
    if (data.hasAlmc) {
        parts.append(almcText(data.almc));
    }
    data.summary = parts.join(QStringLiteral("; "));
    return data;
}

bool SerialEncoderDebugTab::readEeprom(quint8 address, quint8 *value, bool *busy, QString *error)
{
    const QByteArray request = buildFrame({ReadEepromCommand, quint8(address & 0x7F)});
    QByteArray response;
    if (!transactFrame(request, ReadEepromCommand, 4, &response, DefaultTimeoutMs, error)) {
        return false;
    }

    const quint8 responseAdf = quint8(response.at(1));
    if ((responseAdf & 0x7FU) != (address & 0x7FU)) {
        if (error) {
            *error = QStringLiteral("响应地址不匹配: %1").arg(hexByte(responseAdf));
        }
        return false;
    }
    if (value) {
        *value = quint8(response.at(2));
    }
    if (busy) {
        *busy = (responseAdf & 0x80U) != 0U;
    }
    return true;
}

bool SerialEncoderDebugTab::writeEeprom(quint8 address, quint8 value, bool *busy, QString *error)
{
    const QByteArray request = buildFrame({0x32, quint8(address & 0x7F), value});
    QByteArray response;
    if (!transactFrame(request, 0x32, 4, &response, DefaultTimeoutMs, error)) {
        return false;
    }

    const quint8 responseAdf = quint8(response.at(1));
    if ((responseAdf & 0x7FU) != (address & 0x7FU)) {
        if (error) {
            *error = QStringLiteral("响应地址不匹配: %1").arg(hexByte(responseAdf));
        }
        return false;
    }
    if (busy) {
        *busy = (responseAdf & 0x80U) != 0U;
    }
    return true;
}

bool SerialEncoderDebugTab::sendRepeatedResetCommand(quint8 command, QByteArray *response, QString *error)
{
    const QByteArray request(1, char(command));
    m_serial->readAll();
    m_rxBuffer.clear();

    for (int i = 0; i < 10; ++i) {
        if (!writeRequest(request, DefaultTimeoutMs, error)) {
            return false;
        }
        QThread::usleep(100);
    }

    return readResponse(command, 6, response, ResetTimeoutMs, error);
}

bool SerialEncoderDebugTab::confirmOperation(const QString &title, const QString &message)
{
    return QMessageBox::question(this,
                                 title,
                                 message,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}

bool SerialEncoderDebugTab::transact(quint8 command, int expectedLength, QByteArray *response, int timeoutMs, QString *error)
{
    return transactFrame(QByteArray(1, char(command)), command, expectedLength, response, timeoutMs, error);
}

bool SerialEncoderDebugTab::transactFrame(const QByteArray &request,
                                          quint8 expectedCommand,
                                          int expectedLength,
                                          QByteArray *response,
                                          int timeoutMs,
                                          QString *error)
{
    m_serial->readAll();
    m_rxBuffer.clear();
    if (!writeRequest(request, timeoutMs, error)) {
        return false;
    }
    return readResponse(expectedCommand, expectedLength, response, timeoutMs, error);
}

bool SerialEncoderDebugTab::writeRequest(const QByteArray &request, int timeoutMs, QString *error)
{
    if (!m_serial->isOpen()) {
        if (error) {
            *error = QStringLiteral("串口未打开");
        }
        return false;
    }

    if (m_serial->write(request) != request.size()) {
        if (error) {
            *error = QStringLiteral("串口写入失败: %1").arg(m_serial->errorString());
        }
        return false;
    }
    if (!m_serial->waitForBytesWritten(timeoutMs)) {
        if (error) {
            *error = QStringLiteral("写入超时");
        }
        return false;
    }
    appendFrameLog(QStringLiteral("TX"), request);
    return true;
}

bool SerialEncoderDebugTab::readResponse(quint8 expectedCommand,
                                         int expectedLength,
                                         QByteArray *response,
                                         int timeoutMs,
                                         QString *error)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        m_rxBuffer.append(m_serial->readAll());

        while (true) {
            const int header = m_rxBuffer.indexOf(char(expectedCommand));
            if (header < 0) {
                m_rxBuffer.clear();
                break;
            }
            if (header > 0) {
                m_rxBuffer.remove(0, header);
            }
            if (m_rxBuffer.size() < expectedLength) {
                break;
            }

            const QByteArray candidate = m_rxBuffer.left(expectedLength);
            if (!checkXor(candidate)) {
                appendFrameLog(QStringLiteral("RX_BAD"), candidate);
                m_rxBuffer.remove(0, 1);
                continue;
            }

            m_rxBuffer.remove(0, expectedLength);
            if (response) {
                *response = candidate;
            }
            appendFrameLog(QStringLiteral("RX"), candidate);
            return true;
        }

        const int remaining = timeoutMs - int(timer.elapsed());
        if (remaining <= 0) {
            break;
        }
        m_serial->waitForReadyRead(remaining);
    }

    if (error) {
        *error = QStringLiteral("无响应或 CRC 校验失败");
    }
    return false;
}

void SerialEncoderDebugTab::setDataStatus(const EncoderData &data)
{
    if (!data.ok) {
        const QString error = data.errorText.isEmpty() ? QStringLiteral("无响应") : data.errorText;
        m_statusEdit->setText(error);
        m_dataSummaryEdit->setText(error);
        appendMessage(QStringLiteral("读取失败: %1").arg(error));
        return;
    }

    if (data.hasId) {
        m_idStateEdit->setText(encoderIdText(data.encoderId));
    }
    m_statusEdit->setText(statusText(data.status));
    if (data.hasAlmc) {
        m_almcEdit->setText(almcText(data.almc));
    }
    if (data.hasRaw) {
        m_rawEdit->setText(QString::number(data.raw));
        m_angleEdit->setText(QStringLiteral("%1 deg").arg(data.angleDeg, 0, 'f', 6));
    }
    if (data.hasTurns) {
        m_turnsEdit->setText(QString::number(data.turns));
    }
    if (data.hasTotal) {
        m_totalEdit->setText(QString::number(data.totalRaw));
    }
    m_dataSummaryEdit->setText(data.summary);
    appendMessage(data.summary);
}

void SerialEncoderDebugTab::appendMessage(const QString &message)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_protocolLogEdit->appendPlainText(QStringLiteral("[%1] %2").arg(ts, message));
}

void SerialEncoderDebugTab::appendFrameLog(const QString &prefix, const QByteArray &frame)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_protocolLogEdit->appendPlainText(QStringLiteral("[%1] %2 %3").arg(ts, prefix, bytesToHex(frame)));
}

void SerialEncoderDebugTab::onIdentifyClicked()
{
    QString error;
    if (!identifyResolution(&error)) {
        m_idStateEdit->setText(QStringLiteral("识别失败: %1").arg(error));
        appendMessage(QStringLiteral("识别失败: %1").arg(error));
    }
}

void SerialEncoderDebugTab::onReadAbsoluteClicked()
{
    QString error;
    const int bits = selectedResolutionBits(&error);
    if (bits == 0) {
        EncoderData errorData;
        errorData.errorText = error;
        setDataStatus(errorData);
        return;
    }

    const EncoderData data = readDataCommand(bits == 25 ? 0xA2 : 0x02);
    setDataStatus(data);
    if (data.ok && data.hasRaw) {
        ++m_sampleIndex;
        m_chart->appendPoint(m_sampleIndex, data.angleDeg);
    }
}

void SerialEncoderDebugTab::onReadMultiTurnClicked()
{
    setDataStatus(readDataCommand(0x8A));
}

void SerialEncoderDebugTab::onReadIdClicked()
{
    setDataStatus(readDataCommand(0x92));
}

void SerialEncoderDebugTab::onReadAllClicked()
{
    setDataStatus(readDataCommand(0x1A));
}

void SerialEncoderDebugTab::onReadPositionClicked()
{
    QString error;
    const int bits = selectedResolutionBits(&error);
    if (bits == 0) {
        EncoderData errorData;
        errorData.errorText = error;
        setDataStatus(errorData);
        return;
    }
    setDataStatus(readDataCommand(bits == 25 ? 0x2A : 0x2B));
}

void SerialEncoderDebugTab::onEepromReadClicked()
{
    const quint8 address = quint8(m_eepromAddrSpin->value() & 0x7F);
    quint8 value = 0;
    bool busy = false;
    QString error;
    if (!readEeprom(address, &value, &busy, &error)) {
        m_eepromStateEdit->setText(QStringLiteral("读取失败: %1").arg(error));
        appendMessage(QStringLiteral("E2PROM 读取失败: %1").arg(error));
        return;
    }

    m_eepromValueSpin->setValue(value);
    const QString state = QStringLiteral("addr=%1 value=%2 BUSY=%3")
                              .arg(hexByte(address), hexByte(value), busy ? QStringLiteral("1") : QStringLiteral("0"));
    m_eepromStateEdit->setText(state);
    appendMessage(QStringLiteral("E2PROM 读取成功: %1").arg(state));
}

void SerialEncoderDebugTab::onEepromWriteClicked()
{
    const quint8 address = quint8(m_eepromAddrSpin->value() & 0x7F);
    const quint8 value = quint8(m_eepromValueSpin->value() & 0xFF);
    bool busy = false;
    QString error;
    if (!writeEeprom(address, value, &busy, &error)) {
        m_eepromStateEdit->setText(QStringLiteral("写入失败: %1").arg(error));
        appendMessage(QStringLiteral("E2PROM 写入失败: %1").arg(error));
        return;
    }

    QString state = QStringLiteral("写入请求已%1: addr=%2 value=%3 BUSY=%4")
                        .arg(busy ? QStringLiteral("排队/忙") : QStringLiteral("接收"))
                        .arg(hexByte(address), hexByte(value), busy ? QStringLiteral("1") : QStringLiteral("0"));

    QThread::msleep(12);
    quint8 verifyValue = 0;
    bool verifyBusy = false;
    if (readEeprom(address, &verifyValue, &verifyBusy, &error)) {
        state += QStringLiteral("; 回读=%1 BUSY=%2")
                     .arg(hexByte(verifyValue), verifyBusy ? QStringLiteral("1") : QStringLiteral("0"));
    } else {
        state += QStringLiteral("; 回读失败=%1").arg(error);
    }

    m_eepromStateEdit->setText(state);
    appendMessage(QStringLiteral("E2PROM 写入完成: %1").arg(state));
}

void SerialEncoderDebugTab::onResetFaultClicked()
{
    if (!confirmOperation(QStringLiteral("故障标志复位"),
                          QStringLiteral("将连续发送 10 次 0xBA，清除编码器故障标志位。是否继续？"))) {
        return;
    }

    QByteArray response;
    QString error;
    if (!sendRepeatedResetCommand(0xBA, &response, &error)) {
        m_resetStateEdit->setText(QStringLiteral("失败: %1").arg(error));
        appendMessage(QStringLiteral("故障复位失败: %1").arg(error));
        return;
    }
    const EncoderData data = parseDataResponse(0xBA, response, selectedResolutionBits(&error));
    setDataStatus(data);
    m_resetStateEdit->setText(QStringLiteral("故障复位完成: %1").arg(statusText(data.status)));
}

void SerialEncoderDebugTab::onZeroSingleTurnClicked()
{
    if (!confirmOperation(QStringLiteral("单圈值置零"),
                          QStringLiteral("该操作会把当前机械位置写为单圈零点，并掉电保持。是否继续？"))) {
        return;
    }

    QByteArray response;
    QString error;
    if (!sendRepeatedResetCommand(0xC2, &response, &error)) {
        m_resetStateEdit->setText(QStringLiteral("失败: %1").arg(error));
        appendMessage(QStringLiteral("单圈置零失败: %1").arg(error));
        return;
    }
    const EncoderData data = parseDataResponse(0xC2, response, selectedResolutionBits(&error));
    setDataStatus(data);
    m_resetStateEdit->setText(QStringLiteral("单圈置零完成: %1").arg(statusText(data.status)));
}

void SerialEncoderDebugTab::onResetMultiTurnClicked()
{
    if (!confirmOperation(QStringLiteral("多圈值置零"),
                          QStringLiteral("该操作会把多圈数据置零，并同时复位故障标志位。是否继续？"))) {
        return;
    }

    QByteArray response;
    QString error;
    if (!sendRepeatedResetCommand(0x62, &response, &error)) {
        m_resetStateEdit->setText(QStringLiteral("失败: %1").arg(error));
        appendMessage(QStringLiteral("多圈置零失败: %1").arg(error));
        return;
    }
    const EncoderData data = parseDataResponse(0x62, response, selectedResolutionBits(&error));
    setDataStatus(data);
    m_resetStateEdit->setText(QStringLiteral("多圈置零完成: %1").arg(statusText(data.status)));
}

void SerialEncoderDebugTab::onSamplingToggled(bool running)
{
    if (running) {
        if (!m_serial->isOpen()) {
            appendMessage(QStringLiteral("串口未打开，无法采样"));
            return;
        }
        m_sampleIndex = 0;
        m_chart->clear();
        m_samplingTimer->start();
        updateSamplingButtons();
        appendMessage(QStringLiteral("连续角度采样已启动"));
    } else {
        m_samplingTimer->stop();
        updateSamplingButtons();
        appendMessage(QStringLiteral("连续角度采样已停止"));
    }
}

void SerialEncoderDebugTab::onSamplingTick()
{
    QString error;
    const int bits = selectedResolutionBits(&error);
    if (bits == 0) {
        appendMessage(QStringLiteral("采样失败: %1").arg(error));
        return;
    }

    const EncoderData data = readDataCommand(bits == 25 ? 0xA2 : 0x02);
    setDataStatus(data);
    if (!data.ok || !data.hasRaw) {
        return;
    }

    ++m_sampleIndex;
    m_chart->appendPoint(m_sampleIndex, data.angleDeg);
}

QByteArray SerialEncoderDebugTab::buildFrame(std::initializer_list<quint8> bytesWithoutCrc)
{
    QByteArray frame;
    frame.reserve(int(bytesWithoutCrc.size()) + 1);
    quint8 crc = 0;
    for (quint8 value : bytesWithoutCrc) {
        frame.append(char(value));
        crc ^= value;
    }
    frame.append(char(crc));
    return frame;
}

QString SerialEncoderDebugTab::bytesToHex(const QByteArray &bytes)
{
    QString text;
    text.reserve(bytes.size() * 3);
    for (int i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            text.append(QLatin1Char(' '));
        }
        text.append(QStringLiteral("%1").arg(quint8(bytes.at(i)), 2, 16, QLatin1Char('0')).toUpper());
    }
    return text;
}

bool SerialEncoderDebugTab::checkXor(const QByteArray &bytes)
{
    if (bytes.size() < 2) {
        return false;
    }

    quint8 xorValue = 0;
    for (int i = 0; i < bytes.size() - 1; ++i) {
        xorValue ^= quint8(bytes.at(i));
    }
    return xorValue == quint8(bytes.at(bytes.size() - 1));
}

int SerialEncoderDebugTab::encoderIdToResolutionBits(quint8 encoderId)
{
    switch (encoderId) {
    case 0x11:
        return 17;
    case 0x15:
        return 21;
    case 0x17:
        return 23;
    case 0x19:
        return 25;
    default:
        return 0;
    }
}

quint64 SerialEncoderDebugTab::resolutionValue(int bits)
{
    return bits > 0 && bits < 63 ? (quint64(1) << bits) : 0;
}

quint64 SerialEncoderDebugTab::parseUnsignedLe(const QByteArray &bytes, int offset, int count)
{
    quint64 value = 0;
    for (int i = 0; i < count && offset + i < bytes.size(); ++i) {
        value |= quint64(quint8(bytes.at(offset + i))) << (8 * i);
    }
    return value;
}

int SerialEncoderDebugTab::expectedLengthForCommand(quint8 command, int resolutionBits)
{
    switch (command) {
    case 0x02:
    case 0x8A:
    case 0xBA:
    case 0xC2:
    case 0x62:
        return 6;
    case 0x92:
        return 4;
    case 0x1A:
        return 11;
    case 0x2B:
        return resolutionBits == 25 ? 0 : 9;
    case 0xA2:
        return resolutionBits == 25 ? 7 : 0;
    case 0x2A:
        return resolutionBits == 25 ? 9 : 0;
    default:
        return 0;
    }
}

QString SerialEncoderDebugTab::commandName(quint8 command)
{
    switch (command) {
    case 0x02:
        return QStringLiteral("ID0 绝对位置(0x02)");
    case 0x8A:
        return QStringLiteral("ID1 多圈(0x8A)");
    case 0x92:
        return QStringLiteral("ID2 编码器ID(0x92)");
    case 0x1A:
        return QStringLiteral("ID3 全部数据(0x1A)");
    case 0x2B:
        return QStringLiteral("ID4 位置+多圈+ALMC(0x2B)");
    case 0xA2:
        return QStringLiteral("ID4 25位绝对位置(0xA2)");
    case 0x2A:
        return QStringLiteral("ID5 25位位置+多圈(0x2A)");
    case 0x32:
        return QStringLiteral("ID6 写E2PROM(0x32)");
    case 0xBA:
        return QStringLiteral("ID7 故障复位(0xBA)");
    case 0xC2:
        return QStringLiteral("ID8 单圈置零(0xC2)");
    case 0x62:
        return QStringLiteral("IDC 多圈置零+故障复位(0x62)");
    case ReadEepromCommand:
        return QStringLiteral("IDD 读E2PROM(0xEA)");
    default:
        return QStringLiteral("命令 %1").arg(hexByte(command));
    }
}

QString SerialEncoderDebugTab::statusText(quint8 status)
{
    if (status == 0) {
        return QStringLiteral("SF=0x00 正常");
    }

    QStringList flags;
    if ((status & 0x10U) != 0U) {
        flags.append(QStringLiteral("Counting Error"));
    }
    if ((status & 0x20U) != 0U) {
        flags.append(QStringLiteral("多圈/电池告警"));
    }
    const quint8 reserved = status & 0xCFU;
    if (reserved != 0) {
        flags.append(QStringLiteral("保留位=%1").arg(hexByte(reserved)));
    }

    return QStringLiteral("SF=%1 %2").arg(hexByte(status), flags.join(QStringLiteral(", ")));
}

QString SerialEncoderDebugTab::almcText(quint8 almc)
{
    if (almc == 0) {
        return QStringLiteral("ALMC=0x00 正常");
    }

    QStringList flags;
    if ((almc & 0x01U) != 0U) {
        flags.append(QStringLiteral("Over-speed"));
    }
    if ((almc & 0x04U) != 0U) {
        flags.append(QStringLiteral("Counting Error"));
    }
    if ((almc & 0x20U) != 0U) {
        flags.append(QStringLiteral("Multi-turn error"));
    }
    if ((almc & 0x40U) != 0U) {
        flags.append(QStringLiteral("Battery error"));
    }
    if ((almc & 0x80U) != 0U) {
        flags.append(QStringLiteral("Battery alarm"));
    }
    const quint8 reserved = almc & 0x1AU;
    if (reserved != 0) {
        flags.append(QStringLiteral("保留位=%1").arg(hexByte(reserved)));
    }
    return QStringLiteral("ALMC=%1 %2").arg(hexByte(almc), flags.join(QStringLiteral(", ")));
}

QString SerialEncoderDebugTab::encoderIdText(quint8 encoderId)
{
    const int bits = encoderIdToResolutionBits(encoderId);
    if (bits == 0) {
        return QStringLiteral("ID=%1 未知分辨率").arg(hexByte(encoderId));
    }
    return QStringLiteral("ID=%1, 分辨率=%2 bit").arg(hexByte(encoderId)).arg(bits);
}
