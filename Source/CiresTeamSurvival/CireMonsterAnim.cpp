#include "CireMonsterAnim.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/Skeleton.h"
#include "AnimationRuntime.h"
#include "BonePose.h"

namespace
{
FTransform ComponentBone(const UAnimSequence& Sequence, const FReferenceSkeleton& Reference, int32 Bone, double Time)
{
    FTransform Result = FTransform::Identity;
    const FAnimExtractContext Context(Time, false);
    for (int32 Index = Bone; Index != INDEX_NONE; Index = Reference.GetParentIndex(Index))
    {
        // Bones without an animated track keep their reference transform.
        FTransform Local = Reference.GetRefBonePose()[Index];
        Sequence.GetBoneTransform(Local, FSkeletonPoseBoneIndex(Index), Context, false);
        Result = Result * Local;
    }
    return Result;
}

FCompactPoseBoneIndex CompactIndex(const FBoneContainer& Bones, const TCHAR* Name)
{
    const int32 Index = Bones.GetReferenceSkeleton().FindBoneIndex(Name);
    return Index == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
}

// monster-rig: lowest foot (foot/ball bones) of a pose in component space; false when the body has no such bones.
bool LowestFoot(const FCompactPose& Pose, float& OutZ)
{
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    FCSPose<FCompactPose> Space;
    Space.InitPose(Pose);
    bool bAny = false; OutZ = TNumericLimits<float>::Max();
    for (const TCHAR* Name : {TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r")})
    {
        const FCompactPoseBoneIndex Bone = CompactIndex(Bones, Name);
        if (!Bone.IsValid()) continue;
        OutZ = FMath::Min(OutZ, static_cast<float>(Space.GetComponentSpaceTransform(Bone).GetLocation().Z)); bAny = true;
    }
    return bAny;
}

// monster-rig: retargeted Fab clips (lunges, low guards, sword-and-shield gait) can sink the feet below the stance
// floor on bodies with shorter legs; lift the pelvis so the lowest foot never goes below the idle stance's.
void KeepFeetOnFloor(FCompactPose& Pose, float FloorZ)
{
    float Z = 0.f;
    if (!LowestFoot(Pose, Z) || Z >= FloorZ - .5f) return;
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    const FCompactPoseBoneIndex Pelvis = CompactIndex(Bones, TEXT("pelvis"));
    if (!Pelvis.IsValid()) return;
    FCSPose<FCompactPose> Space;
    Space.InitPose(Pose);
    const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Pelvis);
    const FTransform ParentSpace = Parent.IsValid() ? Space.GetComponentSpaceTransform(Parent) : FTransform::Identity;
    Pose[Pelvis].AddToTranslation(ParentSpace.InverseTransformVector(FVector(0, 0, FloorZ - Z)));
}

bool PoseIsSane(const FCompactPose& Pose)
{
    for (const FCompactPoseBoneIndex Bone : Pose.ForEachBoneIndex())
    {
        const FTransform& T = Pose[Bone];
        if (T.ContainsNaN() || !T.GetRotation().IsNormalized() || T.GetTranslation().GetAbsMax() > 1.0e5 ||
            T.GetScale3D().GetAbsMax() > 1.0e4) return false;
    }
    return true;
}
}

namespace
{
// fab-integration: vendor rigs name the hip bone b_pelvis / BARGHEST_-Pelvis / CENTAUR_-Pelvis; exact "pelvis" first.
int32 FindPelvisBone(const FReferenceSkeleton& Reference)
{
    const int32 Exact = Reference.FindBoneIndex(TEXT("pelvis"));
    if (Exact != INDEX_NONE) return Exact;
    for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
        if (Reference.GetBoneName(Index).ToString().Contains(TEXT("pelvis"), ESearchCase::IgnoreCase)) return Index;
    return INDEX_NONE;
}
}

const CireAnimClips::FClipInfo& CireAnimClips::Analyze(const UAnimSequence* Sequence)
{
    static TMap<TWeakObjectPtr<const UAnimSequence>, FClipInfo> Cache;
    static const FClipInfo Invalid;
    if (!Sequence || !Sequence->GetSkeleton()) return Invalid;
    if (const FClipInfo* Found = Cache.Find(Sequence)) return *Found;
    FClipInfo Info;
    Info.Length = Sequence->GetPlayLength();
    const FReferenceSkeleton& Reference = Sequence->GetSkeleton()->GetReferenceSkeleton();
    const int32 Pelvis = FindPelvisBone(Reference);
    const int32 Foot = Reference.FindBoneIndex(TEXT("foot_l"));
    const int32 BallL = Reference.FindBoneIndex(TEXT("ball_l")), BallR = Reference.FindBoneIndex(TEXT("ball_r"));
    if (BallL != INDEX_NONE && BallR != INDEX_NONE)
        Info.StanceBallZ = static_cast<float>(.5 * (ComponentBone(*Sequence, Reference, BallL, 0.).GetLocation().Z + ComponentBone(*Sequence, Reference, BallR, 0.).GetLocation().Z));
    if (Pelvis != INDEX_NONE && Info.Length > KINDA_SMALL_NUMBER)
    {
        FTransform ReferencePelvis = FTransform::Identity;
        for (int32 Index = Pelvis; Index != INDEX_NONE; Index = Reference.GetParentIndex(Index))
            ReferencePelvis = ReferencePelvis * Reference.GetRefBonePose()[Index];
        Info.ReferencePelvis = FVector2D(ReferencePelvis.GetLocation());
        constexpr int32 Samples = 32;
        double SumT = 0, SumTT = 0;
        FVector2D SumP = FVector2D::ZeroVector, SumTP = FVector2D::ZeroVector;
        double ApexZ = -TNumericLimits<double>::Max();
        bool bFinite = true;
        for (int32 Index = 0; Index <= Samples; ++Index)
        {
            const double Time = Info.Length * Index / Samples;
            const FVector P = ComponentBone(*Sequence, Reference, Pelvis, Time).GetLocation();
            bFinite &= !P.ContainsNaN();
            const FVector2D XY(P);
            SumT += Time; SumTT += Time * Time; SumP += XY; SumTP += XY * Time;
            if (Foot != INDEX_NONE && Index < Samples)
            {
                const double Z = ComponentBone(*Sequence, Reference, Foot, Time).GetLocation().Z;
                if (Z > ApexZ) { ApexZ = Z; Info.LeftFootApexPhase = static_cast<float>(Index) / Samples; }
            }
        }
        const double N = Samples + 1, Denominator = N * SumTT - SumT * SumT;
        if (bFinite && FMath::Abs(Denominator) > UE_SMALL_NUMBER)
        {
            Info.DriftVelocity = (SumTP * N - SumP * SumT) / Denominator;
            Info.DriftOffset = (SumP - Info.DriftVelocity * SumT) / N;
            Info.bValid = !Info.DriftVelocity.ContainsNaN() && !Info.DriftOffset.ContainsNaN();
        }
    }
    return Cache.Add(Sequence, Info);
}

FVector2D CireAnimClips::DriftAt(const FClipInfo& Info, float Time, const FVector2D& Target)
{
    return Info.bValid ? Info.DriftOffset + Info.DriftVelocity * Time - Target : FVector2D::ZeroVector;
}

void CireAnimClips::RemovePelvisDrift(FCompactPose& Pose, const FVector2D& Drift)
{
    if (Drift.IsNearlyZero()) return;
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    const FReferenceSkeleton& Reference = Bones.GetReferenceSkeleton();
    const int32 Pelvis = FindPelvisBone(Reference);
    if (Pelvis == INDEX_NONE) return;
    const FCompactPoseBoneIndex PelvisIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Pelvis));
    if (!PelvisIndex.IsValid()) return;
    // Express the component-space correction in the pelvis parent's space (root carries scale 100).
    FTransform Parent = FTransform::Identity;
    for (FCompactPoseBoneIndex Index = Bones.GetParentBoneIndex(PelvisIndex); Index.IsValid(); Index = Bones.GetParentBoneIndex(Index))
        Parent = Parent * Pose[Index];
    const FVector Local = Parent.InverseTransformVector(FVector(Drift.X, Drift.Y, 0));
    if (!Local.ContainsNaN()) Pose[PelvisIndex].AddToTranslation(-Local);
}

void CireAnimClips::UpperBodyMask(const FCompactPose& Pose, TArray<float>& OutUpper)
{
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    OutUpper.SetNumZeroed(Pose.GetNumBones());
    const FCompactPoseBoneIndex Spine = CompactIndex(Bones, TEXT("spine_01"));
    if (!Spine.IsValid()) { for (float& W : OutUpper) W = 1.f; return; }
    // Compact indices are parent-first, so a single forward pass propagates the flag.
    for (const FCompactPoseBoneIndex Bone : Pose.ForEachBoneIndex())
    {
        const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Bone);
        OutUpper[Bone.GetInt()] = (Bone == Spine || (Parent.IsValid() && OutUpper[Parent.GetInt()] > 0.f)) ? 1.f : 0.f;
    }
}

namespace
{
struct FLayerCopy
{
    const UAnimSequence* Sequence = nullptr;
    float Time = 0.f, Weight = 0.f, LowerBody = 1.f;
    bool bRemoveDrift = false;
    FVector2D PelvisTarget = FVector2D::ZeroVector;
    CireAnimClips::FClipInfo Info;
    void Copy(const FCireAnimLayer& Layer)
    {
        Sequence = Layer.Sequence; Time = Layer.Time; Weight = Layer.Weight; LowerBody = Layer.LowerBody;
        bRemoveDrift = Layer.bRemoveDrift; PelvisTarget = Layer.PelvisTarget;
        Info = Layer.bRemoveDrift && Sequence ? CireAnimClips::Analyze(Sequence) : CireAnimClips::FClipInfo();
    }
};

struct FCireMonsterAnimProxy : public FAnimInstanceProxy
{
    explicit FCireMonsterAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    FLayerCopy Idle, Walk, Run, Action, Death, Side;
    float MoveAlpha = 0.f, RunAlpha = 0.f, SideAlpha = 0.f;
    CireGrip::FHands Hands;
    UCireMonsterAnimInstance* Owner = nullptr;
    bool bLockRoot = false; // world-dressing

    virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
        auto* Monster = CastChecked<UCireMonsterAnimInstance>(Instance);
        Owner = Monster;
        Idle.Copy(Monster->Idle); Walk.Copy(Monster->Walk); Run.Copy(Monster->Run);
        Action.Copy(Monster->Action); Death.Copy(Monster->Death); Side.Copy(Monster->Side);
        SideAlpha = FMath::Clamp(Monster->SideAlpha, 0.f, 1.f);
        MoveAlpha = FMath::Clamp(Monster->MoveAlpha, 0.f, 1.f);
        RunAlpha = FMath::Clamp(Monster->RunAlpha, 0.f, 1.f);
        Hands = Monster->Hands;
        bLockRoot = Monster->bLockRootToReference; // world-dressing
    }

    static bool Sample(const FLayerCopy& Layer, FPoseContext& Into)
    {
        if (!Layer.Sequence) return false;
        FAnimationPoseData Data(Into);
        const float Time = FMath::Clamp(Layer.Time, 0.f, Layer.Sequence->GetPlayLength());
        // Root motion is never extracted: the capsule and CharacterMovement own the actor.
        Layer.Sequence->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(Time), false));
        if (Layer.bRemoveDrift && Layer.Info.bValid)
            CireAnimClips::RemovePelvisDrift(Into.Pose, CireAnimClips::DriftAt(Layer.Info, Time, Layer.PelvisTarget));
        return true;
    }

    static void Overlay(FPoseContext& Output, const FLayerCopy& Layer)
    {
        if (!Layer.Sequence || Layer.Weight <= KINDA_SMALL_NUMBER) return;
        FPoseContext Top(Output);
        if (!Sample(Layer, Top)) return;
        TArray<float> Weights;
        CireAnimClips::UpperBodyMask(Output.Pose, Weights);
        const float Weight = FMath::Clamp(Layer.Weight, 0.f, 1.f), Lower = FMath::Clamp(Layer.LowerBody, 0.f, 1.f);
        for (float& W : Weights) W = Weight * (W + (1.f - W) * Lower);
        FPoseContext Result(Output);
        FAnimationPoseData BaseData(Output), TopData(Top), ResultData(Result);
        FAnimationRuntime::BlendTwoPosesTogetherPerBone(BaseData, TopData, Weights, ResultData);
        Output.Pose.CopyBonesFrom(Result.Pose);
        Output.Curve.CopyFrom(Result.Curve);
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        if (!Sample(Idle, Output)) Output.ResetToRefPose();
        float FloorZ = 0.f;
        const bool bFloor = !bLockRoot && LowestFoot(Output.Pose, FloorZ); // monster-rig: the idle stance's floor
        if (MoveAlpha > KINDA_SMALL_NUMBER && (Walk.Sequence || Run.Sequence))
        {
            FPoseContext Moving(Output);
            const bool bBoth = Walk.Sequence && Run.Sequence && RunAlpha > KINDA_SMALL_NUMBER && RunAlpha < 1.f - KINDA_SMALL_NUMBER;
            if (bBoth)
            {
                Sample(Walk, Moving);
                FPoseContext Running(Output);
                Sample(Run, Running);
                FAnimationPoseData MovingData(Moving), RunningData(Running);
                FAnimationRuntime::BlendTwoPosesTogetherInPlace(MovingData, RunningData, 1.f - RunAlpha);
            }
            else Sample(RunAlpha >= .5f && Run.Sequence ? Run : Walk.Sequence ? Walk : Run, Moving);
            if (Side.Sequence && SideAlpha > KINDA_SMALL_NUMBER) // monster-rig: strafe / back-pedal / turn step
            {
                FPoseContext Sideways(Output);
                if (Sample(Side, Sideways))
                {
                    FAnimationPoseData MovingData(Moving), SideData(Sideways);
                    FAnimationRuntime::BlendTwoPosesTogetherInPlace(MovingData, SideData, 1.f - SideAlpha);
                }
            }
            FAnimationPoseData OutputData(Output), MovingData(Moving);
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutputData, MovingData, 1.f - MoveAlpha);
        }
        Overlay(Output, Action);
        if (bFloor && (MoveAlpha > KINDA_SMALL_NUMBER || Action.Weight > KINDA_SMALL_NUMBER) && Death.Weight <= KINDA_SMALL_NUMBER)
            KeepFeetOnFloor(Output.Pose, FloorZ);
        Overlay(Output, Death);
        if (Hands.Any()) CireGrip::Apply(Output.Pose, Hands);
        if (bLockRoot && Output.Pose.GetNumBones() > 0) // world-dressing: the armature proxy root stays at its bind transform
        {
            const FCompactPoseBoneIndex Root(0);
            Output.Pose[Root] = Output.Pose.GetRefPose(Root);
        }
        const bool bSane = PoseIsSane(Output.Pose);
        if (!bSane) Output.ResetToRefPose();
        if (Owner) Owner->bLastPoseRejected = !bSane;
        return true;
    }
};
}

FAnimInstanceProxy* UCireMonsterAnimInstance::CreateAnimInstanceProxy()
{
    return new FCireMonsterAnimProxy(this);
}
