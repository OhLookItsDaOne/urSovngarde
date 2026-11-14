#include <Messages/RequestPull.h>

void RequestPull::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Kind, 8);
    Serialization::WriteVarInt(aWriter, ServerId);
    ObjectId.Serialize(aWriter);
    aWriter.WriteBits(Hand, 8);
    Position.Serialize(aWriter);
    Serialization::WriteBool(aWriter, Active);
}

void RequestPull::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

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
