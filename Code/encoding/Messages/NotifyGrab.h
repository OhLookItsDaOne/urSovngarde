#pragma once

#include "Message.h"

#include <Structs/GameId.h>

#include <algorithm>
#include <iterator>

//! What HIGGS told another player's game about one of that player's hands, passed on by the server (see RequestGrabReport).
//! With it every game knows which hand of which player holds which thing, at which node, and how the node sits in the hand.
struct NotifyGrab final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyGrab;

    NotifyGrab()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyGrab& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Event == acRhs.Event && Hand == acRhs.Hand && Kind == acRhs.Kind && ServerId == acRhs.ServerId &&
               ObjectId == acRhs.ObjectId && NodeName == acRhs.NodeName && GrabX == acRhs.GrabX && GrabY == acRhs.GrabY && GrabZ == acRhs.GrabZ && Fingers == acRhs.Fingers &&
               std::equal(std::begin(GrabRotation), std::end(GrabRotation), std::begin(acRhs.GrabRotation));
    }

    //! Whose hand it is.
    uint32_t PlayerId{};
    //! All as in RequestGrabReport.
    uint8_t Event{};
    uint8_t Hand{};
    uint8_t Kind{};
    uint32_t ServerId{};
    GameId ObjectId{};
    String NodeName{};
    float GrabX{};
    float GrabY{};
    float GrabZ{};
    float GrabRotation[9]{};
    uint64_t Fingers{};
};
