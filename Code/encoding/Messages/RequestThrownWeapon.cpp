#include <Messages/RequestThrownWeapon.h>

void RequestThrownWeapon::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(State, 8);
    aWriter.WriteBits(Hand, 8);
    BaseId.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, X);
    Serialization::WriteFloat(aWriter, Y);
    Serialization::WriteFloat(aWriter, Z);
    for (const float cValue : Rotation)
        Serialization::WriteFloat(aWriter, cValue);
}

void RequestThrownWeapon::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 8);
    State = tmp & 0xFF;
    aReader.ReadBits(tmp, 8);
    Hand = tmp & 0xFF;
    BaseId.Deserialize(aReader);
    X = Serialization::ReadFloat(aReader);
    Y = Serialization::ReadFloat(aReader);
    Z = Serialization::ReadFloat(aReader);
    for (float& value : Rotation)
        value = Serialization::ReadFloat(aReader);
}
