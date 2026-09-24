#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
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

/** Prototype combat layer. Locomotion keeps advancing while the attack fades in/out. */
UCLASS(Transient)
class CIRESTEAMSURVIVAL_API UCireCombatAnimInstance : public UAnimSingleNodeInstance
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> AttackSequence;
    float AttackTime = 0.f;
    float AttackWeight = 0.f;
    float AirWeight = 0.f;
    float RollProgress = -1.f;
    FVector MotionPitchAxis = FVector(1,0,0);
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

private:
    bool Apply(ACireHero& Hero, int32 Archetype);
    void CaptureFallback(ACireHero& Hero);
    void RestoreFallback(ACireHero& Hero);

    UPROPERTY(Transient) TObjectPtr<USkeletalMesh> FallbackMesh;
    UPROPERTY(Transient) TSubclassOf<UAnimInstance> FallbackAnimClass;
    UPROPERTY(Transient) TObjectPtr<UAnimationAsset> FallbackAnimation;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> FallbackMaterials;
    UPROPERTY(Transient) TObjectPtr<UBlendSpace> Locomotion;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> AttackAnimation;
    UPROPERTY(Transient) TObjectPtr<UCireWeaponPresentation> Weapons;
    UPROPERTY(Transient) TObjectPtr<UCireCreatureArt> Creature;
    FTransform FallbackTransform;
    uint8 FallbackAnimationMode = 0;
    bool bFallbackCaptured = false;
    int32 AppliedArchetype = INDEX_NONE;
    int32 AttemptedArchetype = INDEX_NONE;
    FString AttemptedProfile;
    float SmoothedSpeed = 0.f;
    uint32 LastAttackSerial = 0;
};
