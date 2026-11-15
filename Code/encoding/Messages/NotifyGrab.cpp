#include <Messages/NotifyGrab.h>

void NotifyGrab::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    aWriter.WriteBits(Event, 8);
    aWriter.WriteBits(Hand, 8);
    aWriter.WriteBits(Kind, 8);
    Serialization::WriteVarInt(aWriter, ServerId);
    ObjectId.Serialize(aWriter);
    Serialization::WriteString(aWriter, NodeName);
    Serialization::WriteFloat(aWriter, GrabX);
    Serialization::WriteFloat(aWriter, GrabY);
    Serialization::WriteFloat(aWriter, GrabZ);
    for (const float cValue : GrabRotation)
        Serialization::WriteFloat(aWriter, cValue);
    Serialization::WriteVarInt(aWriter, Fingers);
}

void NotifyGrab::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 8);
    Event = tmp & 0xFF;
    aReader.ReadBits(tmp, 8);
    Hand = tmp & 0xFF;
    aReader.ReadBits(tmp, 8);
    Kind = tmp & 0xFF;
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ObjectId.Deserialize(aReader);
    NodeName = Serialization::ReadString(aReader);
    GrabX = Serialization::ReadFloat(aReader);
    GrabY = Serialization::ReadFloat(aReader);
    GrabZ = Serialization::ReadFloat(aReader);
    for (float& value : GrabRotation)
        value = Serialization::ReadFloat(aReader);
    Fingers = Serialization::ReadVarInt(aReader);
}
