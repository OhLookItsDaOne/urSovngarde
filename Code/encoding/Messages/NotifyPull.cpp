#include <Messages/NotifyPull.h>

void NotifyPull::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    aWriter.WriteBits(Kind, 8);
    Serialization::WriteVarInt(aWriter, ServerId);
    ObjectId.Serialize(aWriter);
    aWriter.WriteBits(Hand, 8);
    Position.Serialize(aWriter);
    Serialization::WriteBool(aWriter, Active);
}

void NotifyPull::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 8);
    Kind = tmp & 0xFF;
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ObjectId.Deserialize(aReader);
    aReader.ReadBits(tmp, 8);
    Hand = tmp & 0xFF;
    Position.Deserialize(aReader);
    Active = Serialization::ReadBool(aReader);
}
