#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

//! Another player's hand is on something this game may be moving: where that hand is now (see RequestPull).
//! Only the game that moves the thing acts on it.
struct NotifyPull final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPull;

    NotifyPull()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPull& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Kind == acRhs.Kind && ServerId == acRhs.ServerId && ObjectId == acRhs.ObjectId &&
               Hand == acRhs.Hand && Position == acRhs.Position && Active == acRhs.Active;
    }

    //! Whose hand it is.
    uint32_t PlayerId{};
    //! As in RequestPull.
    uint8_t Kind{};
    uint32_t ServerId{};
    GameId ObjectId{};
    uint8_t Hand{};
    Vector3_NetQuantize Position{};
    bool Active{};
};
