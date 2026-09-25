#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "CireGrip.h"
#include "CireMonsterArt.generated.h"

class ACireGameMode;
class ACireHero;
class ACireMonster;
class UAnimSequence;
class UCireMonsterAnimInstance;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FCireNPCArchetype;

/**
 * Tripo batch-03 monster bodies (Docs/MonsterArt.md).
 * Data: Content/Data/NPCMeshes.tripo.json (bodies, scales, clips) + Content/Data/MonsterArt.json
 * (variants per archetype, clip timing windows, prop de-duplication, rims).
 */
namespace CireMonsterArt
{
    struct FClipWindow { float Start = 0.f, Contact = 0.f, End = 0.f, RecoverRate = 1.f; };
    struct FBody
    {
        FString Variant, MeshPath, BakedWeapon;
        float MeshScale = 1.f, Yaw = -90.f, HeightCm = 180.f;
        /** Role -> clip path: idle, walk, run, attack, attackAlt, hit, death. */
        TMap<FString, FString> Roles;
        /** Every clip of the body by its Tripo name (slash, war_cry, cast_a_spell...). */
        TMap<FString, FString> Clips;
        TSet<FName> DropPropBones;
        /** Optional re-skinned copy (MonsterArt.json "mesh"); same skeleton, so the original clips play on it. */
        FString MeshOverride;
        /** Per-bone prop adjustments on this body: scale multiplier and a replacement offset (cm, character frame). */
        TMap<FName, float> PropScale;
        TMap<FName, FVector> PropOffset;
        TMap<FName, FRotator> PropRotation;
        // world-dressing: free (CC0) creature bodies from RaceMeshes.free.json. Their clips are in place, so the
        // natural ground speeds come from data (cm/s at this mesh scale, actor scale 1); "quadruped" bodies expose
        // head/pelvis/feet/hands as sockets on animal bones; ReachCm bounds the pose (long bodies, tails, legs).
        FString Rig;
        bool bLockRoot = false;
        float WalkSpeedCm = 0.f, RunSpeedCm = 0.f, ReachCm = 0.f;
        /** Socket name -> bone, added to the mesh in memory when the body is applied (head, pelvis, hand_r...). */
        TMap<FName, FName> Sockets;
    };
    struct FArchetypeArt
    {
        TArray<FBody> Bodies;
        TMap<FName, FString> AbilityClips;
    };
    struct FData
    {
        bool bValid = false;
        TMap<FName, FArchetypeArt> Archetypes;
        TMap<FString, FClipWindow> Windows;
        FLinearColor EliteRim = FLinearColor(1.f, .55f, .12f), BossRim = FLinearColor(1.25f, .12f, .05f), EnragedRim = FLinearColor(1.6f, .18f, .04f);
        float DeathHoldSeconds = 2.6f, DeathSinkSeconds = 1.4f, DeathSinkCm = 70.f;
    };
    CIRESTEAMSURVIVAL_API const FData& Data(bool bReload = false);
    /** Art for an archetype; race units without their own art resolve their fallback body (monster-races). */
    CIRESTEAMSURVIVAL_API const FArchetypeArt* Find(FName ArchetypeId);
    /** monster-races: true when the archetype has its own art (NPCMeshes/RaceMeshes), not a borrowed fallback body. */
    CIRESTEAMSURVIVAL_API bool HasOwnBody(FName ArchetypeId);
    /** Timing window of a clip by its Tripo name ("slash"); falls back to the whole clip. */
    CIRESTEAMSURVIVAL_API FClipWindow Window(const UAnimSequence* Sequence);
#if !UE_BUILD_SHIPPING
    /** Native checks: bodies/clips resolve, grounding and heights, finite poses, props, swing timing, death cleanup, fallback. */
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}

/** Replicated swing timing plus local, collision-free presentation of a Tripo monster body. */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireMonsterArt : public UActorComponent
{
    GENERATED_BODY()
public:
    UCireMonsterArt();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ---- replicated ----
    /** Random per spawn (server); picks the body variant so every client draws the same one. */
    UPROPERTY(ReplicatedUsing=OnRep_BodySeed) uint16 BodySeed = 0;
    UPROPERTY(Replicated) uint8 SwingSerial = 0;
    UPROPERTY(Replicated) float SwingStartedAt = 0.f;
    UPROPERTY(Replicated) float SwingWindup = 0.f;
    UFUNCTION(NetMulticast, Reliable) void MulticastDeath();

    // ---- server: melee swings land on the contact frame ----
    /** Schedules a basic melee blow Windup seconds from now. Returns false when disabled (instant hit). */
    bool StartSwing(ACireHero* Victim, float Amount, const FString& AttackName, float Reach, float Period);
    /** Presentation only: a blow that already landed this frame (wall breaches). */
    void PresentInstantStrike();
    /** Releases the scheduled blow when due. Called from CireNPCCombat::Tick. */
    bool ReleaseSwing(float Now);
    void CancelSwing();
    bool HasPendingSwing() const { return bSwingPending && PendingVictim.IsValid(); }
    float PendingReleaseAt = 0.f;
    bool bSwingPending = false;

    // ---- client presentation ----
    /** Applies the Tripo body for this archetype. False keeps (restores) the mannequin fallback. */
    bool ApplyBody(const FCireNPCArchetype& Archetype, TArray<TObjectPtr<UStaticMeshComponent>>& OutParts);
    bool IsTripoApplied() const { return bTripoApplied; }
    bool HasRoleClip(const FString& Role) const { return RoleClip(Role) != nullptr; }
    const FString& GetAppliedVariant() const { return AppliedVariant; }
    UCireMonsterAnimInstance* GetMonsterAnim() const;
    /** Development/tests: force a variant index (-1 = seed). Re-applies the body. */
    void ForceVariant(int32 Index);
    /** Natural ground speed (cm/s at the current scale) of the walk and run clips. */
    FVector2D GroundSpeeds() const;
    /** Tests: pose the body with one role clip at a normalised time and refresh the bones now. */
    bool PoseForTest(const FString& Role, float Normalized, float MoveSpeed = 0.f);
    /** Galleries: hold a role or named clip ("attack", "war_cry") at an absolute clip time; refreshes bones now. */
    bool PoseClip(const FString& RoleOrName, float ClipSeconds);
    /** Timing window of a role or named clip of the applied body (zero window when absent). */
    CireMonsterArt::FClipWindow WindowOf(const FString& RoleOrName) const;
    /** Plays the named role clip as an action starting now (tests, galleries). */
    bool PlayAction(const FString& Role, float WindupSeconds);
    /** Holds the whole presentation (galleries freeze a mid-swing frame). */
    bool bFrozen = false;
    /** Hand poses / two-hand setup of the props this body holds (CireGrip). */
    CireGrip::FHands GripHands;
    UFUNCTION() void OnRep_BodySeed();

private:
    struct FAction
    {
        TObjectPtr<UAnimSequence> Sequence = nullptr;
        CireMonsterArt::FClipWindow Window;
        double StartedAt = 0.0;
        float Windup = 0.f;
        float Weight = 1.f;
        float LowerBody = 1.f;
        bool bCast = false;
        bool bInterrupted = false;
        double InterruptedAt = 0.0;
    };
    void RestoreFallback();
    void CaptureFallback();
    void UpdatePresentation(float DeltaTime);
    void UpdateRim();
    void StartAction(UAnimSequence* Sequence, double StartedAt, float Windup, float Weight, float LowerBody, bool bCast);
    UAnimSequence* RoleClip(const FString& Role) const;
    UAnimSequence* NamedClip(const FString& Name) const;
    UAnimSequence* ClipForAbility(FName AbilityId) const;
    double ServerNow() const;
    void SpawnCorpse();

    UPROPERTY(Transient) TObjectPtr<USkeletalMesh> FallbackMesh;
    UPROPERTY(Transient) TSubclassOf<UAnimInstance> FallbackAnimClass;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Rim;
    UPROPERTY(Transient) TMap<FString, TObjectPtr<UAnimSequence>> RoleClips;
    UPROPERTY(Transient) TMap<FString, TObjectPtr<UAnimSequence>> NamedClips;
    FTransform FallbackTransform;
    bool bFallbackCaptured = false;
    bool bTripoApplied = false;
    bool bDeathPresented = false;
    int32 ForcedVariant = INDEX_NONE;
    FName AppliedArchetype;
    FString AppliedVariant;
    float AppliedMeshScale = 1.f;
    // world-dressing: data-driven natural speeds of the applied body in raw mesh units (0 = fit from the clips).
    float AppliedWalkRaw = 0.f, AppliedRunRaw = 0.f;
    FAction Current;
    uint8 SeenSwingSerial = 0;
    float SeenCastStartedAt = -1.f;
    float LastHealth = -1.f;
    double LastHitAt = -10.0;
    float SmoothedSpeed = 0.f;
    float Phase = 0.f;
    float IdleTime = 0.f;
    FLinearColor AppliedRimColor = FLinearColor::Transparent;
    // server swing
    TWeakObjectPtr<ACireHero> PendingVictim;
    float PendingAmount = 0.f, PendingReach = 0.f;
    FString PendingName;
};

/** Local, non-replicated body left behind by a killed monster: plays the fall clip, holds, sinks, despawns. */
UCLASS(NotPlaceable, Transient)
class CIRESTEAMSURVIVAL_API ACireMonsterCorpse : public AActor
{
    GENERATED_BODY()
public:
    ACireMonsterCorpse();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    bool Initialize(const USkeletalMeshComponent& Source, UAnimSequence* Fall, UAnimSequence* Idle, const TArray<TObjectPtr<UStaticMeshComponent>>& Props, const CireGrip::FHands* Hands = nullptr);
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Body;
    float Age = 0.f;
    float FallSeconds = 3.f;
    float HoldSeconds = 2.6f, SinkSeconds = 1.4f, SinkCm = 70.f;
    FVector StartLocation = FVector::ZeroVector;
    static int32 LiveCount();
};
