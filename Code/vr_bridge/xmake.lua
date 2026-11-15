-- A small SKSE VR plugin. HIGGS, PLANCK and VRIK hand their mod interfaces out through SKSE's messaging, which only
-- an SKSE plugin can use. The client is not one (the launcher carries it), so this plugin fetches the interfaces and
-- the client reads them from it (Code/client/Games/Skyrim/VRModApis.cpp).
target("VRBridge")
    set_kind("shared")
    set_group("Client")
    set_basename("urSovngardeBridge")
    add_files("*.cpp")
