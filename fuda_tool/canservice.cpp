#include "canservice.h"

#include "canidcodec.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <QRegularExpression>

#ifdef Q_OS_WIN
#include "ControlCANFD.h"
#else
#include <QCanBus>
#include <QVariant>
#endif

namespace
{
#ifdef Q_OS_WIN
constexpr UINT ControlCanFdDeviceType = ZCAN_USBCANFD_200U;
constexpr int ReceivePollIntervalMs = 5;
constexpr UINT ReceiveBatchSize = 100;
constexpr BYTE CanFdBrsFlag = 0x01;

struct ControlCanFdInterface
{
    UINT deviceIndex = 0;
    UINT canIndex = 0;
};

UINT displayedCanPortToIndex(UINT canPort)
{
    return canPort == 0 ? 0 : canPort - 1;
}

bool parseControlCanFdInterface(const QString &interfaceName, ControlCanFdInterface *result)
{
    if (!result) {
        return false;
    }

    const QString normalized = interfaceName.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }

    QList<UINT> numbers;
    const QRegularExpression numberPattern(QStringLiteral("(\\d+)"));
    QRegularExpressionMatchIterator matches = numberPattern.globalMatch(normalized);
    while (matches.hasNext()) {
        bool ok = false;
        const UINT value = matches.next().captured(1).toUInt(&ok);
        if (!ok) {
            return false;
        }
        numbers.append(value);
    }

    if (numbers.isEmpty()) {
        return false;
    }

    if (numbers.size() == 1) {
        if (normalized.startsWith(QStringLiteral("can"))) {
            result->deviceIndex = 0;
            result->canIndex = displayedCanPortToIndex(numbers.at(0));
        } else {
            result->deviceIndex = numbers.at(0);
            result->canIndex = 0;
        }
        return true;
    }

    result->deviceIndex = numbers.at(0);
    result->canIndex = normalized.contains(QStringLiteral("can")) ? displayedCanPortToIndex(numbers.at(1))
                                                                  : numbers.at(1);
    return true;
}

QCanBusFrame::FrameType frameTypeFromCanId(UINT rawId)
{
    if (IS_ERR(rawId)) {
        return QCanBusFrame::ErrorFrame;
    }
    if (IS_RTR(rawId)) {
        return QCanBusFrame::RemoteRequestFrame;
    }
    return QCanBusFrame::DataFrame;
}

void setTimestamp(QCanBusFrame *frame, UINT64 timestampUs)
{
    if (!frame) {
        return;
    }

    frame->setTimeStamp(QCanBusFrame::TimeStamp(static_cast<qint64>(timestampUs / 1000000ULL),
                                                static_cast<qint64>(timestampUs % 1000000ULL)));
}
#else
QString defaultCanPluginName()
{
    return QStringLiteral("socketcan");
}
#endif
} // namespace

CanService::CanService(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    m_receiveTimer = new QTimer(this);
    m_receiveTimer->setInterval(ReceivePollIntervalMs);
    m_receiveTimer->setTimerType(Qt::PreciseTimer);
    connect(m_receiveTimer, &QTimer::timeout, this, &CanService::processReceivedFrames);
#endif
}

CanService::~CanService()
{
    clearDevice();
}

bool CanService::isConnected() const
{
#ifdef Q_OS_WIN
    return m_deviceHandle && m_channelHandle;
#else
    return m_device && m_device->state() == QCanBusDevice::ConnectedState;
#endif
}

void CanService::connectInterface(const QString &interfaceName, int bitrate, int dataBitrate, bool bitrateSwitchEnabled)
{
    clearDevice();

#ifdef Q_OS_WIN
    ControlCanFdInterface parsedInterface;
    if (!parseControlCanFdInterface(interfaceName, &parsedInterface)) {
        const QString message = QStringLiteral("设备名格式错误，请使用 usb0、usb0:can1 或 can0");
        emit connectionChanged(false, message);
        emit errorOccurred(message);
        return;
    }

    m_interfaceName = interfaceName;
    m_pluginName = QStringLiteral("ControlCANFD");
    m_bitrateSwitchEnabled = bitrateSwitchEnabled;
    m_deviceIndex = parsedInterface.deviceIndex;
    m_canIndex = parsedInterface.canIndex;

    DEVICE_HANDLE deviceHandle = ZCAN_OpenDevice(ControlCanFdDeviceType, m_deviceIndex, 0);
    if (deviceHandle == INVALID_DEVICE_HANDLE) {
        const QString message = QStringLiteral("ControlCANFD 打开设备失败：usb%1").arg(m_deviceIndex);
        emit connectionChanged(false, message);
        emit errorOccurred(message);
        return;
    }

    auto failAndClose = [this, deviceHandle](const QString &message) {
        ZCAN_CloseDevice(deviceHandle);
        emit connectionChanged(false, message);
        emit errorOccurred(message);
    };

    if (bitrate > 0 && ZCAN_SetAbitBaud(deviceHandle, m_canIndex, static_cast<UINT>(bitrate)) != STATUS_OK) {
        failAndClose(QStringLiteral("ControlCANFD 设置仲裁波特率失败：%1").arg(bitrate));
        return;
    }
    if (m_bitrateSwitchEnabled && dataBitrate > 0 && ZCAN_SetDbitBaud(deviceHandle, m_canIndex, static_cast<UINT>(dataBitrate)) != STATUS_OK) {
        failAndClose(QStringLiteral("ControlCANFD 设置数据波特率失败：%1").arg(dataBitrate));
        return;
    }

    ZCAN_SetCANFDStandard(deviceHandle, m_canIndex, 0);

    ZCAN_CHANNEL_INIT_CONFIG initConfig = {};
    initConfig.can_type = TYPE_CANFD;
    initConfig.canfd.acc_code = 0;
    initConfig.canfd.acc_mask = 0xFFFFFFFFU;
    initConfig.canfd.filter = 0;
    initConfig.canfd.mode = 0;

    CHANNEL_HANDLE channelHandle = ZCAN_InitCAN(deviceHandle, m_canIndex, &initConfig);
    if (channelHandle == INVALID_CHANNEL_HANDLE) {
        failAndClose(QStringLiteral("ControlCANFD 初始化 CANFD 通道失败：usb%1 can%2").arg(m_deviceIndex).arg(m_canIndex));
        return;
    }

    if (ZCAN_StartCAN(channelHandle) != STATUS_OK) {
        ZCAN_ResetCAN(channelHandle);
        failAndClose(QStringLiteral("ControlCANFD 启动 CANFD 通道失败：usb%1 can%2").arg(m_deviceIndex).arg(m_canIndex));
        return;
    }

    m_deviceHandle = deviceHandle;
    m_channelHandle = channelHandle;
    ZCAN_ClearBuffer(channelHandle);
    if (m_receiveTimer) {
        m_receiveTimer->start();
    }

    emit connectionChanged(true,
                           QStringLiteral("已连接 %1 (%2 usb%3 can%4)")
                               .arg(interfaceName, m_pluginName)
                               .arg(m_deviceIndex)
                               .arg(m_canIndex));
#else
    const QString pluginName = defaultCanPluginName();
    QString error;
    m_device = QCanBus::instance()->createDevice(pluginName, interfaceName, &error);
    if (!m_device) {
        emit connectionChanged(false, error);
        emit errorOccurred(error);
        return;
    }

    m_interfaceName = interfaceName;
    m_pluginName = pluginName;
    m_bitrateSwitchEnabled = bitrateSwitchEnabled;

    if (bitrate > 0) {
        m_device->setConfigurationParameter(QCanBusDevice::BitRateKey, QVariant(bitrate));
    }
    m_device->setConfigurationParameter(QCanBusDevice::CanFdKey, QVariant(true));
    if (m_bitrateSwitchEnabled && dataBitrate > 0) {
        m_device->setConfigurationParameter(QCanBusDevice::DataBitRateKey, QVariant(dataBitrate));
    }

    connect(m_device, &QCanBusDevice::framesReceived, this, &CanService::processReceivedFrames);
    connect(m_device, &QCanBusDevice::errorOccurred, this, [this](QCanBusDevice::CanBusError errorCode) {
        if (!m_device || errorCode == QCanBusDevice::NoError) {
            return;
        }
        emit errorOccurred(m_device->errorString());
    });
    connect(m_device, &QCanBusDevice::stateChanged, this, [this](QCanBusDevice::CanBusDeviceState state) {
        const bool connected = state == QCanBusDevice::ConnectedState;
        emit connectionChanged(connected, connected ? QStringLiteral("已连接 %1 (%2)").arg(m_interfaceName, m_pluginName)
                                                   : QStringLiteral("未连接"));
    });

    if (!m_device->connectDevice()) {
        const QString message = m_device->errorString();
        emit connectionChanged(false, message);
        emit errorOccurred(message);
        clearDevice();
        return;
    }

    emit connectionChanged(true, QStringLiteral("已连接 %1 (%2)").arg(interfaceName, pluginName));
#endif
}

void CanService::disconnectInterface()
{
    clearDevice();
    emit connectionChanged(false, QStringLiteral("未连接"));
}

void CanService::sendCommand(quint8 nodeId, quint8 commandId, const QByteArray &payload)
{
    if (!isConnected()) {
        emit errorOccurred(QStringLiteral("CAN 未连接"));
        return;
    }

    if (payload.size() > 64) {
        emit errorOccurred(QStringLiteral("payload 长度 %1 超过 CAN FD 64 字节限制").arg(payload.size()));
        return;
    }

#ifdef Q_OS_WIN
    ZCAN_TransmitFD_Data transmit = {};
    transmit.frame.can_id = MAKE_CAN_ID(CanIdCodec::makeRequestId(nodeId, commandId), 0, 0, 0);
    transmit.frame.len = static_cast<BYTE>(payload.size());
    transmit.frame.flags = m_bitrateSwitchEnabled ? CanFdBrsFlag : 0;
    if (!payload.isEmpty()) {
        std::memcpy(transmit.frame.data, payload.constData(), static_cast<std::size_t>(payload.size()));
    }
    transmit.transmit_type = 0;

    if (ZCAN_TransmitFD(static_cast<CHANNEL_HANDLE>(m_channelHandle), &transmit, 1) != 1) {
        emit errorOccurred(QStringLiteral("ControlCANFD 发送 CANFD 帧失败"));
        return;
    }

    QCanBusFrame frame(CanIdCodec::makeRequestId(nodeId, commandId), payload);
    frame.setFrameType(QCanBusFrame::DataFrame);
    frame.setExtendedFrameFormat(false);
    frame.setFlexibleDataRateFormat(true);
    frame.setBitrateSwitch(m_bitrateSwitchEnabled);
    emit frameTransmitted(frame);
#else
    QCanBusFrame frame(CanIdCodec::makeRequestId(nodeId, commandId), payload);
    frame.setFrameType(QCanBusFrame::DataFrame);
    frame.setExtendedFrameFormat(false);
    frame.setFlexibleDataRateFormat(true);
    frame.setBitrateSwitch(m_bitrateSwitchEnabled);

    if (!m_device->writeFrame(frame)) {
        emit errorOccurred(m_device->errorString());
        return;
    }

    emit frameTransmitted(frame);
#endif
}

void CanService::clearDevice()
{
#ifdef Q_OS_WIN
    if (m_receiveTimer) {
        m_receiveTimer->stop();
    }
    if (m_channelHandle) {
        ZCAN_ResetCAN(static_cast<CHANNEL_HANDLE>(m_channelHandle));
        m_channelHandle = nullptr;
    }
    if (m_deviceHandle) {
        ZCAN_CloseDevice(static_cast<DEVICE_HANDLE>(m_deviceHandle));
        m_deviceHandle = nullptr;
    }
    m_pluginName.clear();
    m_bitrateSwitchEnabled = false;
#else
    if (!m_device) {
        return;
    }

    if (m_device->state() != QCanBusDevice::UnconnectedState) {
        m_device->disconnectDevice();
    }
    delete m_device;
    m_device = nullptr;
    m_pluginName.clear();
    m_bitrateSwitchEnabled = false;
#endif
}

void CanService::processReceivedFrames()
{
#ifdef Q_OS_WIN
    if (!isConnected()) {
        return;
    }

    std::array<ZCAN_ReceiveFD_Data, ReceiveBatchSize> fdFrames;
    while (true) {
        const UINT pending = ZCAN_GetReceiveNum(static_cast<CHANNEL_HANDLE>(m_channelHandle), TYPE_CANFD);
        if (pending == 0) {
            break;
        }

        const UINT requested = std::min<UINT>(pending, ReceiveBatchSize);
        const UINT received = ZCAN_ReceiveFD(static_cast<CHANNEL_HANDLE>(m_channelHandle),
                                             fdFrames.data(),
                                             requested,
                                             0);
        if (received == 0) {
            break;
        }

        for (UINT i = 0; i < received; ++i) {
            const ZCAN_ReceiveFD_Data &receivedFrame = fdFrames.at(i);
            const int payloadLength = std::min<int>(receivedFrame.frame.len, CANFD_MAX_DLEN);
            QCanBusFrame frame(GET_ID(receivedFrame.frame.can_id),
                               QByteArray(reinterpret_cast<const char *>(receivedFrame.frame.data), payloadLength));
            frame.setFrameType(frameTypeFromCanId(receivedFrame.frame.can_id));
            frame.setExtendedFrameFormat(IS_EFF(receivedFrame.frame.can_id));
            frame.setFlexibleDataRateFormat(true);
            frame.setBitrateSwitch((receivedFrame.frame.flags & CanFdBrsFlag) != 0);
            setTimestamp(&frame, receivedFrame.timestamp);
            emit frameReceived(frame);
        }
    }

    std::array<ZCAN_Receive_Data, ReceiveBatchSize> canFrames;
    while (true) {
        const UINT pending = ZCAN_GetReceiveNum(static_cast<CHANNEL_HANDLE>(m_channelHandle), TYPE_CAN);
        if (pending == 0) {
            break;
        }

        const UINT requested = std::min<UINT>(pending, ReceiveBatchSize);
        const UINT received = ZCAN_Receive(static_cast<CHANNEL_HANDLE>(m_channelHandle),
                                           canFrames.data(),
                                           requested,
                                           0);
        if (received == 0) {
            break;
        }

        for (UINT i = 0; i < received; ++i) {
            const ZCAN_Receive_Data &receivedFrame = canFrames.at(i);
            const int payloadLength = std::min<int>(receivedFrame.frame.can_dlc, CAN_MAX_DLEN);
            QCanBusFrame frame(GET_ID(receivedFrame.frame.can_id),
                               QByteArray(reinterpret_cast<const char *>(receivedFrame.frame.data), payloadLength));
            frame.setFrameType(frameTypeFromCanId(receivedFrame.frame.can_id));
            frame.setExtendedFrameFormat(IS_EFF(receivedFrame.frame.can_id));
            frame.setFlexibleDataRateFormat(false);
            setTimestamp(&frame, receivedFrame.timestamp);
            emit frameReceived(frame);
        }
    }
#else
    if (!m_device) {
        return;
    }

    while (m_device->framesAvailable() > 0) {
        emit frameReceived(m_device->readFrame());
    }
#endif
}
