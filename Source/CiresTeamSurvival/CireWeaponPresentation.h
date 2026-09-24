#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CireGrip.h" // creature-anim
#include "CireWeaponPresentation.generated.h"

class ACireHero;
class UStaticMeshComponent;
class USkeletalMesh;

namespace CireWeapons
{
    /** Presentation-only, transactional reload. Existing data survives a malformed file. */
    bool Reload(FString& Error);
    bool RunValidationSmoke(bool bRequireAssets=true);
#if !UE_BUILD_SHIPPING
    /** Local preview only: does not change attack rules, attributes, or replicated state. */
    bool CyclePreview(ACireHero& Hero,FString& Message);
    bool ResetPreview(ACireHero& Hero,FString& Message);
#endif
}

/** Original local prototype equipment. Visual-only and parented to the visible champion. */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireWeaponPresentation : public UActorComponent
{
    GENERATED_BODY()
public:
    void Apply(ACireHero& Hero, int32 Archetype);
    void Update(ACireHero& Hero, float AttackElapsed);
    void Clear();
    const FString& GetEquippedLoadout() const { return EquippedLoadout; }
    int32 GetPartCount() const { return Parts.Num(); }
    // creature-anim: closed hands around the held props and the two-hand setup (CireGrip), read by CireChampionArt.
    CireGrip::FHands GripHands;
    CireGrip::FHandPose DrawPose;          // bow string hand (pinch) while drawing
    const TArray<TObjectPtr<UStaticMeshComponent>>& GetParts() const { return Parts; }
#if !UE_BUILD_SHIPPING
    bool CyclePreview(ACireHero& Hero,FString& Message);
    void ResetPreview(ACireHero& Hero);
#endif
private:
    UStaticMeshComponent* Attach(ACireHero& Hero,const FString& AssetPath,FName BoneName,
        const FVector& OffsetCm,const FRotator& Rotation,float Size);
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Primary;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Arrow;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> BowStrings;
    UPROPERTY(Transient) TWeakObjectPtr<USkeletalMesh> EquippedMesh;
    FString EquippedProfile,EquippedLoadout,Motion,PreviewLoadout,PreviewProfile;
    int32 AppliedRevision=INDEX_NONE;
    float PrimarySize=1.f;
};
