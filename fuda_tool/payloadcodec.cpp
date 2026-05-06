#include "payloadcodec.h"

#include <QRegularExpression>
#include <QtEndian>

#include <cstring>

QByteArray PayloadCodec::encodeInt32(qint32 value)
{
    return encodeUInt32(static_cast<quint32>(value));
}

QByteArray PayloadCodec::encodeUInt32(quint32 value)
{
    QByteArray out(4, Qt::Uninitialized);
    qToLittleEndian(value, out.data());
    return out;
}

QByteArray PayloadCodec::encodeUInt16(quint16 value)
{
    QByteArray out(2, Qt::Uninitialized);
    qToLittleEndian(value, out.data());
    return out;
}

QByteArray PayloadCodec::encodeUInt8(quint8 value)
{
    QByteArray out(1, Qt::Uninitialized);
    out[0] = static_cast<char>(value);
    return out;
}

QByteArray PayloadCodec::encodeFloat(float value)
{
    quint32 bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return encodeUInt32(bits);
}

bool PayloadCodec::decodeInt32(const QByteArray &payload, int offset, qint32 *value)
{
    quint32 raw = 0;
    if (!decodeUInt32(payload, offset, &raw)) {
        return false;
    }
    if (value) {
        *value = static_cast<qint32>(raw);
    }
    return true;
}

bool PayloadCodec::decodeUInt32(const QByteArray &payload, int offset, quint32 *value)
{
    if (offset < 0 || payload.size() < offset + 4) {
        return false;
    }
    if (value) {
        *value = qFromLittleEndian<quint32>(payload.constData() + offset);
    }
    return true;
}

bool PayloadCodec::decodeUInt16(const QByteArray &payload, int offset, quint16 *value)
{
    if (offset < 0 || payload.size() < offset + 2) {
        return false;
    }
    if (value) {
        *value = qFromLittleEndian<quint16>(payload.constData() + offset);
    }
    return true;
}

bool PayloadCodec::decodeUInt8(const QByteArray &payload, int offset, quint8 *value)
{
    if (offset < 0 || payload.size() < offset + 1) {
        return false;
    }
    if (value) {
        *value = static_cast<quint8>(payload.at(offset));
    }
    return true;
}

bool PayloadCodec::decodeFloat(const QByteArray &payload, int offset, float *value)
{
    quint32 bits = 0;
    if (!decodeUInt32(payload, offset, &bits)) {
        return false;
    }
    if (value) {
        std::memcpy(value, &bits, sizeof(bits));
    }
    return true;
}

QString PayloadCodec::bytesToHex(const QByteArray &payload)
{
    QStringList parts;
    parts.reserve(payload.size());
    for (const uchar byte : payload) {
        parts << QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return parts.join(QLatin1Char(' '));
}

bool PayloadCodec::parseHexBytes(const QString &text, QByteArray *payload, QString *error)
{
    QByteArray out;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (payload) {
            *payload = out;
        }
        return true;
    }

    QStringList tokens;
    if (!trimmed.contains(QRegularExpression(QStringLiteral("[\\s,;]")))) {
        QString compact = trimmed;
        if (compact.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            compact = compact.mid(2);
        }
        if ((compact.size() % 2) != 0) {
            if (error) {
                *error = QStringLiteral("十六进制字节串长度必须为偶数");
            }
            return false;
        }
        for (int i = 0; i < compact.size(); i += 2) {
            tokens << compact.mid(i, 2);
        }
    } else {
        tokens = trimmed.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
    }

    for (QString token : tokens) {
        token = token.trimmed();
        if (token.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            token = token.mid(2);
        }
        bool ok = false;
        const int value = token.toInt(&ok, 16);
        if (!ok || value < 0 || value > 0xFF) {
            if (error) {
                *error = QStringLiteral("非法十六进制字节: %1").arg(token);
            }
            return false;
        }
        out.append(static_cast<char>(value));
    }

    if (payload) {
        *payload = out;
    }
    return true;
}

bool PayloadCodec::parseUInt16List(const QString &text, int expectedCount, QByteArray *payload, QString *error)
{
    QByteArray out;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (payload) {
            *payload = out;
        }
        return true;
    }

    const QStringList tokens = trimmed.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
    if (expectedCount > 0 && tokens.size() != expectedCount) {
        if (error) {
            *error = QStringLiteral("需要 %1 个 uint16 参数，当前为 %2 个").arg(expectedCount).arg(tokens.size());
        }
        return false;
    }

    for (const QString &token : tokens) {
        bool ok = false;
        const int value = token.toInt(&ok, 0);
        if (!ok || value < 0 || value > 0xFFFF) {
            if (error) {
                *error = QStringLiteral("非法 uint16 参数: %1").arg(token);
            }
            return false;
        }
        out += encodeUInt16(static_cast<quint16>(value));
    }

    if (payload) {
        *payload = out;
    }
    return true;
}

QString PayloadCodec::formatUInt32Hex(quint32 value)
{
    return QStringLiteral("0x%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
}
