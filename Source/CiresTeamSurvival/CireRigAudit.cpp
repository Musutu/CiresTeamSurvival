#include "CireRigAudit.h"

#include "CireGrip.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "GameFramework/Actor.h"

namespace
{
FVector BindForward(const FReferenceSkeleton& Ref)
{
    FVector F = FVector::ZeroVector;
    for (const TCHAR* Side : {TEXT("_l"), TEXT("_r")})
    {
        const FName Ball(FString(TEXT("ball")) + Side), Foot(FString(TEXT("foot")) + Side);
        if (Ref.FindBoneIndex(Ball) == INDEX_NONE || Ref.FindBoneIndex(Foot) == INDEX_NONE) continue;
        F += CireGrip::ReferenceComponent(Ref, Ball).GetLocation() - CireGrip::ReferenceComponent(Ref, Foot).GetLocation();
    }
    F = F.GetSafeNormal2D();
    return F.IsNearlyZero() ? FVector(0, 1, 0) : F; // Tripo bodies face +Y in mesh space
}
}

bool CireRigAudit::IsDefect(const FArm& Arm)
{
    // A deep forward bend (hand at the shoulder, 165+ deg) is natural flexion; only a backwards bend is a defect.
    return Arm.bValid && Arm.ElbowDeg < HyperextensionLimitDeg;
}

bool CireRigAudit::AnteriorLocal(const FReferenceSkeleton& Ref, int32 Upper, int32 Lower, int32 Hand, FVector& OutLocal, FVector* OutBindForward)
{
    if (!Ref.IsValidIndex(Upper) || !Ref.IsValidIndex(Lower) || !Ref.IsValidIndex(Hand)) return false;
    const FTransform UB = CireGrip::ReferenceComponent(Ref, Ref.GetBoneName(Upper));
    const FVector E = CireGrip::ReferenceComponent(Ref, Ref.GetBoneName(Lower)).GetLocation();
    const FVector W = CireGrip::ReferenceComponent(Ref, Ref.GetBoneName(Hand)).GetLocation();
    const FVector U = (E - UB.GetLocation()).GetSafeNormal(), F = (W - E).GetSafeNormal();
    const FVector Forward = BindForward(Ref);
    if (OutBindForward) *OutBindForward = Forward;
    if (U.IsNearlyZero() || F.IsNearlyZero()) return false;
    FVector Ant = F - U * FVector::DotProduct(F, U);
    // A bent bind arm (over ~20 degrees) is its own hinge; a straight one takes the body's front.
    if (Ant.Size() < FMath::Sin(FMath::DegreesToRadians(20.f))) Ant = Forward - U * FVector::DotProduct(Forward, U);
    if (!Ant.Normalize()) return false;
    OutLocal = UB.GetRotation().UnrotateVector(Ant);
    return true;
}

CireRigAudit::FArm CireRigAudit::MeasurePose(const USkeletalMesh& Body, TConstArrayView<FTransform> Component, bool bRight)
{
    FArm Out;
    const FReferenceSkeleton& Ref = Body.GetRefSkeleton();
    const TCHAR* Side = bRight ? TEXT("_r") : TEXT("_l");
    const int32 U = Ref.FindBoneIndex(FName(FString(TEXT("upperarm")) + Side));
    const int32 L = Ref.FindBoneIndex(FName(FString(TEXT("lowerarm")) + Side));
    const int32 H = Ref.FindBoneIndex(FName(FString(TEXT("hand")) + Side));
    int32 M = Ref.FindBoneIndex(FName(FString(TEXT("middle_01")) + Side));
    if (U == INDEX_NONE || L == INDEX_NONE || H == INDEX_NONE || !Component.IsValidIndex(FMath::Max3(U, L, H))) return Out;
    if (!Component.IsValidIndex(M)) M = INDEX_NONE;
    const FTransform UB = CireGrip::ReferenceComponent(Ref, Ref.GetBoneName(U));
    const FTransform LB = CireGrip::ReferenceComponent(Ref, Ref.GetBoneName(L));
    const FTransform HB = CireGrip::ReferenceComponent(Ref, Ref.GetBoneName(H));
    FVector Forward, AntLocal;
    if (!AnteriorLocal(Ref, U, L, H, AntLocal, &Forward)) return Out;
    const FTransform& UN = Component[U]; const FTransform& LN = Component[L]; const FTransform& HN = Component[H];
    const FVector Up = (LN.GetLocation() - UN.GetLocation()).GetSafeNormal();
    const FVector Fore = (HN.GetLocation() - LN.GetLocation()).GetSafeNormal();
    if (Up.IsNearlyZero() || Fore.IsNearlyZero()) return Out;
    FVector Ant = UN.GetRotation().RotateVector(AntLocal);
    Ant = (Ant - Up * FVector::DotProduct(Ant, Up)).GetSafeNormal();
    const FVector Perp = Fore - Up * FVector::DotProduct(Fore, Up);
    Out.ElbowDeg = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Perp, Ant), FVector::DotProduct(Fore, Up)));
    // The elbow tip points away from the forearm's swing.
    const FVector Tip = -Perp.GetSafeNormal();
    Out.ElbowFwd = Perp.Size() > .2 ? static_cast<float>(FVector::DotProduct(Tip, Forward)) : 0.f;
    // Hand twist about its long axis (hand -> middle knuckle, else the forearm line) relative to the upper arm.
    const FVector AxisBindComp = M != INDEX_NONE ? (CireGrip::ReferenceComponent(Ref, Ref.GetBoneName(M)).GetLocation() - HB.GetLocation()) : (HB.GetLocation() - LB.GetLocation());
    const FVector AxisLocal = HB.GetRotation().UnrotateVector(AxisBindComp).GetSafeNormal();
    if (!AxisLocal.IsNearlyZero())
    {
        const FQuat RelBind = UB.GetRotation().Inverse() * HB.GetRotation();
        const FQuat RelNow = UN.GetRotation().Inverse() * HN.GetRotation();
        const FQuat Delta = (RelBind.Inverse() * RelNow).GetNormalized(); // in hand-local terms: RelNow = RelBind * Delta
        FQuat Swing, Twist; Delta.ToSwingTwist(AxisLocal, Swing, Twist);
        float Angle = FMath::RadiansToDegrees(Twist.GetAngle());
        if (Angle > 180.f) Angle -= 360.f;
        const FVector TwistAxis = Twist.GetRotationAxis();
        Out.TwistDeg = FVector::DotProduct(TwistAxis, AxisLocal) < 0 ? -Angle : Angle;
    }
    Out.bValid = true;
    return Out;
}

CireRigAudit::FArm CireRigAudit::Measure(const USkeletalMeshComponent& Mesh, bool bRight)
{
    const USkeletalMesh* Body = Mesh.GetSkeletalMeshAsset();
    if (!Body) return FArm();
    return MeasurePose(*Body, Mesh.GetComponentSpaceTransforms(), bRight);
}

FString CireRigAudit::Describe(const USkeletalMeshComponent& Mesh, int32* OutDefects)
{
    const FArm A[2] = {Measure(Mesh, false), Measure(Mesh, true)};
    int32 Defects = 0;
    for (const FArm& Arm : A) Defects += IsDefect(Arm) ? 1 : 0;
    if (OutDefects) *OutDefects = Defects;
    if (!A[0].bValid && !A[1].bValid) return TEXT("arms=none");
    // Thumb direction in the actor's frame (forward component): a hanging arm's thumb points forward; back = hand reversed.
    float Thumb[2] = {0.f, 0.f};
    const AActor* Owner = Mesh.GetOwner();
    for (int32 Side = 0; Side < 2 && Owner; ++Side)
    {
        const TCHAR* S = Side ? TEXT("_r") : TEXT("_l");
        const FName Hand(FString(TEXT("hand")) + S), T1(FString(TEXT("thumb_01")) + S), T2(FString(TEXT("thumb_02")) + S);
        if (Mesh.GetBoneIndex(Hand) == INDEX_NONE || Mesh.GetBoneIndex(T2) == INDEX_NONE) continue;
        const FVector Dir = (Mesh.GetSocketLocation(T2) - Mesh.GetSocketLocation(Hand)).GetSafeNormal();
        Thumb[Side] = static_cast<float>(FVector::DotProduct(Dir, Owner->GetActorForwardVector()));
    }
    return FString::Printf(TEXT("elbowL=%.1f elbowR=%.1f twistL=%.1f twistR=%.1f fwdL=%.2f fwdR=%.2f thumbFwdL=%.2f thumbFwdR=%.2f armDefects=%d"),
        A[0].ElbowDeg, A[1].ElbowDeg, A[0].TwistDeg, A[1].TwistDeg, A[0].ElbowFwd, A[1].ElbowFwd, Thumb[0], Thumb[1], Defects);
}

#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BonePose.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireRigAudit, Log, All);

namespace
{
FString Category(const FString& File)
{
    if (File.StartsWith(TEXT("Champion")) || File.StartsWith(TEXT("Summon")) || File.StartsWith(TEXT("Pet"))) return TEXT("champion");
    if (File.StartsWith(TEXT("NPC")) || File.StartsWith(TEXT("Vendor")) || File.StartsWith(TEXT("TownVendor")) || File.StartsWith(TEXT("TownAsset"))) return TEXT("npc");
    if (File.StartsWith(TEXT("Race")) || File.StartsWith(TEXT("Monster")) || File.StartsWith(TEXT("Bestiary")) || File.StartsWith(TEXT("Arena"))) return TEXT("monster");
    return FString();
}

bool EvaluateComponent(const UAnimSequence& Sequence, const USkeletalMesh& Body, float Time, const TArray<int32>& Wanted, TArray<FTransform>& Out)
{
    const FReferenceSkeleton& Ref = Body.GetRefSkeleton();
    FMemMark Mark(FMemStack::Get());
    TArray<FBoneIndexType> Required; Required.Reserve(Ref.GetNum());
    for (int32 I = 0; I < Ref.GetNum(); ++I) Required.Add(static_cast<FBoneIndexType>(I));
    FBoneContainer Container;
    Container.InitializeTo(Required, UE::Anim::FCurveFilterSettings(), const_cast<USkeletalMesh&>(Body));
    if (!Container.IsValid()) return false;
    FCompactPose Pose; Pose.SetBoneContainer(&Container); Pose.ResetToRefPose();
    FBlendedCurve Curve; Curve.InitFrom(Container);
    UE::Anim::FStackAttributeContainer Attributes;
    FAnimationPoseData PoseData(Pose, Curve, Attributes);
    Sequence.GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Time), false));
    Out.SetNum(Ref.GetNum());
    for (const int32 Bone : Wanted) if (Bone != INDEX_NONE) Out[Bone] = CireGrip::ComponentBone(Pose, Bone);
    return true;
}
}

bool CireRigAudit::Initialize(ACireGameMode* Mode)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireRigAudit"))) return false;
    const double Started = FPlatformTime::Seconds();
    FString Only; TArray<FString> Filter;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireRigAuditOnly="), Only, false)) Only.ParseIntoArray(Filter, TEXT(","), true);
    // 1. Bodies and clips referenced by the data files.
    TMap<FString, FString> BodyCategory; TSet<FString> Referenced;
    TArray<FString> Files; IFileManager::Get().FindFiles(Files, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/*.json")), true, false);
    IAssetRegistry& Registry = IAssetRegistry::GetChecked();
    Registry.ScanPathsSynchronous({TEXT("/Game/")}, false); // -game does not gather the whole registry up front
    Registry.WaitForCompletion();
    for (const FString& File : Files)
    {
        const FString Cat = Category(File);
        FString Text; if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), File))) continue;
        for (int32 At = Text.Find(TEXT("\"/Game/")); At != INDEX_NONE; At = Text.Find(TEXT("\"/Game/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1))
        {
            const int32 End = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1);
            if (End == INDEX_NONE) break;
            FString Path = Text.Mid(At + 1, End - At - 1);
            if (!Path.Contains(TEXT("."))) Path += TEXT(".") + FPaths::GetBaseFilename(Path);
            const FAssetData Data = Registry.GetAssetByObjectPath(FSoftObjectPath(Path));
            if (!Data.IsValid()) continue;
            if (Data.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName() && !Cat.IsEmpty())
            {
                FString& Existing = BodyCategory.FindOrAdd(Path);
                if (Existing.IsEmpty() || Cat == TEXT("champion")) Existing = Cat;
            }
            else if (Data.AssetClassPath == UAnimSequence::StaticClass()->GetClassPathName()) Referenced.Add(Path);
        }
    }
    // Explicit meshes (-CireRigAuditMeshes=<object path,...>), e.g. a Blender round-trip import before it replaces a body.
    if (FString Extra; FParse::Value(FCommandLine::Get(), TEXT("CireRigAuditMeshes="), Extra, false))
    {
        TArray<FString> Paths; Extra.ParseIntoArray(Paths, TEXT(","), true);
        for (const FString& P : Paths) BodyCategory.Add(P, TEXT("explicit"));
    }
    // 2. Clips by skeleton: referenced ones plus the derived/Tripo folders.
    TMap<FString, TArray<FString>> BySkeleton;
    auto AddClip = [&BySkeleton](const FAssetData& Data)
    {
        FString Skeleton; if (!Data.IsValid() || !Data.GetTagValue(TEXT("Skeleton"), Skeleton)) return;
        Skeleton = FPackageName::ExportTextPathToObjectPath(Skeleton);
        BySkeleton.FindOrAdd(Skeleton).AddUnique(Data.GetObjectPathString());
    };
    for (const FString& Path : Referenced) AddClip(Registry.GetAssetByObjectPath(FSoftObjectPath(Path)));
    {
        FARFilter F; F.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName()); F.bRecursivePaths = true;
        for (const TCHAR* Root : {TEXT("/Game/FabDerived/Anim"), TEXT("/Game/Art/Characters/ChampionAttacks02"), TEXT("/Game/Tripo"), TEXT("/Game/Art/Characters/TripoBatch")}) F.PackagePaths.Add(Root);
        TArray<FAssetData> Found; Registry.GetAssets(F, Found);
        for (const FAssetData& D : Found) AddClip(D);
    }
    // 3. Sweep.
    TArray<TSharedPtr<FJsonValue>> Rows;
    int32 Bodies = 0, Defective = 0;
    BodyCategory.KeySort([](const FString& A, const FString& B) { return A < B; });
    for (const auto& Pair : BodyCategory)
    {
        if (!Filter.IsEmpty() && !Filter.ContainsByPredicate([&Pair](const FString& S) { return Pair.Key.Contains(S); })) continue;
        USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr, *Pair.Key, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Body || !Body->GetSkeleton()) continue;
        const FReferenceSkeleton& Ref = Body->GetRefSkeleton();
        TArray<int32> Wanted;
        for (const TCHAR* Side : {TEXT("_l"), TEXT("_r")})
            for (const TCHAR* Name : {TEXT("upperarm"), TEXT("lowerarm"), TEXT("hand"), TEXT("middle_01")})
                Wanted.Add(Ref.FindBoneIndex(FName(FString(Name) + Side)));
        if (Wanted[0] == INDEX_NONE || Wanted[4] == INDEX_NONE) continue; // no humanoid arms
        const FString SkeletonPath = Body->GetSkeleton()->GetPathName();
        const TArray<FString>* Clips = BySkeleton.Find(SkeletonPath);
        ++Bodies;
        TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("body"), Pair.Key); Row->SetStringField(TEXT("category"), Pair.Value);
        Row->SetNumberField(TEXT("clips"), Clips ? Clips->Num() : 0);
        float BindL = 0.f, BindR = 0.f;
        {   // bind pose (a hyperextended bind elbow makes every small bend read backwards)
            TArray<FTransform> Bind; Bind.SetNum(Ref.GetNum());
            for (const int32 B : Wanted) if (B != INDEX_NONE) Bind[B] = CireGrip::ReferenceComponent(Ref, Ref.GetBoneName(B));
            BindL = MeasurePose(*Body, Bind, false).ElbowDeg; BindR = MeasurePose(*Body, Bind, true).ElbowDeg;
            Row->SetNumberField(TEXT("bindElbowL"), BindL); Row->SetNumberField(TEXT("bindElbowR"), BindR);
        }
        float WorstElbow = 180.f, WorstTwist = 0.f; FString WorstElbowClip, WorstTwistClip; int32 Samples = 0, Hyper = 0, Twisted = 0;
        TArray<TSharedPtr<FJsonValue>> BadClips;
        const bool bShared = Clips && Clips->Num() > 400; // the mannequin skeleton: its clips are the Fab sources themselves
        Row->SetBoolField(TEXT("sharedSkeleton"), bShared);
        for (int32 C = 0; Clips && !bShared && C < Clips->Num(); ++C)
        {
            UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *(*Clips)[C], nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (!Clip) continue;
            const float Length = Clip->GetPlayLength();
            float ClipWorst = 180.f, ClipTwist = 0.f; int32 ClipHyper = 0, ClipTwisted = 0;
            for (int32 S = 0; S < 12; ++S)
            {
                TArray<FTransform> Pose;
                if (!EvaluateComponent(*Clip, *Body, Length * (S + .5f) / 12.f, Wanted, Pose)) break;
                for (const bool bRight : {false, true})
                {
                    const FArm A = MeasurePose(*Body, Pose, bRight);
                    if (!A.bValid) continue;
                    ++Samples;
                    ClipWorst = FMath::Min(ClipWorst, A.ElbowDeg);
                    if (FMath::Abs(A.TwistDeg) > FMath::Abs(ClipTwist)) ClipTwist = A.TwistDeg;
                    ClipHyper += IsDefect(A); ClipTwisted += FMath::Abs(A.TwistDeg) > 150.f; // twist: reported only
                }
            }
            Hyper += ClipHyper; Twisted += ClipTwisted;
            if (ClipWorst < WorstElbow) { WorstElbow = ClipWorst; WorstElbowClip = Clip->GetName(); }
            if (FMath::Abs(ClipTwist) > FMath::Abs(WorstTwist)) { WorstTwist = ClipTwist; WorstTwistClip = Clip->GetName(); }
            if (ClipHyper || ClipTwisted)
            {
                TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
                B->SetStringField(TEXT("clip"), (*Clips)[C]); B->SetNumberField(TEXT("worstElbow"), ClipWorst); B->SetNumberField(TEXT("worstTwist"), ClipTwist);
                B->SetNumberField(TEXT("hyperSamples"), ClipHyper); B->SetNumberField(TEXT("twistSamples"), ClipTwisted);
                BadClips.Add(MakeShared<FJsonValueObject>(B));
            }
        }
        Row->SetNumberField(TEXT("samples"), Samples); Row->SetNumberField(TEXT("hyperSamples"), Hyper); Row->SetNumberField(TEXT("twistSamples"), Twisted);
        Row->SetNumberField(TEXT("worstElbow"), WorstElbow); Row->SetStringField(TEXT("worstElbowClip"), WorstElbowClip);
        Row->SetNumberField(TEXT("worstTwist"), WorstTwist); Row->SetStringField(TEXT("worstTwistClip"), WorstTwistClip);
        Row->SetArrayField(TEXT("badClips"), BadClips);
        Defective += Hyper > 0;
        UE_LOG(LogCireRigAudit, Display, TEXT("CIRE_RIG_AUDIT_BODY %s cat=%s clips=%d samples=%d hyper=%d twist=%d worstElbow=%.1f(%s) worstTwist=%.1f(%s) bind=%.1f/%.1f"),
            *Pair.Key, *Pair.Value, Clips ? Clips->Num() : 0, Samples, Hyper, Twisted, WorstElbow, *WorstElbowClip, WorstTwist, *WorstTwistClip, BindL, BindR);
        {   // bind positions of a few bones (component space, cm) to compare a round-tripped body against its original
            FString Points;
            for (const TCHAR* N : {TEXT("pelvis"), TEXT("head"), TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r")})
                if (Ref.FindBoneIndex(N) != INDEX_NONE) Points += FString::Printf(TEXT(" %s=%s"), N, *CireGrip::ReferenceComponent(Ref, N).GetLocation().ToCompactString());
            Row->SetStringField(TEXT("bindPoints"), Points.TrimStart());
            UE_LOG(LogCireRigAudit, Display, TEXT("CIRE_RIG_AUDIT_BIND %s%s"), *Pair.Key, *Points);
        }
        Rows.Add(MakeShared<FJsonValueObject>(Row));
        if (Bodies % 20 == 0) CollectGarbage(RF_NoFlags);
    }
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("hyperextensionLimitDeg"), HyperextensionLimitDeg); Root->SetNumberField(TEXT("twistLimitDeg"), TwistLimitDeg);
    Root->SetArrayField(TEXT("bodies"), Rows);
    FString Json; const auto Writer = TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    const FString Out = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RigAudit"), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT(".json")));
    FFileHelper::SaveStringToFile(Json, *Out);
    UE_LOG(LogCireRigAudit, Display, TEXT("CIRE_RIG_AUDIT_DONE bodies=%d defective=%d seconds=%.0f file=%s"), Bodies, Defective, FPlatformTime::Seconds() - Started, *Out);
    FPlatformMisc::RequestExitWithStatus(false, 0);
    return true;
}
#endif
