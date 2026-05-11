#include "spiencoderdebugtab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace
{
constexpr int DefaultBaudrate = 460800;
constexpr int DefaultSpiDividerCode = 5; // 64 分频
constexpr int DefaultTimeoutMs = 200;
constexpr int MaxLogBlockCount = 2000;
constexpr int MaxSamplingPoints = 1200;

QByteArray makeByteArray(std::initializer_list<int> values)
{
    QByteArray bytes;
    bytes.reserve(int(values.size()));
    for (const int value : values) {
        bytes.append(char(value & 0xFF));
    }
    return bytes;
}

struct SeriesData
{
    QString name;
    QColor color;
    QVector<QPointF> points;
    bool visible = true;
};
}

class MultiSeriesChartWidget : public QWidget
{
public:
    explicit MultiSeriesChartWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(240);
        setAutoFillBackground(true);
    }

    void clear()
    {
        for (SeriesData &series : m_series) {
            series.points.clear();
        }
        update();
    }

    void setSeriesVisible(const QString &name, bool visible)
    {
        SeriesData &series = ensureSeries(name);
        series.visible = visible;
        update();
    }

    void appendPoint(const QString &name, double x, double y)
    {
        SeriesData &series = ensureSeries(name);
        series.points.append(QPointF(x, y));
        if (series.points.size() > m_maxPoints) {
            series.points.remove(0, series.points.size() - m_maxPoints);
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
        painter.fillRect(full, QColor(20, 23, 28));
        painter.setPen(QPen(QColor(70, 76, 86), 1));
        painter.drawRect(full);

        const QRectF plot = full.adjusted(52, 12, -12, -36);
        painter.setPen(QPen(QColor(52, 58, 66), 1));
        for (int i = 0; i <= 4; ++i) {
            const qreal y = plot.top() + (plot.height() * i / 4.0);
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        for (int i = 0; i <= 6; ++i) {
            const qreal x = plot.left() + (plot.width() * i / 6.0);
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }

        double minX = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double minY = std::numeric_limits<double>::max();
        double maxY = std::numeric_limits<double>::lowest();
        bool hasPoint = false;

        for (const SeriesData &series : std::as_const(m_series)) {
            if (!series.visible) {
                continue;
            }
            for (const QPointF &pt : series.points) {
                minX = std::min(minX, pt.x());
                maxX = std::max(maxX, pt.x());
                minY = std::min(minY, pt.y());
                maxY = std::max(maxY, pt.y());
                hasPoint = true;
            }
        }

        if (!hasPoint) {
            painter.setPen(QColor(170, 176, 188));
            painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待采样数据..."));
            return;
        }

        if (qFuzzyCompare(minX, maxX)) {
            maxX += 1.0;
        }
        if (qFuzzyCompare(minY, maxY)) {
            minY -= 1.0;
            maxY += 1.0;
        }

        auto mapToPlot = [&](const QPointF &pt) -> QPointF {
            const qreal x = plot.left() + (pt.x() - minX) / (maxX - minX) * plot.width();
            const qreal y = plot.bottom() - (pt.y() - minY) / (maxY - minY) * plot.height();
            return QPointF(x, y);
        };

        for (const SeriesData &series : std::as_const(m_series)) {
            if (!series.visible || series.points.isEmpty()) {
                continue;
            }

            QPainterPath path;
            path.moveTo(mapToPlot(series.points.first()));
            for (int i = 1; i < series.points.size(); ++i) {
                path.lineTo(mapToPlot(series.points.at(i)));
            }
            painter.setPen(QPen(series.color, 1.7));
            painter.drawPath(path);
        }

        painter.setPen(QColor(176, 182, 194));
        painter.drawText(QRectF(full.left() + 6, full.bottom() - 22, 300, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("X: sample"));
        painter.drawText(QRectF(full.right() - 220, full.bottom() - 22, 210, 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("Y: raw/angle"));

        qreal legendX = full.left() + 10;
        const qreal legendY = full.top() + 8;
        for (const SeriesData &series : std::as_const(m_series)) {
            if (!series.visible) {
                continue;
            }
            painter.setBrush(series.color);
            painter.setPen(Qt::NoPen);
            painter.drawRect(QRectF(legendX, legendY, 8, 8));
            painter.setPen(QColor(206, 212, 224));
            painter.drawText(QRectF(legendX + 12, legendY - 4, 180, 18),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             series.name);
            legendX += 160;
        }
    }

private:
    SeriesData &ensureSeries(const QString &name)
    {
        static const QVector<QColor> palette = {
            QColor(72, 195, 255),
            QColor(255, 196, 88),
            QColor(160, 255, 128),
            QColor(255, 128, 164),
            QColor(170, 172, 255),
            QColor(255, 150, 98)};

        auto it = std::find_if(m_series.begin(), m_series.end(), [&](const SeriesData &series) {
            return series.name == name;
        });
        if (it != m_series.end()) {
            return *it;
        }

        SeriesData series;
        series.name = name;
        series.color = palette.at(m_series.size() % palette.size());
        m_series.push_back(series);
        return m_series.back();
    }

    QVector<SeriesData> m_series;
    int m_maxPoints = MaxSamplingPoints;
};

SpiEncoderDebugTab::SpiEncoderDebugTab(QWidget *parent)
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
    topLayout->addWidget(createConnectionGroup());
    topLayout->addWidget(createSpiGroup());
    topLayout->addWidget(createEncoderGroup());
    topLayout->addWidget(createRegisterGroup());

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(top);
    splitter->addWidget(createBottomArea());
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({320, 420});
    root->addWidget(splitter);

    m_protocolLogEdit->setMaximumBlockCount(MaxLogBlockCount);

    m_baudCombo->setCurrentText(QString::number(DefaultBaudrate));
    m_masterSlaveCombo->setCurrentIndex(0);
    m_modeCombo->setCurrentIndex(0);
    m_bitOrderCombo->setCurrentIndex(0);
    m_dividerCombo->setCurrentIndex(DefaultSpiDividerCode);
    m_samplingIntervalSpin->setValue(20);
    m_bctEdit->setText(QStringLiteral("0x20"));
    m_registerAddrEdit->setText(QStringLiteral("0x00"));
    m_registerValueEdit->setText(QStringLiteral("0x00"));

    refreshPorts();
    setSerialOpened(false, QStringLiteral("未连接"));
    m_samplingTimer->setInterval(m_samplingIntervalSpin->value());

    connect(m_samplingTimer, &QTimer::timeout, this, &SpiEncoderDebugTab::onSamplingTick);
    connect(m_samplingIntervalSpin, &QSpinBox::valueChanged, this, [this](int value) {
        m_samplingTimer->setInterval(value);
    });
}

SpiEncoderDebugTab::~SpiEncoderDebugTab()
{
    closeSerial();
}

QWidget *SpiEncoderDebugTab::createConnectionGroup()
{
    auto *group = new QGroupBox(QStringLiteral("1. 串口连接"), this);
    auto *layout = new QGridLayout(group);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(6);

    m_portCombo = new QComboBox(group);
    m_portCombo->setMinimumWidth(220);
    auto *refreshButton = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("刷新"), group);
    m_baudCombo = new QComboBox(group);
    m_baudCombo->setEditable(true);
    m_baudCombo->addItems({QStringLiteral("115200"),
                           QStringLiteral("230400"),
                           QStringLiteral("460800"),
                           QStringLiteral("921600")});
    m_openButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("打开"), group);
    m_closeButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("关闭"), group);
    m_probeButton = new QPushButton(QStringLiteral("识别调试器"), group);
    m_debuggerStateEdit = new QLineEdit(group);
    m_debuggerStateEdit->setReadOnly(true);

    layout->addWidget(new QLabel(QStringLiteral("串口"), group), 0, 0);
    layout->addWidget(m_portCombo, 0, 1);
    layout->addWidget(refreshButton, 0, 2);
    layout->addWidget(new QLabel(QStringLiteral("波特率"), group), 0, 3);
    layout->addWidget(m_baudCombo, 0, 4);
    layout->addWidget(m_openButton, 0, 5);
    layout->addWidget(m_closeButton, 0, 6);
    layout->addWidget(m_probeButton, 0, 7);
    layout->addWidget(new QLabel(QStringLiteral("设备状态"), group), 1, 0);
    layout->addWidget(m_debuggerStateEdit, 1, 1, 1, 7);

    connect(refreshButton, &QPushButton::clicked, this, &SpiEncoderDebugTab::refreshPorts);
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!openSerial(&error)) {
            appendMessage(QStringLiteral("串口打开失败: %1").arg(error));
            setSerialOpened(false, QStringLiteral("打开失败"));
            return;
        }
        setSerialOpened(true, QStringLiteral("已打开"));
        appendMessage(QStringLiteral("串口已打开"));
    });
    connect(m_closeButton, &QPushButton::clicked, this, [this]() {
        closeSerial();
        appendMessage(QStringLiteral("串口已关闭"));
    });
    connect(m_probeButton, &QPushButton::clicked, this, [this]() {
        QString error;
        const bool ok = probeDebugger(&error);
        if (ok) {
            m_debuggerStateEdit->setText(QStringLiteral("已识别 USB-SPI 调试器"));
        } else {
            m_debuggerStateEdit->setText(QStringLiteral("识别失败: %1").arg(error));
        }
    });
    return group;
}

QWidget *SpiEncoderDebugTab::createSpiGroup()
{
    auto *group = new QGroupBox(QStringLiteral("2. SPI 设置"), this);
    auto *layout = new QGridLayout(group);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(6);

    m_masterSlaveCombo = new QComboBox(group);
    m_masterSlaveCombo->addItem(QStringLiteral("主机 (0)"), 0);
    m_masterSlaveCombo->addItem(QStringLiteral("从机 (1)"), 1);

    m_modeCombo = new QComboBox(group);
    m_modeCombo->addItem(QStringLiteral("Mode 0 (00)"), 0);
    m_modeCombo->addItem(QStringLiteral("Mode 1 (01)"), 1);
    m_modeCombo->addItem(QStringLiteral("Mode 2 (10)"), 2);
    m_modeCombo->addItem(QStringLiteral("Mode 3 (11)"), 3);

    m_bitOrderCombo = new QComboBox(group);
    m_bitOrderCombo->addItem(QStringLiteral("MSB (0)"), 0);
    m_bitOrderCombo->addItem(QStringLiteral("LSB (1)"), 1);

    m_dividerCombo = new QComboBox(group);
    m_dividerCombo->addItem(QStringLiteral("2 分频"), 0);
    m_dividerCombo->addItem(QStringLiteral("4 分频"), 1);
    m_dividerCombo->addItem(QStringLiteral("8 分频"), 2);
    m_dividerCombo->addItem(QStringLiteral("16 分频"), 3);
    m_dividerCombo->addItem(QStringLiteral("32 分频"), 4);
    m_dividerCombo->addItem(QStringLiteral("64 分频"), 5);
    m_dividerCombo->addItem(QStringLiteral("128 分频"), 6);
    m_dividerCombo->addItem(QStringLiteral("256 分频"), 7);

    m_applySpiButton = new QPushButton(QStringLiteral("配置 SPI"), group);
    m_querySpiButton = new QPushButton(QStringLiteral("读取 SPI"), group);
    m_csLowButton = new QPushButton(QStringLiteral("CS 置低"), group);
    m_csHighButton = new QPushButton(QStringLiteral("CS 置高"), group);
    m_txHexEdit = new QLineEdit(group);
    m_txHexEdit->setPlaceholderText(QStringLiteral("AA 55 00 01"));
    m_rxHexEdit = new QLineEdit(group);
    m_rxHexEdit->setReadOnly(true);
    m_rawTransferButton = new QPushButton(QStringLiteral("SPI 单次收发"), group);

    layout->addWidget(new QLabel(QStringLiteral("主从"), group), 0, 0);
    layout->addWidget(m_masterSlaveCombo, 0, 1);
    layout->addWidget(new QLabel(QStringLiteral("模式"), group), 0, 2);
    layout->addWidget(m_modeCombo, 0, 3);
    layout->addWidget(new QLabel(QStringLiteral("位序"), group), 0, 4);
    layout->addWidget(m_bitOrderCombo, 0, 5);
    layout->addWidget(new QLabel(QStringLiteral("分频"), group), 0, 6);
    layout->addWidget(m_dividerCombo, 0, 7);
    layout->addWidget(m_applySpiButton, 0, 8);
    layout->addWidget(m_querySpiButton, 0, 9);
    layout->addWidget(m_csLowButton, 1, 8);
    layout->addWidget(m_csHighButton, 1, 9);
    layout->addWidget(new QLabel(QStringLiteral("TX"), group), 1, 0);
    layout->addWidget(m_txHexEdit, 1, 1, 1, 5);
    layout->addWidget(m_rawTransferButton, 1, 6, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("RX"), group), 2, 0);
    layout->addWidget(m_rxHexEdit, 2, 1, 1, 9);

    connect(m_applySpiButton, &QPushButton::clicked, this, &SpiEncoderDebugTab::onApplySpiClicked);
    connect(m_querySpiButton, &QPushButton::clicked, this, &SpiEncoderDebugTab::onQuerySpiClicked);
    connect(m_csLowButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!setCsLevel(false, &error)) {
            appendMessage(QStringLiteral("CS置低失败: %1").arg(error));
        }
    });
    connect(m_csHighButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!setCsLevel(true, &error)) {
            appendMessage(QStringLiteral("CS置高失败: %1").arg(error));
        }
    });
    connect(m_rawTransferButton, &QPushButton::clicked, this, &SpiEncoderDebugTab::onRawTransferClicked);
    return group;
}

QWidget *SpiEncoderDebugTab::createEncoderGroup()
{
    auto *group = new QGroupBox(QStringLiteral("3. 编码器调试"), this);
    auto *layout = new QGridLayout(group);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(6);

    m_axisEncoderCombo = new QComboBox(group);
    m_offAxisEncoderCombo = new QComboBox(group);
    const QVector<QPair<QString, int>> models = {
        {QStringLiteral("MT6826"), static_cast<int>(EncoderModel::Mt6826)},
        {QStringLiteral("MT6835"), static_cast<int>(EncoderModel::Mt6835)},
        {QStringLiteral("MA600"), static_cast<int>(EncoderModel::Ma600)},
        {QStringLiteral("KTH7812-X-N"), static_cast<int>(EncoderModel::Kth7812N)},
        {QStringLiteral("KTH7812-X-C"), static_cast<int>(EncoderModel::Kth7812C)}};
    for (const auto &item : models) {
        m_axisEncoderCombo->addItem(item.first, item.second);
        m_offAxisEncoderCombo->addItem(item.first, item.second);
    }

    m_axisEnabledCheck = new QCheckBox(QStringLiteral("轴编码器参与采样"), group);
    m_offAxisEnabledCheck = new QCheckBox(QStringLiteral("离轴编码器参与采样"), group);
    m_axisEnabledCheck->setChecked(true);
    m_offAxisEnabledCheck->setChecked(true);

    m_configureEncoderButton = new QPushButton(QStringLiteral("编码器配置"), group);
    m_readOnceButton = new QPushButton(QStringLiteral("单次读取"), group);
    m_startSamplingButton = new QPushButton(QStringLiteral("开始连续采样"), group);
    m_stopSamplingButton = new QPushButton(QStringLiteral("停止采样"), group);
    m_samplingIntervalSpin = new QSpinBox(group);
    m_samplingIntervalSpin->setRange(5, 2000);
    m_samplingIntervalSpin->setSuffix(QStringLiteral(" ms"));
    m_bctEdit = new QLineEdit(group);
    m_setBctButton = new QPushButton(QStringLiteral("设置离轴 BCT"), group);
    m_axisStateEdit = new QLineEdit(group);
    m_axisStateEdit->setReadOnly(true);
    m_offAxisStateEdit = new QLineEdit(group);
    m_offAxisStateEdit->setReadOnly(true);

    layout->addWidget(new QLabel(QStringLiteral("轴编码器选择"), group), 0, 0);
    layout->addWidget(m_axisEncoderCombo, 0, 1);
    layout->addWidget(m_axisEnabledCheck, 0, 2, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("离轴编码器选择"), group), 0, 4);
    layout->addWidget(m_offAxisEncoderCombo, 0, 5);
    layout->addWidget(m_offAxisEnabledCheck, 0, 6, 1, 2);
    layout->addWidget(m_configureEncoderButton, 1, 0, 1, 2);
    layout->addWidget(m_readOnceButton, 1, 2, 1, 2);
    layout->addWidget(m_startSamplingButton, 1, 4, 1, 2);
    layout->addWidget(m_stopSamplingButton, 1, 6, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("采样周期"), group), 2, 0);
    layout->addWidget(m_samplingIntervalSpin, 2, 1);
    layout->addWidget(new QLabel(QStringLiteral("离轴BCT"), group), 2, 2);
    layout->addWidget(m_bctEdit, 2, 3);
    layout->addWidget(m_setBctButton, 2, 4, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("轴状态"), group), 3, 0);
    layout->addWidget(m_axisStateEdit, 3, 1, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("离轴状态"), group), 3, 4);
    layout->addWidget(m_offAxisStateEdit, 3, 5, 1, 3);

    connect(m_configureEncoderButton, &QPushButton::clicked, this, &SpiEncoderDebugTab::onConfigureEncoderClicked);
    connect(m_readOnceButton, &QPushButton::clicked, this, &SpiEncoderDebugTab::onReadOnceClicked);
    connect(m_startSamplingButton, &QPushButton::clicked, this, [this]() { onSamplingToggled(true); });
    connect(m_stopSamplingButton, &QPushButton::clicked, this, [this]() { onSamplingToggled(false); });
    connect(m_setBctButton, &QPushButton::clicked, this, &SpiEncoderDebugTab::onSetBctClicked);
    return group;
}

QWidget *SpiEncoderDebugTab::createRegisterGroup()
{
    auto *group = new QGroupBox(QStringLiteral("4. 手动寄存器读写"), this);
    auto *layout = new QHBoxLayout(group);
    layout->setSpacing(8);

    m_registerTargetCombo = new QComboBox(group);
    m_registerTargetCombo->addItem(QStringLiteral("轴编码器"), static_cast<int>(EncoderSlot::Axis));
    m_registerTargetCombo->addItem(QStringLiteral("离轴编码器"), static_cast<int>(EncoderSlot::OffAxis));
    m_registerAddrEdit = new QLineEdit(group);
    m_registerAddrEdit->setMaximumWidth(120);
    m_registerAddrEdit->setPlaceholderText(QStringLiteral("0x00"));
    m_registerValueEdit = new QLineEdit(group);
    m_registerValueEdit->setMaximumWidth(120);
    m_registerValueEdit->setPlaceholderText(QStringLiteral("0x00"));
    m_readRegisterButton = new QPushButton(QStringLiteral("读寄存器"), group);
    m_writeRegisterButton = new QPushButton(QStringLiteral("写寄存器"), group);

    layout->addWidget(new QLabel(QStringLiteral("目标"), group));
    layout->addWidget(m_registerTargetCombo);
    layout->addWidget(new QLabel(QStringLiteral("地址"), group));
    layout->addWidget(m_registerAddrEdit);
    layout->addWidget(new QLabel(QStringLiteral("值"), group));
    layout->addWidget(m_registerValueEdit);
    layout->addWidget(m_readRegisterButton);
    layout->addWidget(m_writeRegisterButton);
    layout->addStretch(1);

    connect(m_readRegisterButton, &QPushButton::clicked, this, &SpiEncoderDebugTab::onReadRegisterClicked);
    connect(m_writeRegisterButton, &QPushButton::clicked, this, &SpiEncoderDebugTab::onWriteRegisterClicked);
    return group;
}

QWidget *SpiEncoderDebugTab::createBottomArea()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_chart = new MultiSeriesChartWidget(widget);
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
    m_protocolLogEdit->setMinimumHeight(140);
    layout->addWidget(m_protocolLogEdit, 2);

    connect(m_clearLogButton, &QPushButton::clicked, this, [this]() {
        m_protocolLogEdit->clear();
        m_chart->clear();
        m_sampleIndex = 0;
    });
    return widget;
}

void SpiEncoderDebugTab::refreshPorts()
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

void SpiEncoderDebugTab::setSerialOpened(bool opened, const QString &state)
{
    m_openButton->setEnabled(!opened);
    m_closeButton->setEnabled(opened);
    m_probeButton->setEnabled(opened);
    m_applySpiButton->setEnabled(opened);
    m_querySpiButton->setEnabled(opened);
    m_csLowButton->setEnabled(opened);
    m_csHighButton->setEnabled(opened);
    m_rawTransferButton->setEnabled(opened);
    m_configureEncoderButton->setEnabled(opened);
    m_readOnceButton->setEnabled(opened);
    m_startSamplingButton->setEnabled(opened);
    m_stopSamplingButton->setEnabled(opened);
    m_readRegisterButton->setEnabled(opened);
    m_writeRegisterButton->setEnabled(opened);
    m_setBctButton->setEnabled(opened);
    m_debuggerStateEdit->setText(state);
    if (!opened) {
        m_samplingTimer->stop();
    }
}

bool SpiEncoderDebugTab::openSerial(QString *error)
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
    return true;
}

void SpiEncoderDebugTab::closeSerial()
{
    if (m_serial->isOpen()) {
        m_serial->close();
    }
    setSerialOpened(false, QStringLiteral("未连接"));
}

bool SpiEncoderDebugTab::probeDebugger(QString *error)
{
    if (!m_serial->isOpen()) {
        if (error) {
            *error = QStringLiteral("串口未打开");
        }
        return false;
    }
    QString summary;
    return querySpiConfig(&summary, error);
}

bool SpiEncoderDebugTab::applySpiConfig(int masterSlave, int mode, int dividerCode, int bitOrderValue, QString *error)
{
    DebugFrame response;
    if (!sendCommand(0x02,
                     makeByteArray({0x00, masterSlave, mode, dividerCode}),
                     0x02,
                     DefaultTimeoutMs,
                     &response,
                     error)) {
        return false;
    }

    if (!sendCommand(0x0B, makeByteArray({0x00, bitOrderValue}), 0x0B, DefaultTimeoutMs, &response, error)) {
        return false;
    }
    return true;
}

bool SpiEncoderDebugTab::querySpiConfig(QString *summary, QString *error)
{
    DebugFrame response;
    if (!sendCommand(0x02, makeByteArray({0x01}), 0x02, DefaultTimeoutMs, &response, error)) {
        return false;
    }
    if (response.payload.size() < 4) {
        if (error) {
            *error = QStringLiteral("SPI配置响应长度异常");
        }
        return false;
    }
    const int masterSlave = quint8(response.payload.at(1));
    const int mode = quint8(response.payload.at(2));
    const int divider = quint8(response.payload.at(3));

    DebugFrame bitResponse;
    if (!sendCommand(0x0B, makeByteArray({0x01, 0x00}), 0x0B, DefaultTimeoutMs, &bitResponse, error)) {
        return false;
    }
    if (bitResponse.payload.size() < 2) {
        if (error) {
            *error = QStringLiteral("位序响应长度异常");
        }
        return false;
    }
    const int bitOrder = quint8(bitResponse.payload.at(1));

    const QString info = QStringLiteral("主从=%1, mode=%2, 分频Code=%3, 位序=%4")
                             .arg(masterSlave)
                             .arg(mode)
                             .arg(divider)
                             .arg(bitOrder == 0 ? QStringLiteral("MSB") : QStringLiteral("LSB"));
    if (summary) {
        *summary = info;
    }
    m_debuggerStateEdit->setText(QStringLiteral("已识别: %1").arg(info));
    return true;
}

bool SpiEncoderDebugTab::setCsLevel(bool high, QString *error)
{
    return sendCommandNoResponse(0x03, makeByteArray({0x00, high ? 1 : 0}), DefaultTimeoutMs, error);
}

bool SpiEncoderDebugTab::spiTransfer(const QByteArray &tx, QByteArray *rx, QString *error)
{
    DebugFrame response;
    QByteArray payload;
    payload.reserve(tx.size() + 1);
    payload.append(char(tx.size() & 0xFF));
    payload.append(tx);
    if (!sendCommand(0x04, payload, 0x05, DefaultTimeoutMs, &response, error)) {
        return false;
    }
    if (response.payload.isEmpty()) {
        if (error) {
            *error = QStringLiteral("SPI响应为空");
        }
        return false;
    }
    const int dataLen = quint8(response.payload.at(0));
    if (response.payload.size() - 1 < dataLen) {
        if (error) {
            *error = QStringLiteral("SPI响应长度不足");
        }
        return false;
    }
    if (rx) {
        *rx = response.payload.mid(1, dataLen);
    }
    return true;
}

bool SpiEncoderDebugTab::performSpiTransaction(const QByteArray &tx,
                                               QByteArray *rx,
                                               int timeoutMs,
                                               bool autoToggleCs,
                                               QString *error)
{
    Q_UNUSED(timeoutMs)
    if (autoToggleCs) {
        if (!setCsLevel(false, error)) {
            return false;
        }
    }

    const bool ok = spiTransfer(tx, rx, error);

    if (autoToggleCs) {
        QString csError;
        if (!setCsLevel(true, &csError)) {
            appendMessage(QStringLiteral("CS置高失败: %1").arg(csError));
        }
    }
    return ok;
}

bool SpiEncoderDebugTab::sendCommand(quint8 cmd,
                                     const QByteArray &payload,
                                     quint8 expectedCmd,
                                     int timeoutMs,
                                     DebugFrame *response,
                                     QString *error)
{
    if (!m_serial->isOpen()) {
        if (error) {
            *error = QStringLiteral("串口未打开");
        }
        return false;
    }

    const QByteArray frame = buildFrame(cmd, payload);
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt > 0) {
            appendMessage(QStringLiteral("重发 cmd=0x%1").arg(QString::number(cmd, 16).toUpper()));
        }

        m_serial->readAll();
        if (m_serial->write(frame) != frame.size()) {
            if (error) {
                *error = QStringLiteral("串口写入失败: %1").arg(m_serial->errorString());
            }
            return false;
        }
        if (!m_serial->waitForBytesWritten(timeoutMs)) {
            if (error) {
                *error = QStringLiteral("写入超时");
            }
            continue;
        }
        appendProtoLog(QStringLiteral("TX"), frame);

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            DebugFrame frameIn;
            QString readError;
            if (!readFrame(&frameIn, timeoutMs - timer.elapsed(), &readError)) {
                if (!readError.isEmpty() && readError != QStringLiteral("超时")) {
                    appendMessage(readError);
                }
                break;
            }
            appendProtoLog(QStringLiteral("RX"), frameIn.raw);
            if (frameIn.cmd == expectedCmd) {
                if (response) {
                    *response = frameIn;
                }
                return true;
            }
            appendMessage(QStringLiteral("忽略非目标响应 cmd=0x%1").arg(QString::number(frameIn.cmd, 16).toUpper()));
        }
    }

    if (error) {
        *error = QStringLiteral("无响应");
    }
    return false;
}

bool SpiEncoderDebugTab::sendCommandNoResponse(quint8 cmd,
                                               const QByteArray &payload,
                                               int timeoutMs,
                                               QString *error)
{
    if (!m_serial->isOpen()) {
        if (error) {
            *error = QStringLiteral("串口未打开");
        }
        return false;
    }

    const QByteArray frame = buildFrame(cmd, payload);
    if (m_serial->write(frame) != frame.size()) {
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
    appendProtoLog(QStringLiteral("TX"), frame);
    return true;
}

bool SpiEncoderDebugTab::readFrame(DebugFrame *frame, int timeoutMs, QString *error)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QString parseError;
        if (tryExtractFrame(frame, &parseError)) {
            return true;
        }
        if (!parseError.isEmpty()) {
            if (error) {
                *error = parseError;
            }
            return false;
        }

        const int remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            break;
        }
        if (!m_serial->waitForReadyRead(remaining)) {
            continue;
        }
        m_rxBuffer.append(m_serial->readAll());
    }

    if (error) {
        *error = QStringLiteral("超时");
    }
    return false;
}

bool SpiEncoderDebugTab::tryExtractFrame(DebugFrame *frame, QString *error)
{
    while (m_rxBuffer.size() >= 2) {
        const int header = m_rxBuffer.indexOf(QByteArray::fromHex("55AA"));
        if (header < 0) {
            m_rxBuffer.clear();
            return false;
        }
        if (header > 0) {
            m_rxBuffer.remove(0, header);
        }
        if (m_rxBuffer.size() < 3) {
            return false;
        }

        const int len = quint8(m_rxBuffer.at(2));
        if (len < 7) {
            m_rxBuffer.remove(0, 1);
            if (error) {
                *error = QStringLiteral("帧长度无效");
            }
            continue;
        }
        if (m_rxBuffer.size() < len) {
            return false;
        }

        const QByteArray raw = m_rxBuffer.left(len);
        m_rxBuffer.remove(0, len);
        if (quint8(raw.at(len - 1)) != 0x5A) {
            if (error) {
                *error = QStringLiteral("帧尾错误");
            }
            continue;
        }

        quint8 xorValue = 0;
        for (int i = 0; i < len - 2; ++i) {
            xorValue ^= quint8(raw.at(i));
        }
        const quint8 recvXor = quint8(raw.at(len - 2));
        if (xorValue != recvXor) {
            if (error) {
                *error = QStringLiteral("XOR 校验错误");
            }
            continue;
        }

        frame->raw = raw;
        frame->isCheck = quint8(raw.at(3));
        frame->cmd = quint8(raw.at(4));
        frame->payload = raw.mid(5, len - 7);
        return true;
    }
    return false;
}

QByteArray SpiEncoderDebugTab::buildFrame(quint8 cmd, const QByteArray &payload)
{
    QByteArray frame;
    frame.reserve(payload.size() + 7);
    frame.append(char(0x55));
    frame.append(char(0xAA));
    frame.append(char(0x00));
    frame.append(char(0x01));
    frame.append(char(cmd));
    frame.append(payload);
    frame.append(char(0x00));
    frame.append(char(0x5A));
    frame[2] = char(frame.size());
    quint8 xorValue = 0;
    for (int i = 0; i < frame.size() - 2; ++i) {
        xorValue ^= quint8(frame.at(i));
    }
    frame[frame.size() - 2] = char(xorValue);
    return frame;
}

quint8 SpiEncoderDebugTab::xorChecksum(const QByteArray &frameWithoutChecksum)
{
    quint8 value = 0;
    for (char byte : frameWithoutChecksum) {
        value ^= quint8(byte);
    }
    return value;
}

QString SpiEncoderDebugTab::bytesToHex(const QByteArray &bytes)
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

bool SpiEncoderDebugTab::parseHexText(const QString &text, QByteArray *out, QString *error)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (error) {
            *error = QStringLiteral("请输入十六进制字节");
        }
        return false;
    }

    const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]+")), Qt::SkipEmptyParts);
    QByteArray parsed;
    parsed.reserve(parts.size());
    for (const QString &part : parts) {
        bool ok = false;
        const int value = part.toInt(&ok, 16);
        if (!ok || value < 0 || value > 0xFF) {
            if (error) {
                *error = QStringLiteral("非法字节: %1").arg(part);
            }
            return false;
        }
        parsed.append(char(value));
    }
    if (parsed.isEmpty()) {
        if (error) {
            *error = QStringLiteral("未解析到有效字节");
        }
        return false;
    }
    if (out) {
        *out = parsed;
    }
    return true;
}

bool SpiEncoderDebugTab::allBytesEqual(const QByteArray &data, char value)
{
    if (data.isEmpty()) {
        return false;
    }
    for (char byte : data) {
        if (byte != value) {
            return false;
        }
    }
    return true;
}

void SpiEncoderDebugTab::appendProtoLog(const QString &prefix, const QByteArray &frame)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_protocolLogEdit->appendPlainText(QStringLiteral("[%1] %2 %3").arg(ts, prefix, bytesToHex(frame)));
}

void SpiEncoderDebugTab::appendMessage(const QString &message)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_protocolLogEdit->appendPlainText(QStringLiteral("[%1] %2").arg(ts, message));
}

void SpiEncoderDebugTab::onApplySpiClicked()
{
    QString error;
    const int master = m_masterSlaveCombo->currentData().toInt();
    const int mode = m_modeCombo->currentData().toInt();
    const int divider = m_dividerCombo->currentData().toInt();
    const int bitOrder = m_bitOrderCombo->currentData().toInt();
    if (!applySpiConfig(master, mode, divider, bitOrder, &error)) {
        appendMessage(QStringLiteral("SPI配置失败: %1").arg(error));
        return;
    }
    appendMessage(QStringLiteral("SPI配置成功: master=%1 mode=%2 bitOrder=%3 dividerCode=%4")
                      .arg(master)
                      .arg(mode)
                      .arg(bitOrder)
                      .arg(divider));
}

void SpiEncoderDebugTab::onQuerySpiClicked()
{
    QString summary;
    QString error;
    if (!querySpiConfig(&summary, &error)) {
        appendMessage(QStringLiteral("SPI读取失败: %1").arg(error));
        return;
    }
    appendMessage(QStringLiteral("SPI当前配置: %1").arg(summary));
}

void SpiEncoderDebugTab::onRawTransferClicked()
{
    QByteArray tx;
    QString error;
    if (!parseHexText(m_txHexEdit->text(), &tx, &error)) {
        appendMessage(QStringLiteral("TX解析失败: %1").arg(error));
        return;
    }
    QByteArray rx;
    if (!performSpiTransaction(tx, &rx, DefaultTimeoutMs, true, &error)) {
        appendMessage(QStringLiteral("SPI收发失败: %1").arg(error));
        return;
    }
    m_rxHexEdit->setText(bytesToHex(rx));
    appendMessage(QStringLiteral("SPI收发成功"));
}

void SpiEncoderDebugTab::onConfigureEncoderClicked()
{
    QString error;
    if (!configureForModel(axisModel(), &error)) {
        appendMessage(QStringLiteral("轴编码器配置失败: %1").arg(error));
        return;
    }
    if (!configureForModel(offAxisModel(), &error)) {
        appendMessage(QStringLiteral("离轴编码器配置失败: %1").arg(error));
        return;
    }
    appendMessage(QStringLiteral("编码器配置完成"));
}

void SpiEncoderDebugTab::onReadOnceClicked()
{
    const EncoderReadResult axisResult = readEncoder(axisModel());
    setSlotStatus(EncoderSlot::Axis, axisResult);
    const EncoderReadResult offAxisResult = readEncoder(offAxisModel());
    setSlotStatus(EncoderSlot::OffAxis, offAxisResult);
}

void SpiEncoderDebugTab::onSamplingToggled(bool running)
{
    if (running) {
        if (!m_serial->isOpen()) {
            appendMessage(QStringLiteral("串口未打开，无法采样"));
            return;
        }
        m_sampleIndex = 0;
        m_chart->clear();
        m_samplingTimer->start();
        appendMessage(QStringLiteral("连续采样已启动"));
    } else {
        m_samplingTimer->stop();
        appendMessage(QStringLiteral("连续采样已停止"));
    }
}

void SpiEncoderDebugTab::onSamplingTick()
{
    ++m_sampleIndex;
    if (m_axisEnabledCheck->isChecked()) {
        const EncoderReadResult result = readEncoder(axisModel());
        setSlotStatus(EncoderSlot::Axis, result);
        if (result.ok) {
            m_chart->appendPoint(QStringLiteral("轴-angle"), m_sampleIndex, result.angleDeg);
            m_chart->appendPoint(QStringLiteral("轴-raw"), m_sampleIndex, double(result.raw));
        }
    }

    if (m_offAxisEnabledCheck->isChecked()) {
        const EncoderReadResult result = readEncoder(offAxisModel());
        setSlotStatus(EncoderSlot::OffAxis, result);
        if (result.ok) {
            m_chart->appendPoint(QStringLiteral("离轴-angle"), m_sampleIndex, result.angleDeg);
            m_chart->appendPoint(QStringLiteral("离轴-raw"), m_sampleIndex, double(result.raw));
        }
    }

    m_chart->setSeriesVisible(QStringLiteral("轴-angle"), m_axisEnabledCheck->isChecked());
    m_chart->setSeriesVisible(QStringLiteral("轴-raw"), m_axisEnabledCheck->isChecked());
    m_chart->setSeriesVisible(QStringLiteral("离轴-angle"), m_offAxisEnabledCheck->isChecked());
    m_chart->setSeriesVisible(QStringLiteral("离轴-raw"), m_offAxisEnabledCheck->isChecked());
}

void SpiEncoderDebugTab::onReadRegisterClicked()
{
    bool ok = false;
    const quint16 addr = quint16(m_registerAddrEdit->text().trimmed().toUInt(&ok, 0));
    if (!ok) {
        appendMessage(QStringLiteral("寄存器地址格式错误"));
        return;
    }

    quint16 value = 0;
    QString error;
    const EncoderSlot slot = static_cast<EncoderSlot>(m_registerTargetCombo->currentData().toInt());
    if (!readRegister(slotModel(slot), addr, &value, &error)) {
        appendMessage(QStringLiteral("%1读寄存器失败: %2").arg(slotName(slot), error));
        return;
    }
    m_registerValueEdit->setText(QStringLiteral("0x%1").arg(value, 0, 16).toUpper());
    appendMessage(QStringLiteral("%1寄存器[0x%2] = 0x%3")
                      .arg(slotName(slot))
                      .arg(addr, 0, 16)
                      .arg(value, 0, 16)
                      .toUpper());
}

void SpiEncoderDebugTab::onWriteRegisterClicked()
{
    bool okAddr = false;
    bool okValue = false;
    const quint16 addr = quint16(m_registerAddrEdit->text().trimmed().toUInt(&okAddr, 0));
    const quint16 value = quint16(m_registerValueEdit->text().trimmed().toUInt(&okValue, 0));
    if (!okAddr || !okValue) {
        appendMessage(QStringLiteral("寄存器地址或值格式错误"));
        return;
    }

    QString error;
    const EncoderSlot slot = static_cast<EncoderSlot>(m_registerTargetCombo->currentData().toInt());
    if (!writeRegister(slotModel(slot), addr, value, &error)) {
        appendMessage(QStringLiteral("%1写寄存器失败: %2").arg(slotName(slot), error));
        return;
    }
    appendMessage(QStringLiteral("%1写寄存器完成 [0x%2] <- 0x%3")
                      .arg(slotName(slot))
                      .arg(addr, 0, 16)
                      .arg(value, 0, 16)
                      .toUpper());
}

void SpiEncoderDebugTab::onSetBctClicked()
{
    bool ok = false;
    const quint16 bctValue = quint16(m_bctEdit->text().trimmed().toUInt(&ok, 0));
    if (!ok) {
        appendMessage(QStringLiteral("BCT 参数格式错误"));
        return;
    }

    QString error;
    if (!writeRegister(offAxisModel(), 0x06, bctValue, &error)) {
        appendMessage(QStringLiteral("设置离轴 BCT 失败: %1").arg(error));
        return;
    }
    appendMessage(QStringLiteral("设置离轴 BCT 成功: 0x%1").arg(bctValue, 0, 16).toUpper());
}

SpiEncoderDebugTab::EncoderModel SpiEncoderDebugTab::axisModel() const
{
    return static_cast<EncoderModel>(m_axisEncoderCombo->currentData().toInt());
}

SpiEncoderDebugTab::EncoderModel SpiEncoderDebugTab::offAxisModel() const
{
    return static_cast<EncoderModel>(m_offAxisEncoderCombo->currentData().toInt());
}

SpiEncoderDebugTab::EncoderModel SpiEncoderDebugTab::slotModel(EncoderSlot slot) const
{
    return slot == EncoderSlot::Axis ? axisModel() : offAxisModel();
}

QString SpiEncoderDebugTab::slotName(EncoderSlot slot) const
{
    return slot == EncoderSlot::Axis ? QStringLiteral("轴编码器") : QStringLiteral("离轴编码器");
}

bool SpiEncoderDebugTab::configureForModel(EncoderModel model, QString *error)
{
    int mode = 0;
    switch (model) {
    case EncoderModel::Mt6826:
    case EncoderModel::Mt6835:
    case EncoderModel::Kth7812N:
    case EncoderModel::Kth7812C:
        mode = 3;
        break;
    case EncoderModel::Ma600:
        mode = 0;
        break;
    }

    return applySpiConfig(0, mode, DefaultSpiDividerCode, 0, error);
}

SpiEncoderDebugTab::EncoderReadResult SpiEncoderDebugTab::readEncoder(EncoderModel model)
{
    switch (model) {
    case EncoderModel::Mt6826:
        return readMt6826();
    case EncoderModel::Mt6835:
        return readMt6835();
    case EncoderModel::Ma600:
        return readMa600();
    case EncoderModel::Kth7812N:
        return readKth7812(false);
    case EncoderModel::Kth7812C:
        return readKth7812(true);
    }
    EncoderReadResult result;
    result.errorText = QStringLiteral("未知编码器模型");
    return result;
}

SpiEncoderDebugTab::EncoderReadResult SpiEncoderDebugTab::readMt6826()
{
    EncoderReadResult result;
    QString error;
    QByteArray rx;
    if (!performSpiTransaction(QByteArray::fromHex("A003000000"), &rx, DefaultTimeoutMs, true, &error)) {
        result.errorText = error;
        return result;
    }
    result.rxHex = bytesToHex(rx);
    if (allBytesEqual(rx, 0x00)) {
        result.errorText = QStringLiteral("全0");
        return result;
    }
    if (allBytesEqual(rx, char(0xFF))) {
        result.errorText = QStringLiteral("全FF");
        return result;
    }
    if (rx.size() < 5) {
        result.errorText = QStringLiteral("响应长度不足");
        return result;
    }

    const quint32 raw15 = (quint32(quint8(rx.at(2))) << 7) | (quint32(quint8(rx.at(3))) >> 1);
    const quint8 status = quint8(rx.at(4)) & 0x07;
    result.raw = raw15;
    result.angleDeg = double(raw15) * 360.0 / 32768.0;
    result.statusText = status == 0 ? QStringLiteral("正常") : QStringLiteral("状态报警:0x%1").arg(status, 1, 16).toUpper();
    result.ok = true;
    return result;
}

SpiEncoderDebugTab::EncoderReadResult SpiEncoderDebugTab::readMt6835()
{
    EncoderReadResult result;
    QString error;
    QByteArray rx;
    if (!performSpiTransaction(QByteArray::fromHex("A00300000000"), &rx, DefaultTimeoutMs, true, &error)) {
        result.errorText = error;
        return result;
    }
    result.rxHex = bytesToHex(rx);
    if (allBytesEqual(rx, 0x00)) {
        result.errorText = QStringLiteral("全0");
        return result;
    }
    if (allBytesEqual(rx, char(0xFF))) {
        result.errorText = QStringLiteral("全FF");
        return result;
    }
    if (rx.size() < 5) {
        result.errorText = QStringLiteral("响应长度不足");
        return result;
    }

    if (rx.size() >= 6) {
        const quint8 expectedCrc = crc8_07(rx.mid(2, 3));
        const quint8 recvCrc = quint8(rx.at(5));
        if (expectedCrc != recvCrc) {
            result.errorText = QStringLiteral("CRC错误");
            return result;
        }
    }

    const quint32 raw21 = (quint32(quint8(rx.at(2))) << 13) | (quint32(quint8(rx.at(3))) << 5) | (quint32(quint8(rx.at(4))) >> 3);
    const quint8 status = quint8(rx.at(4)) & 0x07;
    result.raw = raw21;
    result.angleDeg = double(raw21) * 360.0 / 2097152.0;
    result.statusText = status == 0 ? QStringLiteral("正常") : QStringLiteral("状态报警:0x%1").arg(status, 1, 16).toUpper();
    result.ok = true;
    return result;
}

SpiEncoderDebugTab::EncoderReadResult SpiEncoderDebugTab::readMa600()
{
    EncoderReadResult result;
    QString error;

    QByteArray rx;
    if (!performSpiTransaction(QByteArray::fromHex("0000"), &rx, DefaultTimeoutMs, true, &error)) {
        result.errorText = error;
        return result;
    }
    result.rxHex = bytesToHex(rx);
    if (allBytesEqual(rx, 0x00)) {
        result.errorText = QStringLiteral("全0");
        return result;
    }
    if (allBytesEqual(rx, char(0xFF))) {
        result.errorText = QStringLiteral("全FF");
        return result;
    }
    if (rx.size() < 2) {
        result.errorText = QStringLiteral("响应长度不足");
        return result;
    }

    const quint32 raw16 = (quint32(quint8(rx.at(0))) << 8) | quint32(quint8(rx.at(1)));
    result.raw = raw16;
    result.angleDeg = double(raw16) * 360.0 / 65536.0;
    result.statusText = QStringLiteral("正常");
    result.ok = true;
    return result;
}

SpiEncoderDebugTab::EncoderReadResult SpiEncoderDebugTab::readKth7812(bool crc4)
{
    EncoderReadResult result;
    QString error;

    QByteArray rx;
    if (!performSpiTransaction(QByteArray::fromHex("0000"), &rx, DefaultTimeoutMs, true, &error)) {
        result.errorText = error;
        return result;
    }
    result.rxHex = bytesToHex(rx);
    if (allBytesEqual(rx, 0x00)) {
        result.errorText = QStringLiteral("全0");
        return result;
    }
    if (allBytesEqual(rx, char(0xFF))) {
        result.errorText = QStringLiteral("全FF");
        return result;
    }
    if (rx.size() < 2) {
        result.errorText = QStringLiteral("响应长度不足");
        return result;
    }

    const quint16 raw16 = (quint16(quint8(rx.at(0))) << 8) | quint16(quint8(rx.at(1)));
    if (crc4) {
        const quint16 angle12 = raw16 >> 4;
        const quint8 recvCrc = raw16 & 0x0F;
        const QByteArray crcData = makeByteArray({(angle12 >> 4) & 0xFF, (angle12 & 0x0F) << 4});
        const quint8 calc = crc4_3(crcData, 12);
        if (calc != recvCrc) {
            result.errorText = QStringLiteral("CRC错误");
            return result;
        }
        result.raw = angle12;
        result.angleDeg = double(angle12) * 360.0 / 4096.0;
    } else {
        result.raw = raw16;
        result.angleDeg = double(raw16) * 360.0 / 65536.0;
    }
    result.statusText = QStringLiteral("正常");
    result.ok = true;
    return result;
}

bool SpiEncoderDebugTab::readRegister(EncoderModel model, quint16 addr, quint16 *value, QString *error)
{
    QString configError;
    if (!configureForModel(model, &configError)) {
        if (error) {
            *error = configError;
        }
        return false;
    }

    QByteArray rx;
    switch (model) {
    case EncoderModel::Ma600: {
        QByteArray step1;
        if (!performSpiTransaction(makeByteArray({0xD2, int(addr & 0xFF)}), &step1, DefaultTimeoutMs, true, error)) {
            return false;
        }
        if (!performSpiTransaction(QByteArray::fromHex("0000"), &rx, DefaultTimeoutMs, true, error)) {
            return false;
        }
        if (rx.size() < 2) {
            if (error) {
                *error = QStringLiteral("MA600寄存器读响应不足");
            }
            return false;
        }
        *value = (quint16(quint8(rx.at(0))) << 8) | quint16(quint8(rx.at(1)));
        return true;
    }
    case EncoderModel::Kth7812N:
    case EncoderModel::Kth7812C: {
        const quint16 cmd = 0x4000 | ((addr & 0x3F) << 8);
        QByteArray tx = makeByteArray({(cmd >> 8) & 0xFF, cmd & 0xFF});
        QByteArray step1;
        if (!performSpiTransaction(tx, &step1, DefaultTimeoutMs, true, error)) {
            return false;
        }
        if (!performSpiTransaction(QByteArray::fromHex("0000"), &rx, DefaultTimeoutMs, true, error)) {
            return false;
        }
        if (rx.size() < 2) {
            if (error) {
                *error = QStringLiteral("KTH7812寄存器读响应不足");
            }
            return false;
        }
        *value = (quint16(quint8(rx.at(0))) << 8) | quint16(quint8(rx.at(1)));
        return true;
    }
    case EncoderModel::Mt6826:
    case EncoderModel::Mt6835:
        if (error) {
            *error = QStringLiteral("该型号未定义通用寄存器读指令");
        }
        return false;
    }
    return false;
}

bool SpiEncoderDebugTab::writeRegister(EncoderModel model, quint16 addr, quint16 value, QString *error)
{
    QString configError;
    if (!configureForModel(model, &configError)) {
        if (error) {
            *error = configError;
        }
        return false;
    }

    switch (model) {
    case EncoderModel::Ma600: {
        QByteArray rx;
        if (!performSpiTransaction(QByteArray::fromHex("EA54"), &rx, DefaultTimeoutMs, true, error)) {
            return false;
        }
        QByteArray tx = makeByteArray({addr & 0xFF, value & 0xFF});
        if (!performSpiTransaction(tx, &rx, DefaultTimeoutMs, true, error)) {
            return false;
        }
        if (!performSpiTransaction(QByteArray::fromHex("0000"), &rx, DefaultTimeoutMs, true, error)) {
            return false;
        }
        return true;
    }
    case EncoderModel::Kth7812N:
    case EncoderModel::Kth7812C: {
        const quint16 cmd = 0x8000 | ((addr & 0x3F) << 8) | (value & 0xFF);
        QByteArray tx = makeByteArray({(cmd >> 8) & 0xFF, cmd & 0xFF});
        QByteArray rx;
        if (!performSpiTransaction(tx, &rx, DefaultTimeoutMs, true, error)) {
            return false;
        }
        QThread::msleep(20);
        return true;
    }
    case EncoderModel::Mt6826:
    case EncoderModel::Mt6835:
        if (error) {
            *error = QStringLiteral("该型号未定义通用寄存器写指令");
        }
        return false;
    }
    return false;
}

quint8 SpiEncoderDebugTab::crc8_07(const QByteArray &bytes)
{
    quint8 crc = 0;
    for (char byte : bytes) {
        crc ^= quint8(byte);
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x80) ? quint8((crc << 1) ^ 0x07) : quint8(crc << 1);
        }
    }
    return crc;
}

quint8 SpiEncoderDebugTab::crc4_3(const QByteArray &bytes, int bitCount)
{
    quint8 crc = 0;
    for (int bit = 0; bit < bitCount; ++bit) {
        const int byteIndex = bit / 8;
        const int bitInByte = 7 - (bit % 8);
        const quint8 inputBit = (quint8(bytes.at(byteIndex)) >> bitInByte) & 1U;
        const quint8 msb = (crc >> 3) & 1U;
        crc = quint8((crc << 1) & 0x0F);
        if ((msb ^ inputBit) != 0U) {
            crc ^= 0x03;
        }
    }
    return crc & 0x0F;
}

void SpiEncoderDebugTab::setSlotStatus(EncoderSlot slot, const EncoderReadResult &result)
{
    QLineEdit *stateEdit = slot == EncoderSlot::Axis ? m_axisStateEdit : m_offAxisStateEdit;
    if (!result.ok) {
        QString text = result.errorText;
        if (text.isEmpty()) {
            text = QStringLiteral("无响应");
        }
        stateEdit->setText(text);
        appendMessage(QStringLiteral("%1读取失败: %2").arg(slotName(slot), text));
        return;
    }

    QString status = result.statusText;
    if (status.isEmpty()) {
        status = QStringLiteral("正常");
    }
    stateEdit->setText(QStringLiteral("angle=%1 raw=%2 %3")
                           .arg(result.angleDeg, 0, 'f', 3)
                           .arg(result.raw)
                           .arg(status));
    appendMessage(QStringLiteral("%1 angle=%2 raw=%3 %4")
                      .arg(slotName(slot))
                      .arg(result.angleDeg, 0, 'f', 3)
                      .arg(result.raw)
                      .arg(status));
}
