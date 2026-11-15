#pragma once

#include "Message.h"

#include <Structs/GameId.h>

#include <algorithm>
#include <iterator>

//! A weapon this player threw with Weapon Throw VR (Shizof): where it is, every frame while it moves.
//! That mod fires the weapon as a projectile from a hidden helper, which no other game sees; the other games show a copy
//! of the weapon that only looks, at the places sent here. The server passes it to the players around as NotifyThrownWeapon.
struct RequestThrownWeapon final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestThrownWeapon;

    enum : uint8_t
    {
        kThere, //!< where it is now, in flight or lying
        kGone   //!< caught again, picked up or gone: the copy is removed
    };

    RequestThrownWeapon()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestThrownWeapon& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && State == acRhs.State && Hand == acRhs.Hand && BaseId == acRhs.BaseId && X == acRhs.X && Y == acRhs.Y &&
               Z == acRhs.Z && std::equal(std::begin(Rotation), std::end(Rotation), std::begin(acRhs.Rotation));
    }

    uint8_t State{};
    //! 0 left, 1 right: a player has at most one thrown thing per hand.
    uint8_t Hand{};
    //! The base form of what was thrown (a weapon, a shield, a torch).
    GameId BaseId{};
    //! Where its 3D is, and how it is turned: the rows of its world rotation.
    float X{};
    float Y{};
    float Z{};
    float Rotation[9]{};
};
