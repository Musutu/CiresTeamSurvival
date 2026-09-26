#include "CireLocomotion.h"
#include "CireMonsterAnim.h"
#include "CireGrip.h"

#include "BonePose.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
TAutoConsoleVariable<int32> CVarLocoFeel(TEXT("cire.LocoFeel"), 1,
    TEXT("movement-feel: 1 = stride-matched locomotion, smooth visual turning, leg IK and lean. 0 = previous presentation."));

FTransform RefComponent(const FReferenceSkeleton& Reference, int32 Bone)
{
    FTransform Result = FTransform::Identity;
    for (int32 Index = Bone; Index != INDEX_NONE; Index = Reference.GetParentIndex(Index)) Result = Result * Reference.GetRefBonePose()[Index];
    return Result;
}

FTransform ClipComponent(const UAnimSequence& Sequence, const FReferenceSkeleton& Reference, int32 Bone, double Time)
{
    FTransform Result = FTransform::Identity;
    const FAnimExtractContext Context(Time, false);
    for (int32 Index = Bone; Index != INDEX_NONE; Index = Reference.GetParentIndex(Index))
    {
        FTransform Local = Reference.GetRefBonePose()[Index];
        Sequence.GetBoneTransform(Local, FSkeletonPoseBoneIndex(Index), Context, false);
        Result = Result * Local;
    }
    return Result;
}
}

bool CireLocomotion::Enabled()
{
    static const int32 CommandLine = []() { int32 V = 1; return FParse::Value(FCommandLine::Get(), TEXT("CireLocoFeel="), V) ? V : -1; }();
    if (CommandLine >= 0) return CommandLine != 0;
    return CVarLocoFeel.GetValueOnAnyThread() != 0;
}

void CireLocomotion::FindContactBones(const FReferenceSkeleton& Reference, TArray<int32>& Out)
{
    Out.Reset();
    const auto Pair = [&](const TCHAR* A, const TCHAR* B)
    {
        const int32 L = Reference.FindBoneIndex(A), R = Reference.FindBoneIndex(B);
        if (L == INDEX_NONE || R == INDEX_NONE) return false;
        Out = {L, R};
        return true;
    };
    if (Pair(TEXT("ball_l"), TEXT("ball_r")) || Pair(TEXT("foot_l"), TEXT("foot_r"))) return;
    const int32 Num = Reference.GetNum();
    if (Num < 2) return;
    TArray<FVector> P; P.SetNum(Num);
    TArray<int32> Children; Children.SetNumZeroed(Num);
    double MinZ = TNumericLimits<double>::Max(), MaxZ = -TNumericLimits<double>::Max();
    for (int32 I = 0; I < Num; ++I)
    {
        P[I] = RefComponent(Reference, I).GetLocation();
        if (Reference.GetParentIndex(I) != INDEX_NONE) ++Children[Reference.GetParentIndex(I)];
        MinZ = FMath::Min(MinZ, P[I].Z); MaxZ = FMath::Max(MaxZ, P[I].Z);
    }
    const double Height = FMath::Max(1.0, MaxZ - MinZ);
    TArray<int32> Low;
    for (int32 I = 1; I < Num; ++I) if (Children[I] == 0 && P[I].Z < MinZ + .15 * Height) Low.Add(I);
    if (Low.IsEmpty()) for (int32 I = 1; I < Num; ++I) if (P[I].Z < MinZ + .15 * Height) Low.Add(I);
    Low.Sort([&P](int32 A, int32 B) { return P[A].Z < P[B].Z; });
    for (const int32 Bone : Low)
    {
        bool bNear = false;
        for (const int32 Kept : Out) bNear |= FVector::Dist2D(P[Kept], P[Bone]) < .1 * Height;
        if (!bNear) Out.Add(Bone);
        if (Out.Num() == 4) break;
    }
}

const CireLocomotion::FGait& CireLocomotion::AnalyzeGait(const UAnimSequence* Sequence)
{
    static TMap<TWeakObjectPtr<const UAnimSequence>, FGait> Cache;
    static const FGait Invalid;
    if (!Sequence || !Sequence->GetSkeleton()) return Invalid;
    if (const FGait* Found = Cache.Find(Sequence)) return *Found;
    FGait Gait;
    Gait.Length = Sequence->GetPlayLength();
    const FReferenceSkeleton& Reference = Sequence->GetSkeleton()->GetReferenceSkeleton();
    TArray<int32> Contacts;
    FindContactBones(Reference, Contacts);
    if (!Contacts.IsEmpty() && Gait.Length > .05f)
    {
        const CireAnimClips::FClipInfo& Info = CireAnimClips::Analyze(Sequence);
        const FVector2D Drift = Info.bValid ? Info.DriftVelocity : FVector2D::ZeroVector;
        const int32 N = FMath::Clamp(FMath::RoundToInt(Gait.Length * 60.f), 24, 150);
        const double Dt = Gait.Length / N;
        FVector2D Sum = FVector2D::ZeroVector; double TotalDistance = 0, TotalTime = 0; int32 Stances = 0;
        for (const int32 Bone : Contacts)
        {
            TArray<FVector> P; P.SetNum(N);
            double MinZ = TNumericLimits<double>::Max(), MaxZ = -TNumericLimits<double>::Max();
            for (int32 I = 0; I < N; ++I)
            {
                P[I] = ClipComponent(*Sequence, Reference, Bone, Dt * I).GetLocation();
                MinZ = FMath::Min(MinZ, P[I].Z); MaxZ = FMath::Max(MaxZ, P[I].Z);
            }
            // Stance: the lowest fifth of the contact's lift range (a loop wraps its last sample onto the first). The
            // ground speed is the mean sweep over each whole stance (touch-down to lift-off), which keeps a planted foot
            // still on average; per-frame speeds would overweight the push-off.
            const double Stance = MinZ + FMath::Max(.2 * (MaxZ - MinZ), .25);
            TArray<bool> Down; Down.SetNum(N);
            int32 Count = 0;
            for (int32 I = 0; I < N; ++I) { Down[I] = P[I].Z <= Stance; Count += Down[I] ? 1 : 0; }
            if (Count == 0 || Count == N) continue;
            int32 First = 0;
            while (!(Down[First] && !Down[(First + N - 1) % N])) ++First;
            for (int32 Step = 0; Step < N;)
            {
                const int32 Start = (First + Step) % N;
                if (!Down[Start]) { ++Step; continue; }
                int32 Length = 0;
                while (Length < N && Down[(Start + Length) % N]) ++Length;
                Step += Length;
                if (Length < 3) continue;
                const double Duration = (Length - 1) * Dt;
                // A stance that runs across the loop seam continues from the next cycle, which starts displaced by
                // the clip's own travel (root-travelling Tripo clips); in-place clips have no drift.
                const int32 End = Start + Length - 1;
                const FVector2D EndPos = FVector2D(P[End % N]) + (End >= N ? Drift * Gait.Length : FVector2D::ZeroVector);
                const FVector2D Moved = EndPos - FVector2D(P[Start]) - Drift * Duration;
                if (Moved.ContainsNaN()) continue;
                Sum += Moved; TotalDistance += Moved.Size(); TotalTime += Duration; ++Stances;
            }
        }
        if (Stances > 0 && TotalTime > KINDA_SMALL_NUMBER)
        {
            Gait.Speed = static_cast<float>(TotalDistance / TotalTime);
            Gait.Direction = (-Sum).GetSafeNormal();
            Gait.bValid = FMath::IsFinite(Gait.Speed);
        }
    }
    return Cache.Add(Sequence, Gait);
}

float CireLocomotion::BlendNaturalSpeed(const UBlendSpace* Blend, float Direction, float AxisSpeed, float MeshScale)
{
    if (!Blend) return 0.f;
    TArray<FBlendSampleData> Samples; int32 Triangle = INDEX_NONE;
    if (!Blend->GetSamplesFromBlendInput(FVector(Direction, AxisSpeed, 0), Samples, Triangle, false)) return 0.f;
    double Distance = 0, Length = 0;
    for (const FBlendSampleData& Sample : Samples)
    {
        if (!Blend->IsValidBlendSampleIndex(Sample.SampleDataIndex)) continue;
        const FBlendSample& Source = Blend->GetBlendSample(Sample.SampleDataIndex);
        const UAnimSequence* Clip = Source.Animation;
        if (!Clip) continue;
        const float W = Sample.GetClampedWeight(), L = Clip->GetPlayLength();
        const FGait& Gait = AnalyzeGait(Clip);
        Distance += W * (Gait.bValid ? Gait.Speed : 0.f) * L;
        Length += W * L / FMath::Max(.01f, Source.RateScale);
    }
    return Length > KINDA_SMALL_NUMBER ? static_cast<float>(Distance / Length) * MeshScale : 0.f;
}

CireLocomotion::FBlendWarp CireLocomotion::WarpBlendSpace(const UBlendSpace* Blend, float Direction, float Speed, float MeshScale)
{
    FBlendWarp Out;
    if (!Blend) return Out;
    TArray<float> Axis;
    for (const FBlendSample& Sample : Blend->GetBlendSamples()) Axis.AddUnique(FMath::RoundToFloat(Sample.SampleValue.Y * 10.f) / 10.f);
    Axis.Sort();
    if (Axis.Num() < 2) return Out;
    // Natural stride speed at each moving grid speed for this direction; they must rise with the axis.
    TArray<float> Natural;
    for (int32 I = 1; I < Axis.Num(); ++I)
    {
        Natural.Add(BlendNaturalSpeed(Blend, Direction, Axis[I], MeshScale));
        if (Natural.Last() <= 5.f) return Out;
        // A faster grid row whose clip is not a longer stride (e.g. a brisk walk next to a short run) cannot be told
        // apart by speed: treat it as a small step up so the order holds and the play rate does the matching.
        if (Natural.Num() > 1) Natural.Last() = FMath::Max(Natural.Last(), Natural[Natural.Num() - 2] + 5.f);
    }
    const float Walk = Natural[0];
    Speed = FMath::Max(0.f, Speed);
    if (Speed < .45f * Walk)
        Out.AxisSpeed = Axis[1] * Speed / (.45f * Walk); // blend in from idle (a slow shuffle, not a stretched stride)
    else if (Speed >= Natural.Last())
        Out.AxisSpeed = Axis.Last();
    else
    {
        Out.AxisSpeed = Axis[1];
        for (int32 I = 0; I + 1 < Natural.Num(); ++I)
            if (Speed >= Natural[I] && Speed < Natural[I + 1])
                Out.AxisSpeed = FMath::Lerp(Axis[I + 1], Axis[I + 2], (Speed - Natural[I]) / (Natural[I + 1] - Natural[I]));
    }
    Out.Natural = BlendNaturalSpeed(Blend, Direction, Out.AxisSpeed, MeshScale);
    Out.PlayRate = Out.Natural > 1.f ? FMath::Clamp(Speed / Out.Natural, .5f, 1.75f) : 1.f;
    Out.bValid = true;
    return Out;
}

// ---- visual heading ------------------------------------------------------------------------------------
void CireLocomotion::FVisualTurn::Reset(float TargetYaw, const FVector& Location)
{
    bInit = true; bStepping = false; Yaw = FRotator::NormalizeAxis(TargetYaw); Offset = 0.f; Still = 0.f;
    StepWeight = 0.f; StepDelta = 0.f; LastActorYaw = Yaw; LastLocation = Location;
}

void CireLocomotion::FVisualTurn::Update(float ActorYaw, float TargetYaw, const FVector& Location, float Speed, float Dt, float MaxLag)
{
    // Spawns, teleports, respawns and paused/zero-delta evaluations start from rest.
    if (!bInit || Dt <= 0.f || Dt > .25f || !FMath::IsFinite(TargetYaw) || FVector::DistSquared2D(Location, LastLocation) > FMath::Square(450.f))
    {
        Reset(TargetYaw, Location); Offset = FRotator::NormalizeAxis(Yaw - ActorYaw); LastActorYaw = ActorYaw; return;
    }
    const float ActorRate = FMath::Abs(FMath::FindDeltaAngleDegrees(LastActorYaw, ActorYaw)) / Dt;
    Still = ActorRate < 25.f ? Still + Dt : 0.f;
    const float Delta = FMath::FindDeltaAngleDegrees(Yaw, TargetYaw);
    StepDelta = 0.f;
    if (Speed > 35.f)
    {
        // Travelling: a short exponential follow (about 55 ms) capped at 600 deg/s; the gait carries the turn.
        bStepping = false;
        Yaw += FMath::Clamp(Delta * (1.f - FMath::Exp(-18.f * Dt)), -600.f * Dt, 600.f * Dt);
    }
    else
    {
        // Standing: the feet stay planted for small turns; a larger turn (or one that has settled) steps round.
        if (!bStepping && (FMath::Abs(Delta) > MaxLag * .6f || (Still > .12f && FMath::Abs(Delta) > 8.f))) bStepping = true;
        if (bStepping)
        {
            // A bigger lag steps round faster (a snapped 180 takes about 0.3 s, a nudge settles gently).
            const float Rate = 260.f + 3.5f * FMath::Abs(Delta);
            const float Step = FMath::Clamp(Delta, -Rate * Dt, Rate * Dt);
            Yaw += Step; StepDelta = FMath::Abs(Step); if (Step != 0.f) StepSign = FMath::Sign(Step);
            if (FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, TargetYaw)) < 1.f) bStepping = false;
        }
    }
    Yaw = FRotator::NormalizeAxis(Yaw);
    StepPhase = FMath::Frac(StepPhase + StepDelta / 180.f); // one full stepping cycle per half turn
    StepWeight = FMath::FInterpConstantTo(StepWeight, bStepping && Speed <= 35.f ? 1.f : 0.f, Dt, 6.f);
    Offset = FRotator::NormalizeAxis(Yaw - ActorYaw);
    LastActorYaw = ActorYaw; LastLocation = Location;
}

float CireLocomotion::TravelWarp(float ActorYaw, const FVector& Velocity, float Limit, FVisualTurn& State, float Dt)
{
    if (Velocity.SizeSquared2D() < FMath::Square(35.f)) { State.ReverseHold = 0.f; return ActorYaw; }
    const float Travel = static_cast<float>(Velocity.Rotation().Yaw);
    float Rel = FMath::FindDeltaAngleDegrees(ActorYaw, Travel);
    // Backpedal only when the facing is held against the travel (kiting); a body that is turning round to its new
    // direction (reversal) keeps its forward gait and simply leads the turn.
    const bool bWants = FMath::Abs(Rel) > (State.bReverse ? 100.f : 120.f);
    State.ReverseHold = bWants ? State.ReverseHold + Dt : 0.f;
    if (!bWants) State.bReverse = false;
    else if (State.ReverseHold > .25f) State.bReverse = true;
    if (State.bReverse) Rel = FRotator::NormalizeAxis(Rel + 180.f);
    return ActorYaw + FMath::Clamp(Rel, -Limit, Limit);
}

float CireLocomotion::GaitSpeed(const UAnimSequence* Clip, float Scale, float Fallback)
{
    const FGait& Gait = AnalyzeGait(Clip);
    return Enabled() && Gait.bValid && Gait.Speed * Scale > 15.f ? Gait.Speed * Scale : Fallback;
}

// ---- leg IK ------------------------------------------------------------------------------------------------
void CireLocomotion::FLegIK::Update(const ACharacter& Owner, const USkeletalMeshComponent& Mesh, float Dt, bool bGrounded)
{
    if (!bChecked)
    {
        bChecked = true;
        const USkeletalMesh* Asset = Mesh.GetSkeletalMeshAsset();
        const FReferenceSkeleton* Ref = Asset ? &Asset->GetRefSkeleton() : nullptr;
        bHumanoid = Ref && Ref->FindBoneIndex(TEXT("foot_l")) != INDEX_NONE && Ref->FindBoneIndex(TEXT("foot_r")) != INDEX_NONE &&
            Ref->FindBoneIndex(TEXT("calf_l")) != INDEX_NONE && Ref->FindBoneIndex(TEXT("calf_r")) != INDEX_NONE &&
            Ref->FindBoneIndex(TEXT("thigh_l")) != INDEX_NONE && Ref->FindBoneIndex(TEXT("thigh_r")) != INDEX_NONE && Ref->FindBoneIndex(TEXT("pelvis")) != INDEX_NONE;
    }
    Dt = FMath::Clamp(Dt, 0.f, .1f);
    const UWorld* World = Owner.GetWorld();
    const UCapsuleComponent* Capsule = Owner.GetCapsuleComponent();
    float Want[2] = {0.f, 0.f};
    bool bActive = bHumanoid && bGrounded && World && Capsule && Enabled();
    if (bActive)
    {
        const FVector Center = Owner.GetActorLocation();
        const float Half = Capsule->GetScaledCapsuleHalfHeight(), Reach = FMath::Max(45.f, Half * .5f);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CireLegIK), false, &Owner);
        FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        const auto Ground = [&](const FVector& At, float& OutZ)
        {
            FHitResult Hit;
            const FVector From(At.X, At.Y, Center.Z - Half + Reach), To(At.X, At.Y, Center.Z - Half - Reach);
            if (!World->LineTraceSingleByObjectType(Hit, From, To, Objects, Query) || Hit.ImpactNormal.Z < .6) return false;
            OutZ = static_cast<float>(Hit.ImpactPoint.Z); return true;
        };
        float Base = 0.f;
        if (!Ground(Center, Base)) bActive = false;
        else
        {
            const FName Feet[2] = {TEXT("foot_l"), TEXT("foot_r")};
            for (int32 I = 0; I < 2; ++I)
            {
                float Z = Base;
                if (Ground(Mesh.GetBoneLocation(Feet[I]), Z)) Want[I] = FMath::Clamp(Z - Base, -Reach * .8f, Reach * .8f);
            }
        }
    }
    Weight = FMath::FInterpConstantTo(Weight, bActive ? 1.f : 0.f, Dt, 5.f);
    for (int32 I = 0; I < 2; ++I) Offset[I] = FMath::FInterpTo(Offset[I], Want[I], Dt, 14.f);
    Pelvis = FMath::FInterpTo(Pelvis, FMath::Min3(0.f, Want[0], Want[1]), Dt, 10.f);
}

void CireLocomotion::FPoseFeel::Set(const FVisualTurn& Turn, const FLegIK& Legs, float Scale, float InCounterFraction)
{
    CounterFraction = InCounterFraction;
    VisualYaw = Turn.Yaw; bVisual = Turn.bInit;
    RootYaw = Turn.Offset;
    SpineCounter = FMath::Clamp(-Turn.Offset * CounterFraction, -75.f, 75.f);
    const float Inv = 1.f / FMath::Max(.01f, Scale);
    IKWeight = Legs.bHumanoid ? Legs.Weight : 0.f;
    Pelvis = Legs.Pelvis * Inv; Foot[0] = Legs.Offset[0] * Inv; Foot[1] = Legs.Offset[1] * Inv;
}

void CireLocomotion::FPoseFeel::Resolve(const USkeletalMeshComponent* Mesh)
{
    if (!bVisual || !Mesh) return;
    const ACharacter* Owner = Cast<ACharacter>(Mesh->GetOwner());
    const double BaseYaw = Owner && Owner->GetMesh() == Mesh ? Owner->GetBaseRotationOffset().Rotator().Yaw : Mesh->GetRelativeRotation().Yaw;
    const float Heading = static_cast<float>(Mesh->GetComponentRotation().Yaw - BaseYaw);
    RootYaw = FRotator::NormalizeAxis(VisualYaw - Heading);
    SpineCounter = FMath::Clamp(-RootYaw * CounterFraction, -75.f, 75.f);
}

void CireLocomotion::ApplyPoseFeel(FCompactPose& Pose, const FPoseFeel& Feel)
{
    if (!Feel.Any() || Pose.GetNumBones() == 0) return;
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
    // Legs first, in the unturned component frame (the offsets are vertical, so the order with the yaw does not matter).
    if (Feel.IKWeight > .001f)
    {
        const int32 PelvisMesh = Ref.FindBoneIndex(TEXT("pelvis"));
        const FCompactPoseBoneIndex Pelvis = PelvisMesh == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PelvisMesh));
        if (Pelvis.IsValid() && FMath::Abs(Feel.Pelvis) > .01f)
        {
            FTransform Parent = FTransform::Identity;
            for (FCompactPoseBoneIndex I = Bones.GetParentBoneIndex(Pelvis); I.IsValid(); I = Bones.GetParentBoneIndex(I)) Parent = Parent * Pose[I];
            const FVector Local = Parent.InverseTransformVector(FVector(0, 0, Feel.Pelvis * Feel.IKWeight));
            if (!Local.ContainsNaN()) Pose[Pelvis].AddToTranslation(Local);
        }
        const TCHAR* Chains[2][3] = {{TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l")}, {TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r")}};
        for (int32 Side = 0; Side < 2; ++Side)
        {
            int32 Chain[3];
            bool bOk = true;
            for (int32 J = 0; J < 3; ++J) { Chain[J] = Ref.FindBoneIndex(Chains[Side][J]); bOk &= Chain[J] != INDEX_NONE && Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Chain[J])).IsValid(); }
            if (!bOk) continue;
            // The foot keeps its animated height above the ground and follows the ground under it.
            const float Raise = (Feel.Foot[Side] - Feel.Pelvis) * Feel.IKWeight;
            if (FMath::Abs(Raise) < .01f) continue;
            FTransform Target = CireGrip::ComponentBone(Pose, Chain[2]);
            Target.AddToTranslation(FVector(0, 0, Raise));
            CireGrip::SolveTwoBone(Pose, Chain, Target, 1.f);
        }
    }
    if (FMath::Abs(Feel.RootYaw) > .01f)
    {
        const FCompactPoseBoneIndex Root(0);
        const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(Feel.RootYaw));
        Pose[Root].SetRotation((Turn * Pose[Root].GetRotation()).GetNormalized());
        Pose[Root].SetTranslation(Turn.RotateVector(Pose[Root].GetTranslation()));
    }
    if (FMath::Abs(Feel.SpineCounter) > .01f) CireGrip::TwistSpine(Pose, Feel.SpineCounter);
}

#if !UE_BUILD_SHIPPING
bool CireLocomotion::RunTests()
{
    bool bOk = true; int32 Checks = 0;
    const auto Check = [&](bool b, const TCHAR* What) { ++Checks; if (!b) { bOk = false; UE_LOG(LogTemp, Error, TEXT("CIRE_LOCOMOTION_TEST_FAIL %s"), What); } };
    const float Dt = 1.f / 60.f;
    const FVector Here(0, 0, 0);
    // A snapped 180 degree facing while standing: the body does not jump, it steps round and settles.
    {
        FVisualTurn T; T.Update(90.f, 90.f, Here, 0.f, Dt, 70.f);
        float MaxStep = 0.f, Previous = T.Yaw;
        T.Update(-90.f, -90.f, Here, 0.f, Dt, 70.f);
        Check(FMath::Abs(FMath::FindDeltaAngleDegrees(T.Yaw, 90.f)) < 16.f, TEXT("snapped facing does not show in one frame"));
        for (int32 I = 0; I < 60; ++I) { Previous = T.Yaw; T.Update(-90.f, -90.f, Here, 0.f, Dt, 70.f); MaxStep = FMath::Max(MaxStep, FMath::Abs(FMath::FindDeltaAngleDegrees(Previous, T.Yaw))); }
        Check(FMath::Abs(T.Offset) < 1.5f, TEXT("standing turn settles on the facing within a second"));
        Check(MaxStep < 16.f, TEXT("standing turn has no single-frame snap"));
        Check(T.StepPhase > 0.f, TEXT("standing turn steps the legs"));
    }
    // Travelling: a 90 degree heading change is followed smoothly and fully.
    {
        FVisualTurn T; T.Update(0.f, 0.f, Here, 400.f, Dt, 70.f);
        float MaxStep = 0.f;
        for (int32 I = 0; I < 30; ++I) { const float P = T.Yaw; T.Update(90.f, 90.f, Here, 400.f, Dt, 70.f); MaxStep = FMath::Max(MaxStep, FMath::Abs(FMath::FindDeltaAngleDegrees(P, T.Yaw))); }
        Check(FMath::Abs(T.Offset) < 3.f && MaxStep <= 10.01f, TEXT("travelling turn follows within half a second at <= 600 deg/s"));
        T.Update(0.f, 0.f, FVector(5000, 0, 0), 400.f, Dt, 70.f);
        Check(T.Offset == 0.f, TEXT("teleport restarts from rest"));
    }
    // Forward-only gaits: a sidestep warps the legs toward travel, a held backpedal reverses the gait.
    {
        FVisualTurn T;
        Check(FMath::IsNearlyEqual(TravelWarp(0.f, FVector(0, 200, 0), 70.f, T, Dt), 70.f), TEXT("sidestep warps the legs 70 degrees"));
        float Yaw = 0.f;
        for (int32 I = 0; I < 20; ++I) Yaw = TravelWarp(0.f, FVector(-200, 0, 0), 70.f, T, Dt);
        Check(T.bReverse && FMath::IsNearlyZero(FRotator::NormalizeAxis(Yaw)), TEXT("held backpedal runs the gait backwards facing forward"));
        FVisualTurn U; TravelWarp(0.f, FVector(-200, 0, 0), 70.f, U, Dt);
        Check(!U.bReverse, TEXT("a reversal that is turning round keeps the forward gait"));
    }
    Check(!WarpBlendSpace(nullptr, 0.f, 300.f, 1.f).bValid && !AnalyzeGait(nullptr).bValid, TEXT("missing assets are quiet"));
    UE_LOG(LogTemp, Display, TEXT("%s checks=%d"), bOk ? TEXT("CIRE_LOCOMOTION_TESTS_PASS") : TEXT("CIRE_LOCOMOTION_TESTS_FAIL"), Checks);
    return bOk;
}
#endif
