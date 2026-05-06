#ifndef CANIDCODEC_H
#define CANIDCODEC_H

#include <QtGlobal>

struct DecodedCanId
{
    bool valid = false;
    bool isReply = false;
    quint8 nodeId = 0;
    quint8 commandId = 0;
};

class CanIdCodec
{
public:
    static constexpr quint16 DirectionMask = 0x400;
    static constexpr quint16 NodeMask = 0x3E0;
    static constexpr quint16 CommandMask = 0x01F;
    static constexpr quint8 BroadcastNodeId = 31;

    static quint16 makeRequestId(quint8 nodeId, quint8 commandId);
    static quint16 makeReplyId(quint8 nodeId, quint8 commandId);
    static DecodedCanId decode(quint32 frameId);
    static bool isValidStandardId(quint32 frameId);

private:
    static quint16 makeId(bool isReply, quint8 nodeId, quint8 commandId);
};

#endif // CANIDCODEC_H
