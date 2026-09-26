#pragma once
// weapon-grips: animation-intended weapon placement (Docs/WeaponLoadouts.md, "Grip model").
//
// The Fab animation packs (GDH bundle, Gun & Sword, Crossbow) were authored on the UE5 mannequin with the weapon on a
// socket of its hand (the packs' demo skeletons, e.g. SwordShield "Sword" / "Shield", Spear "Weapon_r", Axe "Axe_r").
// Tools/MeasureMannySockets.py reads those sockets and the demo weapon meshes and writes Content/Data/WeaponSockets.json:
// per animation set and hand, the grip frame in mannequin bone space (grip point, tip direction, edge direction; shields:
// strap point, face-out and up directions) plus the second-hand frame of two-handed sets.
//
// Each champion body plays those clips through an IK retargeter, which maps a source bone's component rotation to the
// target as  Target(t) = Source(t) * Q  with a constant Q per bone. Calibration() measures Q on the body itself by
// evaluating one retargeted clip against its mannequin source (no retargeter asset or rig convention is assumed), so the
// same code serves the Tripo bodies, the Polyphoria plate body and any new body the Fab retarget pipeline produces.
// The intended weapon frame in the body's hand is then  Q^-1 * MannyFrame  : the weapon swings exactly as the clip was
// authored. Bodies without retargeted Fab clips (or with -CireLegacyGrips) keep the bind-pose grip of CireGrip.
#include "CoreMinimal.h"

class USkeletalMesh;
class UAnimSequence;
class UStaticMesh;
namespace CireGrip { struct FWeapon; }

namespace CireWeaponSockets
{
    /** One authored hand frame in mannequin bone space (centimetres). */
    struct CIRESTEAMSURVIVAL_API FHandFrame
    {
        bool bValid = false;
        FName Bone;                         // hand_r / hand_l / lowerarm_l ...
        FVector Point = FVector::ZeroVector; // grip point (shield: strap centre)
        FVector Tip = FVector::UpVector;     // handle -> business end (shield: up, the wide end)
        FVector Edge = FVector::ForwardVector; // blade edge / axe bit (shield: face out)
        bool bShield = false;
        bool bStock = false;                // crossbow fore-end: the hand wraps the stock (Tip = muzzle, Edge = up)
        FString Source;                     // "<skeleton>:<socket> + <demo mesh>" for the docs
    };

    /** Constant retarget rotation per bone of one body (Target = Source * Q), measured from a Fab clip. */
    struct CIRESTEAMSURVIVAL_API FCalibration
    {
        bool bValid = false;
        TMap<FName, FQuat> Q;
        float Scale = 1.f;       // body mesh units per mannequin centimetre (forearm length ratio)
        float SpreadDeg = 0.f;   // largest deviation of Q across the sampled frames (retarget sanity)
        FString Clip;            // the clip it was measured on
    };

    /** Off switch: -CireLegacyGrips (before/after captures) or cire.Grips.Legacy 1. */
    CIRESTEAMSURVIVAL_API bool Legacy();
    /** Animation set a body plays for a weapon style ("sword_shield", "spear", ...) from its Fab attack clip; empty when none. */
    CIRESTEAMSURVIVAL_API FString SetFor(const USkeletalMesh* Body, const FString& Style, const FString& Motion);
    /** Authored frame of Set for the prop in Hand (hand_r / hand_l), or invalid. */
    CIRESTEAMSURVIVAL_API FHandFrame Frame(const FString& Set, FName Hand);
    /** Second-hand target of a two-handed set: off-hand bone frame relative to the main hand bone (mannequin cm). */
    CIRESTEAMSURVIVAL_API bool OffHand(const FString& Set, FName MainHand, FTransform& OutOffInMain);
    /**
     * Cached per body and set; invalid when the body has no calibration clip. Each Fab set is retargeted with its own
     * retargeter (one per source skeleton), so the set's own clip is measured first (the Aetheri Warden plays the Spear
     * set but also has two-handed clips: calibrating on those put the halberd's second-hand target 130 cm away).
     */
    CIRESTEAMSURVIVAL_API const FCalibration& Calibration(const USkeletalMesh& Body, const FString& Set = FString());
    /**
     * The intended grip frame (X = edge, Z = tip; origin = grip point) in the component space of Body's bind pose, mesh
     * units. False when the set has no authored frame for Hand or the body is not calibrated.
     */
    CIRESTEAMSURVIVAL_API bool Intended(const USkeletalMesh& Body, const FString& Set, FName Hand, FTransform& OutGripComponent, FHandFrame& OutFrame);
    /** Off-hand bone target relative to the main hand bone (same convention as CireGrip::FPlacement::OffHandInMain). */
    CIRESTEAMSURVIVAL_API bool IntendedOffHand(const USkeletalMesh& Body, const FString& Set, FName MainHand, FTransform& OutOffHandInMain);
    /** Grip frame of a prop mesh (X = edge, Z = tip) from its WeaponGrips handle/axis/edge and its bounds. */
    CIRESTEAMSURVIVAL_API FTransform PropFrame(const class UStaticMesh& Mesh, const FVector& Handle, const FVector& Axis, const FVector& Edge, bool bShield);
    /**
     * The prop's grip frame (X = edge, Z = tip; origin = the gripped point, prop cm) matching an authored hand Frame:
     * PropFrame of the WeaponGrips handle, or for a stock hold (Frame.bStock: the crossbow's supporting hand) the prop's
     * fore-grip (offHand along offAxis = muzzle) with its handle axis as up.
     */
    CIRESTEAMSURVIVAL_API FTransform PropGrip(const UStaticMesh& Mesh, const CireGrip::FWeapon& Weapon, const FHandFrame& Frame);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke();
#endif
}
