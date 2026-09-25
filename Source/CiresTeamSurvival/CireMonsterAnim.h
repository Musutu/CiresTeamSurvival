#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "CireGrip.h"
#include "CireMonsterAnim.generated.h"

class UAnimSequence;
struct FCompactPose;

/**
 * Native animation for Tripo 61-bone bodies (monsters, and the champion action layer).
 *
 * No AnimBlueprint: the game thread writes clip times and weights, the proxy samples the
 * sequences on the animation worker and blends them. Locomotion is idle/walk/run by speed
 * (phase-synchronised, play rate matched to ground speed so feet do not slide), an action
 * layer (attack/cast/hit) blends per bone so the legs keep walking when the body moves, and a
 * death layer takes the full body. Root motion is never extracted into the pose.
 */
namespace CireAnimClips
{
    /** Analysis of one clip on its own skeleton (component space, mesh units, pre-scale). */
    struct CIRESTEAMSURVIVAL_API FClipInfo
    {
        bool bValid = false;
        float Length = 0.f;
        /** Least-squares horizontal pelvis travel: Offset + Velocity * t (mesh units). */
        FVector2D DriftOffset = FVector2D::ZeroVector;
        FVector2D DriftVelocity = FVector2D::ZeroVector;
        /** Reference-pose pelvis position the drift is removed toward. */
        FVector2D ReferencePelvis = FVector2D::ZeroVector;
        /** Normalised phase at which the left foot is highest (gait alignment). */
        float LeftFootApexPhase = 0.f;
        /** Mean height of the ball (toe) joints at t=0, used to put the soles on the floor. */
        float StanceBallZ = 0.f;
        float GroundSpeed() const { return static_cast<float>(DriftVelocity.Size()); }
    };
    /** Cached per sequence; valid on the game thread after the first call. */
    CIRESTEAMSURVIVAL_API const FClipInfo& Analyze(const UAnimSequence* Sequence);
    /** Pelvis horizontal drift to subtract at clip time T so the pelvis cycles around Target (mesh units, component space). */
    CIRESTEAMSURVIVAL_API FVector2D DriftAt(const FClipInfo& Info, float Time, const FVector2D& Target);
    /** Removes horizontal pelvis travel from a sampled pose (in-place locomotion). */
    CIRESTEAMSURVIVAL_API void RemovePelvisDrift(FCompactPose& Pose, const FVector2D& Drift);
    /** Weight 1 for spine_01 and every descendant (arms, head); 0 for pelvis and legs. */
    CIRESTEAMSURVIVAL_API void UpperBodyMask(const FCompactPose& Pose, TArray<float>& OutUpper);
}

/** One sampled clip layer, copied to the proxy every update. */
USTRUCT()
struct FCireAnimLayer
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> Sequence = nullptr;
    float Time = 0.f;
    float Weight = 0.f;
    /** 0 = the layer only drives the upper body, 1 = full body. */
    float LowerBody = 1.f;
    bool bRemoveDrift = false;
    /** Where the in-place pelvis cycles (the idle clip's mean pelvis; some reference poses are not standing). */
    FVector2D PelvisTarget = FVector2D::ZeroVector;
};

UCLASS(Transient)
class CIRESTEAMSURVIVAL_API UCireMonsterAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) FCireAnimLayer Idle;
    UPROPERTY(Transient) FCireAnimLayer Walk;
    UPROPERTY(Transient) FCireAnimLayer Run;
    UPROPERTY(Transient) FCireAnimLayer Action;
    UPROPERTY(Transient) FCireAnimLayer Death;
    /** 0..1 blend from idle to moving, and inside moving from walk to run. */
    float MoveAlpha = 0.f;
    float RunAlpha = 0.f;
    /** Closed hands around held props and the off-hand IK of two-handed weapons. */
    CireGrip::FHands Hands;
    /** Set when the last evaluation produced a non-finite pose (reference pose was used). */
    bool bLastPoseRejected = false;
    /** world-dressing: keep the root joint at its reference transform (glTF animals key their armature proxy root). */
    bool bLockRootToReference = false;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
