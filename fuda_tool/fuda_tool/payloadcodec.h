#ifndef PAYLOADCODEC_H
#define PAYLOADCODEC_H

#include <QByteArray>
#include <QString>
#include <QStringList>

class PayloadCodec
{
public:
    static QByteArray encodeInt32(qint32 value);
    static QByteArray encodeUInt32(quint32 value);
    static QByteArray encodeUInt16(quint16 value);
    static QByteArray encodeUInt8(quint8 value);
    static QByteArray encodeFloat(float value);

    static bool decodeInt32(const QByteArray &payload, int offset, qint32 *value);
    static bool decodeUInt32(const QByteArray &payload, int offset, quint32 *value);
    static bool decodeUInt16(const QByteArray &payload, int offset, quint16 *value);
    static bool decodeUInt8(const QByteArray &payload, int offset, quint8 *value);
    static bool decodeFloat(const QByteArray &payload, int offset, float *value);

    static QString bytesToHex(const QByteArray &payload);
    static bool parseHexBytes(const QString &text, QByteArray *payload, QString *error);
    static bool parseUInt16List(const QString &text, int expectedCount, QByteArray *payload, QString *error);
    static QString formatUInt32Hex(quint32 value);
};

#endif // PAYLOADCODEC_H
