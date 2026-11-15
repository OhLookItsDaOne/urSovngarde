#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VRHaptics.h>

#ifdef SKYRIMVR

#include <openvr/openvr.h>

#include <chrono>

namespace
{
using namespace std::chrono_literals;

// The game has already brought OpenVR up through its own openvr_api.dll, and asking that DLL shares the game's session.
// A second copy of the DLL would not be connected to SteamVR.
vr::IVRSystem* GetVRSystem() noexcept
{
    static vr::IVRSystem* s_pSystem = nullptr;
    static bool s_tried = false;

    if (s_pSystem)
        return s_pSystem;

    // OpenVR can come up after the first frames, so a failure is retried for a minute before giving up.
    static std::chrono::steady_clock::time_point s_giveUpAt{};
    const auto now = std::chrono::steady_clock::now();
    if (!s_tried)
    {
        s_tried = true;
        s_giveUpAt = now + 60s;
    }
    if (now >= s_giveUpAt)
        return nullptr;

    HMODULE hOpenVR = GetModuleHandleW(L"openvr_api.dll");
    if (!hOpenVR)
        return nullptr;

    using TGetGenericInterface = void*(const char*, vr::EVRInitError*);
    auto* pGetInterface = reinterpret_cast<TGetGenericInterface*>(GetProcAddress(hOpenVR, "VR_GetGenericInterface"));
    if (!pGetInterface)
        return nullptr;

    vr::EVRInitError error = vr::VRInitError_None;
    s_pSystem = static_cast<vr::IVRSystem*>(pGetInterface(vr::IVRSystem_Version, &error));
    return s_pSystem;
}
} // namespace

namespace VRHaptics
{
bool Pulse(const bool aRightHand, uint16_t aDurationMicroSec) noexcept
{
    vr::IVRSystem* pSystem = GetVRSystem();
    if (!pSystem)
        return false;

    const vr::TrackedDeviceIndex_t device =
        pSystem->GetTrackedDeviceIndexForControllerRole(aRightHand ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand);
    if (device == vr::k_unTrackedDeviceIndexInvalid)
        return false;

    // OpenVR caps a single call at just under 4 ms and silently truncates anything longer, so it is clamped here.
    if (aDurationMicroSec > 3999)
        aDurationMicroSec = 3999;

    pSystem->TriggerHapticPulse(device, 0, aDurationMicroSec);
    return true;
}

Grips GripsHeld() noexcept
{
    // The grip button, or Axis2, which Oculus Touch controllers report for the grip.
    constexpr uint64_t cGripMask = (1ull << vr::k_EButton_Grip) | (1ull << vr::k_EButton_Axis2);

    Grips grips{};

    if (vr::IVRSystem* pSystem = GetVRSystem())
    {
        for (const auto cRole : {vr::TrackedControllerRole_LeftHand, vr::TrackedControllerRole_RightHand})
        {
            const vr::TrackedDeviceIndex_t cDevice = pSystem->GetTrackedDeviceIndexForControllerRole(cRole);
            vr::VRControllerState_t state{};
            if (cDevice != vr::k_unTrackedDeviceIndexInvalid && pSystem->GetControllerState(cDevice, &state, sizeof(state)) && (state.ulButtonPressed & cGripMask) != 0)
                ++grips.OpenVR;
        }
    }

    // BSInputDeviceManager keeps its devices in a list at 0x60; slots 3 to 8 are the VR controllers, two per headset type.
    // Only a BSOpenVRControllerDevice (known by its vtable) is read: its last poll's state sits at 0xC0.
    POINTER_SKYRIMSE(const uint8_t*, s_inputDeviceManager, 516574, 516574);
    POINTER_SKYRIMSE(void, s_controllerVtable, 9000014, 9000014);
    const uintptr_t cControllerVtable = reinterpret_cast<uintptr_t>(s_controllerVtable.GetPtr());
    const uint8_t* const* ppManager = s_inputDeviceManager.Get();
    if (const uint8_t* pManager = ppManager && cControllerVtable ? *ppManager : nullptr)
    {
        for (int slot = 3; slot <= 8; ++slot)
        {
            const uint8_t* pDevice = *reinterpret_cast<const uint8_t* const*>(pManager + 0x60 + slot * sizeof(void*));
            if (!pDevice || *reinterpret_cast<const uintptr_t*>(pDevice) != cControllerVtable)
                continue;
            const auto& cState = *reinterpret_cast<const vr::VRControllerState_t*>(pDevice + 0xC0);
            if ((cState.ulButtonPressed & cGripMask) != 0)
                ++grips.Game;
        }
    }

    return grips;
}

bool HandPressed(const bool aRightHand) noexcept
{
    // Grip, Axis2 (the grip on Oculus Touch) and Axis1 (the trigger).
    constexpr uint64_t cMask = (1ull << vr::k_EButton_Grip) | (1ull << vr::k_EButton_Axis2) | (1ull << vr::k_EButton_Axis1);

    vr::IVRSystem* pSystem = GetVRSystem();
    if (!pSystem)
        return false;
    const vr::TrackedDeviceIndex_t cDevice =
        pSystem->GetTrackedDeviceIndexForControllerRole(aRightHand ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand);
    vr::VRControllerState_t state{};
    return cDevice != vr::k_unTrackedDeviceIndexInvalid && pSystem->GetControllerState(cDevice, &state, sizeof(state)) && (state.ulButtonPressed & cMask) != 0;
}

} // namespace VRHaptics

namespace
{
vr::IVRCompositor* GetVRCompositor() noexcept
{
    static vr::IVRCompositor* s_pCompositor = nullptr;
    if (s_pCompositor || !GetVRSystem())
        return s_pCompositor;

    using TGetGenericInterface = void*(const char*, vr::EVRInitError*);
    const HMODULE hOpenVR = GetModuleHandleW(L"openvr_api.dll");
    auto* pGetInterface = hOpenVR ? reinterpret_cast<TGetGenericInterface*>(GetProcAddress(hOpenVR, "VR_GetGenericInterface")) : nullptr;
    if (!pGetInterface)
        return nullptr;

    vr::EVRInitError error = vr::VRInitError_None;
    s_pCompositor = static_cast<vr::IVRCompositor*>(pGetInterface(vr::IVRCompositor_Version, &error));
    return s_pCompositor;
}

struct FrameTimingSum
{
    uint32_t Frames = 0;
    double GpuMs = 0.0;
    float GpuMaxMs = 0.f;
    double CpuMs = 0.0;
    // Frames SteamVR had to show more than once, because the next one was not ready in time.
    uint32_t Late = 0;
    uint32_t Dropped = 0;
    uint32_t Missed = 0;
};
FrameTimingSum s_timing;
uint32_t s_lastFrameIndex = 0;
} // namespace

namespace VRHaptics
{
void SampleFrameTiming() noexcept
{
    vr::IVRCompositor* pCompositor = GetVRCompositor();
    if (!pCompositor)
        return;

    static vr::Compositor_FrameTiming s_frames[128];
    for (auto& frame : s_frames)
        frame.m_nSize = sizeof(vr::Compositor_FrameTiming);
    const uint32_t cCount = pCompositor->GetFrameTimings(s_frames, static_cast<uint32_t>(std::size(s_frames)));

    uint32_t newest = s_lastFrameIndex;
    uint32_t oldest = 0xFFFFFFFF;
    for (uint32_t i = 0; i < cCount; ++i)
    {
        const auto& frame = s_frames[i];
        oldest = std::min(oldest, frame.m_nFrameIndex);
        if (frame.m_nFrameIndex <= s_lastFrameIndex)
            continue;
        newest = std::max(newest, frame.m_nFrameIndex);
        ++s_timing.Frames;
        s_timing.GpuMs += frame.m_flTotalRenderGpuMs;
        s_timing.GpuMaxMs = std::max(s_timing.GpuMaxMs, frame.m_flTotalRenderGpuMs);
        s_timing.CpuMs += frame.m_flClientFrameIntervalMs;
        s_timing.Late += frame.m_nNumFramePresents > 1;
        s_timing.Dropped += frame.m_nNumDroppedFrames;
    }
    // Frames between the last one counted and the oldest SteamVR still had were not seen.
    if (s_lastFrameIndex != 0 && cCount != 0 && oldest > s_lastFrameIndex + 1)
        s_timing.Missed += oldest - s_lastFrameIndex - 1;
    s_lastFrameIndex = newest;
}

std::string TakeFrameTimingSummary() noexcept
{
    const FrameTimingSum cSum = s_timing;
    s_timing = {};
    if (cSum.Frames == 0)
        return {};

    float hz = 0.f;
    if (vr::IVRSystem* pSystem = GetVRSystem())
        hz = pSystem->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float);

    return fmt::format("; SteamVR: headset {:.0f} Hz ({:.1f} ms a frame), graphics card avg {:.1f} ms, most {:.1f} ms, game frame avg {:.1f} ms, {} of {} frames shown again because the "
                       "next was late, {} dropped{}",
                       hz, hz > 0.f ? 1000.f / hz : 0.f, cSum.GpuMs / cSum.Frames, cSum.GpuMaxMs, cSum.CpuMs / cSum.Frames, cSum.Late, cSum.Frames, cSum.Dropped,
                       cSum.Missed ? fmt::format(" ({} frames not seen)", cSum.Missed) : std::string());
}
} // namespace VRHaptics

#else

namespace VRHaptics
{
bool Pulse(bool, uint16_t) noexcept
{
    return false;
}

Grips GripsHeld() noexcept
{
    return {};
}

bool HandPressed(bool) noexcept
{
    return false;
}

void SampleFrameTiming() noexcept
{
}

std::string TakeFrameTimingSummary() noexcept
{
    return {};
}
} // namespace VRHaptics

#endif
