#include "canidcodec.h"

namespace
{
bool isValidStandardId(quint32 frameId)
{
    return frameId <= 0x7FF;
}
}

quint16 CanIdCodec::makeRequestId(quint8 nodeId, quint8 commandId)
{
    return makeId(false, nodeId, commandId);
}

DecodedCanId CanIdCodec::decode(quint32 frameId)
{
    DecodedCanId result;
    result.valid = ::isValidStandardId(frameId);
    if (!result.valid) {
        return result;
    }

    result.isReply = (frameId & DirectionMask) != 0;
    result.nodeId = static_cast<quint8>((frameId & NodeMask) >> 5);
    result.commandId = static_cast<quint8>(frameId & CommandMask);
    return result;
}

quint16 CanIdCodec::makeId(bool isReply, quint8 nodeId, quint8 commandId)
{
    const quint16 dirPart = isReply ? DirectionMask : 0;
    const quint16 nodePart = static_cast<quint16>((nodeId & 0x1F) << 5);
    const quint16 cmdPart = static_cast<quint16>(commandId & CommandMask);
    return static_cast<quint16>(dirPart | nodePart | cmdPart);
}
