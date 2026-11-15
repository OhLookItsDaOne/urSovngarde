#pragma once

#include "Message.h"

#include <Structs/GameId.h>

//! What HIGGS told this player's game about one of its hands: it grabbed a thing, let it go, or pulled it toward itself.
//! Read from HIGGS's mod interface, so it is what the hand really holds and not a guess from buttons and distances.
//! TEMPORARY in this form: the server only logs it, so that both ends of the report can be compared.
struct RequestGrabReport final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestGrabReport;

    enum : uint8_t
    {
        kGrabbed,
        kLetGo,
        kPulled
    };

    enum : uint8_t
    {
        kActor,       //!< ServerId is a character's server id
        kWorldObject, //!< ObjectId is a placed reference
        kDroppedItem, //!< ServerId is a dropped item's server id
        kUnknown      //!< nothing the server knows: a reference only this game has
    };

    RequestGrabReport()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestGrabReport& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Event == acRhs.Event && Hand == acRhs.Hand && Kind == acRhs.Kind && ServerId == acRhs.ServerId && ObjectId == acRhs.ObjectId &&
               FormId == acRhs.FormId && NodeName == acRhs.NodeName && GrabX == acRhs.GrabX && GrabY == acRhs.GrabY && GrabZ == acRhs.GrabZ && Fingers == acRhs.Fingers;
    }

    uint8_t Event{};
    //! Which hand, 0 left or 1 right.
    uint8_t Hand{};
    uint8_t Kind{};
    uint32_t ServerId{};
    GameId ObjectId{};
    //! The reference's id in the reporting game, for the log only.
    uint32_t FormId{};
    //! The node of the thing the hand holds; on a body, the limb. Empty unless grabbed.
    String NodeName{};
    //! Where that node sits in the hand's own space. Zero unless grabbed.
    float GrabX{};
    float GrabY{};
    float GrabZ{};
    //! How far each finger is curled, thumb first, in the low five bytes: 0 fully curled to 255 fully open. Zero unless grabbed.
    uint64_t Fingers{};
};
