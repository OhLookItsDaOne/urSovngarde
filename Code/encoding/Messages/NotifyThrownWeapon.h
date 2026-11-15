#pragma once

#include "Message.h"

#include <Structs/GameId.h>

#include <algorithm>
#include <iterator>

//! Where another player's thrown weapon is, passed on by the server (see RequestThrownWeapon).
struct NotifyThrownWeapon final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyThrownWeapon;

    enum : uint8_t
    {
        kThere, //!< where it is now, in flight or lying
        kGone   //!< caught again, picked up or gone: the copy is removed
    };

    NotifyThrownWeapon()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyThrownWeapon& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && State == acRhs.State && Hand == acRhs.Hand && BaseId == acRhs.BaseId && X == acRhs.X && Y == acRhs.Y &&
               Z == acRhs.Z && std::equal(std::begin(Rotation), std::end(Rotation), std::begin(acRhs.Rotation));
    }

    //! Who threw it.
    uint32_t PlayerId{};
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
