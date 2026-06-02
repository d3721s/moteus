#include "canservice.h"

#include "canidcodec.h"

#include <QCanBus>
#include <QVariant>

namespace
{
QString defaultCanPluginName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("peakcan");
#else
    return QStringLiteral("socketcan");
#endif
}
} // namespace

CanService::CanService(QObject *parent)
    : QObject(parent)
{
}

CanService::~CanService()
{
    clearDevice();
}

bool CanService::isConnected() const
{
    return m_device && m_device->state() == QCanBusDevice::ConnectedState;
}

void CanService::connectInterface(const QString &interfaceName, int bitrate, int dataBitrate)
{
    clearDevice();

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
    m_bitrateSwitchEnabled = dataBitrate > 0 && dataBitrate != bitrate;

    if (bitrate > 0) {
        m_device->setConfigurationParameter(QCanBusDevice::BitRateKey, QVariant(bitrate));
    }
    m_device->setConfigurationParameter(QCanBusDevice::CanFdKey, QVariant(true));
    if (dataBitrate > 0) {
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
}

void CanService::clearDevice()
{
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
}

void CanService::processReceivedFrames()
{
    if (!m_device) {
        return;
    }

    while (m_device->framesAvailable() > 0) {
        emit frameReceived(m_device->readFrame());
    }
}
