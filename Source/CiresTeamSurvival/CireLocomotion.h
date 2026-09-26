#pragma once
// movement-feel: visual locomotion shared by champions and monsters (presentation only; gameplay movement,
// capsules, speeds, input and replication are untouched). See Docs/MovementFeel.md.
//
//  * Gait analysis: the ground speed an in-place (or root-travelling) clip was authored for, measured from its
//    planted contacts, so play rates can be matched to the capsule's real velocity (no foot sliding).
//  * Blend-space warping: champion 2D locomotion BlendSpaces get a gait position and play rate that make the
//    blended stride match the velocity at any speed and direction (idle -> walk -> run -> faster).
//  * Visual yaw: the body turns smoothly toward the actor's yaw (instant mouselook turns, attack facing, AI steering
//    jitter), with stepped turn-in-place when standing; applied to the root bone, never to the actor or capsule.
//  * Leg IK: feet follow uneven ground (slopes, steps) with a pelvis drop, two-bone IK on humanoid legs.
//  * Lean: small body lean into acceleration and turns.
// cire.LocoFeel 0 (or -CireLocoFeel=0) restores the previous presentation exactly (before/after captures).
#include "CoreMinimal.h"

class ACharacter;
class UAnimSequence;
class UBlendSpace;
class USkeletalMesh;
class USkeletalMeshComponent;
struct FReferenceSkeleton;
struct FCompactPose;

namespace CireLocomotion
{
    CIRESTEAMSURVIVAL_API bool Enabled();

    /** Ground-contact bones of a rig: toes/balls or feet by name, else the lowest distinct leaf clusters (up to 4). */
    CIRESTEAMSURVIVAL_API void FindContactBones(const FReferenceSkeleton& Reference, TArray<int32>& Out);

    struct FGait
    {
        bool bValid = false;
        float Length = 0.f;
        /** Authored ground speed, mesh units per second (pre component scale), from planted contacts. */
        float Speed = 0.f;
        /** Authored travel direction in component space (unit, XY). */
        FVector2D Direction = FVector2D::ZeroVector;
    };
    /** Cached per sequence (game thread). */
    CIRESTEAMSURVIVAL_API const FGait& AnalyzeGait(const UAnimSequence* Sequence);

    /** Stride-matched BlendSpace input for a travelling body: the gait position on the speed axis and the play rate. */
    struct FBlendWarp { float AxisSpeed = 0.f; float PlayRate = 1.f; float Natural = 0.f; bool bValid = false; };
    /** Direction in degrees (blend axis 0), Speed in cm/s, MeshScale = component world scale. */
    CIRESTEAMSURVIVAL_API FBlendWarp WarpBlendSpace(const UBlendSpace* Blend, float Direction, float Speed, float MeshScale);
    /** Natural ground speed (cm/s at MeshScale) of a blend at an input position (0 when unknown). */
    CIRESTEAMSURVIVAL_API float BlendNaturalSpeed(const UBlendSpace* Blend, float Direction, float AxisSpeed, float MeshScale);

    /**
     * Game thread: the body's visual heading. Moving, it follows the target heading with a short critically damped lag
     * (no single-frame snaps from mouselook, attack facing or steering jitter). Standing, the feet stay planted while the
     * actor turns up to MaxLag, then the body steps round (StepWeight/StepPhase drive a stepping leg cycle).
     */
    struct CIRESTEAMSURVIVAL_API FVisualTurn
    {
        bool bInit = false, bStepping = false, bReverse = false;
        float Yaw = 0.f, Offset = 0.f, Still = 0.f, StepWeight = 0.f, StepPhase = 0.f, StepDelta = 0.f, StepSign = 1.f, LastActorYaw = 0.f;
        float ReverseHold = 0.f;
        FVector LastLocation = FVector::ZeroVector;
        void Reset(float TargetYaw, const FVector& Location);
        /** ActorYaw = gameplay facing; TargetYaw = where the legs should point (ActorYaw, or warped toward travel). */
        void Update(float ActorYaw, float TargetYaw, const FVector& Location, float Speed, float Dt, float MaxLag);
    };

    /** Warped heading for bodies with only forward gaits (monsters): legs point along travel within +-Limit of the
     *  facing; beyond ~110 degrees (with hysteresis, bInOutReverse keeps the state) they backpedal with the legs
     *  pointing opposite to travel. */
    CIRESTEAMSURVIVAL_API float TravelWarp(float ActorYaw, const FVector& Velocity, float Limit, FVisualTurn& State, float Dt);
    /** Authored ground speed of a gait clip at a component scale (cm/s), or Fallback when it cannot be measured. */
    CIRESTEAMSURVIVAL_API float GaitSpeed(const UAnimSequence* Clip, float Scale, float Fallback);

    /** Game thread: per-foot ground offsets under a humanoid (foot_l / foot_r) relative to the ground below the capsule. */
    struct CIRESTEAMSURVIVAL_API FLegIK
    {
        float Offset[2] = {0.f, 0.f};
        float Pelvis = 0.f;
        float Weight = 0.f;
        bool bChecked = false, bHumanoid = false;
        void Update(const ACharacter& Owner, const USkeletalMeshComponent& Mesh, float Dt, bool bGrounded);
    };

    /** Worker-thread pose adjustments (component units). */
    struct CIRESTEAMSURVIVAL_API FPoseFeel
    {
        float RootYaw = 0.f;        // degrees about component up, applied to the root bone
        float SpineCounter = 0.f;   // degrees the torso turns back (upper body keeps facing the actor yaw)
        float Pelvis = 0.f;         // component units along up (negative drops the hips)
        float Foot[2] = {0.f, 0.f}; // component units along up for foot_l / foot_r targets
        float IKWeight = 0.f;
        /** Absolute visual heading; Resolve() turns it into RootYaw/SpineCounter against the mesh's heading at evaluation. */
        float VisualYaw = 0.f, CounterFraction = 0.f;
        bool bVisual = false;
        bool Any() const { return FMath::Abs(RootYaw) > .01f || FMath::Abs(SpineCounter) > .01f || IKWeight > .001f; }
        /** Fill from the game-thread state; Scale = component world scale. */
        void Set(const FVisualTurn& Turn, const FLegIK& Legs, float Scale, float InCounterFraction);
        /** Game thread, anim PreUpdate: the root offset against the mesh's current (network-smoothed) heading, so an actor
         *  rotation applied earlier in the frame never shows for a frame unsmoothed. */
        void Resolve(const USkeletalMeshComponent* Mesh);
    };
    CIRESTEAMSURVIVAL_API void ApplyPoseFeel(FCompactPose& Pose, const FPoseFeel& Feel);
#if !UE_BUILD_SHIPPING
    /** Heading, stepping and warp rules; logs CIRE_LOCOMOTION_TESTS_PASS / _FAIL. */
    CIRESTEAMSURVIVAL_API bool RunTests();
#endif
}
