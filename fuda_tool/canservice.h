#ifndef CANSERVICE_H
#define CANSERVICE_H

#include <QObject>
#include <QCanBusDevice>
#include <QCanBusFrame>

class CanService : public QObject
{
    Q_OBJECT

public:
    explicit CanService(QObject *parent = nullptr);
    ~CanService() override;

    bool isConnected() const;
    QString interfaceName() const;

    bool connectInterface(const QString &interfaceName, int bitrate, int dataBitrate);
    void disconnectInterface();
    bool sendCommand(quint8 nodeId,
                     quint8 commandId,
                     const QByteArray &payload,
                     QString *errorMessage = nullptr);

signals:
    void connectionChanged(bool connected, const QString &message);
    void frameReceived(const QCanBusFrame &frame);
    void frameTransmitted(const QCanBusFrame &frame);
    void errorOccurred(const QString &message);

private:
    void clearDevice();
    void processReceivedFrames();

    QCanBusDevice *m_device = nullptr;
    QString m_interfaceName;
    bool m_bitrateSwitchEnabled = false;
};

#endif // CANSERVICE_H
