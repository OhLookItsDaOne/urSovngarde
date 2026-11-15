#pragma once

#include <Events/EventDispatcher.h>

#include <Structs/Inventory.h>

struct World;
struct TransportService;
struct ConnectedEvent;
struct DisconnectedEvent;
struct CellChangeEvent;
struct ItemDroppedEvent;
struct ItemPickedUpEvent;
struct NotifyDroppedItem;
struct NotifyDroppedItemRemoved;
struct NotifyDroppedItemMove;
struct NotifyWorldObjectMove;
struct NotifyPull;
struct NotifyGrab;
struct UpdateEvent;
struct TESObjectREFR;

/**
 * @brief Keeps items players drop lying in every player's world, including items dropped in earlier sessions.
 *
 * Reports local drops and pick-ups to the server, and adopts the server's items if already here or places them.
 * Passes on the movement of dropped items and of placed world objects (a bottle, a cart).
 * Also announces older drops, read from the game's ExtraDroppedItemList on the player.
 */
class DroppedItemService
{
public:
    DroppedItemService(World&, entt::dispatcher&, TransportService&);

private:
    void OnConnected(const ConnectedEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnCellChange(const CellChangeEvent&) noexcept;
    void OnItemDropped(const ItemDroppedEvent&) noexcept;
    void OnItemPickedUp(const ItemPickedUpEvent&) noexcept;
    void OnNotifyDroppedItem(const NotifyDroppedItem&) noexcept;
    void OnNotifyDroppedItemRemoved(const NotifyDroppedItemRemoved&) noexcept;
    void OnNotifyDroppedItemMove(const NotifyDroppedItemMove&) noexcept;
    void OnNotifyWorldObjectMove(const NotifyWorldObjectMove&) noexcept;
    void UpdateWorldObjects(std::chrono::steady_clock::time_point aNow) noexcept;
    //! A hand of this player's on something another player's game is moving: its position is sent there, which pulls the thing toward it.
    void RunPulls(std::chrono::steady_clock::time_point aNow) noexcept;
    //! A dead body another player has in a hand, where this game simulates the body (it owns it, or this player holds it too):
    //! its part nearest that player's copy's hand is pulled toward the hand, so both hold it, each in their own game.
    void RunSharedBodies() noexcept;
    //! Sends what HIGGS said about this player's hands (grabbed, let go, pulled) to the server, which logs it.
    void RunGrabReports() noexcept;
    //! Another player's hand on something this game is moving.
    void OnNotifyPull(const NotifyPull&) noexcept;
    //! What HIGGS said about another player's hand, passed on by the server.
    void OnNotifyGrab(const NotifyGrab&) noexcept;
    struct Motion;
    //! The other game put down what a hand here pulls on: this game takes it over, and the hand pulls it here.
    //! True if it did; the caller then leaves the thing to this game's physics.
    bool TakeOverPulled(uint8_t aKind, uint32_t aServerId, uint32_t aFormId, TESObjectREFR* apRef, Motion& aMotion) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;

    //! Send a reference that lies on the floor to the server. False when its place cannot be named to the server.
    bool SendAdd(TESObjectREFR* apRef, const Inventory::Entry& acItem, bool aAnnouncement) noexcept;
    void AnnounceOldDrops() noexcept;

    void MapServerIdToRef(uint32_t aId, uint32_t aRefFormId) noexcept;

    World& m_world;
    TransportService& m_transport;

    //! Server id to the reference that stands for it here, and back.
    std::unordered_map<uint32_t, uint32_t> m_refById;
    std::unordered_map<uint32_t, uint32_t> m_idByRef;

    //! This player's own drops and announcements, waiting for the id the server gives them.
    struct Pending
    {
        uint32_t RefFormId{};
        GameId BaseId{};
        bool Announcement{};
    };
    std::vector<Pending> m_pending;

    //! How each remembered item has been moving here, by server id.
    //! Only one side sends an item's position at a time, so an item moved by the other side is held (keyframed) here.
    struct Motion
    {
        glm::vec3 LastPosition{};
        glm::vec3 LastRotation{};
        //! A world object's 3D rotation where LastPosition was taken.
        //! Its Euler angles can wrap around or lag the 3D, so a restart is judged on this.
        glm::mat3 LastTurn{1.f};
        std::chrono::steady_clock::time_point LastLocalMove{};
        std::chrono::steady_clock::time_point LastRemote{};
        std::chrono::steady_clock::time_point QuietUntil{};
        //! The item's base form when it was mapped.
        //! The game reuses a deleted item's FF id for another object, which must not be moved: it could be an actor.
        uint32_t BaseFormId{};
        bool Moving{};
        bool Held{};
        //! Measurement: the other side's moves applied while held, and how many of them rebuilt the object's 3D.
        uint32_t HeldMoves{};
        uint32_t HeldRebuilds{};
        //! Where the other side last put it, to compare with where its 3D is drawn when it is let go.
        glm::vec3 RemoteTarget{};
        //! The other side's last update said it was put down.
        //! Still held for a moment, since a still hand sends the same.
        bool RemoteAtRest{};
        //! Its 3D was there on the last pass. A cell loading settles every item in it, which is not a move.
        bool Loaded{};
        //! When the "Shared:" line was last written for it.
        std::chrono::steady_clock::time_point SharedLogAt{};
        //! This player just took it out of another game's motion (RunPulls): until then that game's last moves of it are not followed.
        std::chrono::steady_clock::time_point TakenUntil{};
        //! Since when another player's hand pulls it here while no hand of this player's is on it; empty while one is.
        std::chrono::steady_clock::time_point PulledUnheldSince{};
        //! A 3D load in a fresh drop's first two seconds is part of the throw, not a cell reload.
        std::chrono::steady_clock::time_point FreshDropUntil{};
        //! The other side's last place and rotation, on the way to which this game moves it frame by frame (FollowMove).
        glm::vec3 FollowPosition{};
        glm::vec3 FollowRotation{};
        std::chrono::steady_clock::time_point FollowArrivedAt{};
        std::chrono::steady_clock::time_point FollowDue{};
        std::chrono::steady_clock::time_point FollowSteppedAt{};
        bool Following{};
    };
    //! A place another game sent for a thing it moves: gone to over the time until its next place is due, not in one jump.
    void FollowMove(TESObjectREFR* apRef, Motion& aMotion, const glm::vec3& acPosition, const glm::vec3& acRotation, std::chrono::steady_clock::time_point aNow) noexcept;
    //! One frame's part of that way.
    void StepFollow(TESObjectREFR* apRef, Motion& aMotion, std::chrono::steady_clock::time_point aNow) noexcept;
    std::unordered_map<uint32_t, Motion> m_motion;

    //! Objects placed in the world near the player, by reference id, watched for moves here or held for another player.
    //! The list is rebuilt once a second from the player's cell.
    std::unordered_map<uint32_t, Motion> m_objectMotion;
    std::vector<uint32_t> m_objectCandidates;
    std::chrono::steady_clock::time_point m_candidatesAt{};

    //! What each hand (0 left, 1 right) pulls on, if anything. Kind is RequestPull's.
    struct HandPull
    {
        bool Active{};
        bool WasPressed{};
        //! The game that moved it let go and this game runs it now: this hand pulls it here, no longer over there.
        bool Local{};
        uint8_t Kind{};
        uint32_t ServerId{};
        uint32_t FormId{};
        GameId ObjectId{};
        std::chrono::steady_clock::time_point Since{};
    };
    HandPull m_handPulls[2];

    //! What another player's hand holds, as that player's HIGGS said it. Key: player id * 2 + hand (0 left, 1 right).
    struct RemoteGrab
    {
        //! RequestGrabReport's kind and ids, and the reference it is in this game (0 if it is not loaded here).
        uint8_t Kind{};
        uint32_t ServerId{};
        GameId ObjectId{};
        uint32_t FormId{};
        std::string NodeName{};
        //! The grabbed node in the hand's space: where it is and how it is turned (the game's matrix, three rows of three).
        float Position[3]{};
        float Rotation[9]{};
        float Fingers[5]{};
        std::chrono::steady_clock::time_point Since{};
        //! When the "SharedBody:" line was last written for it.
        std::chrono::steady_clock::time_point SharedLogAt{};
    };
    std::unordered_map<uint64_t, RemoteGrab> m_remoteGrabs;
    //! HIGGS took a thing another game was moving: see the definition.
    void TakeFromOtherGame(uint32_t aFormId, bool aIsLeft, bool aPulled) noexcept;
    //! Whether another player's hand holds this thing, by what their HIGGS said (kind and ids as in RequestGrabReport).
    bool IsInAnotherHand(uint8_t aKind, uint32_t aServerId, const GameId& acObjectId) const noexcept;
    //! Whether this player has the thing in a hand: HIGGS holds it, or it was just taken and HIGGS is about to.
    bool IsInOwnHand(uint32_t aFormId, const Motion& acMotion, std::chrono::steady_clock::time_point aNow) const noexcept;
    //! Another game's move of a thing this player has in a hand. True if it was dealt with here and must not be followed.
    //! Held by both: each game simulates it in its own player's hand, and pulls it toward where the other game has it, so the other's grip is felt.
    bool SharedMove(uint8_t aKind, uint32_t aServerId, const GameId& acObjectId, TESObjectREFR* apRef, Motion& aMotion, const glm::vec3& acPosition,
                    std::chrono::steady_clock::time_point aNow) noexcept;

    //! Old drops already announced this session, so a cell change does not send them again.
    std::unordered_set<uint32_t> m_announced;
    //! What the player dropped while not connected (reference and item), announced once connected.
    //! The game's own dropped-item list is not found on the player, so the client keeps this one.
    std::vector<std::pair<uint32_t, Inventory::Entry>> m_droppedOffline;
    //! Whether this connection has logged what it found on the player.
    bool m_reportedOldDrops{false};

    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_cellChangeConnection;
    entt::scoped_connection m_droppedConnection;
    entt::scoped_connection m_pickedUpConnection;
    entt::scoped_connection m_notifyConnection;
    entt::scoped_connection m_removedConnection;
    entt::scoped_connection m_moveConnection;
    entt::scoped_connection m_objectMoveConnection;
    entt::scoped_connection m_pullConnection;
    entt::scoped_connection m_grabConnection;
    entt::scoped_connection m_updateConnection;
};
