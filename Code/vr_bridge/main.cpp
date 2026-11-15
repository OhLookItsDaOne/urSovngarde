// urSovngardeBridge.dll, an SKSE VR plugin (Data/SKSE/Plugins).
//
// Weapon Throw VR (Shizof, nexusmods.com/skyrimspecialedition/mods/31374) hands out its interface the same way; the message
// number and receiver name are those of the WeaponThrowVRinterface001 files in that mod's published source.
//
// HIGGS, PLANCK and VRIK each hand out an interface object to other mods, but only through SKSE's messaging, and only
// an SKSE plugin has the plugin handle that messaging asks for. The co-op client is not an SKSE plugin: the launcher
// carries it. So this plugin asks the three mods for their interfaces once all plugins are loaded, and keeps the
// pointers where the client can read them (urSovngardeBridge_State, read in Code/client/Games/Skyrim/VRModApis.cpp).
//
// The message numbers and receiver names are those of the mods' published headers: higgsinterface001, planckinterface001
// (github.com/adamhynek/higgs and /activeragdoll, GPL-3) and vrikinterface001 (published in the HIGGS repository).
// The SKSE structures are the first members of SKSEInterface, PluginInfo and SKSEMessagingInterface in SKSE's PluginAPI.h.

#include <cstdint>

namespace
{
using PluginHandle = uint32_t;

struct SKSEInterface
{
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    void* (*QueryInterface)(uint32_t aId);
    PluginHandle (*GetPluginHandle)();
    uint32_t (*GetReleaseIndex)();
};

struct PluginInfo
{
    uint32_t infoVersion;
    const char* name;
    uint32_t version;
};

struct SKSEMessagingInterface
{
    struct Message
    {
        const char* sender;
        uint32_t type;
        uint32_t dataLen;
        void* data;
    };
    using EventCallback = void (*)(Message* apMessage);

    uint32_t interfaceVersion;
    bool (*RegisterListener)(PluginHandle aListener, const char* acpSender, EventCallback aHandler);
    bool (*Dispatch)(PluginHandle aSender, uint32_t aMessageType, void* apData, uint32_t aDataLen, const char* acpReceiver);
    void* (*GetEventDispatcher)(uint32_t aDispatcherId);
};

constexpr uint32_t kInterfaceMessaging = 5;
constexpr uint32_t kMessagePostPostLoad = 1;

// What each of the three mods fills in when asked for its interface.
struct InterfaceRequest
{
    void* (*GetApi)(unsigned int aRevision) = nullptr;
};

// Read by the client. Only ever grows at its end; Size says how much of it this build has.
struct BridgeState
{
    uint32_t Size = sizeof(BridgeState);
    // 0 loaded by Windows, 1 SKSE asked about the plugin, 2 SKSE loaded it, 3 all plugins are loaded and the mods were asked.
    uint32_t Stage = 0;
    uint32_t SkseVersion = 0;
    uint32_t RuntimeVersion = 0;
    void* Higgs = nullptr;
    void* Planck = nullptr;
    void* Vrik = nullptr;
    // Since bridge version 2. Null without Weapon Throw VR, or with a version of it that has no interface.
    void* WeaponThrow = nullptr;
};

BridgeState s_state;
PluginHandle s_handle = 0;
SKSEMessagingInterface* s_pMessaging = nullptr;

void* Ask(uint32_t aMessageType, const char* acpReceiver)
{
    InterfaceRequest request;
    // The length is that of a pointer, as in the mods' own fetch functions.
    s_pMessaging->Dispatch(s_handle, aMessageType, &request, sizeof(void*), acpReceiver);
    return request.GetApi ? request.GetApi(1) : nullptr;
}

void OnSkseMessage(SKSEMessagingInterface::Message* apMessage)
{
    if (!apMessage || apMessage->type != kMessagePostPostLoad || s_state.Stage >= 3)
        return;

    // HIGGS and VRIK listen from PostLoad on, PLANCK from PostPostLoad on at the latest by its header.
    s_state.Higgs = Ask(0xF9279A57, "HIGGS");
    s_state.Planck = Ask(0x92F38745, "PLANCK");
    s_state.Vrik = Ask(0xF2AFAEE6, "VRIK");
    // Weapon Throw VR listens from PostLoad on.
    s_state.WeaponThrow = Ask(0xFA07A15D, "WeaponThrowVR");
    s_state.Stage = 3;
}
} // namespace

extern "C"
{
__declspec(dllexport) bool SKSEPlugin_Query(const SKSEInterface* apSkse, PluginInfo* apInfo)
{
    apInfo->infoVersion = 1;
    apInfo->name = "urSovngardeBridge";
    apInfo->version = 2;

    s_handle = apSkse->GetPluginHandle();
    s_state.SkseVersion = apSkse->skseVersion;
    s_state.RuntimeVersion = apSkse->runtimeVersion;
    s_state.Stage = 1;
    return apSkse->isEditor == 0;
}

__declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* apSkse)
{
    s_pMessaging = static_cast<SKSEMessagingInterface*>(apSkse->QueryInterface(kInterfaceMessaging));
    if (!s_pMessaging)
        return false;

    s_state.Stage = 2;
    return s_pMessaging->RegisterListener(s_handle, "SKSE", OnSkseMessage);
}

__declspec(dllexport) const void* urSovngardeBridge_State()
{
    return &s_state;
}
}
