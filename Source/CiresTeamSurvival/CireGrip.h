#pragma once

#include "CoreMinimal.h"

class ACireGameMode;
class USkeletalMesh;
class UStaticMesh;
struct FCompactPose;
struct FReferenceSkeleton;

/**
 * Hand grips for held props on the Tripo 61-bone rigs (Docs/MonsterArt.md, "Grips").
 *
 * Everything is derived from the bind pose of each body: the palm frame (knuckle direction A, pinky->index
 * direction T, palm normal N), a finger curl that wraps a cylinder of the weapon's handle radius, and the
 * handle axis through the centres of the curled fingers. Weapon meshes carry a handle point and axes in
 * Content/Data/WeaponGrips.json. The anim instances override the finger bones with the curled pose and
 * place the off hand of two-handed weapons with a two-bone IK.
 */
namespace CireGrip
{
    enum class EHand : uint8 { None, Power, Pinch, Shield };

    /** One weapon's handle, in the static mesh's own centimetre space. */
    struct CIRESTEAMSURVIVAL_API FWeapon
    {
        FString Mesh;                    // static mesh object name, e.g. SM_PrototypeSword
        FVector Handle = FVector::ZeroVector;
        FVector Axis = FVector::UpVector; // handle axis: runs pinky -> index/thumb through the fist
        FVector Edge = FVector::ForwardVector; // faces the knuckle direction (blade edge, axe/hammer head, bow belly)
        float RadiusCm = 1.8f;
        float TiltDeg = 0.f;             // tip leans toward the knuckles (blade ahead of the fist)
        EHand Hand = EHand::Power;
        bool bShield = false;            // strapped on the forearm instead of held
        bool bAmmo = false;              // attached to another prop (arrow, bolt)
        bool bTwoHand = false;
        bool bCarry = false;             // idle/locomotion: main arm carries it upright in front (staffs, totem, lance)
        FVector CarryAt = FVector(.34f, .2f, .62f); // carry grip: forward, inward, drop below the shoulder (fractions of arm length)
        FVector OffHand = FVector::ZeroVector;   // second grip point (mesh cm)
        FVector OffAxis = FVector::UpVector;     // handle direction at the second grip
    };

    /** Bind-pose palm frame and curled finger rotations for one hand of one body. */
    struct CIRESTEAMSURVIVAL_API FHandPose
    {
        bool bValid = false;
        EHand Type = EHand::None;
        int32 Hand = INDEX_NONE;          // mesh bone index of hand_l / hand_r
        TArray<int32> Bones;              // finger bones (mesh indices)
        TArray<FQuat> Local;              // target local rotations
        FTransform GripInHand;            // grip frame (Z = handle axis, X = knuckles) in hand-bone space
        FTransform GripComponent;         // same frame in bind component space (mesh units)
        float RadiusMesh = 0.f;           // handle radius the curl wraps (mesh units)
        FVector PinchInHand = FVector::ZeroVector; // pinch point (thumb tip / index tip) in hand-bone space
    };

    /** Result of placing a prop on a body. */
    struct CIRESTEAMSURVIVAL_API FPlacement
    {
        bool bValid = false;
        FName Bone;                       // attach bone
        FTransform Relative;              // relative transform to Bone (includes scale)
        FTransform Component;             // bind-pose component-space transform of the prop
        FHandPose Pose;                   // hand pose of the holding hand (Type None for shields' strap side)
        bool bTwoHand = false;
        FName OffHandBone;
        FTransform OffHandInMain;         // off-hand bone target relative to the main hand bone
        FHandPose OffPose;
        bool bCarry = false;
        FTransform CarryInChest;          // main hand bone target relative to spine_03 while carrying
    };

    /** Runtime hand input shared by the monster and champion anim instances. */
    struct CIRESTEAMSURVIVAL_API FHands
    {
        FHandPose Pose[2];                // 0 = left, 1 = right
        float Weight[2] = {0.f, 0.f};
        bool bTwoHand = false;
        int32 MainSide = 1;
        FTransform OffHandInMain;
        float TwoHandWeight = 0.f;
        bool bCarry = false;
        FTransform CarryInChest;
        int32 Chest = INDEX_NONE;
        /** Arm chain mesh indices per side: upperarm, lowerarm, hand. */
        int32 Arm[2][3] = {{INDEX_NONE, INDEX_NONE, INDEX_NONE}, {INDEX_NONE, INDEX_NONE, INDEX_NONE}};
        bool Any() const { return Weight[0] > 0.f || Weight[1] > 0.f; }
    };

    CIRESTEAMSURVIVAL_API const FWeapon* FindWeapon(const UStaticMesh* Mesh);
    CIRESTEAMSURVIVAL_API const FWeapon* FindWeapon(const FString& MeshName);
    /** Presets whose cast/crossbow clips were authored for the other hand swap their hand props. */
    CIRESTEAMSURVIVAL_API bool SwapsHands(const FString& Preset);
    /** Curled hand pose wrapping a handle of RadiusMesh (cached per mesh/side/type/radius). */
    CIRESTEAMSURVIVAL_API FHandPose BuildHandPose(const USkeletalMesh& Body, bool bRight, EHand Type, float RadiusMesh);
    /**
     * Places Weapon held in Bone (hand_l/hand_r) of Body. MeshScale is the skeletal mesh component's relative
     * scale and PropScale the prop's world size factor, so a prop keeps its real centimetres on any body.
     */
    CIRESTEAMSURVIVAL_API FPlacement Place(const USkeletalMesh& Body, FName Bone, const FWeapon& Weapon, float PropScale, float MeshScale);
    /** Adds a placement's hand poses / two-hand setup to Hands. */
    CIRESTEAMSURVIVAL_API void AddToHands(const USkeletalMesh& Body, const FPlacement& Placement, FHands& Hands);
    /** Anim worker: finger overrides and the off-hand IK on an evaluated pose. */
    CIRESTEAMSURVIVAL_API void Apply(FCompactPose& Pose, const FHands& Hands);
    /** Turns the torso about the vertical: spine_01..03 share Degrees (sweeping swings). */
    CIRESTEAMSURVIVAL_API void TwistSpine(FCompactPose& Pose, float Degrees);
    /** Component-space transform of a bone in an evaluated pose (mesh bone index). */
    CIRESTEAMSURVIVAL_API FTransform ComponentBone(const FCompactPose& Pose, int32 MeshBone);
#if !UE_BUILD_SHIPPING
    /** Grip alignment per weapon class on champions and monster props (CireGripTests.cpp). */
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
    /** Bind-pose component transform of a named bone. */
    CIRESTEAMSURVIVAL_API FTransform ReferenceComponent(const FReferenceSkeleton& Skeleton, FName Bone);
}
