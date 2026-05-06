#include "mainwindow.h"

#include "canidcodec.h"
#include "canservice.h"
#include "payloadcodec.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QDateTime>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <limits>

namespace
{
constexpr int LogRowLimit = 2000;

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

QLabel *valueLabel(const QString &text = QStringLiteral("-"))
{
    auto *label = new QLabel(text);
    label->setMinimumWidth(92);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_canService(new CanService(this))
{
    setWindowTitle(QStringLiteral("fuda_tool - SocketCAN 测试工具"));
    resize(1500, 920);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    root->addWidget(createConnectionPanel());

    auto *verticalSplitter = new QSplitter(Qt::Vertical, central);
    auto *upperSplitter = new QSplitter(Qt::Horizontal, verticalSplitter);
    upperSplitter->addWidget(createCommandPanel());
    upperSplitter->addWidget(createStatusPanel());
    upperSplitter->setStretchFactor(0, 4);
    upperSplitter->setStretchFactor(1, 1);

    verticalSplitter->addWidget(upperSplitter);
    verticalSplitter->addWidget(createConfigPanel());
    verticalSplitter->addWidget(createLogPanel());
    verticalSplitter->setStretchFactor(0, 5);
    verticalSplitter->setStretchFactor(1, 3);
    verticalSplitter->setStretchFactor(2, 2);

    root->addWidget(verticalSplitter, 1);
    setCentralWidget(central);

    applyVisualStyle();
    setupCanConnections();
    setConnectionState(false, QStringLiteral("未连接"));
}

MainWindow::~MainWindow() = default;

QWidget *MainWindow::createConnectionPanel()
{
    auto *box = new QGroupBox(QStringLiteral("CAN 连接"), this);
    auto *layout = new QGridLayout(box);
    layout->setContentsMargins(12, 18, 12, 12);
    layout->setHorizontalSpacing(10);
    layout->setVerticalSpacing(8);

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

    layout->addWidget(new QLabel(QStringLiteral("接口"), box), 0, 0);
    layout->addWidget(m_interfaceEdit, 0, 1);
    layout->addWidget(new QLabel(QStringLiteral("节点 ID"), box), 0, 2);
    layout->addWidget(m_nodeSpin, 0, 3);
    layout->addWidget(new QLabel(QStringLiteral("仲裁波特率"), box), 0, 4);
    layout->addWidget(m_bitrateSpin, 0, 5);
    layout->addWidget(new QLabel(QStringLiteral("帧格式"), box), 0, 6);
    layout->addWidget(new QLabel(QStringLiteral("CAN FD"), box), 0, 7);
    layout->addWidget(new QLabel(QStringLiteral("数据波特率"), box), 0, 8);
    layout->addWidget(m_dataBitrateSpin, 0, 9);
    layout->addWidget(connectButton, 0, 10);
    layout->addWidget(disconnectButton, 0, 11);
    layout->addWidget(m_connectionStateLabel, 0, 12);
    layout->setColumnStretch(12, 1);

    connect(connectButton, &QPushButton::clicked, this, [this]() {
        const QString interfaceName = m_interfaceEdit->text().trimmed();
        if (interfaceName.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("参数错误"), QStringLiteral("CAN 接口名不能为空"));
            return;
        }
        m_canService->connectInterface(interfaceName,
                                       m_bitrateSpin->value(),
                                       m_dataBitrateSpin->value());
    });
    connect(disconnectButton, &QPushButton::clicked, m_canService, &CanService::disconnectInterface);

    return box;
}

QWidget *MainWindow::createCommandPanel()
{
    auto *box = new QGroupBox(QStringLiteral("命令测试"), this);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(10, 18, 10, 10);

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

    const auto &commands = Protocol::commandDefinitions();
    m_commandTable->setRowCount(commands.size());
    for (int row = 0; row < commands.size(); ++row) {
        const CommandDef &command = commands.at(row);
        m_commandTable->setItem(row, 0, readOnlyItem(QString::number(command.id)));
        m_commandTable->setItem(row, 1, readOnlyItem(QStringLiteral("%1\n%2").arg(command.enumName, command.displayName)));
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
    m_commandTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_commandTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_commandTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_commandTable->verticalHeader()->setDefaultSectionSize(42);

    layout->addWidget(m_commandTable);
    return box;
}

QWidget *MainWindow::createConfigPanel()
{
    auto *box = new QGroupBox(QStringLiteral("参数读写"), this);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(10, 18, 10, 10);

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
    m_configTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_configTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_configTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_configTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    m_configTable->verticalHeader()->setDefaultSectionSize(34);

    layout->addWidget(m_configTable);
    return box;
}

QWidget *MainWindow::createStatusPanel()
{
    auto *box = new QGroupBox(QStringLiteral("实时状态"), this);
    auto *layout = new QFormLayout(box);
    layout->setContentsMargins(12, 18, 12, 12);
    layout->setSpacing(8);
    layout->setLabelAlignment(Qt::AlignRight);

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
    m_value1Label = valueLabel();
    m_value1Label->setWordWrap(true);
    m_value1Label->setMinimumWidth(260);

    layout->addRow(QStringLiteral("最近节点"), m_lastNodeLabel);
    layout->addRow(QStringLiteral("最近命令"), m_lastCommandLabel);
    layout->addRow(QStringLiteral("status_code"), m_statusWordLabel);
    layout->addRow(QStringLiteral("switched_on"), m_switchedOnLabel);
    layout->addRow(QStringLiteral("target_reached"), m_targetReachedLabel);
    layout->addRow(QStringLiteral("current_limit_active"), m_currentLimitLabel);
    layout->addRow(QStringLiteral("errors_code"), m_errorsWordLabel);
    layout->addRow(QStringLiteral("adc_selftest_fatal"), m_adcSelftestLabel);
    layout->addRow(QStringLiteral("encoder_offline"), m_encoderOfflineLabel);
    layout->addRow(QStringLiteral("over_voltage"), m_overVoltageLabel);
    layout->addRow(QStringLiteral("under_voltage"), m_underVoltageLabel);
    layout->addRow(QStringLiteral("over_current"), m_overCurrentLabel);
    layout->addRow(QStringLiteral("固件版本"), m_fwVersionLabel);
    layout->addRow(QStringLiteral("校准上报"), m_calibLabel);
    layout->addRow(QStringLiteral("齿槽补偿"), m_anticoggingLabel);
    layout->addRow(QStringLiteral("VALUE_1"), m_value1Label);

    updateStatusWords(0, 0);
    return box;
}

QWidget *MainWindow::createLogPanel()
{
    auto *box = new QGroupBox(QStringLiteral("CAN 日志"), this);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(10, 18, 10, 10);

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
    m_logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_logTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    m_logTable->verticalHeader()->setDefaultSectionSize(30);

    layout->addWidget(m_logTable);
    return box;
}

void MainWindow::setupCanConnections()
{
    connect(m_canService, &CanService::connectionChanged, this, &MainWindow::setConnectionState);
    connect(m_canService, &CanService::frameReceived, this, &MainWindow::handleReceivedFrame);
    connect(m_canService, &CanService::frameTransmitted, this, &MainWindow::handleTransmittedFrame);
    connect(m_canService, &CanService::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 5000);
        appendSystemLog(QStringLiteral("ERROR: %1").arg(message));
    });
}

void MainWindow::applyVisualStyle()
{
    qApp->setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #F5F7FA;
            color: #1F2937;
            font-size: 13px;
        }
        QGroupBox {
            background: #FFFFFF;
            border: 1px solid #D6DCE5;
            border-radius: 6px;
            margin-top: 8px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px;
        }
        QLineEdit, QSpinBox {
            min-height: 26px;
            border: 1px solid #C9D1DD;
            border-radius: 4px;
            padding: 2px 6px;
            background: #FFFFFF;
            selection-background-color: #2563EB;
        }
        QLineEdit:disabled {
            color: #6B7280;
            background: #F1F3F6;
        }
        QPushButton {
            min-height: 26px;
            border: 1px solid #B9C2D0;
            border-radius: 4px;
            padding: 3px 10px;
            background: #FFFFFF;
        }
        QPushButton:hover {
            background: #EEF4FF;
            border-color: #7EA6F7;
        }
        QPushButton:pressed {
            background: #DCEBFF;
        }
        QPushButton:disabled {
            color: #9CA3AF;
            background: #F3F4F6;
        }
        QHeaderView::section {
            background: #E8EDF5;
            border: 0;
            border-right: 1px solid #D5DBE5;
            border-bottom: 1px solid #D5DBE5;
            padding: 6px;
            font-weight: 600;
        }
        QTableWidget {
            gridline-color: #E2E7EF;
            background: #FFFFFF;
            alternate-background-color: #FAFBFD;
            selection-background-color: #D8E8FF;
            selection-color: #111827;
            border: 1px solid #D6DCE5;
            border-radius: 4px;
        }
        QLabel#connectionState {
            padding: 3px 10px;
            border-radius: 4px;
            background: #EEF2F7;
            color: #374151;
        }
    )"));
}

void MainWindow::sendProtocolCommand(quint8 commandId)
{
    const CommandDef *command = Protocol::commandById(commandId);
    if (!command || !command->txAllowed) {
        return;
    }

    if (commandId == 20) {
        const auto result = QMessageBox::question(this,
                                                  QStringLiteral("确认恢复出厂配置"),
                                                  QStringLiteral("该命令会恢复全部配置，且仅在失能下有效。确认发送？"));
        if (result != QMessageBox::Yes) {
            return;
        }
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

    if (!m_canService->sendCommand(currentNodeId(), commandId, payload, &error)) {
        QMessageBox::warning(this, QStringLiteral("发送失败"), error);
    }
}

void MainWindow::sendConfigRead(quint32 index)
{
    QString error;
    const QByteArray payload = PayloadCodec::encodeUInt32(index);
    if (!m_canService->sendCommand(currentNodeId(), 18, payload, &error)) {
        QMessageBox::warning(this, QStringLiteral("发送失败"), error);
    }
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
    if (!m_canService->sendCommand(currentNodeId(), 17, payload, &error)) {
        QMessageBox::warning(this, QStringLiteral("发送失败"), error);
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
        if (!PayloadCodec::parseUInt16List(text, 8, &out, error)) {
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
        if (payload.size() >= 16) {
            QStringList names = {QStringLiteral("速度"),
                                 QStringLiteral("位置"),
                                 QStringLiteral("hall偏移"),
                                 QStringLiteral("hall值"),
                                 QStringLiteral("status"),
                                 QStringLiteral("errors"),
                                 QStringLiteral("板载NTC"),
                                 QStringLiteral("Iq电流")};
            QStringList parts;
            for (int i = 0; i < 8; ++i) {
                quint16 value = 0;
                PayloadCodec::decodeUInt16(payload, i * 2, &value);
                parts << QStringLiteral("%1=%2").arg(names.at(i)).arg(value);
            }
            const QString text = parts.join(QStringLiteral("  "));
            if (updateUi) {
                m_value1Label->setText(text);
            }
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
    m_logTable->scrollToBottom();
}

void MainWindow::appendSystemLog(const QString &message)
{
    if (!m_logTable) {
        return;
    }

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
    m_logTable->scrollToBottom();
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
    m_connectionStateLabel->setText(message);
    m_connectionStateLabel->setStyleSheet(connected ? QStringLiteral("background: #E7F6EC; color: #067647;")
                                                    : QStringLiteral("background: #EEF2F7; color: #374151;"));
    statusBar()->showMessage(message, 3000);
    appendSystemLog(message);
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
