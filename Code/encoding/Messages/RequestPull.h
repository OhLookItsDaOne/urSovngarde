#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

//! This player's hand is on something another player's game is moving (a body they carry, an object they hold): where that hand is now.
//! The game that moves the thing pulls it toward the hand, so two players can carry one body or pull it apart.
struct RequestPull final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestPull;

    enum : uint8_t
    {
        kActor,       //!< ServerId is a character's server id
        kWorldObject, //!< ObjectId is a placed reference
        kDroppedItem  //!< ServerId is a dropped item's server id
    };

    RequestPull()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestPull& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Kind == acRhs.Kind && ServerId == acRhs.ServerId && ObjectId == acRhs.ObjectId && Hand == acRhs.Hand &&
               Position == acRhs.Position && Active == acRhs.Active;
    }

    uint8_t Kind{};
    uint32_t ServerId{};
    GameId ObjectId{};
    //! Which hand, 0 left or 1 right: each hand's pull is its own.
    uint8_t Hand{};
    //! Where the hand is, in the world.
    Vector3_NetQuantize Position{};
    //! False once: the hand let go.
    bool Active{};
};
