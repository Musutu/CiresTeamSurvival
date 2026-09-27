#pragma once
// blender-rig: numeric arm checks on an evaluated pose (Docs/RigAudit.md).
//
// Elbow: the signed flexion of the forearm in the upper arm's frame. "Anterior" is the body's front (feet: foot -> ball)
// carried into the upper arm's bind frame, so it follows the humerus' twist in the clip. Positive = the forearm swings to
// the front of the upper arm (a natural bend); negative = it swings behind (hyperextension, the "elbow bends backwards").
// Twist: rotation of the hand about its own long axis relative to the upper arm, measured from the bind pose. A hand
// turned more than ~120 degrees from its bind relation reads as "the hand is backwards".
#include "CoreMinimal.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class ACireGameMode;
struct FReferenceSkeleton;

namespace CireRigAudit
{
    struct CIRESTEAMSURVIVAL_API FArm
    {
        bool bValid = false;
        float ElbowDeg = 0.f;     // signed flexion (+ natural, - hyperextended)
        float TwistDeg = 0.f;     // hand twist about its long axis vs. bind, relative to the upper arm
        float ElbowFwd = 0.f;     // elbow tip direction . body front (1: the elbow points forward)
    };

    /** Hyperextension beyond this is a defect (a straight arm reads about 0 +- 5 on the Tripo rigs). */
    constexpr float HyperextensionLimitDeg = -12.f;
    /** Twist is reported, not failed: the forearm's pronation range makes +-180 reachable in sound clips. */
    constexpr float TwistLimitDeg = 180.f;
    /**
     * The upper arm's anatomical front (the side the forearm flexes toward) in upper-arm bone space. A body modelled with
     * bent arms (Tripo monsters holding a weapon across the chest) defines it by its own bind forearm; a straight bind arm
     * by the body's front (feet: foot -> ball). Shared by the audit and the arm IK (CireGrip).
     */
    CIRESTEAMSURVIVAL_API bool AnteriorLocal(const FReferenceSkeleton& Ref, int32 Upper, int32 Lower, int32 Hand, FVector& OutLocal, FVector* OutBindForward = nullptr);

    /** Arm of Mesh's current evaluated pose (component-space transforms). */
    CIRESTEAMSURVIVAL_API FArm Measure(const USkeletalMeshComponent& Mesh, bool bRight);
    /** Same on a component-space bone array of Body (mesh bone indices). */
    CIRESTEAMSURVIVAL_API FArm MeasurePose(const USkeletalMesh& Body, TConstArrayView<FTransform> Component, bool bRight);
    /** "elbowL=.. elbowR=.. twistL=.. twistR=.. fwdL=.. fwdR=.. defects=.." for logs. */
    CIRESTEAMSURVIVAL_API FString Describe(const USkeletalMeshComponent& Mesh, int32* OutDefects = nullptr);
    CIRESTEAMSURVIVAL_API bool IsDefect(const FArm& Arm);
#if !UE_BUILD_SHIPPING
    /**
     * -CireRigAudit: every skeletal body referenced by Content/Data/*.json (champions, NPCs/vendors, monsters) is swept over
     * every clip on its skeleton (data-referenced clips plus /Game/FabDerived/Anim, /Game/Art/Characters/ChampionAttacks02,
     * /Game/Tripo), 12 samples per clip, and its arms are measured. Writes Saved/RigAudit/<stamp>.json and logs
     * CIRE_RIG_AUDIT_BODY lines, then exits (CIRE_RIG_AUDIT_DONE). -CireRigAuditOnly=<substr,...> filters bodies.
     */
    bool Initialize(ACireGameMode* Mode);
#endif
}
