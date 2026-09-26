#include "CireWeaponSockets.h"

#include "CireFabAnimation.h"
#include "CireGrip.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/Skeleton.h"
#include "BonePose.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireWeaponSockets, Log, All);

namespace
{
using namespace CireWeaponSockets;

TAutoConsoleVariable<int32> CVarLegacyGrips(TEXT("cire.Grips.Legacy"), 0,
    TEXT("1: weapons keep the bind-pose hand grip. 0: weapons follow the grip the Fab clips were authored for (Content/Data/WeaponSockets.json)."));

struct FOff { FName Main; FTransform OffInMain; };
struct FSocketData
{
    bool bLoaded = false;
    TMap<FString, TMap<FName, FHandFrame>> Sets;   // set -> hand -> frame
    TMap<FString, FOff> OffHands;                  // set -> second hand
    TArray<TPair<FString, FString>> CalibrationClips; // Fab clip name -> mannequin source clip
};
FSocketData GData;
TMap<TPair<TWeakObjectPtr<const USkeletalMesh>, FString>, FCalibration> GCalibrations; // (body, preferred set)

FVector Vec(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FVector& Default)
{
    const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
    if (!O->TryGetArrayField(Key, V) || V->Num() != 3) return Default;
    const FVector R((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber());
    return R.ContainsNaN() ? Default : R;
}

const FSocketData& Data()
{
    if (GData.bLoaded) return GData;
    GData = FSocketData(); GData.bLoaded = true;
    FString Text; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/WeaponSockets.json"))) || Text.Len() > 300000 ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
    {
        UE_LOG(LogCireWeaponSockets, Warning, TEXT("CIRE_WEAPON_SOCKETS_DATA missing WeaponSockets.json; weapons keep the bind-pose grip."));
        return GData;
    }
    const TSharedPtr<FJsonObject>* Sets = nullptr;
    if (Root->TryGetObjectField(TEXT("sets"), Sets))
        for (const auto& SetPair : (*Sets)->Values)
        {
            const TSharedPtr<FJsonObject>* Hands = nullptr;
            if (!SetPair.Value->TryGetObject(Hands)) continue;
            const FString Set(SetPair.Key.ToView());
            for (const auto& HandPair : (*Hands)->Values)
            {
                const TSharedPtr<FJsonObject>* O = nullptr;
                if (!HandPair.Value->TryGetObject(O)) continue;
                const FString Key(HandPair.Key.ToView());
                if (Key == TEXT("offHand"))
                {
                    FString Main; const TArray<TSharedPtr<FJsonValue>>* Q = nullptr;
                    if (!(*O)->TryGetStringField(TEXT("main"), Main) || !(*O)->TryGetArrayField(TEXT("quat"), Q) || Q->Num() != 4) continue;
                    const FQuat Rot((*Q)[0]->AsNumber(), (*Q)[1]->AsNumber(), (*Q)[2]->AsNumber(), (*Q)[3]->AsNumber());
                    GData.OffHands.Add(Set, FOff{FName(*Main), FTransform(Rot.GetNormalized(), Vec(*O, TEXT("loc"), FVector::ZeroVector))});
                    continue;
                }
                if (Key != TEXT("hand_r") && Key != TEXT("hand_l")) continue;
                FHandFrame F; FString Bone;
                if (!(*O)->TryGetStringField(TEXT("bone"), Bone)) continue;
                F.Bone = FName(*Bone); F.Point = Vec(*O, TEXT("point"), FVector::ZeroVector);
                F.Tip = Vec(*O, TEXT("tip"), FVector::UpVector).GetSafeNormal(); F.Edge = Vec(*O, TEXT("edge"), FVector::ForwardVector);
                F.Edge = (F.Edge - F.Tip * FVector::DotProduct(F.Edge, F.Tip)).GetSafeNormal();
                (*O)->TryGetBoolField(TEXT("shield"), F.bShield); (*O)->TryGetStringField(TEXT("source"), F.Source);
                FString Grip; F.bStock = (*O)->TryGetStringField(TEXT("grip"), Grip) && Grip == TEXT("stock");
                F.bValid = !F.Tip.IsNearlyZero() && !F.Edge.IsNearlyZero();
                if (F.bValid) GData.Sets.FindOrAdd(Set).Add(FName(*Key), F);
            }
        }
    const TArray<TSharedPtr<FJsonValue>>* Clips = nullptr;
    if (Root->TryGetArrayField(TEXT("calibration"), Clips))
        for (const auto& V : *Clips)
        {
            const TSharedPtr<FJsonObject>* O = nullptr; FString Clip, Source;
            if (V->TryGetObject(O) && (*O)->TryGetStringField(TEXT("clip"), Clip) && (*O)->TryGetStringField(TEXT("source"), Source) && Source.StartsWith(TEXT("/Game/")))
                GData.CalibrationClips.Add({Clip, Source});
        }
    UE_LOG(LogCireWeaponSockets, Log, TEXT("CIRE_WEAPON_SOCKETS_DATA sets=%d offHands=%d calibrationClips=%d"), GData.Sets.Num(), GData.OffHands.Num(), GData.CalibrationClips.Num());
    return GData;
}

/** Component-space transforms of Names in Sequence at Time, evaluated on Asset (a skeletal mesh or a skeleton). */
bool Evaluate(const UAnimSequence& Sequence, const UObject& Asset, const FReferenceSkeleton& Ref, float Time, const TArray<FName>& Names, TArray<FTransform>& Out)
{
    FMemMark Mark(FMemStack::Get());
    TArray<FBoneIndexType> Required; Required.Reserve(Ref.GetNum());
    for (int32 I = 0; I < Ref.GetNum(); ++I) Required.Add(static_cast<FBoneIndexType>(I));
    FBoneContainer Container;
    Container.InitializeTo(Required, UE::Anim::FCurveFilterSettings(), Asset);
    if (!Container.IsValid()) return false;
    FCompactPose Pose; Pose.SetBoneContainer(&Container); Pose.ResetToRefPose();
    FBlendedCurve Curve; Curve.InitFrom(Container);
    UE::Anim::FStackAttributeContainer Attributes;
    FAnimationPoseData PoseData(Pose, Curve, Attributes);
    Sequence.GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Time), false));
    Out.Reset();
    for (const FName& Name : Names)
    {
        const int32 Index = Ref.FindBoneIndex(Name);
        if (Index == INDEX_NONE) return false;
        Out.Add(CireGrip::ComponentBone(Pose, Index));
    }
    return true;
}

template<typename T> T* LoadIfPresent(const FString& Path)
{
    const FString Package = FPackageName::ObjectPathToPackageName(Path);
    if (!FPackageName::IsValidLongPackageName(Package) || !FPackageName::DoesPackageExist(Package)) return nullptr;
    return LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

float Forearm(const FReferenceSkeleton& Ref, const TCHAR* Side)
{
    const FVector Hand = CireGrip::ReferenceComponent(Ref, FName(FString(TEXT("hand")) + Side)).GetLocation();
    const FVector Elbow = CireGrip::ReferenceComponent(Ref, FName(FString(TEXT("lowerarm")) + Side)).GetLocation();
    return static_cast<float>((Hand - Elbow).Size());
}
}

bool CireWeaponSockets::Legacy()
{
    static const bool bFlag = FParse::Param(FCommandLine::Get(), TEXT("CireLegacyGrips"));
    return bFlag || CVarLegacyGrips.GetValueOnAnyThread() != 0;
}

FString CireWeaponSockets::SetFor(const USkeletalMesh* Body, const FString& Style, const FString& Motion)
{
    if (!Body) return FString();
    UAnimSequence* Clip = nullptr; FString Name;
    if (!CireFabAnimation::Pick(Body, CireFabAnimation::FolderFor(Body), Style, Motion, TEXT("attack"), 0, Clip, Name) || !Clip) return FString();
    const int32 Cut = Name.Find(TEXT("_attack"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    return Cut > 0 ? Name.Left(Cut) : FString();
}

CireWeaponSockets::FHandFrame CireWeaponSockets::Frame(const FString& Set, FName Hand)
{
    const auto* Hands = Data().Sets.Find(Set);
    const FHandFrame* F = Hands ? Hands->Find(Hand) : nullptr;
    return F ? *F : FHandFrame();
}

bool CireWeaponSockets::OffHand(const FString& Set, FName MainHand, FTransform& OutOffInMain)
{
    const FOff* Off = Data().OffHands.Find(Set);
    if (!Off || Off->Main != MainHand) return false;
    OutOffInMain = Off->OffInMain;
    return true;
}

const CireWeaponSockets::FCalibration& CireWeaponSockets::Calibration(const USkeletalMesh& Body, const FString& Set)
{
    const TPair<TWeakObjectPtr<const USkeletalMesh>, FString> Key(&Body, Set);
    if (const FCalibration* Found = GCalibrations.Find(Key)) return *Found;
    FCalibration& Out = GCalibrations.Add(Key);
    const FString Folder = CireFabAnimation::FolderFor(&Body);
    if (Folder.IsEmpty()) return Out;
    static const TArray<FName> Bones = {TEXT("hand_r"), TEXT("hand_l"), TEXT("lowerarm_r"), TEXT("lowerarm_l")};
    // The set's own calibration clip first, then any other the body has.
    TArray<TPair<FString, FString>> Clips = Data().CalibrationClips;
    if (!Set.IsEmpty())
        Clips.StableSort([&Set](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
            { return A.Key.StartsWith(Set + TEXT("_")) && !B.Key.StartsWith(Set + TEXT("_")); });
    for (const auto& Pair : Clips)
    {
        UAnimSequence* Target = CireFabAnimation::Find(&Body, Folder, Pair.Key);
        UAnimSequence* Source = Target ? LoadIfPresent<UAnimSequence>(Pair.Value) : nullptr;
        USkeleton* SourceSkeleton = Source ? Source->GetSkeleton() : nullptr;
        if (!Target || !SourceSkeleton) continue;
        const FReferenceSkeleton& SourceRef = SourceSkeleton->GetReferenceSkeleton();
        const float Length = FMath::Min(Target->GetPlayLength(), Source->GetPlayLength());
        TArray<TArray<FQuat>> Samples;
        for (const float Phase : {.15f, .35f, .55f, .75f, .95f})
        {
            TArray<FTransform> S, T;
            if (!Evaluate(*Source, *SourceSkeleton, SourceRef, Phase * Length, Bones, S) || !Evaluate(*Target, Body, Body.GetRefSkeleton(), Phase * Length, Bones, T)) break;
            TArray<FQuat>& Q = Samples.AddDefaulted_GetRef();
            for (int32 I = 0; I < Bones.Num(); ++I) Q.Add((S[I].GetRotation().Inverse() * T[I].GetRotation()).GetNormalized());
        }
        if (Samples.Num() < 3) continue;
        // The mean over the frames is the estimate; the spread says how constant the retarget rotation is (hand IK
        // goals in the retargeter bend it a little frame to frame).
        float Spread = 0.f;
        for (int32 I = 0; I < Bones.Num(); ++I)
        {
            FQuat Sum(0, 0, 0, 0);
            for (const auto& Q : Samples) { const FQuat A = (Q[I] | Samples[0][I]) < 0 ? FQuat(-Q[I].X, -Q[I].Y, -Q[I].Z, -Q[I].W) : Q[I]; Sum = FQuat(Sum.X + A.X, Sum.Y + A.Y, Sum.Z + A.Z, Sum.W + A.W); }
            const FQuat Mean = Sum.GetNormalized();
            for (const auto& Q : Samples) Spread = FMath::Max(Spread, FMath::RadiansToDegrees(static_cast<float>(Mean.AngularDistance(Q[I]))));
            Out.Q.Add(Bones[I], Mean);
        }
        const float MannyForearm = FMath::Max(1.f, Forearm(SourceRef, TEXT("_r")));
        const float BodyForearm = Forearm(Body.GetRefSkeleton(), TEXT("_r"));
        Out.Scale = BodyForearm > UE_SMALL_NUMBER ? BodyForearm / MannyForearm : 1.f;
        Out.SpreadDeg = Spread; Out.Clip = Pair.Key; Out.bValid = true;
        UE_LOG(LogCireWeaponSockets, Log, TEXT("CIRE_WEAPON_SOCKETS_CALIBRATED %s folder=%s clip=%s scale=%.4f spread=%.2fdeg"), *Body.GetName(), *Folder, *Pair.Key, Out.Scale, Spread);
        break;
    }
    return Out;
}

bool CireWeaponSockets::Intended(const USkeletalMesh& Body, const FString& Set, FName Hand, FTransform& OutGripComponent, FHandFrame& OutFrame)
{
    OutFrame = Frame(Set, Hand);
    if (!OutFrame.bValid) return false;
    const FCalibration& Cal = Calibration(Body, Set);
    const FQuat* Q = Cal.bValid ? Cal.Q.Find(OutFrame.Bone) : nullptr;
    const FReferenceSkeleton& Ref = Body.GetRefSkeleton();
    if (!Q || Ref.FindBoneIndex(OutFrame.Bone) == INDEX_NONE) return false;
    const FTransform Bone = CireGrip::ReferenceComponent(Ref, OutFrame.Bone);
    const FQuat Manny = FRotationMatrix::MakeFromZX(OutFrame.Tip, OutFrame.Edge).ToQuat();
    const FQuat InBone = Q->Inverse() * Manny;
    const FVector Point = Q->Inverse().RotateVector(OutFrame.Point) * Cal.Scale;
    OutGripComponent = FTransform((Bone.GetRotation() * InBone).GetNormalized(), Bone.GetLocation() + Bone.GetRotation().RotateVector(Point), FVector::OneVector);
    return !OutGripComponent.ContainsNaN();
}

bool CireWeaponSockets::IntendedOffHand(const USkeletalMesh& Body, const FString& Set, FName MainHand, FTransform& OutOffHandInMain)
{
    FTransform Off;
    if (!OffHand(Set, MainHand, Off)) return false;
    const FCalibration& Cal = Calibration(Body, Set);
    const FName OffHandBone = MainHand == TEXT("hand_r") ? FName(TEXT("hand_l")) : FName(TEXT("hand_r"));
    const FQuat* QMain = Cal.bValid ? Cal.Q.Find(MainHand) : nullptr;
    const FQuat* QOff = Cal.bValid ? Cal.Q.Find(OffHandBone) : nullptr;
    if (!QMain || !QOff) return false;
    const FReferenceSkeleton& Ref = Body.GetRefSkeleton();
    const FTransform Main = CireGrip::ReferenceComponent(Ref, MainHand), OffRef = CireGrip::ReferenceComponent(Ref, OffHandBone);
    // Manny: OffWorld = MainWorld * M. Body: Main_b = Main_m * Qm, Off_b = Off_m * Qo  =>  Off_b = Main_b * (Qm^-1 M Qo).
    const FQuat Rot = QMain->Inverse() * Off.GetRotation() * *QOff;
    const FVector Loc = QMain->Inverse().RotateVector(Off.GetLocation()) * Cal.Scale;
    const FTransform OffComponent((Main.GetRotation() * Rot).GetNormalized(), Main.GetLocation() + Main.GetRotation().RotateVector(Loc), OffRef.GetScale3D());
    OutOffHandInMain = OffComponent.GetRelativeTransform(Main);
    return !OutOffHandInMain.ContainsNaN();
}

FTransform CireWeaponSockets::PropFrame(const UStaticMesh& Mesh, const FVector& Handle, const FVector& Axis, const FVector& Edge, bool bShield)
{
    FVector Tip = Axis.GetSafeNormal();
    FVector Side = -Edge; // shields: the grip data's edge points from the face into the strap
    if (!bShield)
    {
        // The business end is the far end of the handle axis (blade, head, spear tip), read from the bounds.
        const FBox Box = Mesh.GetBoundingBox();
        double Far = -1.e9, Near = 1.e9;
        for (int32 I = 0; I < 8; ++I)
        {
            const FVector Corner((I & 1) ? Box.Max.X : Box.Min.X, (I & 2) ? Box.Max.Y : Box.Min.Y, (I & 4) ? Box.Max.Z : Box.Min.Z);
            const double D = FVector::DotProduct(Corner - Handle, Tip);
            Far = FMath::Max(Far, D); Near = FMath::Min(Near, D);
        }
        if (Far < -Near) Tip = -Tip;
        Side = Edge;
    }
    Side = (Side - Tip * FVector::DotProduct(Side, Tip)).GetSafeNormal();
    if (Side.IsNearlyZero()) Side = FVector::CrossProduct(Tip, FVector::UpVector).GetSafeNormal();
    return FTransform(FRotationMatrix::MakeFromZX(Tip, Side).ToQuat(), Handle, FVector::OneVector);
}

FTransform CireWeaponSockets::PropGrip(const UStaticMesh& Mesh, const CireGrip::FWeapon& Weapon, const FHandFrame& Frame)
{
    if (Frame.bStock && !Weapon.bShield && !Weapon.OffHand.IsNearlyZero())
    {
        // The given muzzle direction is kept as is (the bounds test of PropFrame would point it at the longer stock end).
        const FVector Muzzle = Weapon.OffAxis.GetSafeNormal();
        FVector Up = Weapon.Axis - Muzzle * FVector::DotProduct(Weapon.Axis, Muzzle);
        if (Up.Normalize() && !Muzzle.IsNearlyZero())
            return FTransform(FRotationMatrix::MakeFromZX(Muzzle, Up).ToQuat(), Weapon.OffHand, FVector::OneVector);
    }
    return PropFrame(Mesh, Weapon.Handle, Weapon.Axis, Weapon.Edge, Weapon.bShield);
}

#if !UE_BUILD_SHIPPING
bool CireWeaponSockets::RunSmoke()
{
    const FSocketData& D = Data();
    bool bPass = true;
    auto Check = [&](bool b, const FString& Why) { if (!b) { bPass = false; UE_LOG(LogCireWeaponSockets, Error, TEXT("CIRE_WEAPON_SOCKETS_CHECK_FAIL %s"), *Why); } };
    Check(D.Sets.Num() >= 6, TEXT("WeaponSockets.json lists the authored sets"));
    Check(D.CalibrationClips.Num() >= 1, TEXT("WeaponSockets.json lists calibration clips"));
    for (const auto& Set : D.Sets)
        for (const auto& Hand : Set.Value)
            Check(Hand.Value.bValid && FMath::Abs(FVector::DotProduct(Hand.Value.Tip, Hand.Value.Edge)) < .01f && Hand.Value.Point.Size() < 60.f,
                FString::Printf(TEXT("%s/%s frame is orthonormal and near the hand"), *Set.Key, *Hand.Key.ToString()));
    UE_LOG(LogCireWeaponSockets, Display, TEXT("CIRE_WEAPON_SOCKETS_SMOKE_%s sets=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), D.Sets.Num());
    return bPass;
}
#endif
