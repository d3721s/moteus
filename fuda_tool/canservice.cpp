#include "canservice.h"

#include "canidcodec.h"

#include <QCanBus>
#include <QVariant>

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

QString CanService::interfaceName() const
{
    return m_interfaceName;
}

bool CanService::connectInterface(const QString &interfaceName, int bitrate, int dataBitrate)
{
    clearDevice();

    QString error;
    m_device = QCanBus::instance()->createDevice(QStringLiteral("socketcan"), interfaceName, &error);
    if (!m_device) {
        emit connectionChanged(false, error);
        emit errorOccurred(error);
        return false;
    }

    m_interfaceName = interfaceName;
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
        emit connectionChanged(connected, connected ? QStringLiteral("已连接 %1").arg(m_interfaceName)
                                                   : QStringLiteral("未连接"));
    });

    if (!m_device->connectDevice()) {
        const QString message = m_device->errorString();
        emit connectionChanged(false, message);
        emit errorOccurred(message);
        clearDevice();
        return false;
    }

    emit connectionChanged(true, QStringLiteral("已连接 %1").arg(interfaceName));
    return true;
}

void CanService::disconnectInterface()
{
    clearDevice();
    emit connectionChanged(false, QStringLiteral("未连接"));
}

bool CanService::sendCommand(quint8 nodeId,
                             quint8 commandId,
                             const QByteArray &payload,
                             QString *errorMessage)
{
    if (!isConnected()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CAN 未连接");
        }
        return false;
    }

    if (payload.size() > 64) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("payload 长度 %1 超过 CAN FD 64 字节限制").arg(payload.size());
        }
        return false;
    }

    QCanBusFrame frame(CanIdCodec::makeRequestId(nodeId, commandId), payload);
    frame.setFrameType(QCanBusFrame::DataFrame);
    frame.setExtendedFrameFormat(false);
    frame.setFlexibleDataRateFormat(true);
    frame.setBitrateSwitch(m_bitrateSwitchEnabled);

    if (!m_device->writeFrame(frame)) {
        if (errorMessage) {
            *errorMessage = m_device->errorString();
        }
        return false;
    }

    emit frameTransmitted(frame);
    return true;
}

void CanService::clearDevice()
{
    if (!m_device) {
        return;
    }

    if (m_device->state() != QCanBusDevice::UnconnectedState) {
        m_device->disconnectDevice();
    }
    m_device->deleteLater();
    m_device = nullptr;
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
