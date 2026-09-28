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
    /** blender-rig: index (0..2) of the prop-space axis a handle direction runs along (within ~8 degrees), else INDEX_NONE. */
    CIRESTEAMSURVIVAL_API int32 PrincipalAxis(const FVector& Axis);
    /** blender-rig: prop-space stretch by Factor along the handle axis, anchored at the handle point (identity if not principal). */
    CIRESTEAMSURVIVAL_API FTransform HandleStretch(const CireGrip::FWeapon& Grip, float Factor);
    /** blender-rig: WeaponLoadouts.json sizeClasses for a monster/creature held prop. A one-handed weapon (grip data with
     *  no second grip, not a shield, ammo, carried staff or two-hander) is "mace" (mace/hammer/flail/club by mesh name)
     *  or "one_hand"; false when the prop keeps its size. Girth is 1 when the handle axis is not principal. */
    CIRESTEAMSURVIVAL_API bool HeldSizeClass(const class UStaticMesh& Mesh, const CireGrip::FWeapon& Grip, FString& OutClass, float& OutScale, float& OutGirth, float& OutMaxBodyFraction);
    /**
     * blender-rig: the handle axis pointed at the prop's business end (blade, head, spike). WeaponGrips.fab.json lists the
     * Fab meshes' axes by their modelling direction, so on the bind grip SM_Sword_1, SM_Axe_1, SM_Dagger_1 and SM_WarHammer
     * came out pommel-first (tip behind the fist at idle). Flips Axis when the far end along it is clearly the shorter one
     * (the other side at least twice as long); symmetric grips (bows) and pistol grips keep the data's direction.
     */
    CIRESTEAMSURVIVAL_API FVector BusinessAxis(const class UStaticMesh& Mesh, const CireGrip::FWeapon& Grip);
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
    /** blender-rig: motion of the equipped loadout (a preview loadout such as ranger_crossbow included). */
    const FString& GetEquippedMotion() const { return Motion; }
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
        float Size=1.f, Girth=1.f; // blender-rig: prop scale along the handle axis, and the cross-section factor (girth) of it
        FString SizeClass;         // blender-rig: WeaponLoadouts.json sizeClasses key ("one_hand", "mace", ...)
        float BaseSize=1.f;        // blender-rig: the size before the class (and its cap) applied
        bool bMuzzleSpun=false;    // blender-rig: carried stock prop turned muzzle-forward
        bool bMuzzleCheck=false; int32 MuzzleFrames=0; FVector MuzzleAxis=FVector::ForwardVector, HandleAxis=FVector::UpVector, Handle=FVector::ZeroVector;
    };
    const TArray<FGripInfo>& GetGripInfo() const { return GripInfo; }
    const FString& GetGripSet() const { return GripSet; }
#if !UE_BUILD_SHIPPING
    bool CyclePreview(ACireHero& Hero,FString& Message);
    void ResetPreview(ACireHero& Hero);
#endif
private:
    UStaticMeshComponent* Attach(ACireHero& Hero,const FString& AssetPath,FName BoneName,
        const FVector& OffsetCm,const FRotator& Rotation,float Size,bool bPrimary=false,float Girth=1.f,float MaxBodyFraction=0.f,float MinSize=0.f);
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
