#include <Messages/RequestGrabReport.h>

void RequestGrabReport::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Event, 8);
    aWriter.WriteBits(Hand, 8);
    aWriter.WriteBits(Kind, 8);
    Serialization::WriteVarInt(aWriter, ServerId);
    ObjectId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, FormId);
    Serialization::WriteString(aWriter, NodeName);
    Serialization::WriteFloat(aWriter, GrabX);
    Serialization::WriteFloat(aWriter, GrabY);
    Serialization::WriteFloat(aWriter, GrabZ);
    Serialization::WriteVarInt(aWriter, Fingers);
}

void RequestGrabReport::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 8);
    Event = tmp & 0xFF;
    aReader.ReadBits(tmp, 8);
    Hand = tmp & 0xFF;
    aReader.ReadBits(tmp, 8);
    Kind = tmp & 0xFF;
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ObjectId.Deserialize(aReader);
    FormId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    NodeName = Serialization::ReadString(aReader);
    GrabX = Serialization::ReadFloat(aReader);
    GrabY = Serialization::ReadFloat(aReader);
    GrabZ = Serialization::ReadFloat(aReader);
    Fingers = Serialization::ReadVarInt(aReader);
}
