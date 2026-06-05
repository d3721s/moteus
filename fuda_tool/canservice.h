#ifndef CANSERVICE_H
#define CANSERVICE_H

#include <QObject>
#include <QCanBusFrame>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <QTimer>
#else
#include <QCanBusDevice>
#endif

class CanService : public QObject
{
    Q_OBJECT

public:
    explicit CanService(QObject *parent = nullptr);
    ~CanService() override;

public slots:
    void connectInterface(const QString &interfaceName, int bitrate, int dataBitrate, bool bitrateSwitchEnabled);
    void disconnectInterface();
    void sendCommand(quint8 nodeId, quint8 commandId, const QByteArray &payload);

private:
    bool isConnected() const;

signals:
    void connectionChanged(bool connected, const QString &message);
    void frameReceived(const QCanBusFrame &frame);
    void frameTransmitted(const QCanBusFrame &frame);
    void errorOccurred(const QString &message);

private:
    void clearDevice();
    void processReceivedFrames();

#ifdef Q_OS_WIN
    QTimer *m_receiveTimer = nullptr;
    void *m_deviceHandle = nullptr;
    void *m_channelHandle = nullptr;
    unsigned int m_deviceIndex = 0;
    unsigned int m_canIndex = 0;
#else
    QCanBusDevice *m_device = nullptr;
#endif
    QString m_interfaceName;
    QString m_pluginName;
    bool m_bitrateSwitchEnabled = false;
};

#endif // CANSERVICE_H
