#include <TiltedOnlinePCH.h>

#include "VRModApis.h"

#include <Utils.h>

#include <TESObjectREFR.h>

#include <chrono>
#include <mutex>

namespace
{
// As the bridge declares it (Code/vr_bridge/main.cpp). It only ever grows at its end.
struct BridgeState
{
    uint32_t Size;
    uint32_t Stage;
    uint32_t SkseVersion;
    uint32_t RuntimeVersion;
    void* Higgs;
    void* Planck;
    void* Vrik;
    // Not in the first bridge: read only where Size says it is there.
    void* WeaponThrow;
};
// The size of the first bridge's record, which this client still takes.
constexpr uint32_t kFirstBridgeSize = offsetof(BridgeState, WeaponThrow);

// Weapon Throw VR: what it said (on its own thread) and what the launch hook saw (on the main thread), each kept until paired.
std::mutex s_throwLock;
struct
{
    uint32_t WeaponFormId = 0;
    bool IsLeft = false;
    std::chrono::steady_clock::time_point At{};
} s_saidThrown;
struct
{
    uint32_t Handle = 0;
    std::chrono::steady_clock::time_point At{};
} s_helperLaunch;
bool s_returned[2]{};

void OnWeaponThrown(TESForm* apWeapon, bool aIsLeft, void*)
{
    std::scoped_lock _{s_throwLock};
    s_saidThrown.WeaponFormId = apWeapon ? apWeapon->formID : 0;
    s_saidThrown.IsLeft = aIsLeft;
    s_saidThrown.At = std::chrono::steady_clock::now();
}

void OnWeaponReturned(TESForm*, bool aIsLeft, void*, void*)
{
    std::scoped_lock _{s_throwLock};
    s_returned[aIsLeft ? 0 : 1] = true;
}

VRModApis::HiggsInterface* s_pHiggs = nullptr;
VRModApis::PlanckInterface* s_pPlanck = nullptr;
VRModApis::VrikInterface* s_pVrik = nullptr;
bool s_done = false;

std::mutex s_eventLock;
std::vector<VRModApis::HandEvent> s_events;

// What each hand (0 right, 1 left) is taken to hold after HIGGS's reports, and since when HIGGS says it does not. Main thread only.
uint32_t s_taken[2]{};
std::chrono::steady_clock::time_point s_goneSince[2]{};

void Queue(VRModApis::HandEvent&& aEvent) noexcept
{
    std::scoped_lock _{s_eventLock};
    // Nobody takes them while not in the game; the newest are the ones worth having.
    if (s_events.size() >= 64)
        s_events.erase(s_events.begin());
    s_events.push_back(std::move(aEvent));
}

const char* Side(bool aIsLeft) noexcept
{
    return aIsLeft ? "left" : "right";
}

// TEMPORARY: what HIGGS reports, in the log, until the shared grab is built on it.
// HIGGS calls this just before it marks the hand as holding, and it gives the grabbed node's name only for a hand that holds.
// So the name is read on the next update (FinishEvents), and the grab is logged there.
void OnGrabbed(bool aIsLeft, TESObjectREFR* apRefr)
{
    const VRModApis::Transform cGrab = s_pHiggs->GetGrabTransform(aIsLeft);
    float fingers[5]{};
    s_pHiggs->GetFingerValues(aIsLeft, fingers);

    VRModApis::HandEvent event;
    event.Event = VRModApis::HandEvent::kGrabbed;
    event.IsLeft = aIsLeft;
    event.FormId = apRefr ? apRefr->formID : 0;
    std::copy(std::begin(cGrab.Translation), std::end(cGrab.Translation), event.Grab);
    std::copy(std::begin(fingers), std::end(fingers), event.Fingers);
    std::copy(std::begin(cGrab.Rotation), std::end(cGrab.Rotation), event.GrabRotation);
    Queue(std::move(event));
}

void OnDropped(bool aIsLeft, TESObjectREFR* apRefr)
{
    Queue({VRModApis::HandEvent::kLetGo, aIsLeft, apRefr ? apRefr->formID : 0});
}

void OnPulled(bool aIsLeft, TESObjectREFR* apRefr)
{
    Queue({VRModApis::HandEvent::kPulled, aIsLeft, apRefr ? apRefr->formID : 0});
}
// Each new event gets its time, a grab also its node's name, and all go into the log, in the order HIGGS said them.
void FinishEvents() noexcept
{
    std::scoped_lock _{s_eventLock};
    for (auto& event : s_events)
    {
        if (event.Ready)
            continue;
        event.Ready = true;
        event.At = std::chrono::steady_clock::now();

        // What each hand is taken to hold, for the check below.
        if (event.Event == VRModApis::HandEvent::kGrabbed)
        {
            s_taken[event.IsLeft] = event.FormId;
            s_goneSince[event.IsLeft] = {};
        }
        else if (event.Event == VRModApis::HandEvent::kLetGo && s_taken[event.IsLeft] == event.FormId)
            s_taken[event.IsLeft] = 0;

        if (event.Event == VRModApis::HandEvent::kLetGo)
            spdlog::info("Higgs: {} hand let go of {:X}", Side(event.IsLeft), event.FormId);
        else if (event.Event == VRModApis::HandEvent::kPulled)
            spdlog::info("Higgs: {} hand pulled {:X} toward itself", Side(event.IsLeft), event.FormId);
        else
        {
            const TESObjectREFR* pHeld = s_pHiggs->GetGrabbedObject(event.IsLeft);
            const bool cStillHeld = pHeld && pHeld->formID == event.FormId;
            if (cStillHeld)
            {
                const VRModApis::NodeName cNode = s_pHiggs->GetGrabbedNodeName(event.IsLeft);
                event.NodeName = cNode.pData ? cNode.pData : "";
            }
            spdlog::info("Higgs: {} hand grabbed {:X} at its node '{}'{}; node in hand space at ({:.1f}, {:.1f}, {:.1f}); fingers {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}", Side(event.IsLeft),
                         event.FormId, event.NodeName, cStillHeld ? "" : " (let go again before the name could be read)", event.Grab[0], event.Grab[1], event.Grab[2], event.Fingers[0],
                         event.Fingers[1], event.Fingers[2], event.Fingers[3], event.Fingers[4]);
        }
    }

    // HIGGS does not say every end of a hold: of 152 grabs in a session of 2026-11-15 only 89 were followed by its dropped
    // callback (a thing passed to the other hand, taken by another player, stashed or used up ends without one). The server
    // and the other games then took the hand to hold on, and treated the thing as held by two. So the hand is asked: a
    // hand that has not held its thing for 0.3 s has let go, and that is said like HIGGS's own report.
    const auto cNow = std::chrono::steady_clock::now();
    for (const bool cIsLeft : {false, true})
    {
        if (!s_taken[cIsLeft])
            continue;
        const TESObjectREFR* pHeld = s_pHiggs->GetGrabbedObject(cIsLeft);
        if (pHeld && pHeld->formID == s_taken[cIsLeft])
        {
            s_goneSince[cIsLeft] = {};
            continue;
        }
        if (s_goneSince[cIsLeft] == std::chrono::steady_clock::time_point{})
            s_goneSince[cIsLeft] = cNow;
        else if (cNow - s_goneSince[cIsLeft] > std::chrono::milliseconds(300))
        {
            spdlog::info("Higgs: {} hand no longer holds {:X}, and HIGGS did not say it let go; taken as let go", Side(cIsLeft), s_taken[cIsLeft]);
            VRModApis::HandEvent event;
            event.Event = VRModApis::HandEvent::kLetGo;
            event.IsLeft = cIsLeft;
            event.FormId = s_taken[cIsLeft];
            event.Ready = true;
            event.At = cNow;
            if (s_events.size() >= 64)
                s_events.erase(s_events.begin());
            s_events.push_back(std::move(event));
            s_taken[cIsLeft] = 0;
            s_goneSince[cIsLeft] = {};
        }
    }
}
} // namespace

namespace VRModApis
{
void Poll() noexcept
{
    if (s_done)
    {
        if (s_pHiggs)
            FinishEvents();
        return;
    }

    const auto cNow = std::chrono::steady_clock::now();
    static const auto s_start = cNow;
    static auto s_next = cNow;
    static bool s_saidMissing = false;
    if (cNow < s_next)
        return;
    s_next = cNow + std::chrono::seconds(1);

    const BridgeState* pState = nullptr;
    if (const HMODULE cBridge = GetModuleHandleW(L"urSovngardeBridge.dll"))
    {
        using TState = const void* (*)();
        if (const auto cGet = reinterpret_cast<TState>(GetProcAddress(cBridge, "urSovngardeBridge_State")))
            pState = static_cast<const BridgeState*>(cGet());
    }

    if (!pState || pState->Size < kFirstBridgeSize || pState->Stage < 3)
    {
        // Said on the screen too, every two minutes: without the bridge this game guesses what its hands hold, a thing another player
        // moves cannot be taken, and the other players never learn what this player holds. A player does not read the log.
        static auto s_nextOnScreen = s_start + std::chrono::seconds(45);
        if (cNow >= s_nextOnScreen)
        {
            s_nextOnScreen = cNow + std::chrono::minutes(2);
            Utils::ShowHudMessage(pState ? "urSovngarde: the VR bridge did not start. Grabbing with other players will not work. Is SKSE VR running?"
                                         : "urSovngarde: urSovngardeBridge.dll is missing. Install 'urSovngarde mod' as a mod (Data/SKSE/Plugins). Grabbing with other players will not work.");
        }
        if (!s_saidMissing && cNow - s_start > std::chrono::seconds(30))
        {
            s_saidMissing = true;
            if (!pState)
                spdlog::warn("VRModApis: urSovngardeBridge.dll is not loaded after 30 s (it belongs in Data/SKSE/Plugins); HIGGS, PLANCK and VRIK are not asked, grabbing is guessed");
            else
                spdlog::warn("VRModApis: urSovngardeBridge.dll is loaded but stopped at stage {} (1 SKSE asked, 2 SKSE loaded it, 3 mods asked); grabbing is guessed", pState->Stage);
        }
        return;
    }

    s_done = true;
    s_pHiggs = static_cast<HiggsInterface*>(pState->Higgs);
    s_pPlanck = static_cast<PlanckInterface*>(pState->Planck);
    s_pVrik = static_cast<VrikInterface*>(pState->Vrik);

    const auto cBuild = [](auto* apInterface) { return apInterface ? fmt::format("build {}", apInterface->GetBuildNumber()) : std::string("not there"); };
    spdlog::info("VRModApis: SKSE {:#x} on runtime {:#x}; HIGGS {}, PLANCK {}, VRIK {}", pState->SkseVersion, pState->RuntimeVersion, cBuild(s_pHiggs), cBuild(s_pPlanck),
                 cBuild(s_pVrik));

    // Weapon Throw VR is no requirement: without it, or with a bridge from before it was asked, thrown weapons are simply not shown.
    if (auto* pThrow = pState->Size >= sizeof(BridgeState) ? static_cast<WeaponThrowInterface*>(pState->WeaponThrow) : nullptr)
    {
        pThrow->RegisterThrowCallback(OnWeaponThrown);
        pThrow->RegisterReturnCallback(OnWeaponReturned);
        spdlog::info("VRModApis: Weapon Throw VR build {} answered; thrown weapons are shown to the other players", pThrow->GetBuildNumber());
    }
    else
        spdlog::info("VRModApis: Weapon Throw VR did not answer ({}); thrown weapons are not shown to the other players",
                     pState->Size >= sizeof(BridgeState) ? "not installed, or a version without an interface" : "urSovngardeBridge.dll is an older one");

    if (s_pHiggs)
    {
        s_pHiggs->AddGrabbedCallback(OnGrabbed);
        s_pHiggs->AddDroppedCallback(OnDropped);
        s_pHiggs->AddPulledCallback(OnPulled);
    }
}

void TakeHandEvents(std::vector<HandEvent>& aEvents) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    std::scoped_lock _{s_eventLock};
    auto it = s_events.begin();
    for (; it != s_events.end() && it->Ready; ++it)
    {
        if (cNow - it->At < std::chrono::seconds(2))
            aEvents.push_back(std::move(*it));
    }
    s_events.erase(s_events.begin(), it);
}

bool TakeThrown(Thrown& aThrown) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    std::scoped_lock _{s_throwLock};
    // The mod queues the shot and then reports the throw, so either can come first; two seconds apart at most.
    if (s_saidThrown.WeaponFormId && cNow - s_saidThrown.At > std::chrono::seconds(2))
        s_saidThrown.WeaponFormId = 0;
    if (s_helperLaunch.Handle && cNow - s_helperLaunch.At > std::chrono::seconds(2))
        s_helperLaunch.Handle = 0;
    if (!s_saidThrown.WeaponFormId || !s_helperLaunch.Handle)
        return false;

    aThrown.WeaponFormId = s_saidThrown.WeaponFormId;
    aThrown.IsLeft = s_saidThrown.IsLeft;
    aThrown.ProjectileHandle = s_helperLaunch.Handle;
    s_saidThrown.WeaponFormId = 0;
    s_helperLaunch.Handle = 0;
    return true;
}

bool TakeReturned(bool& aIsLeft) noexcept
{
    std::scoped_lock _{s_throwLock};
    for (int side = 0; side < 2; ++side)
    {
        if (s_returned[side])
        {
            s_returned[side] = false;
            aIsLeft = side == 0;
            return true;
        }
    }
    return false;
}

void NoteHelperLaunch(const uint32_t aProjectileHandle) noexcept
{
    std::scoped_lock _{s_throwLock};
    s_helperLaunch.Handle = aProjectileHandle;
    s_helperLaunch.At = std::chrono::steady_clock::now();
}

uint32_t HeldByHand(const uint8_t aSide) noexcept
{
    if (!s_pHiggs || aSide > 1)
        return 0;
    const TESObjectREFR* pHeld = s_pHiggs->GetGrabbedObject(aSide == 0);
    return pHeld ? pHeld->formID : 0;
}

bool KnowsGrabs() noexcept
{
    return s_pHiggs != nullptr;
}

HiggsInterface* Higgs() noexcept
{
    return s_pHiggs;
}

PlanckInterface* Planck() noexcept
{
    return s_pPlanck;
}

VrikInterface* Vrik() noexcept
{
    return s_pVrik;
}
} // namespace VRModApis
