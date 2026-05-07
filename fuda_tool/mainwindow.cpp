#include "mainwindow.h"

#include "calibrationrunner.h"
#include "canidcodec.h"
#include "canservice.h"
#include "payloadcodec.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMetaType>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTextCursor>
#include <QThread>
#include <QVBoxLayout>

#include <limits>

namespace
{
constexpr int LogRowLimit = 2000;
constexpr int Value1PartCount = 8;
constexpr int Value1SpeedIndex = 0;
constexpr int Value1PositionIndex = 1;
constexpr int Value1IqCurrentIndex = 7;
constexpr int CommandActionColumnWidth = 96;
constexpr int ConfigActionColumnWidth = 86;
constexpr int CalibrationOutputMaxBlockCount = 3000;
constexpr int LogTimeColumnWidth = 92;
constexpr int LogDirectionColumnWidth = 48;
constexpr int LogIdColumnWidth = 72;
constexpr int LogNodeColumnWidth = 52;
constexpr int LogCommandColumnWidth = 150;
constexpr int LogDlcColumnWidth = 48;
constexpr int LogDataColumnWidth = 640;
constexpr int LogParsedColumnWidth = 1080;

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QString idText(quint32 frameId)
{
    return QStringLiteral("0x%1").arg(frameId, 3, 16, QLatin1Char('0')).toUpper();
}

QString dlcText(const QCanBusFrame &frame)
{
    return QString::number(frame.payload().size());
}

QString returnCodeText(const QByteArray &payload)
{
    qint32 result = 0;
    if (!PayloadCodec::decodeInt32(payload, 0, &result)) {
        return QStringLiteral("返回值解析失败");
    }
    return QStringLiteral("返回值=%1%2").arg(result).arg(result == 0 ? QStringLiteral("，成功") : QStringLiteral("，失败"));
}

bool isReturnCodeCommand(quint8 commandId)
{
    switch (commandId) {
    case 0:
    case 1:
    case 5:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 19:
    case 20:
    case 29:
    case 30:
    case 31:
        return true;
    default:
        return false;
    }
}

QStringList splitArguments(const QString &text)
{
    return text.trimmed().split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
}

QStringList value1PartNames()
{
    return {QStringLiteral("速度"),
            QStringLiteral("位置"),
            QStringLiteral("hall偏移"),
            QStringLiteral("hall值"),
            QStringLiteral("status"),
            QStringLiteral("errors"),
            QStringLiteral("板载NTC"),
            QStringLiteral("Iq电流")};
}

bool isSignedValue1Part(int index)
{
    return index == Value1SpeedIndex || index == Value1PositionIndex || index == Value1IqCurrentIndex;
}

QString value1PartText(const QByteArray &payload, int index)
{
    quint16 rawValue = 0;
    if (!PayloadCodec::decodeUInt16(payload, index * 2, &rawValue)) {
        return QStringLiteral("-");
    }
    if (isSignedValue1Part(index)) {
        return QString::number(static_cast<qint16>(rawValue));
    }
    return QString::number(rawValue);
}

QLabel *valueLabel(const QString &text = QStringLiteral("-"))
{
    auto *label = new QLabel(text);
    label->setMinimumWidth(92);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

bool shouldFollowLogTail(const QTableWidget *table)
{
    if (!table || !table->verticalScrollBar()) {
        return false;
    }

    const QScrollBar *scrollBar = table->verticalScrollBar();
    return scrollBar->value() >= scrollBar->maximum() - 1;
}

struct TitledPanel
{
    QWidget *panel = nullptr;
    QWidget *body = nullptr;
};

TitledPanel createTitledPanel(const QString &title, QWidget *parent)
{
    auto *panel = new QWidget(parent);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);

    auto *titleLabel = new QLabel(title, panel);
    titleLabel->setObjectName(QStringLiteral("panelTitle"));

    auto *body = new QWidget(panel);
    body->setObjectName(QStringLiteral("panelBody"));

    panelLayout->addWidget(titleLabel);
    panelLayout->addWidget(body, 1);
    return {panel, body};
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_canThread(new QThread(this))
    , m_canService(new CanService)
{
    qRegisterMetaType<QCanBusFrame>("QCanBusFrame");
    qRegisterMetaType<quint8>("quint8");

    m_canThread->setObjectName(QStringLiteral("CanServiceThread"));
    m_canService->moveToThread(m_canThread);
    connect(m_canThread, &QThread::finished, m_canService, &QObject::deleteLater);
    m_canThread->start();

    setWindowTitle(QStringLiteral("福达驱动器测试工具"));
    resize(1320, 840);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    root->addWidget(createConnectionPanel());

    auto *verticalSplitter = new QSplitter(Qt::Vertical, central);
    m_canContentArea = verticalSplitter;
    auto *workSplitter = new QSplitter(Qt::Horizontal, verticalSplitter);
    auto *tabs = new QTabWidget(workSplitter);
    tabs->addTab(createCommandPanel(), QStringLiteral("命令测试"));
    tabs->addTab(createConfigPanel(), QStringLiteral("参数读写"));
    tabs->addTab(createCalibrationPanel(), QStringLiteral("电机校准"));
    tabs->addTab(createAnticoggingPanel(), QStringLiteral("齿槽补偿"));
    tabs->addTab(createDfuPanel(), QStringLiteral("远程升级"));

    workSplitter->addWidget(tabs);
    QWidget *statusPanel = createStatusPanel();
    statusPanel->setMaximumWidth(560);
    statusPanel->setMinimumWidth(440);
    workSplitter->addWidget(statusPanel);
    workSplitter->setStretchFactor(0, 6);
    workSplitter->setStretchFactor(1, 2);
    workSplitter->setSizes({820, 500});

    verticalSplitter->addWidget(workSplitter);
    verticalSplitter->addWidget(createLogPanel());
    verticalSplitter->setStretchFactor(0, 2);
    verticalSplitter->setStretchFactor(1, 3);
    verticalSplitter->setSizes({310, 500});

    root->addWidget(verticalSplitter, 1);
    setCentralWidget(central);

    applyVisualStyle();
    setupCanConnections();
    setConnectionState(false, QStringLiteral("未连接"));
}

MainWindow::~MainWindow()
{
    shutdownWorkers();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    shutdownWorkers();
    QMainWindow::closeEvent(event);
}

void MainWindow::shutdownWorkers()
{
    if (m_shuttingDown) {
        return;
    }

    m_shuttingDown = true;
    stopProcessPanel(&m_calibrationProcess, true);
    stopProcessPanel(&m_anticoggingProcess, true);
    shutdownCanThread();
}

void MainWindow::shutdownCanThread()
{
    if (!m_canThread) {
        m_canService = nullptr;
        return;
    }

    QThread *thread = m_canThread;
    CanService *service = m_canService;
    m_canThread = nullptr;
    m_canService = nullptr;

    if (thread->isRunning()) {
        if (service) {
            QMetaObject::invokeMethod(service, "disconnectInterface", Qt::BlockingQueuedConnection);
        }
        thread->quit();
        if (!thread->wait(3000)) {
            thread->terminate();
            thread->wait(1000);
        }
    }
}

QWidget *MainWindow::createConnectionPanel()
{
    auto *box = new QWidget(this);
    box->setObjectName(QStringLiteral("connectionBar"));
    box->setFixedHeight(58);
    auto *layout = new QGridLayout(box);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(0);

    m_interfaceEdit = new QLineEdit(QStringLiteral("can0"), box);
    m_interfaceEdit->setMinimumWidth(110);

    m_nodeSpin = new QSpinBox(box);
    m_nodeSpin->setRange(1, CanIdCodec::BroadcastNodeId);
    m_nodeSpin->setValue(1);
    m_nodeSpin->setSuffix(QStringLiteral("  节点"));

    m_bitrateSpin = new QSpinBox(box);
    m_bitrateSpin->setRange(0, 8000000);
    m_bitrateSpin->setSingleStep(100000);
    m_bitrateSpin->setValue(1000000);
    m_bitrateSpin->setSuffix(QStringLiteral(" bit/s"));

    m_dataBitrateSpin = new QSpinBox(box);
    m_dataBitrateSpin->setRange(0, 8000000);
    m_dataBitrateSpin->setSingleStep(100000);
    m_dataBitrateSpin->setValue(2000000);
    m_dataBitrateSpin->setSuffix(QStringLiteral(" bit/s"));

    auto *connectButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("连接"), box);
    auto *disconnectButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("断开"), box);
    m_connectionStateLabel = new QLabel(box);
    m_connectionStateLabel->setObjectName(QStringLiteral("connectionState"));

    auto *titleLabel = new QLabel(QStringLiteral("CAN 连接"), box);
    titleLabel->setObjectName(QStringLiteral("barTitle"));

    layout->addWidget(titleLabel, 0, 0);
    layout->addWidget(new QLabel(QStringLiteral("设备"), box), 0, 1);
    layout->addWidget(m_interfaceEdit, 0, 2);
    layout->addWidget(new QLabel(QStringLiteral("节点 ID"), box), 0, 3);
    layout->addWidget(m_nodeSpin, 0, 4);
    layout->addWidget(new QLabel(QStringLiteral("仲裁波特率"), box), 0, 5);
    layout->addWidget(m_bitrateSpin, 0, 6);
    layout->addWidget(new QLabel(QStringLiteral("数据波特率"), box), 0, 7);
    layout->addWidget(m_dataBitrateSpin, 0, 8);
    layout->addWidget(connectButton, 0, 9);
    layout->addWidget(disconnectButton, 0, 10);
    layout->addWidget(m_connectionStateLabel, 0, 11);
    layout->setColumnStretch(11, 1);

    connect(connectButton, &QPushButton::clicked, this, [this]() {
        const QString interfaceName = m_interfaceEdit->text().trimmed();
        if (interfaceName.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("参数错误"), QStringLiteral("设备名不能为空"));
            return;
        }
        emit connectCanInterfaceRequested(interfaceName,
                                          m_bitrateSpin->value(),
                                          m_dataBitrateSpin->value());
    });
    connect(disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectCanInterfaceRequested);

    return box;
}

QWidget *MainWindow::createCommandPanel()
{
    auto *box = new QWidget(this);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(6, 6, 6, 6);

    m_commandTable = new QTableWidget(box);
    m_commandTable->setColumnCount(6);
    m_commandTable->setHorizontalHeaderLabels({QStringLiteral("ID"),
                                               QStringLiteral("命令"),
                                               QStringLiteral("DLC"),
                                               QStringLiteral("参数文本框"),
                                               QStringLiteral("说明"),
                                               QStringLiteral("操作")});
    m_commandTable->verticalHeader()->setVisible(false);
    m_commandTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_commandTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_commandTable->setAlternatingRowColors(true);
    m_commandTable->setWordWrap(false);
    m_commandTable->horizontalHeader()->setStretchLastSection(false);

    const auto &commands = Protocol::commandDefinitions();
    m_commandTable->setRowCount(commands.size());
    for (int row = 0; row < commands.size(); ++row) {
        const CommandDef &command = commands.at(row);
        m_commandTable->setItem(row, 0, readOnlyItem(QString::number(command.id)));
        m_commandTable->setItem(row, 1, readOnlyItem(QStringLiteral("%1  %2").arg(command.enumName, command.displayName)));
        m_commandTable->setItem(row, 2, readOnlyItem(command.dlcText));
        m_commandTable->setItem(row, 4, readOnlyItem(command.description));

        auto *input = new QLineEdit(m_commandTable);
        input->setPlaceholderText(command.parameterHint);
        input->setToolTip(command.parameterHint);
        m_commandInputs.insert(command.id, input);

        switch (command.payloadKind) {
        case PayloadKind::None:
            input->setText(command.parameterHint);
            input->setEnabled(false);
            break;
        case PayloadKind::Float32:
            input->setValidator(new QDoubleValidator(input));
            input->setText(QStringLiteral("0.0"));
            break;
        case PayloadKind::UInt8:
            input->setValidator(new QIntValidator(0, 255, input));
            input->setText(QStringLiteral("1"));
            break;
        case PayloadKind::ConfigRead:
            input->setText(QStringLiteral("1"));
            break;
        case PayloadKind::ConfigWrite:
            input->setText(QStringLiteral("1,0"));
            break;
        case PayloadKind::HexBytes:
            if (command.id == 30) {
                input->setText(QStringLiteral("00"));
            } else if (command.id == 31) {
                input->setText(QStringLiteral("00 00 00 00 00 00 00 00"));
            }
            break;
        case PayloadKind::OptionalUInt16x8:
            input->clear();
            break;
        }
        if (command.reportOnly) {
            input->setEnabled(false);
        }
        m_commandTable->setCellWidget(row, 3, input);

        auto *button = new QPushButton(command.reportOnly ? QStringLiteral("仅接收") : QStringLiteral("发送"), m_commandTable);
        button->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
        button->setEnabled(command.txAllowed);
        button->setToolTip(command.reportOnly ? QStringLiteral("该命令由设备主动上报") : QStringLiteral("发送该命令"));
        connect(button, &QPushButton::clicked, this, [this, command]() {
            sendProtocolCommand(command.id);
        });
        m_commandTable->setCellWidget(row, 5, button);
    }

    m_commandTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_commandTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_commandTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_commandTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_commandTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_commandTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_commandTable->setColumnWidth(3, 210);
    m_commandTable->setColumnWidth(5, CommandActionColumnWidth);
    m_commandTable->verticalHeader()->setDefaultSectionSize(32);

    layout->addWidget(m_commandTable);
    return box;
}

QWidget *MainWindow::createConfigPanel()
{
    auto *box = new QWidget(this);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(6, 6, 6, 6);

    m_configTable = new QTableWidget(box);
    m_configTable->setColumnCount(8);
    m_configTable->setHorizontalHeaderLabels({QStringLiteral("索引"),
                                              QStringLiteral("名称"),
                                              QStringLiteral("类型"),
                                              QStringLiteral("当前值"),
                                              QStringLiteral("新值"),
                                              QStringLiteral("具体含义"),
                                              QStringLiteral("读取"),
                                              QStringLiteral("写入")});
    m_configTable->verticalHeader()->setVisible(false);
    m_configTable->setAlternatingRowColors(true);
    m_configTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_configTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_configTable->setWordWrap(false);
    m_configTable->horizontalHeader()->setStretchLastSection(false);

    const auto &configs = Protocol::configDefinitions();
    m_configTable->setRowCount(configs.size());
    for (int row = 0; row < configs.size(); ++row) {
        const ConfigDef &config = configs.at(row);
        m_configTable->setItem(row, 0, readOnlyItem(QString::number(config.index)));
        m_configTable->setItem(row, 1, readOnlyItem(config.name));
        m_configTable->setItem(row, 2, readOnlyItem(Protocol::configTypeName(config.type)));

        auto *currentItem = readOnlyItem(QStringLiteral("-"));
        m_configCurrentItems.insert(config.index, currentItem);
        m_configTable->setItem(row, 3, currentItem);

        auto *editor = new QLineEdit(m_configTable);
        editor->setPlaceholderText(Protocol::configTypeName(config.type));
        editor->setToolTip(config.description);
        if (config.type == ConfigValueType::Float32) {
            editor->setValidator(new QDoubleValidator(editor));
        } else {
            editor->setValidator(new QIntValidator(editor));
        }
        m_configEditors.insert(config.index, editor);
        m_configTable->setCellWidget(row, 4, editor);

        m_configTable->setItem(row, 5, readOnlyItem(config.description));

        auto *readButton = new QPushButton(QStringLiteral("读"), m_configTable);
        readButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        connect(readButton, &QPushButton::clicked, this, [this, index = config.index]() {
            sendConfigRead(index);
        });
        m_configTable->setCellWidget(row, 6, readButton);

        auto *writeButton = new QPushButton(QStringLiteral("写"), m_configTable);
        writeButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
        connect(writeButton, &QPushButton::clicked, this, [this, index = config.index]() {
            sendConfigWrite(index);
        });
        m_configTable->setCellWidget(row, 7, writeButton);
    }

    m_configTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_configTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_configTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_configTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_configTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    m_configTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_configTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    m_configTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);
    m_configTable->setColumnWidth(4, 150);
    m_configTable->setColumnWidth(6, ConfigActionColumnWidth);
    m_configTable->setColumnWidth(7, ConfigActionColumnWidth);
    m_configTable->verticalHeader()->setDefaultSectionSize(28);

    layout->addWidget(m_configTable);
    return box;
}

QWidget *MainWindow::createCalibrationPanel()
{
    auto *box = new QWidget(this);
    auto *layout = new QGridLayout(box);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    auto *startButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("启动校准"), box);
    auto *abortButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), QStringLiteral("中止校准"), box);
    auto *statusButton = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("读取状态"), box);
    auto *calibValidButton = new QPushButton(QStringLiteral("读取校准标志"), box);
    auto *encoderDirButton = new QPushButton(QStringLiteral("读取编码器方向"), box);
    auto *encoderOffsetButton = new QPushButton(QStringLiteral("读取编码器偏移"), box);
    auto *setHomeButton = new QPushButton(QStringLiteral("设置当前位置为零点"), box);
    auto *saveButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("保存配置"), box);
    m_calibrationProcess.startButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("一键电机校准"), box);
    m_calibrationProcess.stopButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("结束电机校准"), box);
    m_calibrationProcess.stopButton->setEnabled(false);
    m_calibrationProcess.outputEdit = new QPlainTextEdit(box);
    m_calibrationProcess.outputEdit->setReadOnly(true);
    m_calibrationProcess.outputEdit->setMinimumHeight(220);
    m_calibrationProcess.outputEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_calibrationProcess.outputEdit->setMaximumBlockCount(CalibrationOutputMaxBlockCount);

    connect(startButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(5); });
    connect(abortButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(7); });
    connect(statusButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(13); });
    connect(calibValidButton, &QPushButton::clicked, this, [this]() { sendConfigRead(34); });
    connect(encoderDirButton, &QPushButton::clicked, this, [this]() { sendConfigRead(35); });
    connect(encoderOffsetButton, &QPushButton::clicked, this, [this]() { sendConfigRead(36); });
    connect(setHomeButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(11); });
    connect(saveButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(19); });
    connect(m_calibrationProcess.startButton, &QPushButton::clicked, this, &MainWindow::startOneClickCalibration);
    connect(m_calibrationProcess.stopButton, &QPushButton::clicked, this, &MainWindow::stopOneClickCalibrationProcess);

    layout->addWidget(startButton, 0, 0);
    layout->addWidget(abortButton, 0, 1);
    layout->addWidget(statusButton, 0, 2);
    layout->addWidget(calibValidButton, 1, 0);
    layout->addWidget(encoderDirButton, 1, 1);
    layout->addWidget(encoderOffsetButton, 1, 2);
    layout->addWidget(setHomeButton, 2, 0);
    layout->addWidget(saveButton, 2, 1);
    layout->addWidget(m_calibrationProcess.startButton, 3, 0);
    layout->addWidget(m_calibrationProcess.stopButton, 3, 1);
    layout->addWidget(m_calibrationProcess.outputEdit, 4, 0, 1, 4);
    layout->setColumnStretch(3, 1);
    layout->setRowStretch(4, 1);
    return box;
}

QWidget *MainWindow::createAnticoggingPanel()
{
    auto *box = new QWidget(this);
    auto *layout = new QGridLayout(box);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    auto *startButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("启动补偿"), box);
    auto *abortButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), QStringLiteral("中止补偿"), box);
    auto *statusButton = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("读取状态"), box);
    auto *enableButton = new QPushButton(QStringLiteral("启用补偿"), box);
    auto *disableButton = new QPushButton(QStringLiteral("关闭补偿"), box);
    auto *readEnableButton = new QPushButton(QStringLiteral("读取启用状态"), box);
    auto *readLutButton = new QPushButton(QStringLiteral("读取补偿表"), box);
    auto *saveButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("保存配置"), box);
    m_anticoggingProcess.startButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("一键齿槽校准"), box);
    m_anticoggingProcess.stopButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("结束齿槽校准"), box);
    m_anticoggingProcess.stopButton->setEnabled(false);
    m_anticoggingProcess.outputEdit = new QPlainTextEdit(box);
    m_anticoggingProcess.outputEdit->setReadOnly(true);
    m_anticoggingProcess.outputEdit->setMinimumHeight(220);
    m_anticoggingProcess.outputEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_anticoggingProcess.outputEdit->setMaximumBlockCount(CalibrationOutputMaxBlockCount);

    connect(startButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(8); });
    connect(abortButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(10); });
    connect(statusButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(13); });
    connect(enableButton, &QPushButton::clicked, this, [this]() {
        sendRawProtocolCommand(17, PayloadCodec::encodeUInt32(16) + PayloadCodec::encodeInt32(1));
    });
    connect(disableButton, &QPushButton::clicked, this, [this]() {
        sendRawProtocolCommand(17, PayloadCodec::encodeUInt32(16) + PayloadCodec::encodeInt32(0));
    });
    connect(readEnableButton, &QPushButton::clicked, this, [this]() { sendConfigRead(16); });
    connect(readLutButton, &QPushButton::clicked, this, [this]() { sendConfigRead(37); });
    connect(saveButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(19); });
    connect(m_anticoggingProcess.startButton, &QPushButton::clicked, this, &MainWindow::startOneClickAnticogging);
    connect(m_anticoggingProcess.stopButton, &QPushButton::clicked, this, &MainWindow::stopOneClickAnticoggingProcess);

    layout->addWidget(startButton, 0, 0);
    layout->addWidget(abortButton, 0, 1);
    layout->addWidget(statusButton, 0, 2);
    layout->addWidget(enableButton, 1, 0);
    layout->addWidget(disableButton, 1, 1);
    layout->addWidget(readEnableButton, 1, 2);
    layout->addWidget(readLutButton, 2, 0);
    layout->addWidget(saveButton, 2, 1);
    layout->addWidget(m_anticoggingProcess.startButton, 3, 0);
    layout->addWidget(m_anticoggingProcess.stopButton, 3, 1);
    layout->addWidget(m_anticoggingProcess.outputEdit, 4, 0, 1, 4);
    layout->setColumnStretch(3, 1);
    layout->setRowStretch(4, 1);
    return box;
}

QWidget *MainWindow::createDfuPanel()
{
    auto *box = new QWidget(this);
    auto *layout = new QGridLayout(box);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    auto *startButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("开始升级"), box);
    auto *versionButton = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("读取版本"), box);
    auto *dataEdit = new QLineEdit(box);
    auto *endEdit = new QLineEdit(QStringLiteral("00 00 00 00 00 00 00 00"), box);
    auto *sendDataButton = new QPushButton(QStringLiteral("发送数据包"), box);
    auto *endButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("结束升级"), box);

    dataEdit->setPlaceholderText(QStringLiteral("1~8 字节，例如: 01 02 03 04"));
    endEdit->setPlaceholderText(QStringLiteral("8 字节校验数据"));

    connect(startButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(29); });
    connect(versionButton, &QPushButton::clicked, this, [this]() { sendProtocolCommand(28); });
    connect(sendDataButton, &QPushButton::clicked, this, [this, dataEdit]() {
        QByteArray payload;
        QString error;
        if (!PayloadCodec::parseHexBytes(dataEdit->text(), &payload, &error)) {
            QMessageBox::warning(this, QStringLiteral("参数错误"), error);
            return;
        }
        if (payload.isEmpty() || payload.size() > 8) {
            QMessageBox::warning(this, QStringLiteral("参数错误"), QStringLiteral("DFU_DATA 需要 1~8 个字节"));
            return;
        }
        sendRawProtocolCommand(30, payload);
    });
    connect(endButton, &QPushButton::clicked, this, [this, endEdit]() {
        QByteArray payload;
        QString error;
        if (!PayloadCodec::parseHexBytes(endEdit->text(), &payload, &error)) {
            QMessageBox::warning(this, QStringLiteral("参数错误"), error);
            return;
        }
        if (payload.size() != 8) {
            QMessageBox::warning(this, QStringLiteral("参数错误"), QStringLiteral("DFU_END 需要 8 个字节"));
            return;
        }
        sendRawProtocolCommand(31, payload);
    });

    layout->addWidget(startButton, 0, 0);
    layout->addWidget(versionButton, 0, 1);
    layout->addWidget(new QLabel(QStringLiteral("DFU_DATA"), box), 1, 0);
    layout->addWidget(dataEdit, 1, 1, 1, 2);
    layout->addWidget(sendDataButton, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("DFU_END"), box), 2, 0);
    layout->addWidget(endEdit, 2, 1, 1, 2);
    layout->addWidget(endButton, 2, 3);
    layout->setColumnStretch(2, 1);
    layout->setRowStretch(3, 1);
    return box;
}

QWidget *MainWindow::createStatusPanel()
{
    const TitledPanel panel = createTitledPanel(QStringLiteral("实时状态"), this);
    auto *box = panel.body;
    auto *layout = new QGridLayout(box);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setHorizontalSpacing(10);
    layout->setVerticalSpacing(6);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(3, 1);

    auto addStatusItem = [box, layout](int row, int group, const QString &name, QLabel *value) {
        const int column = group * 2;
        auto *nameLabel = new QLabel(name, box);
        nameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(nameLabel, row, column);
        layout->addWidget(value, row, column + 1);
    };

    m_lastNodeLabel = valueLabel();
    m_lastCommandLabel = valueLabel();
    m_statusWordLabel = valueLabel(PayloadCodec::formatUInt32Hex(0));
    m_errorsWordLabel = valueLabel(PayloadCodec::formatUInt32Hex(0));
    m_switchedOnLabel = valueLabel();
    m_targetReachedLabel = valueLabel();
    m_currentLimitLabel = valueLabel();
    m_adcSelftestLabel = valueLabel();
    m_encoderOfflineLabel = valueLabel();
    m_overVoltageLabel = valueLabel();
    m_underVoltageLabel = valueLabel();
    m_overCurrentLabel = valueLabel();
    m_fwVersionLabel = valueLabel();
    m_calibLabel = valueLabel();
    m_anticoggingLabel = valueLabel();
    m_value1Labels.clear();
    m_value1Labels.reserve(Value1PartCount);
    for (int i = 0; i < Value1PartCount; ++i) {
        m_value1Labels.append(valueLabel());
    }

    int statusRow = 0;
    addStatusItem(statusRow++, 0, QStringLiteral("最近节点"), m_lastNodeLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("最近命令"), m_lastCommandLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("status_code"), m_statusWordLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("switched_on"), m_switchedOnLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("target_reached"), m_targetReachedLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("current_limit_active"), m_currentLimitLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("errors_code"), m_errorsWordLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("adc_selftest_fatal"), m_adcSelftestLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("encoder_offline"), m_encoderOfflineLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("over_voltage"), m_overVoltageLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("under_voltage"), m_underVoltageLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("over_current"), m_overCurrentLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("固件版本"), m_fwVersionLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("校准上报"), m_calibLabel);
    addStatusItem(statusRow++, 0, QStringLiteral("齿槽补偿"), m_anticoggingLabel);

    const QStringList value1Names = value1PartNames();
    for (int i = 0; i < Value1PartCount; ++i) {
        addStatusItem(i, 1, value1Names.at(i), m_value1Labels.at(i));
    }

    updateStatusWords(0, 0);
    return panel.panel;
}

QWidget *MainWindow::createLogPanel()
{
    const TitledPanel panel = createTitledPanel(QStringLiteral("CAN 日志"), this);
    auto *box = panel.body;
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *clearButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogResetButton), QStringLiteral("清空日志"), box);
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        m_logTable->setRowCount(0);
    });
    layout->addWidget(clearButton, 0, Qt::AlignLeft);

    m_logTable = new QTableWidget(box);
    m_logTable->setColumnCount(8);
    m_logTable->setHorizontalHeaderLabels({QStringLiteral("时间"),
                                           QStringLiteral("方向"),
                                           QStringLiteral("ID"),
                                           QStringLiteral("节点"),
                                           QStringLiteral("命令"),
                                           QStringLiteral("DLC"),
                                           QStringLiteral("Data"),
                                           QStringLiteral("解析")});
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->setAlternatingRowColors(true);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_logTable->setWordWrap(false);
    m_logTable->horizontalHeader()->setStretchLastSection(false);
    m_logTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_logTable->setColumnWidth(0, LogTimeColumnWidth);
    m_logTable->setColumnWidth(1, LogDirectionColumnWidth);
    m_logTable->setColumnWidth(2, LogIdColumnWidth);
    m_logTable->setColumnWidth(3, LogNodeColumnWidth);
    m_logTable->setColumnWidth(4, LogCommandColumnWidth);
    m_logTable->setColumnWidth(5, LogDlcColumnWidth);
    m_logTable->setColumnWidth(6, LogDataColumnWidth);
    m_logTable->setColumnWidth(7, LogParsedColumnWidth);
    m_logTable->verticalHeader()->setDefaultSectionSize(25);

    layout->addWidget(m_logTable);
    return panel.panel;
}

void MainWindow::setupCanConnections()
{
    connect(this, &MainWindow::connectCanInterfaceRequested, m_canService, &CanService::connectInterface, Qt::QueuedConnection);
    connect(this, &MainWindow::disconnectCanInterfaceRequested, m_canService, &CanService::disconnectInterface, Qt::QueuedConnection);
    connect(this, &MainWindow::sendCanCommandRequested, m_canService, &CanService::sendCommand, Qt::QueuedConnection);

    connect(m_canService, &CanService::connectionChanged, this, &MainWindow::setConnectionState, Qt::QueuedConnection);
    connect(m_canService, &CanService::frameReceived, this, &MainWindow::handleReceivedFrame, Qt::QueuedConnection);
    connect(m_canService, &CanService::frameTransmitted, this, &MainWindow::handleTransmittedFrame, Qt::QueuedConnection);
    connect(m_canService, &CanService::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 5000);
    }, Qt::QueuedConnection);
}

void MainWindow::applyVisualStyle()
{
    qApp->setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #ECECEC;
            color: #202020;
            font-size: 14px;
        }
        QGroupBox {
            background: #F3F3F3;
            border: 1px solid #9A9A9A;
            border-radius: 2px;
            margin-top: 7px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
            background: transparent;
            color: #202020;
        }
        QLabel {
            background: transparent;
        }
        QLabel#panelTitle {
            background: #DCDCDC;
            border: 1px solid #9A9A9A;
            border-bottom: 0;
            padding: 4px 8px;
            color: #202020;
            font-weight: 700;
        }
        QWidget#panelBody {
            background: #F3F3F3;
            border: 1px solid #9A9A9A;
        }
        QWidget#connectionBar {
            background: #F3F3F3;
            border: 1px solid #9A9A9A;
        }
        QLabel#barTitle {
            background: transparent;
            color: #202020;
            font-weight: 700;
            padding: 0 6px;
        }
        QTabWidget::pane {
            background: #F3F3F3;
            border: 1px solid #9A9A9A;
            border-radius: 0;
            top: -1px;
        }
        QTabBar::tab {
            min-width: 96px;
            min-height: 28px;
            padding: 3px 16px;
            margin-right: 2px;
            border: 1px solid #9A9A9A;
            border-bottom: 0;
            border-top-left-radius: 2px;
            border-top-right-radius: 2px;
            background: #DCDCDC;
            color: #202020;
            font-weight: 600;
        }
        QTabBar::tab:selected {
            background: #F3F3F3;
            color: #000000;
            border-top: 1px solid #FFFFFF;
        }
        QSplitter::handle {
            background: #B8B8B8;
        }
        QSplitter::handle:horizontal {
            width: 5px;
        }
        QSplitter::handle:vertical {
            height: 5px;
        }
        QLineEdit, QSpinBox {
            min-height: 24px;
            border: 1px solid #8A8A8A;
            border-radius: 0;
            padding: 1px 6px;
            background: #FFFFFF;
            selection-background-color: #0A64AD;
        }
        QLineEdit:disabled {
            color: #707070;
            background: #E2E2E2;
        }
        QPushButton {
            min-height: 24px;
            border: 1px solid #7F7F7F;
            border-radius: 0;
            padding: 2px 9px;
            background: #E6E6E6;
            color: #202020;
        }
        QPushButton:hover {
            background: #EFEFEF;
            border-color: #4F4F4F;
        }
        QPushButton:pressed {
            background: #D0D0D0;
        }
        QPushButton:disabled {
            color: #8A8A8A;
            background: #DADADA;
        }
        QHeaderView::section {
            background: #D7D7D7;
            border: 0;
            border-right: 1px solid #A0A0A0;
            border-bottom: 1px solid #A0A0A0;
            padding: 4px 6px;
            font-size: 14px;
            font-weight: 700;
            color: #202020;
        }
        QTableWidget {
            gridline-color: #C7C7C7;
            background: #FFFFFF;
            alternate-background-color: #F5F5F5;
            selection-background-color: #0A64AD;
            selection-color: #FFFFFF;
            border: 1px solid #9A9A9A;
            border-radius: 0;
            outline: 0;
        }
        QTableWidget::item {
            padding: 2px 5px;
        }
        QLabel#connectionState {
            padding: 2px 10px;
            border: 1px solid #9A9A9A;
            border-radius: 0;
            background: #DCDCDC;
            color: #202020;
            font-weight: 600;
        }
        QStatusBar {
            background: #DCDCDC;
            color: #202020;
        }
    )"));
}

void MainWindow::sendProtocolCommand(quint8 commandId)
{
    const CommandDef *command = Protocol::commandById(commandId);
    if (!command || !command->txAllowed) {
        return;
    }

    if (commandId == 20 && !confirmFactoryReset()) {
        return;
    }

    QByteArray payload;
    QString error;
    if (!buildCommandPayload(*command, &payload, &error)) {
        QMessageBox::warning(this, QStringLiteral("参数错误"), error);
        return;
    }

    if (commandId == 30 && (payload.isEmpty() || payload.size() > 8)) {
        QMessageBox::warning(this, QStringLiteral("参数错误"), QStringLiteral("DFU_DATA 需要 1~8 个字节"));
        return;
    }
    if (commandId == 31 && payload.size() != 8) {
        QMessageBox::warning(this, QStringLiteral("参数错误"), QStringLiteral("DFU_END 需要 8 个字节"));
        return;
    }

    emit sendCanCommandRequested(currentNodeId(), commandId, payload);
}

bool MainWindow::confirmFactoryReset()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("确认恢复出厂配置"));
    dialog.setModal(true);
    dialog.setMinimumSize(580, 190);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);

    auto *messageLabel = new QLabel(QStringLiteral("该命令会恢复全部配置，且仅在失能下有效。"), &dialog);
    messageLabel->setWordWrap(true);
    messageLabel->setMinimumWidth(520);
    layout->addWidget(messageLabel);

    auto *detailLabel = new QLabel(QStringLiteral("确认发送恢复出厂配置命令？"), &dialog);
    detailLabel->setWordWrap(true);
    detailLabel->setMinimumWidth(520);
    layout->addWidget(detailLabel);
    layout->addStretch(1);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);
    buttonLayout->addStretch(1);

    auto *confirmButton = new QPushButton(QStringLiteral("确认发送"), &dialog);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), &dialog);
    confirmButton->setMinimumSize(168, 34);
    cancelButton->setMinimumSize(168, 34);
    cancelButton->setDefault(true);

    buttonLayout->addWidget(confirmButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);

    connect(confirmButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.adjustSize();
    dialog.resize(qMax(dialog.width(), 580), qMax(dialog.height(), 190));
    const QPoint center = frameGeometry().center();
    dialog.move(center.x() - dialog.width() / 2, center.y() - dialog.height() / 2);

    return dialog.exec() == QDialog::Accepted;
}

void MainWindow::sendRawProtocolCommand(quint8 commandId, const QByteArray &payload)
{
    const CommandDef *command = Protocol::commandById(commandId);
    if (!command || !command->txAllowed) {
        return;
    }
    if (payload.size() > 64) {
        QMessageBox::warning(this,
                             QStringLiteral("参数错误"),
                             QStringLiteral("payload 长度 %1 超过 CAN FD 64 字节限制").arg(payload.size()));
        return;
    }

    emit sendCanCommandRequested(currentNodeId(), commandId, payload);
}

void MainWindow::sendConfigRead(quint32 index)
{
    const QByteArray payload = PayloadCodec::encodeUInt32(index);
    emit sendCanCommandRequested(currentNodeId(), 18, payload);
}

void MainWindow::sendConfigWrite(quint32 index)
{
    const ConfigDef *config = Protocol::configByIndex(index);
    if (!config) {
        return;
    }

    const QLineEdit *editor = m_configEditors.value(index);
    if (!editor) {
        return;
    }

    QByteArray valuePayload;
    QString error;
    if (!encodeConfigValue(*config, editor->text(), &valuePayload, &error)) {
        QMessageBox::warning(this, QStringLiteral("参数错误"), error);
        return;
    }

    QByteArray payload = PayloadCodec::encodeUInt32(index);
    payload += valuePayload;
    emit sendCanCommandRequested(currentNodeId(), 17, payload);
}

void MainWindow::startOneClickCalibration()
{
    const QString command = QStringLiteral("MOTEUS_CAL_DIR=/tmp python3 -u -m moteus.moteus_tool --calibrate --cal-never-encoder-current-mode");
    startProcessPanel(&m_calibrationProcess, command);
}

void MainWindow::startOneClickAnticogging()
{
    const QString command = QStringLiteral("python3 -u compensate_cogging.py --store -v");
    startProcessPanel(&m_anticoggingProcess, command);
}

void MainWindow::startProcessPanel(ProcessPanel *panel, const QString &command)
{
    if (!panel || !panel->outputEdit) {
        return;
    }
    if (panel->running) {
        appendProcessOutput(panel, QStringLiteral("\n进程正在运行。\n"));
        return;
    }

    panel->outputEdit->clear();
    panel->currentLine.clear();
    panel->liveLineVisible = false;
    appendProcessOutput(panel, QStringLiteral("开始执行命令。\n\n"));

    panel->running = true;
    if (panel->startButton) {
        panel->startButton->setEnabled(false);
    }
    if (panel->stopButton) {
        panel->stopButton->setEnabled(true);
    }

    auto *thread = new QThread(this);
    auto *runner = new CalibrationRunner(command);
    runner->moveToThread(thread);
    panel->thread = thread;
    panel->runner = runner;

    connect(thread, &QThread::started, runner, &CalibrationRunner::start);
    connect(thread, &QThread::finished, runner, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [panel, thread]() {
        if (panel->thread == thread) {
            panel->thread = nullptr;
            panel->runner = nullptr;
        }
    });
    connect(runner, &CalibrationRunner::outputReady, this, [this, panel](const QString &text) {
        appendProcessOutput(panel, text);
    }, Qt::QueuedConnection);
    connect(runner, &CalibrationRunner::finished, this, [this, panel](const QString &message) {
        finishProcessPanel(panel, message);
    }, Qt::QueuedConnection);
    connect(runner, &CalibrationRunner::finished, thread, &QThread::quit, Qt::QueuedConnection);

    thread->start();
}

void MainWindow::appendProcessOutput(ProcessPanel *panel, const QString &text)
{
    if (!panel || !panel->outputEdit || text.isEmpty()) {
        return;
    }

    QString committedText;
    bool liveLineDirty = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\x1B')) {
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('[')) {
                i += 2;
                while (i < text.size() && (text.at(i).unicode() < 0x40 || text.at(i).unicode() > 0x7E)) {
                    ++i;
                }
            }
            continue;
        }
        if (ch == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
            committedText += panel->currentLine;
            committedText += QLatin1Char('\n');
            panel->currentLine.clear();
            liveLineDirty = true;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('\r')) {
            panel->currentLine.clear();
            liveLineDirty = true;
            continue;
        }
        if (ch == QLatin1Char('\n')) {
            committedText += panel->currentLine;
            committedText += QLatin1Char('\n');
            panel->currentLine.clear();
            liveLineDirty = true;
            continue;
        }
        if (ch == QLatin1Char('\b')) {
            if (!panel->currentLine.isEmpty()) {
                panel->currentLine.chop(1);
                liveLineDirty = true;
            }
            continue;
        }
        if (ch.unicode() < 0x20 && ch != QLatin1Char('\t')) {
            continue;
        }

        panel->currentLine += ch;
        liveLineDirty = true;
    }

    panel->outputEdit->moveCursor(QTextCursor::End);
    QTextCursor cursor = panel->outputEdit->textCursor();
    cursor.movePosition(QTextCursor::End);

    if (!committedText.isEmpty()) {
        if (panel->liveLineVisible) {
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.removeSelectedText();
            panel->liveLineVisible = false;
        }
        cursor.insertText(committedText);
    }

    if (liveLineDirty) {
        if (panel->liveLineVisible) {
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.removeSelectedText();
        }
        if (!panel->currentLine.isEmpty()) {
            cursor.insertText(panel->currentLine);
            panel->liveLineVisible = true;
        } else {
            panel->liveLineVisible = false;
        }
    }

    panel->outputEdit->setTextCursor(cursor);
    panel->outputEdit->moveCursor(QTextCursor::End);
}

void MainWindow::stopOneClickCalibrationProcess()
{
    stopProcessPanel(&m_calibrationProcess);
}

void MainWindow::stopOneClickAnticoggingProcess()
{
    stopProcessPanel(&m_anticoggingProcess);
}

void MainWindow::stopProcessPanel(ProcessPanel *panel, bool waitForThread)
{
    if (!panel || !panel->thread || !panel->thread->isRunning()) {
        if (panel) {
            panel->running = false;
            panel->runner = nullptr;
            panel->thread = nullptr;
            if (panel->startButton) {
                panel->startButton->setEnabled(true);
            }
            if (panel->stopButton) {
                panel->stopButton->setEnabled(false);
            }
        }
        return;
    }

    CalibrationRunner *runner = panel->runner.data();
    if (runner) {
        QMetaObject::invokeMethod(runner, "stop", waitForThread ? Qt::BlockingQueuedConnection : Qt::QueuedConnection);
        if (panel->stopButton) {
            panel->stopButton->setEnabled(false);
        }
        if (!waitForThread) {
            return;
        }
    }

    QThread *thread = panel->thread;
    thread->quit();
    if (!thread->wait(5000)) {
        thread->terminate();
        thread->wait(1000);
    }
    panel->running = false;
    panel->runner = nullptr;
    panel->thread = nullptr;
    if (panel->startButton) {
        panel->startButton->setEnabled(true);
    }
    if (panel->stopButton) {
        panel->stopButton->setEnabled(false);
    }
}

void MainWindow::finishProcessPanel(ProcessPanel *panel, const QString &message)
{
    if (!panel) {
        return;
    }
    appendProcessOutput(panel, message);
    panel->running = false;
    panel->runner = nullptr;
    if (panel->startButton) {
        panel->startButton->setEnabled(true);
    }
    if (panel->stopButton) {
        panel->stopButton->setEnabled(false);
    }
}

bool MainWindow::buildCommandPayload(const CommandDef &command, QByteArray *payload, QString *error) const
{
    QByteArray out;
    const QString text = commandInputText(command.id);

    switch (command.payloadKind) {
    case PayloadKind::None:
        break;
    case PayloadKind::Float32: {
        bool ok = false;
        const float value = text.toFloat(&ok);
        if (!ok) {
            if (error) {
                *error = QStringLiteral("%1 需要 float 参数").arg(command.displayName);
            }
            return false;
        }
        out = PayloadCodec::encodeFloat(value);
        break;
    }
    case PayloadKind::UInt8: {
        bool ok = false;
        const int value = text.toInt(&ok, 0);
        if (!ok || value < 0 || value > 255) {
            if (error) {
                *error = QStringLiteral("%1 需要 0~255 的 uint8 参数").arg(command.displayName);
            }
            return false;
        }
        out = PayloadCodec::encodeUInt8(static_cast<quint8>(value));
        break;
    }
    case PayloadKind::HexBytes:
        if (!PayloadCodec::parseHexBytes(text, &out, error)) {
            return false;
        }
        break;
    case PayloadKind::ConfigRead: {
        bool ok = false;
        const quint32 index = text.toUInt(&ok, 0);
        if (!ok || index == 0) {
            if (error) {
                *error = QStringLiteral("GET_CONFIG 需要配置索引");
            }
            return false;
        }
        out = PayloadCodec::encodeUInt32(index);
        break;
    }
    case PayloadKind::ConfigWrite: {
        const QStringList args = splitArguments(text);
        if (args.size() != 2) {
            if (error) {
                *error = QStringLiteral("SET_CONFIG 参数格式为 index,value");
            }
            return false;
        }
        bool ok = false;
        const quint32 index = args.at(0).toUInt(&ok, 0);
        if (!ok || index == 0) {
            if (error) {
                *error = QStringLiteral("SET_CONFIG 的 index 非法");
            }
            return false;
        }

        const ConfigDef fallback{index, ConfigValueType::Int32, QString(), QString()};
        const ConfigDef *config = Protocol::configByIndex(index);
        if (!config) {
            config = &fallback;
        }

        QByteArray valuePayload;
        if (!encodeConfigValue(*config, args.at(1), &valuePayload, error)) {
            return false;
        }
        out = PayloadCodec::encodeUInt32(index);
        out += valuePayload;
        break;
    }
    case PayloadKind::OptionalUInt16x8:
        if (!PayloadCodec::parseUInt16List(text, Value1PartCount, &out, error)) {
            return false;
        }
        break;
    }

    if (payload) {
        *payload = out;
    }
    return true;
}

bool MainWindow::encodeConfigValue(const ConfigDef &config, const QString &text, QByteArray *payload, QString *error) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (error) {
            *error = QStringLiteral("%1 需要填写新值").arg(config.name.isEmpty() ? QStringLiteral("配置项") : config.name);
        }
        return false;
    }

    if (config.type == ConfigValueType::Float32) {
        bool ok = false;
        const float value = trimmed.toFloat(&ok);
        if (!ok) {
            if (error) {
                *error = QStringLiteral("%1 需要 float 值").arg(config.name);
            }
            return false;
        }
        if (payload) {
            *payload = PayloadCodec::encodeFloat(value);
        }
        return true;
    }

    bool ok = false;
    const qlonglong value = trimmed.toLongLong(&ok, 0);
    if (!ok || value < std::numeric_limits<qint32>::min() || value > std::numeric_limits<qint32>::max()) {
        if (error) {
            *error = QStringLiteral("%1 需要 int32 值").arg(config.name);
        }
        return false;
    }
    if (payload) {
        *payload = PayloadCodec::encodeInt32(static_cast<qint32>(value));
    }
    return true;
}

void MainWindow::handleReceivedFrame(const QCanBusFrame &frame)
{
    const QString parsed = describeFrame(frame, true);
    addLogFrame(QStringLiteral("RX"), frame, parsed);
}

void MainWindow::handleTransmittedFrame(const QCanBusFrame &frame)
{
    const QString parsed = describeFrame(frame, false);
    addLogFrame(QStringLiteral("TX"), frame, parsed);
}

QString MainWindow::describeFrame(const QCanBusFrame &frame, bool updateUi)
{
    if (frame.frameType() != QCanBusFrame::DataFrame) {
        return QStringLiteral("非数据帧");
    }

    const DecodedCanId decoded = CanIdCodec::decode(frame.frameId());
    if (!decoded.valid) {
        return QStringLiteral("非标准 11-bit ID");
    }

    const QByteArray payload = frame.payload();
    if (updateUi) {
        m_lastNodeLabel->setText(QString::number(decoded.nodeId));
        m_lastCommandLabel->setText(QStringLiteral("%1 %2").arg(decoded.commandId).arg(Protocol::commandName(decoded.commandId)));
    }

    const QString prefix = decoded.isReply ? QStringLiteral("设备回复") : QStringLiteral("主机请求");

    if (isReturnCodeCommand(decoded.commandId) && payload.size() >= 4) {
        return QStringLiteral("%1，%2").arg(prefix, returnCodeText(payload));
    }

    switch (decoded.commandId) {
    case 2: {
        float value = 0.0F;
        if (PayloadCodec::decodeFloat(payload, 0, &value)) {
            return QStringLiteral("%1，转矩=%2 Nm").arg(prefix).arg(value, 0, 'f', 4);
        }
        break;
    }
    case 3: {
        float value = 0.0F;
        if (PayloadCodec::decodeFloat(payload, 0, &value)) {
            return QStringLiteral("%1，速度=%2 turn/s").arg(prefix).arg(value, 0, 'f', 4);
        }
        break;
    }
    case 4: {
        float value = 0.0F;
        if (PayloadCodec::decodeFloat(payload, 0, &value)) {
            return QStringLiteral("%1，位置=%2 turn").arg(prefix).arg(value, 0, 'f', 4);
        }
        break;
    }
    case 6: {
        qint32 step = 0;
        quint32 data = 0;
        if (PayloadCodec::decodeInt32(payload, 0, &step) && PayloadCodec::decodeUInt32(payload, 4, &data)) {
            const QString text = QStringLiteral("step=%1 data=%2").arg(step).arg(PayloadCodec::formatUInt32Hex(data));
            if (updateUi) {
                m_calibLabel->setText(text);
            }
            return QStringLiteral("%1，校准上报 %2").arg(prefix, text);
        }
        break;
    }
    case 9: {
        qint32 step = 0;
        qint32 value = 0;
        if (PayloadCodec::decodeInt32(payload, 0, &step) && PayloadCodec::decodeInt32(payload, 4, &value)) {
            const QString text = QStringLiteral("step=%1 value=%2").arg(step).arg(value);
            if (updateUi) {
                m_anticoggingLabel->setText(text);
            }
            return QStringLiteral("%1，齿槽补偿上报 %2").arg(prefix, text);
        }
        break;
    }
    case 13:
    case 14: {
        quint32 status = 0;
        quint32 errors = 0;
        if (PayloadCodec::decodeUInt32(payload, 0, &status) && PayloadCodec::decodeUInt32(payload, 4, &errors)) {
            if (updateUi) {
                updateStatusWords(status, errors);
            }
            return QStringLiteral("%1，status=%2 errors=%3")
                .arg(prefix, PayloadCodec::formatUInt32Hex(status), PayloadCodec::formatUInt32Hex(errors));
        }
        break;
    }
    case 15: {
        if (payload.size() >= 2) {
            const QStringList names = value1PartNames();
            QStringList parts;
            for (int i = 0; i < Value1PartCount; ++i) {
                const QString valueText = value1PartText(payload, i);
                parts << QStringLiteral("%1=%2").arg(names.at(i), valueText);
                if (updateUi && i < m_value1Labels.size()) {
                    m_value1Labels.at(i)->setText(valueText);
                }
            }
            const QString text = parts.join(QStringLiteral("  "));
            return QStringLiteral("%1，VALUE_1 %2").arg(prefix, text);
        }
        break;
    }
    case 17:
    case 18: {
        quint32 index = 0;
        quint32 rawValue = 0;
        if (PayloadCodec::decodeUInt32(payload, 0, &index) && PayloadCodec::decodeUInt32(payload, 4, &rawValue)) {
            if (updateUi) {
                updateConfigCurrentValue(index, rawValue);
            }
            const ConfigDef *config = Protocol::configByIndex(index);
            QString valueText;
            if (config && config->type == ConfigValueType::Float32) {
                float value = 0.0F;
                PayloadCodec::decodeFloat(payload, 4, &value);
                valueText = QString::number(value, 'g', 8);
            } else {
                valueText = QString::number(static_cast<qint32>(rawValue));
            }
            return QStringLiteral("%1，配置[%2] %3=%4")
                .arg(prefix)
                .arg(index)
                .arg(config ? config->name : QStringLiteral("unknown"))
                .arg(valueText);
        }
        break;
    }
    case 22:
        return QStringLiteral("%1，心跳").arg(prefix);
    case 27: {
        quint8 value = 0;
        if (PayloadCodec::decodeUInt8(payload, 0, &value)) {
            return QStringLiteral("%1，自动推送开关=%2").arg(prefix).arg(value);
        }
        break;
    }
    case 28: {
        quint32 major = 0;
        quint32 minor = 0;
        if (PayloadCodec::decodeUInt32(payload, 0, &major) && PayloadCodec::decodeUInt32(payload, 4, &minor)) {
            const QString version = QStringLiteral("%1.%2").arg(major).arg(minor);
            if (updateUi) {
                m_fwVersionLabel->setText(version);
            }
            return QStringLiteral("%1，固件版本=%2").arg(prefix, version);
        }
        break;
    }
    default:
        break;
    }

    return QStringLiteral("%1，raw=%2").arg(prefix, PayloadCodec::bytesToHex(payload));
}

void MainWindow::addLogFrame(const QString &direction, const QCanBusFrame &frame, const QString &parsed)
{
    if (!m_logTable) {
        return;
    }

    const bool followTail = shouldFollowLogTail(m_logTable);

    while (m_logTable->rowCount() >= LogRowLimit) {
        m_logTable->removeRow(0);
    }

    const int row = m_logTable->rowCount();
    m_logTable->insertRow(row);

    const DecodedCanId decoded = CanIdCodec::decode(frame.frameId());
    const QString node = decoded.valid ? QString::number(decoded.nodeId) : QStringLiteral("-");
    const QString command = decoded.valid ? QStringLiteral("%1 %2").arg(decoded.commandId).arg(Protocol::commandById(decoded.commandId) ? Protocol::commandById(decoded.commandId)->displayName : QStringLiteral("UNKNOWN"))
                                          : QStringLiteral("-");

    m_logTable->setItem(row, 0, readOnlyItem(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))));
    m_logTable->setItem(row, 1, readOnlyItem(direction));
    m_logTable->setItem(row, 2, readOnlyItem(idText(frame.frameId())));
    m_logTable->setItem(row, 3, readOnlyItem(node));
    m_logTable->setItem(row, 4, readOnlyItem(command));
    m_logTable->setItem(row, 5, readOnlyItem(dlcText(frame)));
    m_logTable->setItem(row, 6, readOnlyItem(PayloadCodec::bytesToHex(frame.payload())));
    m_logTable->setItem(row, 7, readOnlyItem(parsed));
    if (followTail) {
        m_logTable->verticalScrollBar()->setValue(m_logTable->verticalScrollBar()->maximum());
    }
}

void MainWindow::appendSystemLog(const QString &message)
{
    if (!m_logTable) {
        return;
    }

    const bool followTail = shouldFollowLogTail(m_logTable);

    while (m_logTable->rowCount() >= LogRowLimit) {
        m_logTable->removeRow(0);
    }

    const int row = m_logTable->rowCount();
    m_logTable->insertRow(row);
    m_logTable->setItem(row, 0, readOnlyItem(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))));
    m_logTable->setItem(row, 1, readOnlyItem(QStringLiteral("SYS")));
    m_logTable->setItem(row, 2, readOnlyItem(QStringLiteral("-")));
    m_logTable->setItem(row, 3, readOnlyItem(QStringLiteral("-")));
    m_logTable->setItem(row, 4, readOnlyItem(QStringLiteral("-")));
    m_logTable->setItem(row, 5, readOnlyItem(QStringLiteral("-")));
    m_logTable->setItem(row, 6, readOnlyItem(QStringLiteral("-")));
    m_logTable->setItem(row, 7, readOnlyItem(message));
    if (followTail) {
        m_logTable->verticalScrollBar()->setValue(m_logTable->verticalScrollBar()->maximum());
    }
}

void MainWindow::updateStatusWords(quint32 status, quint32 errors)
{
    m_statusWordLabel->setText(PayloadCodec::formatUInt32Hex(status));
    m_errorsWordLabel->setText(PayloadCodec::formatUInt32Hex(errors));

    setFlagLabel(m_switchedOnLabel, (status & (1U << 0)) != 0);
    setFlagLabel(m_targetReachedLabel, (status & (1U << 1)) != 0);
    setFlagLabel(m_currentLimitLabel, (status & (1U << 2)) != 0);

    setFlagLabel(m_adcSelftestLabel, (errors & (1U << 0)) != 0);
    setFlagLabel(m_encoderOfflineLabel, (errors & (1U << 1)) != 0);
    setFlagLabel(m_overVoltageLabel, (errors & (1U << 16)) != 0);
    setFlagLabel(m_underVoltageLabel, (errors & (1U << 17)) != 0);
    setFlagLabel(m_overCurrentLabel, (errors & (1U << 18)) != 0);
}

void MainWindow::updateConfigCurrentValue(quint32 index, quint32 rawValue)
{
    QTableWidgetItem *item = m_configCurrentItems.value(index);
    if (!item) {
        return;
    }

    const ConfigDef *config = Protocol::configByIndex(index);
    if (config && config->type == ConfigValueType::Float32) {
        const QByteArray raw = PayloadCodec::encodeUInt32(rawValue);
        float value = 0.0F;
        PayloadCodec::decodeFloat(raw, 0, &value);
        item->setText(QString::number(value, 'g', 8));
    } else {
        item->setText(QString::number(static_cast<qint32>(rawValue)));
    }
}

void MainWindow::setFlagLabel(QLabel *label, bool active)
{
    label->setText(active ? QStringLiteral("1") : QStringLiteral("0"));
    label->setStyleSheet(active ? QStringLiteral("color: #B42318; font-weight: 700;")
                                : QStringLiteral("color: #067647; font-weight: 600;"));
}

void MainWindow::setConnectionState(bool connected, const QString &message)
{
    if (m_canContentArea) {
        m_canContentArea->setEnabled(connected);
    }
    m_connectionStateLabel->setText(message);
    m_connectionStateLabel->setStyleSheet(connected ? QStringLiteral("background: #DDE8D6; color: #1F4D1F; border: 1px solid #7F9A7F; border-radius: 0;")
                                                    : QStringLiteral("background: #DCDCDC; color: #202020; border: 1px solid #9A9A9A; border-radius: 0;"));
    statusBar()->showMessage(message, 3000);
}

quint8 MainWindow::currentNodeId() const
{
    return static_cast<quint8>(m_nodeSpin->value());
}

QString MainWindow::commandInputText(quint8 commandId) const
{
    const QLineEdit *input = m_commandInputs.value(commandId);
    return input ? input->text().trimmed() : QString();
}
