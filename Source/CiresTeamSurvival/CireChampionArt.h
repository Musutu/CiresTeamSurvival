#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "CireGrip.h" // creature-anim
#include "CireChampionArt.generated.h"

class ACireHero;
class UAnimationAsset;
class UAnimInstance;
class UBlendSpace;
class UMaterialInterface;
class USkeletalMesh;
class UAnimSequence;
class UCireWeaponPresentation;
class UCireCreatureArt;
class UMeshComponent;
class FJsonObject;
struct FCireChampionArtDefinition;
/** creature-anim: native tests force the Tripo champion bodies without -CireTripoChampions. */
extern CIRESTEAMSURVIVAL_API bool GCireForceTripoChampionArt;

/** Prototype combat layer. Locomotion keeps advancing while the attack fades in/out. */
UCLASS(Transient)
class CIRESTEAMSURVIVAL_API UCireCombatAnimInstance : public UAnimSingleNodeInstance
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> AttackSequence;
    float AttackTime = 0.f;
    float AttackWeight = 0.f;
    // creature-anim: 0 = the action layer drives only spine and above (legs keep walking), 1 = full body.
    float AttackLowerBody = 1.f;
    // creature-anim: closed hands around held props and the off-hand IK of two-handed weapons.
    CireGrip::FHands Hands;
    float SpineTwist = 0.f;
    float AirWeight = 0.f;
    float RollProgress = -1.f;
    FVector MotionPitchAxis = FVector(1,0,0);
    // new-champions: 1 = seated rider (thighs forward, knees bent), used by mounted champions.
    float SeatWeight = 0.f;
    // new-champions: 1 = lower arms that the body's idle keeps near T-pose (some Tripo retargets); scaled by (1 - attack weight).
    float RelaxArms = 0.f;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};

/** Local presentation only. Gameplay, collision and replicated state stay on ACireHero. */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireChampionArt : public UActorComponent
{
    GENERATED_BODY()
public:
    UCireChampionArt();
    void UpdateVisuals(ACireHero& Hero, float DeltaSeconds);
    bool IsApplied() const { return AppliedArchetype != INDEX_NONE; }
    int32 GetAppliedArchetype() const { return AppliedArchetype; }
    UMeshComponent* GetVisualMesh() const;
    UCireCreatureArt* GetCreature() const { return Creature; }
    /** new-champions: the profile is drawn by UCireCreatureArt (bear/whisp/centaur, or a monster_native/mounted binding). */
    static bool IsCreatureProfile(const FString& ProfileId);
    /** fab-integration: the creature binding in effect (the Fab overlay when installed); false when the profile has none. */
    static bool EffectiveCreatureBinding(const FString& ProfileId, FString& OutMesh, FString& OutMotion, bool& bOutFab);
    /** new-champions: re-tints a Tripo body with the race skin material (base/accent/rim), e.g. temporary champion bodies. */
    static bool TintBody(class USkeletalMeshComponent* Mesh, UObject* Outer, FLinearColor Base, FLinearColor Accent, float Strength, FLinearColor Rim);
    /** new-champions (tests): apply the profile's art now, as the review flag would. */
    bool DebugApply(ACireHero& Hero);
    /** paladin-hq: the Fab humanoid body in effect for a profile (ChampionArtBindings.fab.json "humanoid" row, pack
     *  installed, not -CireNoFab); false otherwise. OutScale is the mesh scale at actor scale 1. */
    static bool FabHumanoidBody(const FString& ProfileId, FString& OutMesh, float& OutHeightCm, float& OutScale);
    /** paladin-hq: leader-posed armour/head parts of a Fab humanoid body (empty otherwise). */
    const TArray<TObjectPtr<class USkeletalMeshComponent>>& GetBodyParts() const { return BodyParts; }
    /** paladin-hq: material overrides from data: {"<slot name|index>": "/Game/..MI" | {"base": path, "vectors":
     *  {"R: Primary": [r,g,b,a]}, "scalars": {name: v}}}. Returns the number of slots changed. */
    static int32 ApplyMaterialSpec(class UMeshComponent* Mesh, const TSharedPtr<class FJsonObject>& Spec, UObject* Outer);

private:
    bool Apply(ACireHero& Hero, int32 Archetype);
    void CaptureFallback(ACireHero& Hero);
    void RestoreFallback(ACireHero& Hero);
    void ClearBodyParts();
    bool ApplyHumanoid(ACireHero& Hero, int32 Archetype, const FCireChampionArtDefinition& Definition);
    bool ApplyFabBody(ACireHero& Hero, const TSharedPtr<FJsonObject>& Raw); // paladin-hq
    void ApplyStaticParts(ACireHero& Hero, const TSharedPtr<FJsonObject>& Raw); // champion-hq: segmented props (quiver)

    UPROPERTY(Transient) TObjectPtr<USkeletalMesh> FallbackMesh;
    UPROPERTY(Transient) TSubclassOf<UAnimInstance> FallbackAnimClass;
    UPROPERTY(Transient) TObjectPtr<UAnimationAsset> FallbackAnimation;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> FallbackMaterials;
    UPROPERTY(Transient) TObjectPtr<UBlendSpace> Locomotion;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> AttackAnimation;
    UPROPERTY(Transient) TObjectPtr<UCireWeaponPresentation> Weapons;
    UPROPERTY(Transient) TObjectPtr<UCireCreatureArt> Creature;
    UPROPERTY(Transient) TArray<TObjectPtr<class USkeletalMeshComponent>> BodyParts; // paladin-hq
    UPROPERTY(Transient) TArray<TObjectPtr<class UStaticMeshComponent>> StaticParts; // champion-hq
    FTransform FallbackTransform;
    uint8 FallbackAnimationMode = 0;
    bool bFallbackCaptured = false;
    int32 AppliedArchetype = INDEX_NONE;
    int32 AttemptedArchetype = INDEX_NONE;
    FString AttemptedProfile;
    float SmoothedSpeed = 0.f;
    uint32 LastAttackSerial = 0;
};
