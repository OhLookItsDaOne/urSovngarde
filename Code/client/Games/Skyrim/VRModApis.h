#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

struct TESObjectREFR;
struct TESForm;

//! The mod interfaces of HIGGS, PLANCK and VRIK, as urSovngardeBridge.dll fetched them (Code/vr_bridge).
//!
//! The declarations below repeat the virtual functions of higgsinterface001.h and planckinterface001.h
//! (github.com/adamhynek/higgs and /activeragdoll, GPL-3) and vrikinterface001.h, in the same order, with this client's
//! types. A function this client does not call yet is still declared: its place in the order is what counts.
namespace VRModApis
{
//! Hand-to-grabbed-node transform as HIGGS returns it: the game's NiTransform (rotation rows, translation, scale).
struct Transform
{
    float Rotation[9];
    float Translation[3];
    float Scale;
};

//! A game string as HIGGS returns it by value. The destructor is there so that it is returned the way the game's own is.
//! The game's count on the string is not given back; a node name stays in the game's string pool anyway.
struct NodeName
{
    const char* pData = nullptr;
    NodeName() noexcept {}
    ~NodeName() {}
};

struct HiggsInterface
{
    using RefrCallback = void (*)(bool aIsLeft, TESObjectREFR* apRefr);
    using FormCallback = void (*)(bool aIsLeft, TESForm* apForm);
    using CollisionCallback = void (*)(bool aIsLeft, float aMass, float aSeparatingVelocity);
    using NoArgCallback = void (*)();
    using FilterCallback = uint8_t (*)(void* apCollisionFilter, uint32_t aFilterInfoA, uint32_t aFilterInfoB);
    using PrePhysicsStepCallback = void (*)(void* apWorld);

    virtual unsigned int GetBuildNumber() = 0;
    virtual void AddPulledCallback(RefrCallback aCallback) = 0;
    virtual void AddGrabbedCallback(RefrCallback aCallback) = 0;
    virtual void AddDroppedCallback(RefrCallback aCallback) = 0;
    virtual void AddStashedCallback(FormCallback aCallback) = 0;
    virtual void AddConsumedCallback(FormCallback aCallback) = 0;
    virtual void AddCollisionCallback(CollisionCallback aCallback) = 0;
    virtual void GrabObject(TESObjectREFR* apObject, bool aIsLeft) = 0;
    virtual TESObjectREFR* GetGrabbedObject(bool aIsLeft) = 0;
    virtual bool IsHandInGrabbableState(bool aIsLeft) = 0;
    virtual void DisableHand(bool aIsLeft) = 0;
    virtual void EnableHand(bool aIsLeft) = 0;
    virtual bool IsDisabled(bool aIsLeft) = 0;
    virtual void DisableWeaponCollision(bool aIsLeft) = 0;
    virtual void EnableWeaponCollision(bool aIsLeft) = 0;
    virtual bool IsWeaponCollisionDisabled(bool aIsLeft) = 0;
    virtual bool IsTwoHanding() = 0;
    virtual void AddStartTwoHandingCallback(NoArgCallback aCallback) = 0;
    virtual void AddStopTwoHandingCallback(NoArgCallback aCallback) = 0;
    virtual bool CanGrabObject(bool aIsLeft) = 0;
    virtual void AddCollisionFilterComparisonCallback(FilterCallback aCallback) = 0;
    virtual void AddPrePhysicsStepCallback(PrePhysicsStepCallback aCallback) = 0;
    virtual uint64_t GetHiggsLayerBitfield() = 0;
    virtual void SetHiggsLayerBitfield(uint64_t aBitfield) = 0;
    virtual void* GetHandRigidBody(bool aIsLeft) = 0;
    virtual void* GetWeaponRigidBody(bool aIsLeft) = 0;
    virtual void* GetGrabbedRigidBody(bool aIsLeft) = 0;
    virtual void ForceWeaponCollisionEnabled(bool aIsLeft) = 0;
    virtual bool IsHoldingObject(bool aIsLeft) = 0;
    virtual void GetFingerValues(bool aIsLeft, float aValues[5]) = 0;
    virtual void AddPreVrikPreHiggsCallback(NoArgCallback aCallback) = 0;
    virtual void AddPreVrikPostHiggsCallback(NoArgCallback aCallback) = 0;
    virtual void AddPostVrikPreHiggsCallback(NoArgCallback aCallback) = 0;
    virtual void AddPostVrikPostHiggsCallback(NoArgCallback aCallback) = 0;
    virtual bool Deprecated1(const void* acpName, double& aOut) = 0;
    virtual bool Deprecated2(const void* acpName, double aValue) = 0;
    virtual Transform GetGrabTransform(bool aIsLeft) = 0;
    virtual void SetGrabTransform(bool aIsLeft, const Transform& acTransform) = 0;
    virtual bool GetSettingDouble(const char* acpName, double& aOut) = 0;
    virtual bool SetSettingDouble(const char* acpName, double aValue) = 0;
    virtual NodeName GetGrabbedNodeName(bool aIsLeft) = 0;
};

struct PlanckInterface
{
    virtual unsigned int GetBuildNumber() = 0;
    virtual bool Deprecated1(const void* acpName, double& aOut) = 0;
    virtual bool Deprecated2(const void* acpName, double aValue) = 0;
    virtual void AddIgnoredActor(void* apActor) = 0;
    virtual void RemoveIgnoredActor(void* apActor) = 0;
    virtual void AddAggressionIgnoredActor(void* apActor) = 0;
    virtual void RemoveAggressionIgnoredActor(void* apActor) = 0;
    virtual void SetAggressionLowTopic(void* apActor, void* apTopic) = 0;
    virtual void SetAggressionHighTopic(void* apActor, void* apTopic) = 0;
    virtual void AddRagdollCollisionIgnoredActor(void* apActor) = 0;
    virtual void RemoveRagdollCollisionIgnoredActor(void* apActor) = 0;
    // GetLastHitData returns a structure by value; not called yet, so only its place is kept. Do not call it through this.
    virtual void GetLastHitDataPlaceholder() = 0;
    virtual void* GetCurrentHitEvent() = 0;
    virtual bool GetSettingDouble(const char* acpName, double& aOut) = 0;
    virtual bool SetSettingDouble(const char* acpName, double aValue) = 0;
};

struct VrikInterface
{
    virtual unsigned int GetBuildNumber() = 0;
    virtual double GetSettingDouble(const char* acpName) = 0;
    virtual void SetSettingDouble(const char* acpName, double aValue) = 0;
    virtual void GetSettingString(const char* acpName, char* apBuffer, size_t aBufferSize) = 0;
    virtual void SetSettingString(const char* acpName, const char* acpValue) = 0;
    virtual void SaveSettings() = 0;
    virtual void RestoreSettings() = 0;
    virtual void AddGestureAction(void (*aCallback)(int), const char* acpMenuName) = 0;
    virtual void BeginGestureProfile() = 0;
    virtual void SetProfileAction(int aGestureNumber, void (*aCallback)(int)) = 0;
    virtual void EndGestureProfile() = 0;
    virtual float GetFingerPos(bool aIsLeft, int aFingerIndex) = 0;
    virtual void SetFingerRange(bool aIsLeft, float aMin0, float aMax0, float aMin1, float aMax1, float aMin2, float aMax2, float aMin3, float aMax3, float aMin4, float aMax4) = 0;
    virtual void RestoreFingers(bool aIsLeft) = 0;
};

//! Weapon Throw VR's interface (Shizof; WeaponThrowVRinterface001.h in that mod's published source), as far as it is used.
struct WeaponThrowInterface
{
    using ThrowCallback = void (*)(TESForm* apWeapon, bool aIsLeft, void* apEntryData);
    using ReturnCallback = void (*)(TESForm* apWeapon, bool aIsLeft, void* apExtraList, void* apExtras);

    virtual unsigned int GetBuildNumber() = 0;
    // DisableAutoReturnForThrownWeaponNode takes a smart pointer by value; never called, only its place is kept. Do not call it through this.
    virtual void Reserved1() = 0;
    virtual void RegisterThrowCallback(ThrowCallback aCallback) = 0;
    virtual void RegisterReturnCallback(ReturnCallback aCallback) = 0;
};

//! A weapon this player threw with Weapon Throw VR, once both the mod's report and the projectile it fired are known.
struct Thrown
{
    uint32_t WeaponFormId{};
    bool IsLeft{};
    //! The handle of the projectile that is the flying weapon.
    uint32_t ProjectileHandle{};
};
//! True, and the throw, once for each throw.
bool TakeThrown(Thrown& aThrown) noexcept;
//! A launch from a helper that no throw report claimed: the flight of a thrown weapon back to the hand, which Weapon Throw VR
//! fires as a second projectile after deleting the first. Only asked for while a thrown weapon's projectile has just gone.
bool TakeReturnLaunch(uint32_t& aProjectileHandle) noexcept;
//! True once for each time the mod said a thrown weapon came back to a hand.
bool TakeReturned(bool& aIsLeft) noexcept;
//! Called by the projectile launch hook for a launch from a reference that is no character, with a weapon and no spell:
//! that is how Weapon Throw VR fires (Papyrus Fire from its hidden helper). Kept two seconds, to be paired with the mod's report.
void NoteHelperLaunch(uint32_t aProjectileHandle) noexcept;

//! One thing HIGGS said about a hand of this player. Position and fingers are filled for a grab only.
struct HandEvent
{
    enum : uint8_t
    {
        kGrabbed,
        kLetGo,
        kPulled
    };
    uint8_t Event{};
    bool IsLeft{};
    uint32_t FormId{};
    std::string NodeName{};
    float Grab[3]{};
    float Fingers[5]{};
    //! The rotation part of the same hand-to-node transform, as the game keeps it: three rows of three.
    float GrabRotation[9]{};
    //! Set by Poll once the grabbed node's name has been read and the event is in the log; only then is it handed out.
    bool Ready{};
    std::chrono::steady_clock::time_point At{};
};

//! Moves what HIGGS said since the last call into aEvents, oldest first. HIGGS may say it on another thread than the caller's.
//! Anything older than two seconds is left out: it happened while nobody asked, such as before the player connected.
void TakeHandEvents(std::vector<HandEvent>& aEvents) noexcept;

//! Looks for the bridge until it has the interfaces, at most once a second; call it every frame from the game's update.
//! Says once in the log what arrived, or after 30 s that the bridge is not there.
void Poll() noexcept;

//! The reference HIGGS says this player's hand holds right now (0 left, 1 right), or 0: nothing, or HIGGS is not there.
//! Ask KnowsGrabs first: without HIGGS a 0 says nothing. Call it on the game's main thread.
//! HIGGS takes only what this game simulates; a thing held still here for another player's game never shows up.
uint32_t HeldByHand(uint8_t aSide) noexcept;
bool KnowsGrabs() noexcept;

//! Null until Poll has found them, and for good if that mod is not installed.
HiggsInterface* Higgs() noexcept;
PlanckInterface* Planck() noexcept;
VrikInterface* Vrik() noexcept;
} // namespace VRModApis
