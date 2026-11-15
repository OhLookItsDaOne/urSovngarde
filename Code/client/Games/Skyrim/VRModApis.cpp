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
};

VRModApis::HiggsInterface* s_pHiggs = nullptr;
VRModApis::PlanckInterface* s_pPlanck = nullptr;
VRModApis::VrikInterface* s_pVrik = nullptr;
bool s_done = false;

std::mutex s_eventLock;
std::vector<VRModApis::HandEvent> s_events;

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

    if (!pState || pState->Size < sizeof(BridgeState) || pState->Stage < 3)
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
