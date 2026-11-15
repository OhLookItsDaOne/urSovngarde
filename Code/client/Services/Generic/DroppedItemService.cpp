#include <Services/DroppedItemService.h>

#include <World.h>
#include <Components.h>
#include <Actor.h>
#include <Services/TransportService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/DroppedItemEvents.h>

#include <Messages/RequestDroppedItemAdd.h>
#include <Messages/RequestDroppedItemRemove.h>
#include <Messages/NotifyDroppedItem.h>
#include <Messages/NotifyDroppedItemRemoved.h>
#include <Messages/RequestDroppedItemMove.h>
#include <Messages/NotifyDroppedItemMove.h>
#include <Messages/RequestWorldObjectMove.h>
#include <Messages/NotifyWorldObjectMove.h>
#include <Messages/RequestPull.h>
#include <Messages/RequestGrabReport.h>
#include <Games/Skyrim/VRModApis.h>
#include <Messages/NotifyPull.h>
#include <Messages/NotifyGrab.h>
#include <Games/Skyrim/VRBodySync.h>
#include <Events/UpdateEvent.h>
#include <NetImmerse/NiTransform.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <PlayerCharacter.h>
#include <ExtraData/ExtraCount.h>

#include <Games/Overrides.h>

namespace
{
//! How close a floor item must be to the server's position to count as the same item.
//! The item may have rolled or been reloaded since the drop.
constexpr float kSameItemRadius = 200.f;

//! Outdoors, how far from the player an item can be and still be in a loaded cell.
//! The loaded grid is 5 by 5 cells of 4096 units, two cells either side of the player's.
constexpr float kLoadedRadius = 8192.f;

const Vector<FormType> kItemTypes{FormType::Weapon, FormType::Armor, FormType::Ammo, FormType::Book, FormType::Ingredient, FormType::Alchemy,
                                  FormType::Misc, FormType::Key, FormType::SoulGem, FormType::Scroll, FormType::Light};

bool IsItem(const TESObjectREFR* apRef) noexcept
{
    if (!apRef || !apRef->baseForm)
        return false;
    for (const FormType type : kItemTypes)
        if (apRef->baseForm->formType == type)
            return true;
    return false;
}

float DistanceTo(const TESObjectREFR* apRef, const glm::vec3& acPosition) noexcept
{
    return glm::distance(glm::vec3(apRef->position.x, apRef->position.y, apRef->position.z), acPosition);
}

//! Whether the reference behind an id is still the item that was mapped.
//! A mapping made before the base was known (0) is trusted; anything else must match.
template <class T> bool IsStillOurs(const TESObjectREFR* apRef, const T& acMotion) noexcept
{
    return acMotion.BaseFormId == 0 || (apRef->baseForm && apRef->baseForm->formID == acMotion.BaseFormId);
}

//! Movement worth sending: a couple of units or a few degrees of turn; less is physics jitter.
constexpr float kMovedDistance = 2.f;
constexpr float kMovedAngle = 0.05f;

//! A resting world object starts moving again only on a clear move.
//! Physics settling and a still hand stay under this, and each restart holds the object in the other games again.
constexpr float kRestartDistance = 8.f;
constexpr float kRestartAngle = 0.3f;
// A move from the other side further than this is a jump (a load, a teleport), made with MoveTo; nearer ones move the 3D in place.
constexpr float kMoveInPlaceDistance = 1000.f;

// A thing another game moves must not fall or roll here between that game's moves.
// Without HIGGS its body is made keyframed. With HIGGS it is pinned instead (VRBodySync::PinBody, after each placing and every frame):
// HIGGS takes only bodies physics may move, so a keyframed thing could not be selected, grabbed or pulled until the other game let go.
void HoldStill(TESObjectREFR* apRef) noexcept
{
    if (!VRModApis::KnowsGrabs())
        apRef->SetMotionType(TESObjectREFR::kMotionKeyframed);
}

// Puts an object the other side moves where it says, without building its 3D again when it only moved a little.
void PlaceMovedObject(TESObjectREFR* apRef, const glm::vec3& acPosition, const glm::vec3& acRotation) noexcept
{
    apRef->SetRotation(acRotation.x, acRotation.y, acRotation.z);
    const NiPoint3 cTarget(acPosition);
    const glm::vec3 cNow(apRef->position.x, apRef->position.y, apRef->position.z);
    if (apRef->GetNiNode() && glm::distance(cNow, acPosition) < kMoveInPlaceDistance)
    {
        // That moves only the reference: its 3D and collision are moved to it too, warped there.
        apRef->SetPositionInPlace(cTarget);
        apRef->Update3DPosition(true);
    }
    else if (TESObjectCELL* pCell = apRef->GetParentCellEx())
        apRef->MoveTo(pCell, cTarget);
}

//! Where an item really is: its node's world transform follows physics, while the reference's position may lag.
//! NiAVObject::world is at 0x7C, as VRBodySync reads it.
glm::vec3 WorldPositionOf(TESObjectREFR* apRef) noexcept
{
    if (NiNode* pNode = apRef->GetNiNode())
    {
        const auto& world = *reinterpret_cast<const NiTransform*>(reinterpret_cast<const uint8_t*>(pNode) + 0x7C);
        return {world.translate.x, world.translate.y, world.translate.z};
    }
    return {apRef->position.x, apRef->position.y, apRef->position.z};
}

glm::vec3 RotationOf(const TESObjectREFR* apRef) noexcept
{
    return {apRef->rotation.x, apRef->rotation.y, apRef->rotation.z};
}

//! The rotation of a 3D's world transform, which follows physics like WorldPositionOf.
glm::mat3 WorldTurnOf(const NiNode* apNode) noexcept
{
    const auto& world = *reinterpret_cast<const NiTransform*>(reinterpret_cast<const uint8_t*>(apNode) + 0x7C);
    glm::mat3 turn(1.f);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            turn[row][column] = world.rotate.data[row][column];
    return turn;
}

//! The angle in radians of the turn from one rotation to another.
float AngleBetween(const glm::mat3& acFrom, const glm::mat3& acTo) noexcept
{
    // The trace of the relative rotation is 1 + 2 cos(angle).
    float trace = 0.f;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            trace += acFrom[row][column] * acTo[row][column];
    return std::acos(std::clamp((trace - 1.f) * 0.5f, -1.f, 1.f));
}

bool IsMemoryReadable(const void* apAddress, const size_t aBytes) noexcept
{
    MEMORY_BASIC_INFORMATION info{};
    if (!apAddress || !VirtualQuery(apAddress, &info, sizeof(info)))
        return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return reinterpret_cast<uintptr_t>(apAddress) + aBytes <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
}

// The game's ExtraDroppedItemList, as CommonLibSSE has it: the BSExtraData header, then a BSSimpleList of handles.
// Only read after RTTI confirms the type, and every node is checked readable, so a wrong layout reads nothing.
struct DroppedItemNode
{
    uint32_t Handle;
    uint32_t Pad;
    DroppedItemNode* Next;
};
struct DroppedItemListView
{
    void* VTable;
    void* NextExtra;
    DroppedItemNode Head;
};

// The extra data of the player's stack the placed item is dropped out of again: the list handed to the game, while the inventory still holds it.
// Otherwise an equal list the item went into, or none; a list the game no longer holds is never handed back to it.
ExtraDataList* StackListOf(TESObjectREFR* apHolder, const TESForm* apObject, ExtraDataList* apGiven, const Inventory::Entry& acEntry) noexcept
{
    const ExtraContainerChanges::Data* pChanges = apHolder->GetContainerChanges();
    if (!apGiven || !pChanges || !pChanges->entries)
        return nullptr;

    ExtraDataList* pEqual = nullptr;
    for (ExtraContainerChanges::Entry* pEntry : *pChanges->entries)
    {
        if (!pEntry || pEntry->form != apObject || !pEntry->dataList)
            continue;
        for (ExtraDataList* pList : *pEntry->dataList)
        {
            if (!pList)
                continue;
            if (pList == apGiven)
                return apGiven;
            if (!pEqual)
            {
                Inventory::Entry held{};
                held.BaseId = acEntry.BaseId;
                held.Count = 1;
                TESObjectREFR::GetItemFromExtraData(held, pList);
                if (held.IsExtraDataEquals(acEntry))
                    pEqual = pList;
            }
        }
    }
    return pEqual;
}
} // namespace

DroppedItemService::DroppedItemService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&DroppedItemService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&DroppedItemService::OnDisconnected>(this);
    m_cellChangeConnection = aDispatcher.sink<CellChangeEvent>().connect<&DroppedItemService::OnCellChange>(this);
    m_droppedConnection = aDispatcher.sink<ItemDroppedEvent>().connect<&DroppedItemService::OnItemDropped>(this);
    m_pickedUpConnection = aDispatcher.sink<ItemPickedUpEvent>().connect<&DroppedItemService::OnItemPickedUp>(this);
    m_notifyConnection = aDispatcher.sink<NotifyDroppedItem>().connect<&DroppedItemService::OnNotifyDroppedItem>(this);
    m_removedConnection = aDispatcher.sink<NotifyDroppedItemRemoved>().connect<&DroppedItemService::OnNotifyDroppedItemRemoved>(this);
    m_moveConnection = aDispatcher.sink<NotifyDroppedItemMove>().connect<&DroppedItemService::OnNotifyDroppedItemMove>(this);
    m_objectMoveConnection = aDispatcher.sink<NotifyWorldObjectMove>().connect<&DroppedItemService::OnNotifyWorldObjectMove>(this);
    m_pullConnection = aDispatcher.sink<NotifyPull>().connect<&DroppedItemService::OnNotifyPull>(this);
    m_grabConnection = aDispatcher.sink<NotifyGrab>().connect<&DroppedItemService::OnNotifyGrab>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&DroppedItemService::OnUpdate>(this);
}

void DroppedItemService::OnConnected(const ConnectedEvent&) noexcept
{
    AnnounceOldDrops();
}

void DroppedItemService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    // Ids belong to one server session's view; a reconnect gets them again with the next cell entry.
    m_refById.clear();
    m_idByRef.clear();
    m_remoteGrabs.clear();
    VRBodySync::ClearBodiesInOtherHands();
    m_pending.clear();
    m_announced.clear();
    m_reportedOldDrops = false;
    m_motion.clear();

    // Whatever the other player was holding is let go here.
    for (const auto& [formId, motion] : m_objectMotion)
    {
        if (!motion.Held)
            continue;
        if (TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(formId)); pRef && !pRef->IsDeleted())
            pRef->SetMotionType(TESObjectREFR::kMotionDynamic);
    }
    m_objectMotion.clear();
    m_objectCandidates.clear();
}

void DroppedItemService::OnCellChange(const CellChangeEvent&) noexcept
{
    if (m_transport.IsConnected())
        AnnounceOldDrops();
}

void DroppedItemService::MapServerIdToRef(const uint32_t aId, const uint32_t aRefFormId) noexcept
{
    m_refById[aId] = aRefFormId;
    m_idByRef[aRefFormId] = aId;

    // A placed or adopted item needs time to settle before its movement is sent.
    // A new drop's owner clears this delay once the server returns its id, so its initial flight is sent too.
    Motion& motion = m_motion[aId];
    motion = Motion{};
    if (TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(aRefFormId)))
    {
        motion.LastPosition = WorldPositionOf(pRef);
        motion.LastRotation = RotationOf(pRef);
        motion.BaseFormId = pRef->baseForm ? pRef->baseForm->formID : 0;
        motion.Loaded = pRef->GetNiNode() != nullptr;
    }
    motion.QuietUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
}

bool DroppedItemService::SendAdd(TESObjectREFR* apRef, const Inventory::Entry& acItem, const bool aAnnouncement) noexcept
{
    auto& modSystem = m_world.GetModSystem();

    RequestDroppedItemAdd request{};
    request.Item = acItem;
    request.Announcement = aAnnouncement;
    request.Position = glm::vec3(apRef->position.x, apRef->position.y, apRef->position.z);
    request.Rotation = RotationOf(apRef);

    TESObjectCELL* pCell = apRef->GetParentCellEx();
    if (!pCell || !modSystem.GetServerModId(pCell->formID, request.CellId))
    {
        spdlog::warn("DroppedItem: cannot name the cell item {:X} lies in, so it is not shared", apRef->formID);
        return false;
    }
    if (TESWorldSpace* pWorldSpace = apRef->GetWorldSpace())
        modSystem.GetServerModId(pWorldSpace->formID, request.WorldSpaceId);

    m_transport.Send(request);
    m_pending.push_back(Pending{apRef->formID, acItem.BaseId, aAnnouncement});
    return true;
}

void DroppedItemService::OnItemDropped(const ItemDroppedEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
    {
        // Kept for the next connection (see m_droppedOffline).
        if (acEvent.Item.BaseId != GameId{} && m_droppedOffline.size() < 256)
        {
            m_droppedOffline.emplace_back(acEvent.RefFormId, acEvent.Item);
            spdlog::info("DroppedItem: dropped {:X} while not connected; it is announced once connected", acEvent.RefFormId);
        }
        return;
    }

    TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(acEvent.RefFormId));
    if (!pRef || acEvent.Item.BaseId == GameId{})
        return;

    if (SendAdd(pRef, acEvent.Item, false))
        spdlog::info("DroppedItem: dropped {:X}:{:X} x{} as {:X}, shared with the server", acEvent.Item.BaseId.ModId, acEvent.Item.BaseId.BaseId,
                     std::abs(acEvent.Item.Count), pRef->formID);
}

void DroppedItemService::OnItemPickedUp(const ItemPickedUpEvent& acEvent) noexcept
{
    const auto it = m_idByRef.find(acEvent.RefFormId);
    if (it == m_idByRef.end())
    {
        // A placed book taken: sent as a world object taken, so every other game disables its own copy.
        // Activation sync leaves books out (TESObjectREFR.cpp, HookActivate).
        if (acEvent.PlacedBook && m_transport.IsConnected())
        {
            RequestWorldObjectMove request{};
            request.ObjectId = acEvent.ObjectId;
            request.CellId = acEvent.CellId;
            request.WorldSpaceId = acEvent.WorldSpaceId;
            request.Position = acEvent.Position;
            request.Taken = true;
            m_transport.Send(request);
            m_objectMotion.erase(acEvent.RefFormId);
            spdlog::info("WorldObject: book {:X} taken here; asking everyone to take it off the floor", acEvent.RefFormId);
        }
        return; // not an item the server was keeping
    }

    RequestDroppedItemRemove request{};
    request.Id = it->second;
    m_transport.Send(request);

    spdlog::info("DroppedItem: picked up {} ({:X}); asking everyone to take it off the floor", it->second, acEvent.RefFormId);
    m_motion.erase(it->second);
    m_refById.erase(it->second);
    m_idByRef.erase(it);
}

void DroppedItemService::OnNotifyDroppedItem(const NotifyDroppedItem& acMessage) noexcept
{
    if (const auto mapped = m_refById.find(acMessage.Id); mapped != m_refById.end())
    {
        // Already here: the server resends it on every cell change, with where it was last left.
        TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(mapped->second));
        Motion& motion = m_motion[acMessage.Id];
        const auto now = std::chrono::steady_clock::now();
        const glm::vec3 cLeftAt(acMessage.Position);
        if (pRef && IsStillOurs(pRef, motion) && !pRef->IsDeleted() && !motion.Held && !motion.Moving && now - motion.LastLocalMove > std::chrono::seconds(2) &&
            DistanceTo(pRef, cLeftAt) > 20.f)
        {
            spdlog::info("DroppedItem: {} ({:X}) was moved while we were away; putting it where it was left, {:.0f} units off", acMessage.Id, mapped->second,
                         DistanceTo(pRef, cLeftAt));
            pRef->SetRotation(acMessage.Rotation.x, acMessage.Rotation.y, acMessage.Rotation.z);
            if (TESObjectCELL* pCell = pRef->GetParentCellEx())
                pRef->MoveTo(pCell, NiPoint3(cLeftAt));
            motion.LastPosition = cLeftAt;
            motion.LastRotation = acMessage.Rotation;
            motion.QuietUntil = now + std::chrono::seconds(2);
        }
        return;
    }

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    auto& modSystem = m_world.GetModSystem();
    const glm::vec3 cPosition(acMessage.Position);

    // Only an item in a loaded cell can be adopted or placed; the server sends it again when the player gets there.
    if (!acMessage.WorldSpaceId)
    {
        TESObjectCELL* pCell = pPlayer->GetParentCellEx();
        GameId cellId{};
        if (!pCell || !modSystem.GetServerModId(pCell->formID, cellId) || cellId != acMessage.CellId)
            return;
    }
    else
    {
        TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace();
        GameId worldSpaceId{};
        if (!pWorldSpace || !modSystem.GetServerModId(pWorldSpace->formID, worldSpaceId) || worldSpaceId != acMessage.WorldSpaceId ||
            DistanceTo(pPlayer, cPosition) > kLoadedRadius)
            return;
    }

    const uint32_t cBaseFormId = modSystem.GetGameId(acMessage.Item.BaseId);

    // 1. This player's own drop or announcement, coming back with its id (first in, first answered).
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
    {
        if (it->BaseId != acMessage.Item.BaseId)
            continue;
        const uint32_t cRefFormId = it->RefFormId;
        const bool cFreshDrop = !it->Announcement;
        m_pending.erase(it);
        MapServerIdToRef(acMessage.Id, cRefFormId);
        if (cFreshDrop)
        {
            Motion& motion = m_motion[acMessage.Id];
            const auto now = std::chrono::steady_clock::now();
            motion.QuietUntil = now;
            motion.FreshDropUntil = now + std::chrono::seconds(2);
        }
        spdlog::info("DroppedItem: {} is our own {:X}", acMessage.Id, cRefFormId);
        return;
    }

    // 2. Already lying here: kept by this save from an earlier session, or placed before a reconnect.
    TESObjectREFR* pFound = nullptr;
    float nearest = kSameItemRadius;
    const auto scan = [&](const TESObjectCELL* apCell)
    {
        if (!apCell)
            return;
        for (TESObjectREFR* pRef : apCell->GetRefsByFormTypes(kItemTypes))
        {
            if (!pRef || !pRef->baseForm || pRef->baseForm->formID != cBaseFormId || pRef->IsDisabled() || pRef->IsDeleted() || m_idByRef.count(pRef->formID))
                continue;
            const float distance = DistanceTo(pRef, cPosition);
            if (distance <= nearest)
            {
                nearest = distance;
                pFound = pRef;
            }
        }
    };
    scan(Cast<TESObjectCELL>(TESForm::GetById(modSystem.GetGameId(acMessage.CellId))));
    scan(pPlayer->GetParentCellEx());

    if (pFound)
    {
        MapServerIdToRef(acMessage.Id, pFound->formID);
        spdlog::info("DroppedItem: {} was already lying here as {:X}, {:.0f} units from where it was dropped", acMessage.Id, pFound->formID, nearest);
        return;
    }

    // 3. Missing: give it to the player and drop it straight back out at the item's position.
    // Both run under the inventory override, so neither is sent as a change or as this player's drop.
    TESBoundObject* pObject = Cast<TESBoundObject>(TESForm::GetById(cBaseFormId));
    if (!pObject)
    {
        spdlog::warn("DroppedItem: {} is {:X}:{:X}, which this game does not have", acMessage.Id, acMessage.Item.BaseId.ModId, acMessage.Item.BaseId.BaseId);
        return;
    }

    Inventory::Entry entry = acMessage.Item;
    const int32_t cCount = std::max(1, std::abs(entry.Count));
    entry.Count = cCount;

    BSPointerHandle<TESObjectREFR> handle{};
    {
        ScopedInventoryOverride _;
        ExtraDataList* pExtra = TESObjectREFR::GetExtraDataFromItem(entry);
        NiPoint3 location(cPosition);
        NiPoint3 rotation(acMessage.Rotation);
        pPlayer->AddObjectToContainer(pObject, pExtra, cCount, nullptr);
        ExtraDataList* pStack = StackListOf(pPlayer, pObject, pExtra, entry);
        if (pExtra && pStack != pExtra)
            spdlog::warn("DroppedItem: {} ({:X}): the game did not keep the item's extra data as given; dropped from {}", acMessage.Id, pObject->formID,
                         pStack ? "an equal stack" : "a plain stack");
        handle = pPlayer->RemoveItem(pObject, cCount, ITEM_REMOVE_REASON::kDropping, pStack, nullptr, &location, &rotation);
    }

    if (TESObjectREFR* pPlaced = TESObjectREFR::GetByHandle(handle.handle.iBits))
    {
        MapServerIdToRef(acMessage.Id, pPlaced->formID);
        spdlog::info("DroppedItem: placed {} ({:X} x{}) as {:X}", acMessage.Id, pObject->formID, cCount, pPlaced->formID);
    }
    else
        spdlog::warn("DroppedItem: placing {} ({:X}) produced no reference", acMessage.Id, pObject->formID);
}

void DroppedItemService::OnNotifyDroppedItemRemoved(const NotifyDroppedItemRemoved& acMessage) noexcept
{
    const auto it = m_refById.find(acMessage.Id);
    if (it == m_refById.end())
        return; // not here, or picked up here

    const uint32_t cRefFormId = it->second;
    m_idByRef.erase(cRefFormId);
    m_refById.erase(it);
    m_motion.erase(acMessage.Id);

    // Disabled, then Papyrus Delete, so the game frees it once nothing holds it.
    TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(cRefFormId));
    if (pRef && !pRef->IsDeleted())
    {
        pRef->Disable();
        pRef->Delete();
        spdlog::info("DroppedItem: {} was picked up elsewhere; removed {:X} here", acMessage.Id, cRefFormId);
    }
}

void DroppedItemService::AnnounceOldDrops() noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    // First what this client saw the player drop while offline, if it still lies where it fell.
    if (!m_droppedOffline.empty() && m_transport.IsConnected())
    {
        uint32_t announced = 0;
        for (const auto& [refFormId, item] : m_droppedOffline)
        {
            TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(refFormId));
            if (!IsItem(pRef) || pRef->IsDisabled() || pRef->IsDeleted() || m_idByRef.count(refFormId) || m_announced.count(refFormId))
                continue;
            m_announced.insert(refFormId);
            if (SendAdd(pRef, item, true))
                ++announced;
        }
        spdlog::info("DroppedItem: {} of the {} item(s) dropped while not connected announced", announced, m_droppedOffline.size());
        m_droppedOffline.clear();
    }

    // Both types have to have resolved on this build: DynamicCast with a null type descriptor is not safe to call.
    if (!internal::RttiLocator<BSExtraData>::Get() || !internal::RttiLocator<ExtraDroppedItemList>::Get())
    {
        static bool s_said = false;
        if (!s_said)
        {
            s_said = true;
            spdlog::warn("DroppedItem: the dropped-item list type did not resolve on this build; earlier drops are not announced");
        }
        return;
    }

    auto& modSystem = m_world.GetModSystem();
    uint32_t found = 0;
    uint32_t sent = 0;

    uint32_t guard = 0;
    bool listFound = false;
    for (BSExtraData* pExtra = pPlayer->extraData.data; pExtra && guard < 512; pExtra = pExtra->next, ++guard)
    {
        if (!IsMemoryReadable(pExtra, sizeof(BSExtraData)) || !Cast<ExtraDroppedItemList>(pExtra))
            continue;
        listFound = true;

        const auto* pList = reinterpret_cast<const DroppedItemListView*>(pExtra);
        const DroppedItemNode* pNode = &pList->Head;
        for (uint32_t n = 0; pNode && n < 1024 && IsMemoryReadable(pNode, sizeof(DroppedItemNode)); ++n, pNode = pNode->Next)
        {
            if (!pNode->Handle)
                continue;

            TESObjectREFR* pRef = TESObjectREFR::GetByHandle(pNode->Handle);
            if (!IsItem(pRef) || pRef->IsDisabled() || pRef->IsDeleted())
                continue;
            ++found;

            if (m_idByRef.count(pRef->formID) || m_announced.count(pRef->formID))
                continue;
            m_announced.insert(pRef->formID);

            Inventory::Entry item{};
            if (!modSystem.GetServerModId(pRef->baseForm->formID, item.BaseId))
                continue;
            item.Count = 1;
            if (auto* pCount = static_cast<ExtraCount*>(pRef->extraData.GetByType(ExtraDataType::Count)))
                item.Count = std::max<int32_t>(1, pCount->count);
            TESObjectREFR::GetItemFromExtraData(item, &pRef->extraData);

            if (SendAdd(pRef, item, true))
                ++sent;
        }
        break; // one list per actor
    }

    // Logged once per connection even when nothing is found.
    if (found || sent || !m_reportedOldDrops)
    {
        m_reportedOldDrops = true;
        if (listFound)
            spdlog::info("DroppedItem: the player's dropped-item list holds {} item(s); announced {} not yet shared this session", found, sent);
    }
}

void DroppedItemService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_transport.IsConnected())
        return;

    // Every frame, at most 90 times a second: the receiver snaps to each update, and a hand on a thing another game moves waits for its answer.
    // At 30 a second, up to 33 ms passed before a move or a hand was sent, twice on the way round. Only things that move are sent, so it costs a few small messages.
    static std::chrono::steady_clock::time_point s_next{};
    const auto now = std::chrono::steady_clock::now();
    if (now < s_next)
        return;
    s_next = now + std::chrono::milliseconds(11);

    for (const auto& [id, refFormId] : m_refById)
    {
        Motion& motion = m_motion[id];
        TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(refFormId));
        if (!pRef || !IsStillOurs(pRef, motion) || pRef->IsDeleted() || pRef->IsDisabled() || !pRef->GetNiNode())
        {
            motion.Loaded = false;
            continue;
        }

        const glm::vec3 position = WorldPositionOf(pRef);
        const glm::vec3 rotation = RotationOf(pRef);

        // Its cell just loaded: its settling is ignored, so neither side sends it.
        if (!motion.Loaded)
        {
            motion.Loaded = true;
            if (now >= motion.FreshDropUntil)
                motion.QuietUntil = std::max(motion.QuietUntil, now + std::chrono::seconds(2));
        }

        // Nothing heard for two seconds from the side moving it: back to physics here.
        if (motion.Held && now - motion.LastRemote > std::chrono::seconds(2))
        {
            pRef->SetMotionType(TESObjectREFR::kMotionDynamic);
            motion.Held = false;
            motion.QuietUntil = now + std::chrono::seconds(2);
            if (motion.RemoteAtRest)
                spdlog::info("DroppedItem: {} was put down over there; handed back to physics here", id);
            else
                spdlog::info("DroppedItem: {} stopped being moved over there without coming to rest; released it here", id);
        }

        if (motion.Held && VRModApis::KnowsGrabs())
            VRBodySync::PinBody(pRef, nullptr);

        if (motion.Held || now < motion.QuietUntil)
        {
            motion.LastPosition = position;
            motion.LastRotation = rotation;
            continue;
        }

        const glm::vec3 turned = glm::abs(rotation - motion.LastRotation);
        const bool cMoved = glm::distance(position, motion.LastPosition) > kMovedDistance || std::max({turned.x, turned.y, turned.z}) > kMovedAngle;

        const auto send = [&](const bool aAtRest)
        {
            RequestDroppedItemMove request{};
            request.Id = id;
            request.Position = position;
            request.Rotation = rotation;
            request.AtRest = aAtRest;
            m_transport.Send(request);
        };

        if (cMoved)
        {
            if (!motion.Moving)
                spdlog::info("DroppedItem: {} ({:X}) is being moved here; sending where it is", id, refFormId);
            motion.LastPosition = position;
            motion.LastRotation = rotation;
            motion.LastLocalMove = now;
            motion.Moving = true;
            send(false);
        }
        else if (motion.Moving && now - motion.LastLocalMove >= std::chrono::milliseconds(600))
        {
            // Still for long enough to call it put down. The server keeps this place for anyone who comes later.
            motion.Moving = false;
            send(true);
            spdlog::info("DroppedItem: {} came to rest at ({:.0f}, {:.0f}, {:.0f})", id, position.x, position.y, position.z);
        }
    }

    UpdateWorldObjects(now);
    RunPulls(now);
    RunSharedBodies();
    RunGrabReports();
}

namespace
{
// What a hand can pick up or push: items, alchemy apparatus (33) and movable statics (36: carts, hanging buckets).
// Statics and furniture never move.
bool IsMovableObject(const TESObjectREFR* apRef) noexcept
{
    if (IsItem(apRef))
        return true;
    if (!apRef || !apRef->baseForm)
        return false;
    const auto cType = static_cast<uint8_t>(apRef->baseForm->formType);
    return cType == 33 || cType == 36;
}
} // namespace

// Objects placed in the world, moved by the player's hand (HIGGS) or pushed.
// Watched like dropped items, but only near the player, and named by their shared reference id.
void DroppedItemService::UpdateWorldObjects(const std::chrono::steady_clock::time_point aNow) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    if (!pCell)
        return;

    constexpr float cWatchRadius = 400.f;
    const glm::vec3 playerAt(pPlayer->position.x, pPlayer->position.y, pPlayer->position.z);

    if (aNow >= m_candidatesAt)
    {
        m_candidatesAt = aNow + std::chrono::seconds(1);
        m_objectCandidates.clear();

        static const Vector<FormType> s_types = []
        {
            Vector<FormType> types(kItemTypes);
            types.push_back(static_cast<FormType>(33));
            types.push_back(static_cast<FormType>(36));
            return types;
        }();
        for (TESObjectREFR* pRef : pCell->GetRefsByFormTypes(s_types))
        {
            // Temporary references are dropped items, which have their own record above.
            if (!pRef || pRef->IsTemporary() || m_idByRef.count(pRef->formID))
                continue;
            if (pRef->IsDisabled() || pRef->IsDeleted() || !pRef->GetNiNode() || DistanceTo(pRef, playerAt) > cWatchRadius)
                continue;
            m_objectCandidates.push_back(pRef->formID);
            if (m_objectCandidates.size() >= 128)
            {
                // Measurement: the watch list is full, so movable objects past the first 128 the cell lists go unwatched.
                static std::chrono::steady_clock::time_point s_nextFullLog{};
                if (aNow >= s_nextFullLog)
                {
                    s_nextFullLog = aNow + std::chrono::seconds(30);
                    spdlog::info("WorldObject: 128 movable objects within {:.0f} units are watched, the most there is room for; others nearby are not", cWatchRadius);
                }
                break;
            }
        }

        // Held for the other player until it is let go, and carried here until it comes to rest, wherever it is now.
        // A carried object dropped from the list would never send its rest, and the other side would let it fall mid-carry.
        for (const auto& [formId, motion] : m_objectMotion)
        {
            if (!motion.Held && !motion.Moving)
                continue;
            if (std::find(m_objectCandidates.begin(), m_objectCandidates.end(), formId) != m_objectCandidates.end())
                continue;
            auto* pRef = Cast<TESObjectREFR>(TESForm::GetById(formId));
            if (motion.Held || (pRef && pRef->GetNiNode()))
                m_objectCandidates.push_back(formId);
        }

        for (auto it = m_objectMotion.begin(); it != m_objectMotion.end();)
        {
            const bool cWatched = std::find(m_objectCandidates.begin(), m_objectCandidates.end(), it->first) != m_objectCandidates.end();
            it = cWatched ? std::next(it) : m_objectMotion.erase(it);
        }
    }

    auto& modSystem = m_world.GetModSystem();
    const auto send = [&](TESObjectREFR* apRef, const uint32_t aFormId, const glm::vec3& acPosition, const glm::vec3& acRotation, const bool aAtRest)
    {
        RequestWorldObjectMove request{};
        if (!modSystem.GetServerModId(aFormId, request.ObjectId))
            return;
        if (TESObjectCELL* pObjectCell = apRef->GetParentCellEx())
            modSystem.GetServerModId(pObjectCell->formID, request.CellId);
        if (TESWorldSpace* pWorldSpace = apRef->GetWorldSpace())
            modSystem.GetServerModId(pWorldSpace->formID, request.WorldSpaceId);
        request.Position = acPosition;
        request.Rotation = acRotation;
        request.AtRest = aAtRest;
        m_transport.Send(request);
    };

    // The other players' copies, where they stand: something they bump here is theirs to send, not ours.
    std::vector<glm::vec3> others;
    {
        auto view = m_world.view<FormIdComponent, PlayerComponent>();
        for (const auto cEntity : view)
            if (const Actor* pOther = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(cEntity).Id)); pOther && pOther->formID != 0x14)
                others.emplace_back(pOther->position.x, pOther->position.y, pOther->position.z);
    }

    // Objects that start moving in this pass, judged together once the pass is done.
    struct Start
    {
        TESObjectREFR* pRef;
        uint32_t FormId;
        glm::vec3 Position;
        glm::vec3 Rotation;
        glm::mat3 Turn;
        float Distance;
        float Angle;
    };
    std::vector<Start> starts;

    for (const uint32_t cFormId : m_objectCandidates)
    {
        TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(cFormId));
        const NiNode* pNode = pRef ? pRef->GetNiNode() : nullptr;
        if (!pRef || pRef->IsDeleted() || pRef->IsDisabled() || !pNode || !IsMovableObject(pRef))
            continue;

        const glm::vec3 position = WorldPositionOf(pRef);
        const glm::vec3 rotation = RotationOf(pRef);
        const glm::mat3 turn = WorldTurnOf(pNode);

        const auto found = m_objectMotion.find(cFormId);
        if (found == m_objectMotion.end())
        {
            // First seen: where it is now is where it was, and a second of quiet covers a cell still settling.
            Motion motion{};
            motion.LastPosition = position;
            motion.LastRotation = rotation;
            motion.LastTurn = turn;
            motion.QuietUntil = aNow + std::chrono::seconds(1);
            m_objectMotion.emplace(cFormId, motion);
            continue;
        }
        Motion& motion = found->second;

        // A put-down is let go half a second after it arrives, so the player here can take the object at once.
        // A move that just stops arriving is let go after two seconds.
        const auto cHoldFor = motion.RemoteAtRest ? std::chrono::milliseconds(500) : std::chrono::milliseconds(2000);
        if (motion.Held && aNow - motion.LastRemote > cHoldFor)
        {
            pRef->SetMotionType(TESObjectREFR::kMotionDynamic);
            motion.Held = false;
            motion.QuietUntil = aNow + cHoldFor;
            // Where its 3D is drawn, against the last place the other side put it: what this player sees of the carry.
            spdlog::info("WorldObject: {:X} {}; handed back to physics here ({} of its {} moves here rebuilt its 3D; its 3D is {:.0f} units from the last place it was put)",
                         cFormId, motion.RemoteAtRest ? "was put down over there" : "stopped being moved over there", motion.HeldRebuilds, motion.HeldMoves,
                         glm::distance(position, motion.RemoteTarget));
            motion.HeldMoves = 0;
            motion.HeldRebuilds = 0;
        }

        if (motion.Held && VRModApis::KnowsGrabs())
            VRBodySync::PinBody(pRef, nullptr);

        if (motion.Held || aNow < motion.QuietUntil)
        {
            motion.LastPosition = position;
            motion.LastRotation = rotation;
            motion.LastTurn = turn;
            continue;
        }

        if (!motion.Moving)
        {
            const float cDistance = glm::distance(position, motion.LastPosition);
            const float cAngle = AngleBetween(motion.LastTurn, turn);
            if (cDistance > kRestartDistance || cAngle > kRestartAngle)
                starts.push_back({pRef, cFormId, position, rotation, turn, cDistance, cAngle});
            continue;
        }

        const glm::vec3 turned = glm::abs(rotation - motion.LastRotation);
        const bool cMoved = glm::distance(position, motion.LastPosition) > kMovedDistance || std::max({turned.x, turned.y, turned.z}) > kMovedAngle;

        if (cMoved)
        {
            motion.LastPosition = position;
            motion.LastRotation = rotation;
            motion.LastTurn = turn;
            motion.LastLocalMove = aNow;
            send(pRef, cFormId, position, rotation, false);
        }
        // A second of stillness: a hand pausing mid-carry is not a put-down, which the other side lets go of at once.
        else if (aNow - motion.LastLocalMove >= std::chrono::milliseconds(1000))
        {
            // A restart is measured from where it rests.
            motion.Moving = false;
            motion.LastPosition = position;
            motion.LastRotation = rotation;
            motion.LastTurn = turn;
            send(pRef, cFormId, position, rotation, true);
            spdlog::info("WorldObject: {:X} came to rest at ({:.0f}, {:.0f}, {:.0f})", cFormId, position.x, position.y, position.z);
        }
    }

    // Many objects starting at once is a cell settling or a blast, not a hand: nothing is sent.
    if (starts.size() >= 5)
    {
        for (const Start& start : starts)
        {
            Motion& motion = m_objectMotion[start.FormId];
            motion.LastPosition = start.Position;
            motion.LastRotation = start.Rotation;
            motion.LastTurn = start.Turn;
            motion.QuietUntil = aNow + std::chrono::seconds(2);
        }
        spdlog::info("WorldObject: {} objects began moving at once (a cell settling or a blast, not a hand); not sent", starts.size());
        return;
    }

    for (const Start& start : starts)
    {
        Motion& motion = m_objectMotion[start.FormId];

        // Nearer another player's copy than this player: their body bumped it here.
        // Their own game does not push clutter with a VR body, so it is not sent.
        // Unless this player moved it a moment ago: then it is a carry going on next to them, a hand-over say.
        const float cMine = glm::distance(start.Position, playerAt);
        const bool cCarriedHere = aNow - motion.LastLocalMove < std::chrono::seconds(3);
        bool cTheirs = false;
        for (const glm::vec3& other : others)
            if (const float cTheirsAt = glm::distance(start.Position, other); !cCarriedHere && cTheirsAt < 150.f && cTheirsAt < cMine)
                cTheirs = true;
        if (cTheirs)
        {
            motion.LastPosition = start.Position;
            motion.LastRotation = start.Rotation;
            motion.LastTurn = start.Turn;
            motion.QuietUntil = aNow + std::chrono::seconds(1);
            spdlog::info("WorldObject: {:X} moved next to another player's copy; not sent", start.FormId);
            continue;
        }

        spdlog::info("WorldObject: {:X} is being moved here; sending where it is (moved {:.0f} units, turned {:.2f} rad)", start.FormId, start.Distance, start.Angle);
        motion.LastPosition = start.Position;
        motion.LastRotation = start.Rotation;
        motion.LastTurn = start.Turn;
        motion.LastLocalMove = aNow;
        motion.Moving = true;
        send(start.pRef, start.FormId, start.Position, start.Rotation, false);
    }
}

void DroppedItemService::OnNotifyWorldObjectMove(const NotifyWorldObjectMove& acMessage) noexcept
{
    const uint32_t cFormId = m_world.GetModSystem().GetGameId(acMessage.ObjectId);
    TESObjectREFR* pRef = cFormId ? Cast<TESObjectREFR>(TESForm::GetById(cFormId)) : nullptr;

    // Taken into another player's inventory (a book, see OnItemPickedUp): gone from this world too.
    // Disabled, not deleted: it is a plugin's placed reference, and disabled is how this save keeps it.
    if (acMessage.Taken)
    {
        if (pRef && !pRef->IsDeleted() && !pRef->IsDisabled())
        {
            if (const auto held = m_objectMotion.find(cFormId); held != m_objectMotion.end() && held->second.Held)
                pRef->SetMotionType(TESObjectREFR::kMotionDynamic);
            m_objectMotion.erase(cFormId);
            pRef->Disable();
            spdlog::info("WorldObject: {:X} was taken by another player; removed here", cFormId);
        }
        return;
    }

    // Not loaded here: the server sends where it was left again when this player comes near.
    if (!pRef || pRef->IsDeleted() || pRef->IsDisabled() || !pRef->GetNiNode() || !IsMovableObject(pRef))
        return;

    const auto now = std::chrono::steady_clock::now();
    Motion& motion = m_objectMotion[cFormId];
    // In this player's hand: not followed. See SharedMove.
    if (SharedMove(RequestPull::kWorldObject, 0, acMessage.ObjectId, pRef, motion, glm::vec3(acMessage.Position), now))
        return;
    if (acMessage.AtRest)
    {
        // A put-down from the game that had it before, while this player carries it: it is this game's now, and stays in the hand.
        if (!motion.Held && motion.Moving && now - motion.LastLocalMove < std::chrono::milliseconds(500))
            return;
        if (TakeOverPulled(RequestPull::kWorldObject, 0, cFormId, pRef, motion))
            return;
    }
    if (!motion.Held)
    {
        HoldStill(pRef);
        motion.Held = true;
        spdlog::info("WorldObject: {:X} is being moved over there; holding it here and following", cFormId);
    }

    const glm::vec3 position(acMessage.Position);
    const NiNode* pNodeBefore = pRef->GetNiNode();
    // With HIGGS the thing stays a body physics may move, pinned where the other game has it (HoldStill). Before the placing: see PinBody.
    if (VRModApis::KnowsGrabs())
        VRBodySync::PinBody(pRef, &position);
    PlaceMovedObject(pRef, position, acMessage.Rotation);
    ++motion.HeldMoves;
    if (const NiNode* pNodeAfter = pRef->GetNiNode(); pNodeBefore && pNodeAfter != pNodeBefore)
    {
        ++motion.HeldRebuilds;
        if (pNodeAfter)
            HoldStill(pRef);
    }

    motion.LastRemote = now;
    motion.LastPosition = position;
    motion.RemoteTarget = position;
    motion.LastRotation = acMessage.Rotation;
    motion.Moving = false;
    motion.RemoteAtRest = acMessage.AtRest;

    if (std::find(m_objectCandidates.begin(), m_objectCandidates.end(), cFormId) == m_objectCandidates.end())
        m_objectCandidates.push_back(cFormId);
}

void DroppedItemService::OnNotifyDroppedItemMove(const NotifyDroppedItemMove& acMessage) noexcept
{
    const auto it = m_refById.find(acMessage.Id);
    if (it == m_refById.end())
        return; // not placed here yet; the server hands over its resting place with the item

    TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(it->second));
    Motion& motion = m_motion[acMessage.Id];
    if (!pRef || !IsStillOurs(pRef, motion) || pRef->IsDeleted())
        return;

    const auto now = std::chrono::steady_clock::now();

    // As for a world object (OnNotifyWorldObjectMove).
    if (SharedMove(RequestPull::kDroppedItem, acMessage.Id, GameId{}, pRef, motion, glm::vec3(acMessage.Position), now))
        return;

    if (acMessage.AtRest)
    {
        // As for a world object (OnNotifyWorldObjectMove).
        if (!motion.Held && motion.Moving && now - motion.LastLocalMove < std::chrono::milliseconds(500))
            return;
        if (TakeOverPulled(RequestPull::kDroppedItem, acMessage.Id, it->second, pRef, motion))
            return;
    }

    // Held still while the other side moves it, or local physics pulls it down between updates and it stutters.
    // Also for a lone "put down": it is placed exactly, then let go.
    if (!motion.Held)
    {
        HoldStill(pRef);
        motion.Held = true;
        spdlog::info("DroppedItem: {} ({:X}) is being moved over there; holding it here and following", acMessage.Id, it->second);
    }

    const glm::vec3 position(acMessage.Position);
    const NiNode* pNodeBefore = pRef->GetNiNode();
    // With HIGGS the thing stays a body physics may move, pinned where the other game has it (HoldStill). Before the placing: see PinBody.
    if (VRModApis::KnowsGrabs())
        VRBodySync::PinBody(pRef, &position);
    PlaceMovedObject(pRef, position, acMessage.Rotation);

    // A rebuilt 3D has a dynamic body again, which would fall, so it is held again.
    // Logged once, to learn whether a move ever rebuilds it.
    if (const NiNode* pNodeAfter = pRef->GetNiNode(); pNodeBefore && pNodeAfter && pNodeAfter != pNodeBefore)
    {
        HoldStill(pRef);
        static bool s_reported = false;
        if (!s_reported)
        {
            s_reported = true;
            spdlog::info("DroppedItem: moving {} rebuilt its 3D; holding it again after each move", acMessage.Id);
        }
    }

    motion.LastRemote = now;
    motion.LastPosition = position;
    motion.LastRotation = acMessage.Rotation;
    motion.Moving = false;
    // Let go two seconds after the last update (OnUpdate), not now: a still hand also sends "at rest".
    motion.RemoteAtRest = acMessage.AtRest;
}

// EXPERIMENTAL, see VRBodySync's pulls.
// What another player's game moves is held still here (keyframed) or posed from their bones, so a hand here cannot take it.
// Instead, a grip or trigger pressed with the hand on it sends the hand's position, and their game pulls the thing toward it.
// It then comes back here with their updates, like everything else they move.
void DroppedItemService::RunPulls(const std::chrono::steady_clock::time_point aNow) noexcept
{
    VRBodySync::ExpirePulls();

    constexpr float cGrabReach = 40.f;

    for (uint8_t side = 0; side < 2; ++side)
    {
        HandPull& pull = m_handPulls[side];
        glm::vec3 hand{};
        const bool cPressed = VRBodySync::PressedHand(side, hand);
        const bool cEdge = cPressed && !pull.WasPressed;
        pull.WasPressed = cPressed;

        const auto send = [&](const bool aActive)
        {
            RequestPull request{};
            request.Kind = pull.Kind;
            request.ServerId = pull.ServerId;
            request.ObjectId = pull.ObjectId;
            request.Hand = side;
            request.Position = hand;
            request.Active = aActive;
            // A lost position is replaced by the next one; the end is sent reliably, though the other game also lets go by itself.
            if (aActive)
                m_transport.SendUnreliable(request);
            else
                m_transport.Send(request);
        };

        // The key of this player's own hand, for a pull this game does itself.
        const uint64_t cLocalKey = (static_cast<uint64_t>(0xFFFFFFFFu) << 1) | side;

        if (pull.Active && pull.Local)
        {
            // This game runs the thing now (TakeOverPulled), and the hand pulls it here: no HIGGS grab holds it, as the press began before it was ours.
            Motion* pMotion = nullptr;
            if (pull.Kind == RequestPull::kWorldObject)
            {
                if (const auto cMotion = m_objectMotion.find(pull.FormId); cMotion != m_objectMotion.end())
                    pMotion = &cMotion->second;
            }
            else if (const auto cMotion = m_motion.find(pull.ServerId); cMotion != m_motion.end())
                pMotion = &cMotion->second;
            TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(pull.FormId));

            if (pMotion && pMotion->Held)
            {
                // Another game took it again (the referee gave it the hold): this hand pulls over there once more.
                VRBodySync::EndPull(cLocalKey);
                pull.Local = false;
                spdlog::info("PullDiag: {:X} is another game's to move again; this player's {} hand pulls it over there", pull.FormId, side ? "right" : "left");
            }
            else if (cPressed && pMotion && pRef && !pRef->IsDeleted() && !pRef->IsDisabled() && VRBodySync::PullBody(cLocalKey, pRef, hand))
            {
                // Carried: not put down while it hangs still from the hand.
                pMotion->LastLocalMove = aNow;
                continue;
            }
            else
            {
                VRBodySync::EndPull(cLocalKey);
                pull.Active = false;
                spdlog::info("PullDiag: this player's {} hand let go of {:X}, which this game ran, after {:.1f} s", side ? "right" : "left", pull.FormId,
                             std::chrono::duration<float>(aNow - pull.Since).count());
                continue;
            }
        }

        if (pull.Active)
        {
            // Still theirs to move: held for them here, or a body they still run.
            bool still = cPressed;
            if (still && pull.Kind == RequestPull::kWorldObject)
            {
                const auto cMotion = m_objectMotion.find(pull.FormId);
                still = cMotion != m_objectMotion.end() && cMotion->second.Held;
                // A thing hanging still from this hand sends no moves, and would be let go here as put down.
                if (still)
                    cMotion->second.LastRemote = aNow;
            }
            else if (still && pull.Kind == RequestPull::kDroppedItem)
            {
                const auto cMotion = m_motion.find(pull.ServerId);
                still = cMotion != m_motion.end() && cMotion->second.Held;
                if (still)
                    cMotion->second.LastRemote = aNow;
            }
            else if (still)
            {
                still = false;
                auto view = m_world.view<RemoteComponent, FormIdComponent>();
                for (const auto cEntity : view)
                    if (view.get<RemoteComponent>(cEntity).Id == pull.ServerId && view.get<FormIdComponent>(cEntity).Id == pull.FormId)
                        still = true;
            }

            if (still)
            {
                send(true);
                continue;
            }
            send(false);
            pull.Active = false;
            spdlog::info("PullDiag: this player's {} hand let go of {:X} after {:.1f} s ({})", side ? "right" : "left", pull.FormId,
                         std::chrono::duration<float>(aNow - pull.Since).count(), cPressed ? "it is no longer the other game's to move" : "the press ended");
            continue;
        }

        if (!cPressed)
            continue;

        // With HIGGS nothing is guessed from the button: what another game moves stays a body HIGGS can take (HoldStill),
        // and its grab or pull is what takes the thing over (TakeFromOtherGame).
        if (VRModApis::KnowsGrabs())
            continue;

        // What HIGGS says this hand holds. A hand that holds one thing is not pulling at another next to it.
        // The thing itself is let through: handed to another game while in this hand, HIGGS may still call it held.
        const uint32_t cHiggsHeld = VRModApis::HeldByHand(side);
        const auto otherInHand = [&](const uint32_t aFormId) { return cHiggsHeld != 0 && cHiggsHeld != aFormId; };

        // The nearest thing under the hand that another game is moving right now.
        // A new press takes anything; a press already held takes only what this hand was just carrying, which the referee gave to another game.
        const auto carriedHere = [&](const Motion& acMotion) { return aNow - acMotion.LastLocalMove < std::chrono::seconds(1); };
        HandPull found{};
        float nearest = cGrabReach;
        for (auto& [formId, motion] : m_objectMotion)
        {
            if (!motion.Held || !(cEdge || carriedHere(motion)) || otherInHand(formId))
                continue;
            TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(formId));
            GameId objectId{};
            if (!pRef || pRef->IsDeleted() || pRef->IsDisabled() || !m_world.GetModSystem().GetServerModId(formId, objectId))
                continue;
            if (const float cApart = VRBodySync::NearestNodeTo(pRef, hand); cApart < nearest)
            {
                nearest = cApart;
                found = HandPull{};
                found.Kind = RequestPull::kWorldObject;
                found.FormId = formId;
                found.ObjectId = objectId;
            }
        }
        for (const auto& [id, refFormId] : m_refById)
        {
            const auto cMotion = m_motion.find(id);
            if (cMotion == m_motion.end() || !cMotion->second.Held || !(cEdge || carriedHere(cMotion->second)) || otherInHand(refFormId))
                continue;
            TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(refFormId));
            if (!pRef || !IsStillOurs(pRef, cMotion->second) || pRef->IsDeleted() || pRef->IsDisabled())
                continue;
            if (const float cApart = VRBodySync::NearestNodeTo(pRef, hand); cApart < nearest)
            {
                nearest = cApart;
                found = HandPull{};
                found.Kind = RequestPull::kDroppedItem;
                found.ServerId = id;
                found.FormId = refFormId;
            }
        }
        {
            auto view = m_world.view<RemoteComponent, FormIdComponent>();
            for (const auto cEntity : view)
            {
                const uint32_t cFormId = view.get<FormIdComponent>(cEntity).Id;
                Actor* pActor = Cast<Actor>(TESForm::GetById(cFormId));
                // A body its owner is carrying; one that lies still is taken over by grabbing it, as before (IsRemoteBodyGrabbedHere).
                // With HIGGS the hand takes this game's copy of the body itself, and the two games pull on each other (RunSharedBodies).
                if (VRModApis::KnowsGrabs() || !cEdge || !pActor || !pActor->actorState.IsDead() || !VRBodySync::IsBodyMovedByOwner(cFormId) || otherInHand(cFormId))
                    continue;
                if (const float cApart = VRBodySync::NearestNodeTo(pActor, hand); cApart < nearest)
                {
                    nearest = cApart;
                    found = HandPull{};
                    found.Kind = RequestPull::kActor;
                    found.ServerId = view.get<RemoteComponent>(cEntity).Id;
                    found.FormId = cFormId;
                }
            }
        }

        if (found.FormId == 0)
            continue;

        found.Active = true;
        found.WasPressed = true;
        found.Since = aNow;
        pull = found;
        spdlog::info("PullDiag: this player's {} hand pressed {:.0f} units from {:X}, which another game is moving ({}); sending the hand there", side ? "right" : "left",
                     nearest, pull.FormId, pull.Kind == RequestPull::kActor ? "a body" : pull.Kind == RequestPull::kWorldObject ? "a placed object" : "a dropped item");
        send(true);
    }
}

// TEMPORARY: what HIGGS reports goes to the server as it is, and the server logs it, so both ends can be compared.
void DroppedItemService::RunGrabReports() noexcept
{
    static std::vector<VRModApis::HandEvent> s_events;
    s_events.clear();
    VRModApis::TakeHandEvents(s_events);
    if (s_events.empty() || !m_transport.IsConnected())
        return;

    for (const auto& event : s_events)
    {
        if (event.Event != VRModApis::HandEvent::kLetGo)
            TakeFromOtherGame(event.FormId, event.IsLeft, event.Event == VRModApis::HandEvent::kPulled);

        RequestGrabReport report{};
        report.Event = event.Event;
        report.Hand = event.IsLeft ? 0 : 1;
        report.Kind = RequestGrabReport::kUnknown;
        report.FormId = event.FormId;
        report.NodeName = event.NodeName.c_str();
        report.GrabX = event.Grab[0];
        report.GrabY = event.Grab[1];
        report.GrabZ = event.Grab[2];
        std::copy(std::begin(event.GrabRotation), std::end(event.GrabRotation), report.GrabRotation);
        for (int i = 0; i < 5; ++i)
            report.Fingers |= static_cast<uint64_t>(std::clamp(event.Fingers[i], 0.f, 1.f) * 255.f + 0.5f) << (8 * i);

        // What the server knows the thing as: a character, a dropped item, or a placed reference.
        TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(event.FormId));
        if (Cast<Actor>(pRef))
        {
            for (const auto cEntity : m_world.view<FormIdComponent>())
            {
                if (m_world.get<FormIdComponent>(cEntity).Id != event.FormId)
                    continue;
                if (const auto* pRemote = m_world.try_get<RemoteComponent>(cEntity))
                    report.ServerId = pRemote->Id;
                else if (const auto* pLocal = m_world.try_get<LocalComponent>(cEntity))
                    report.ServerId = pLocal->Id;
                else
                    break;
                report.Kind = RequestGrabReport::kActor;
                break;
            }
        }
        else if (const auto cItem = std::find_if(m_refById.begin(), m_refById.end(), [&](const auto& acPair) { return acPair.second == event.FormId; }); cItem != m_refById.end())
        {
            report.Kind = RequestGrabReport::kDroppedItem;
            report.ServerId = cItem->first;
        }
        else if (pRef && !pRef->IsTemporary() && m_world.GetModSystem().GetServerModId(event.FormId, report.ObjectId))
            report.Kind = RequestGrabReport::kWorldObject;

        m_transport.Send(report);
    }
}

bool DroppedItemService::IsInAnotherHand(const uint8_t aKind, const uint32_t aServerId, const GameId& acObjectId) const noexcept
{
    // RequestPull and RequestGrabReport number the kinds differently.
    const uint8_t cKind = aKind == RequestPull::kWorldObject ? RequestGrabReport::kWorldObject : aKind == RequestPull::kDroppedItem ? RequestGrabReport::kDroppedItem : RequestGrabReport::kActor;
    for (const auto& [key, grab] : m_remoteGrabs)
    {
        if (grab.Kind != cKind)
            continue;
        if (cKind == RequestGrabReport::kWorldObject ? grab.ObjectId == acObjectId : grab.ServerId == aServerId)
            return true;
    }
    return false;
}

// HIGGS grabbed a thing, or began pulling it to the hand, that another game was moving and this one kept pinned: it is this game's to
// simulate from this moment. The server gives this player the hold on the same report, unless another player has it in a hand too;
// then both hold it (SharedMove).
void DroppedItemService::TakeFromOtherGame(const uint32_t aFormId, const bool aIsLeft, const bool aPulled) noexcept
{
    Motion* pMotion = nullptr;
    uint8_t kind = RequestPull::kDroppedItem;
    uint32_t serverId = 0;
    GameId objectId{};
    if (const auto cItem = m_idByRef.find(aFormId); cItem != m_idByRef.end())
    {
        serverId = cItem->second;
        if (const auto cMotion = m_motion.find(serverId); cMotion != m_motion.end())
            pMotion = &cMotion->second;
    }
    else if (const auto cMotion = m_objectMotion.find(aFormId); cMotion != m_objectMotion.end())
    {
        pMotion = &cMotion->second;
        kind = RequestPull::kWorldObject;
        m_world.GetModSystem().GetServerModId(aFormId, objectId);
    }
    TESObjectREFR* pRef = Cast<TESObjectREFR>(TESForm::GetById(aFormId));
    if (!pMotion || !pRef || !pMotion->Held)
        return;

    const auto cNow = std::chrono::steady_clock::now();
    VRBodySync::UnpinBody(pRef);
    pMotion->Held = false;
    pMotion->Moving = true;
    pMotion->LastLocalMove = cNow;
    pMotion->QuietUntil = {};
    // A pulled thing flies to the hand first, and HIGGS calls it held only when it arrives.
    pMotion->TakenUntil = cNow + std::chrono::milliseconds(aPulled ? 2000 : 500);
    pMotion->LastPosition = WorldPositionOf(pRef);
    pMotion->LastRotation = RotationOf(pRef);
    spdlog::info("GrabNow: this player's {} hand {} {:X}, which another game moved and {}; this game runs it now", aIsLeft ? "left" : "right", aPulled ? "pulls" : "grabbed", aFormId,
                 IsInAnotherHand(kind, serverId, objectId) ? "another player has in a hand: held by both now" : "nobody held");
}

bool DroppedItemService::SharedMove(const uint8_t aKind, const uint32_t aServerId, const GameId& acObjectId, TESObjectREFR* apRef, Motion& aMotion, const glm::vec3& acPosition,
                                    const std::chrono::steady_clock::time_point aNow) noexcept
{
    if (!apRef || !IsInOwnHand(apRef->formID, aMotion, aNow))
        return false;

    // Nobody else holds it: these are the last moves of the game that had it before, until the referee has given it over. Ignored.
    if (!IsInAnotherHand(aKind, aServerId, acObjectId))
        return true;

    // EXPERIMENTAL: only where it is, not how it is turned; each hand turns its own copy.
    // The key names this thing, not a hand: two things held by both are pulled separately.
    const uint64_t cKey = 0xC000000000000000ull | (static_cast<uint64_t>(apRef->formID) << 1);
    const bool cPulled = VRBodySync::PullBody(cKey, apRef, acPosition);
    // Carried, not put down, while the other player moves it under this hand.
    aMotion.LastLocalMove = aNow;
    if (aNow - aMotion.SharedLogAt > std::chrono::seconds(2))
    {
        aMotion.SharedLogAt = aNow;
        spdlog::info("Shared: {:X} is in this player's hand and in another player's; their game has it {:.0f} units from where it is here{}", apRef->formID,
                     glm::distance(acPosition, WorldPositionOf(apRef)), cPulled ? ", pulled toward it" : ", not pulled");
    }
    return true;
}

bool DroppedItemService::IsInOwnHand(const uint32_t aFormId, const Motion& acMotion, const std::chrono::steady_clock::time_point aNow) const noexcept
{
    if (aNow < acMotion.TakenUntil)
        return true;
    return VRModApis::KnowsGrabs() && (VRModApis::HeldByHand(0) == aFormId || VRModApis::HeldByHand(1) == aFormId);
}

// Kept per player and hand, for placing the thing in the copy's hand and for the hand as a second grip (both still to be built on it).
void DroppedItemService::OnNotifyGrab(const NotifyGrab& acMessage) noexcept
{
    const uint64_t cKey = (static_cast<uint64_t>(acMessage.PlayerId) << 1) | (acMessage.Hand & 1);
    const char* cpHand = (acMessage.Hand & 1) ? "right" : "left";

    // What the thing is in this game.
    uint32_t formId = 0;
    if (acMessage.Kind == RequestGrabReport::kActor)
    {
        for (const auto cEntity : m_world.view<FormIdComponent>())
        {
            const auto* pRemote = m_world.try_get<RemoteComponent>(cEntity);
            const auto* pLocal = m_world.try_get<LocalComponent>(cEntity);
            if ((pRemote && pRemote->Id == acMessage.ServerId) || (pLocal && pLocal->Id == acMessage.ServerId))
            {
                formId = m_world.get<FormIdComponent>(cEntity).Id;
                break;
            }
        }
    }
    else if (acMessage.Kind == RequestGrabReport::kDroppedItem)
    {
        if (const auto cRef = m_refById.find(acMessage.ServerId); cRef != m_refById.end())
            formId = cRef->second;
    }
    else if (acMessage.Kind == RequestGrabReport::kWorldObject)
        formId = m_world.GetModSystem().GetGameId(acMessage.ObjectId);

    const char* cpKind = acMessage.Kind == RequestGrabReport::kActor ? "character" : acMessage.Kind == RequestGrabReport::kDroppedItem ? "dropped item" : "placed object";

    if (acMessage.Event == RequestGrabReport::kPulled)
    {
        spdlog::info("RemoteGrab: player {}'s {} hand pulled {} {:X} toward itself (here {:X})", acMessage.PlayerId, cpHand, cpKind, acMessage.ServerId, formId);
        return;
    }

    if (acMessage.Event == RequestGrabReport::kLetGo)
    {
        const auto cGrab = m_remoteGrabs.find(cKey);
        if (cGrab == m_remoteGrabs.end())
        {
            spdlog::info("RemoteGrab: player {}'s {} hand let go of {} {:X} (here {:X}); no grab of it was known here", acMessage.PlayerId, cpHand, cpKind, acMessage.ServerId, formId);
            return;
        }
        VRBodySync::SetBodyInAnotherHand(cKey, 0);
        spdlog::info("RemoteGrab: player {}'s {} hand let go of {} {:X} (here {:X}) after {:.1f} s", acMessage.PlayerId, cpHand, cpKind, acMessage.ServerId, formId,
                     std::chrono::duration<float>(std::chrono::steady_clock::now() - cGrab->second.Since).count());
        m_remoteGrabs.erase(cGrab);
        return;
    }

    RemoteGrab grab{};
    grab.Kind = acMessage.Kind;
    grab.ServerId = acMessage.ServerId;
    grab.ObjectId = acMessage.ObjectId;
    grab.FormId = formId;
    grab.NodeName = acMessage.NodeName.c_str();
    grab.Position[0] = acMessage.GrabX;
    grab.Position[1] = acMessage.GrabY;
    grab.Position[2] = acMessage.GrabZ;
    std::copy(std::begin(acMessage.GrabRotation), std::end(acMessage.GrabRotation), grab.Rotation);
    for (int i = 0; i < 5; ++i)
        grab.Fingers[i] = static_cast<float>((acMessage.Fingers >> (8 * i)) & 0xFF) / 255.f;
    grab.Since = std::chrono::steady_clock::now();

    spdlog::info("RemoteGrab: player {}'s {} hand grabbed {} {:X} (here {:X}{}) at node '{}', node in hand space at ({:.1f}, {:.1f}, {:.1f})", acMessage.PlayerId, cpHand, cpKind,
                 acMessage.ServerId, formId, formId ? "" : ", not loaded here", grab.NodeName, grab.Position[0], grab.Position[1], grab.Position[2]);
    VRBodySync::SetBodyInAnotherHand(cKey, acMessage.Kind == RequestGrabReport::kActor ? formId : 0);
    m_remoteGrabs[cKey] = std::move(grab);
}

// EXPERIMENTAL. The other player's hand is read from their copy here, so nothing more is sent for it.
void DroppedItemService::RunSharedBodies() noexcept
{
    for (auto& [key, grab] : m_remoteGrabs)
    {
        if (grab.Kind != RequestGrabReport::kActor || grab.FormId == 0)
            continue;
        Actor* pBody = Cast<Actor>(TESForm::GetById(grab.FormId));
        if (!pBody || pBody->formID == 0x14 || !pBody->actorState.IsDead() || !pBody->GetNiNode())
            continue;

        // Only where this game simulates the body: its owner's game, or one whose player has it in a hand too.
        bool mine = VRModApis::KnowsGrabs() && (VRModApis::HeldByHand(0) == grab.FormId || VRModApis::HeldByHand(1) == grab.FormId);
        const char* cpWhy = "this player holds it too";
        if (!mine)
        {
            auto view = m_world.view<LocalComponent, FormIdComponent>();
            for (const auto cEntity : view)
                if (view.get<FormIdComponent>(cEntity).Id == grab.FormId)
                    mine = true;
            cpWhy = "this game owns it";
        }
        if (!mine)
            continue;

        Actor* pCopy = nullptr;
        {
            auto view = m_world.view<FormIdComponent, PlayerComponent>();
            for (const auto cEntity : view)
                if (view.get<PlayerComponent>(cEntity).Id == static_cast<uint32_t>(key >> 1))
                    pCopy = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(cEntity).Id));
        }
        glm::vec3 hand{};
        if (!pCopy || pCopy->formID == 0x14 || !VRBodySync::CopyHandPosition(pCopy, static_cast<uint8_t>(key & 1), hand))
            continue;

        const bool cPulled = VRBodySync::PullBody(key, pBody, hand);
        const auto cNow = std::chrono::steady_clock::now();
        if (cNow - grab.SharedLogAt > std::chrono::seconds(2))
        {
            grab.SharedLogAt = cNow;
            spdlog::info("SharedBody: player {}'s {} hand holds body {:X} at '{}', and {}; its nearest part is {:.0f} units from that hand here{}", key >> 1, (key & 1) ? "right" : "left",
                         grab.FormId, grab.NodeName, cpWhy, VRBodySync::NearestNodeTo(pBody, hand), cPulled ? ", pulled toward it" : ", not pulled");
        }
    }
}

void DroppedItemService::OnNotifyPull(const NotifyPull& acMessage) noexcept
{
    const uint64_t cKey = (static_cast<uint64_t>(acMessage.PlayerId) << 1) | (acMessage.Hand & 1);
    if (!acMessage.Active)
    {
        VRBodySync::EndPull(cKey);
        return;
    }

    // Only the game that moves the thing pulls it: one that holds it still for another game leaves it alone.
    TESObjectREFR* pRef = nullptr;
    Motion* pMotion = nullptr;
    if (acMessage.Kind == RequestPull::kActor)
    {
        auto view = m_world.view<LocalComponent, FormIdComponent>();
        for (const auto cEntity : view)
        {
            if (view.get<LocalComponent>(cEntity).Id != acMessage.ServerId)
                continue;
            Actor* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(cEntity).Id));
            // Bodies only: a living actor's ragdoll is driven by its animation.
            if (pActor && pActor->formID != 0x14 && pActor->actorState.IsDead())
                pRef = pActor;
            break;
        }
    }
    else if (acMessage.Kind == RequestPull::kWorldObject)
    {
        const uint32_t cFormId = m_world.GetModSystem().GetGameId(acMessage.ObjectId);
        pRef = cFormId ? Cast<TESObjectREFR>(TESForm::GetById(cFormId)) : nullptr;
        if (pRef && !IsMovableObject(pRef))
            pRef = nullptr;
        if (const auto cMotion = m_objectMotion.find(cFormId); cMotion != m_objectMotion.end())
            pMotion = &cMotion->second;
    }
    else if (acMessage.Kind == RequestPull::kDroppedItem)
    {
        if (const auto cRef = m_refById.find(acMessage.ServerId); cRef != m_refById.end())
        {
            pRef = Cast<TESObjectREFR>(TESForm::GetById(cRef->second));
            if (const auto cMotion = m_motion.find(acMessage.ServerId); cMotion != m_motion.end())
                pMotion = &cMotion->second;
            if (pRef && pMotion && !IsStillOurs(pRef, *pMotion))
                pRef = nullptr;
        }
    }

    if (!pRef || pRef->IsDeleted() || pRef->IsDisabled() || (pMotion && pMotion->Held))
    {
        VRBodySync::EndPull(cKey);
        return;
    }

    const auto cNow = std::chrono::steady_clock::now();

    // Just handed over (below): their last positions are still arriving, and it is theirs now.
    if (pMotion && cNow < pMotion->QuietUntil)
    {
        VRBodySync::EndPull(cKey);
        return;
    }

    // Counted as carried here: it is not called put down under the other player's hand, and its moves are sent though it is next to their copy.
    if (!VRBodySync::PullBody(cKey, pRef, glm::vec3(acMessage.Position)) || !pMotion)
        return;
    pMotion->LastLocalMove = cNow;

    // With no hand of this player's on it, the other player has it alone, and their own game should run it: no delay for them.
    // Said as a put-down, which the referee takes as letting go; their game takes it over (TakeOverPulled).
    // This game simulates it, so HIGGS knows whether a hand here holds it; without HIGGS, a pressed hand within 60 units is the guess.
    bool held = false;
    for (uint8_t side = 0; side < 2; ++side)
    {
        if (VRModApis::KnowsGrabs())
            held = held || VRModApis::HeldByHand(side) == pRef->formID;
        else if (glm::vec3 hand{}; VRBodySync::PressedHand(side, hand) && VRBodySync::NearestNodeTo(pRef, hand) <= 60.f)
            held = true;
    }
    if (held)
    {
        pMotion->PulledUnheldSince = {};
        return;
    }
    if (pMotion->PulledUnheldSince == std::chrono::steady_clock::time_point{})
        pMotion->PulledUnheldSince = cNow;
    if (cNow - pMotion->PulledUnheldSince < std::chrono::milliseconds(400))
        return;

    const glm::vec3 cPosition = WorldPositionOf(pRef);
    const glm::vec3 cRotation = RotationOf(pRef);
    if (acMessage.Kind == RequestPull::kWorldObject)
    {
        RequestWorldObjectMove request{};
        request.ObjectId = acMessage.ObjectId;
        auto& modSystem = m_world.GetModSystem();
        if (TESObjectCELL* pObjectCell = pRef->GetParentCellEx())
            modSystem.GetServerModId(pObjectCell->formID, request.CellId);
        if (TESWorldSpace* pWorldSpace = pRef->GetWorldSpace())
            modSystem.GetServerModId(pWorldSpace->formID, request.WorldSpaceId);
        request.Position = cPosition;
        request.Rotation = cRotation;
        request.AtRest = true;
        m_transport.Send(request);
    }
    else
    {
        RequestDroppedItemMove request{};
        request.Id = acMessage.ServerId;
        request.Position = cPosition;
        request.Rotation = cRotation;
        request.AtRest = true;
        m_transport.Send(request);
    }
    VRBodySync::EndPull(cKey);
    pMotion->PulledUnheldSince = {};
    pMotion->Moving = false;
    pMotion->LastPosition = cPosition;
    pMotion->LastRotation = cRotation;
    pMotion->QuietUntil = cNow + std::chrono::seconds(1);
    spdlog::info("PullDiag: {:X} is pulled by player {:X} and no hand here is on it; handed over to their game", pRef->formID, acMessage.PlayerId);
}

bool DroppedItemService::TakeOverPulled(const uint8_t aKind, const uint32_t aServerId, const uint32_t aFormId, TESObjectREFR* apRef, Motion& aMotion) noexcept
{
    for (uint8_t side = 0; side < 2; ++side)
    {
        HandPull& pull = m_handPulls[side];
        if (!pull.Active || pull.Local || pull.Kind != aKind || pull.FormId != aFormId || pull.ServerId != aServerId)
            continue;

        const auto cNow = std::chrono::steady_clock::now();
        if (aMotion.Held)
            apRef->SetMotionType(TESObjectREFR::kMotionDynamic);
        aMotion.Held = false;
        aMotion.Moving = true;
        aMotion.LastLocalMove = cNow;
        aMotion.QuietUntil = {};
        aMotion.LastPosition = WorldPositionOf(apRef);
        aMotion.LastRotation = RotationOf(apRef);
        pull.Local = true;
        spdlog::info("PullDiag: the game that moved {:X} let go of it while this player's {} hand pulls on it; this game runs it now", aFormId, side ? "right" : "left");
        return true;
    }
    return false;
}
