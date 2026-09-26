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
    /** weapon-grips: true when props keep the bind-pose grip (-CireLegacyGrips, cire.Grips.Legacy 1). */
    CIRESTEAMSURVIVAL_API bool LegacyGrips();
    /** weapon-grips: one line per held prop (mesh, bone, grip mode and set, deviation from the animation's authored
     *  grip, length against the body) for galleries and tests. */
    CIRESTEAMSURVIVAL_API FString DescribeGrips(const ACireHero& Hero);
}

/** Original local prototype equipment. Visual-only and parented to the visible champion. */
namespace CireWeaponFab
{
    /** fab-integration: the Fab weapon mesh replacing an asset token (WeaponLoadouts.fab.json) when installed, else Fallback. */
    CIRESTEAMSURVIVAL_API FString ResolveMesh(const FString& Token, const FString& Fallback, float& InOutSize);
    /** paladin-hq: a per-profile Fab prop (WeaponLoadouts.fab.json "profiles"."<profile>"."<token>") first, then the
     *  token override. OutMaterials is the prop's material spec (UCireChampionArt::ApplyMaterialSpec), or null. */
    CIRESTEAMSURVIVAL_API FString ResolveMesh(const FString& Profile, const FString& Token, const FString& Fallback, float& InOutSize, TSharedPtr<class FJsonObject>& OutMaterials, bool& bOutProfileProp);
}

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
    /** weapon-grips: how each prop was placed (CireWeaponSockets), for DescribeGrips and the grip tests. */
    struct FGripInfo
    {
        TWeakObjectPtr<UStaticMeshComponent> Part; FName Bone; FString Mode, Set;
        float TipDeviationDeg=-1.f, EdgeDeviationDeg=-1.f, LengthCm=0.f; bool bTwoHand=false;
    };
    const TArray<FGripInfo>& GetGripInfo() const { return GripInfo; }
    const FString& GetGripSet() const { return GripSet; }
#if !UE_BUILD_SHIPPING
    bool CyclePreview(ACireHero& Hero,FString& Message);
    void ResetPreview(ACireHero& Hero);
#endif
private:
    UStaticMeshComponent* Attach(ACireHero& Hero,const FString& AssetPath,FName BoneName,
        const FVector& OffsetCm,const FRotator& Rotation,float Size,bool bPrimary=false);
    /** weapon-grips: places Part on the animation-authored grip of GripSet; false keeps the bind-pose grip. */
    bool PlaceAuthored(ACireHero& Hero,UStaticMeshComponent& Part,const CireGrip::FWeapon& Grip,FName BoneName,float Size,bool bPrimary,FGripInfo& Info);
    TArray<FGripInfo> GripInfo;
    FString GripSet;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Primary;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Arrow;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> BowStrings;
    UPROPERTY(Transient) TWeakObjectPtr<USkeletalMesh> EquippedMesh;
    FString EquippedProfile,EquippedLoadout,Motion,PreviewLoadout,PreviewProfile;
    int32 AppliedRevision=INDEX_NONE;
    float PrimarySize=1.f;
};
