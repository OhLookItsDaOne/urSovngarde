#include <TiltedOnlinePCH.h>

#include "VRModApis.h"

#include <TESObjectREFR.h>

#include <chrono>

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

const char* Side(bool aIsLeft) noexcept
{
    return aIsLeft ? "left" : "right";
}

// TEMPORARY: what HIGGS reports, in the log, until the shared grab is built on it.
void OnGrabbed(bool aIsLeft, TESObjectREFR* apRefr)
{
    const VRModApis::Transform cGrab = s_pHiggs->GetGrabTransform(aIsLeft);
    const VRModApis::NodeName cNode = s_pHiggs->GetGrabbedNodeName(aIsLeft);
    float fingers[5]{};
    s_pHiggs->GetFingerValues(aIsLeft, fingers);
    spdlog::info("Higgs: {} hand grabbed {:X} at its node '{}'; node in hand space at ({:.1f}, {:.1f}, {:.1f}), scale {:.2f}; fingers {:.2f} {:.2f} {:.2f} {:.2f} {:.2f}",
                 Side(aIsLeft), apRefr ? apRefr->formID : 0, cNode.pData ? cNode.pData : "", cGrab.Translation[0], cGrab.Translation[1], cGrab.Translation[2], cGrab.Scale,
                 fingers[0], fingers[1], fingers[2], fingers[3], fingers[4]);
}

void OnDropped(bool aIsLeft, TESObjectREFR* apRefr)
{
    spdlog::info("Higgs: {} hand let go of {:X}", Side(aIsLeft), apRefr ? apRefr->formID : 0);
}

void OnPulled(bool aIsLeft, TESObjectREFR* apRefr)
{
    spdlog::info("Higgs: {} hand pulled {:X} toward itself", Side(aIsLeft), apRefr ? apRefr->formID : 0);
}
} // namespace

namespace VRModApis
{
void Poll() noexcept
{
    if (s_done)
        return;

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
