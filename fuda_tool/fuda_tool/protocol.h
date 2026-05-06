#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QString>
#include <QVector>

enum class PayloadKind
{
    None,
    Float32,
    UInt8,
    HexBytes,
    ConfigRead,
    ConfigWrite,
    OptionalUInt16x8
};

enum class ConfigValueType
{
    Int32,
    Float32
};

struct CommandDef
{
    quint8 id = 0;
    QString enumName;
    QString displayName;
    QString dlcText;
    PayloadKind payloadKind = PayloadKind::None;
    QString parameterHint;
    QString description;
    bool txAllowed = true;
    bool expectsReply = false;
    bool reportOnly = false;
};

struct ConfigDef
{
    quint32 index = 0;
    ConfigValueType type = ConfigValueType::Int32;
    QString name;
    QString description;
};

namespace Protocol
{
const QVector<CommandDef> &commandDefinitions();
const QVector<ConfigDef> &configDefinitions();

const CommandDef *commandById(quint8 id);
const ConfigDef *configByIndex(quint32 index);

QString commandName(quint8 id);
QString configTypeName(ConfigValueType type);
QString payloadKindHint(PayloadKind kind);
}

#endif // PROTOCOL_H
