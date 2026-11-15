#pragma once

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
};

//! Moves what HIGGS said since the last call into aEvents, oldest first. HIGGS may say it on another thread than the caller's.
void TakeHandEvents(std::vector<HandEvent>& aEvents) noexcept;

//! Looks for the bridge until it has the interfaces, at most once a second; call it every frame from the game's update.
//! Says once in the log what arrived, or after 30 s that the bridge is not there.
void Poll() noexcept;

//! Null until Poll has found them, and for good if that mod is not installed.
HiggsInterface* Higgs() noexcept;
PlanckInterface* Planck() noexcept;
VrikInterface* Vrik() noexcept;
} // namespace VRModApis
