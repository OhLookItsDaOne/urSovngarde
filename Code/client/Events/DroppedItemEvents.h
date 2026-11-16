#pragma once

#include <Structs/Inventory.h>

//! The local player dropped an item; raised after the real drop, so the reference on the floor exists.
struct ItemDroppedEvent
{
    uint32_t RefFormId{};
    Inventory::Entry Item{};
};

//! The local player is picking up a reference that lies on the floor.
//! Raised before the pick-up runs, while the reference still exists to be named.
struct ItemPickedUpEvent
{
    uint32_t RefFormId{};

    //! A placed thing (a plugin's reference, no quest object), named here because the reference is gone by the time the event runs.
    //! Activation sync tells only the players who are there at that moment, and leaves books out; this is what the server remembers.
    bool Placed{};
    GameId ObjectId{};
    GameId CellId{};
    GameId WorldSpaceId{};
    glm::vec3 Position{};
};
