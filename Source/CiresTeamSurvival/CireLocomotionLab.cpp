#include "CireLocomotionLab.h"

#include "CireGame.h"
#include "CireChampionArt.h"
#include "CireCreatureArt.h"
#include "CireLocomotion.h"
#include "CireMobility.h"
#include "CireMonsterArt.h"
#include "CireMonsterAnim.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireLocoLab, Log, All);

namespace
{
enum class EFace : uint8 { Orient, Fixed, Turn };
enum class ESpecial : uint8 { None, Hill, Path, Teleport };
struct FSeg
{
    const TCHAR* Name; float Duration; FVector2D Dir; float Scale; EFace Face; float Yaw; float TurnRate; ESpecial Special; float Capture;
};
// Subjects start at the course origin facing +X. Face Fixed = the body keeps Yaw while moving (player mouselook strafe,
// monster kiting); Turn = actor yaw goes to Yaw at TurnRate deg/s (0 = instant: attack facing, mouse flick).
const FSeg Course[] = {
    {TEXT("idle"),       .8f, {0, 0},  1.f, EFace::Orient, 0,   0,   ESpecial::None, 0},
    {TEXT("start"),     1.2f, {1, 0},  1.f, EFace::Orient, 0,   0,   ESpecial::None, .5f},
    {TEXT("stop"),       .9f, {0, 0},  1.f, EFace::Orient, 0,   0,   ESpecial::None, .5f},
    {TEXT("run"),        .8f, {1, 0},  1.f, EFace::Orient, 0,   0,   ESpecial::None, 0},
    {TEXT("cruise"),    2.5f, {1, 0},  1.f, EFace::Orient, 0,   0,   ESpecial::None, 0},
    {TEXT("turn90"),    1.0f, {0, 1},  1.f, EFace::Orient, 0,   0,   ESpecial::None, .5f},
    {TEXT("reverse"),   1.0f, {0, -1}, 1.f, EFace::Orient, 0,   0,   ESpecial::None, .6f},
    {TEXT("stop2"),      .6f, {0, 0},  1.f, EFace::Orient, 0,   0,   ESpecial::None, 0},
    {TEXT("_face"),      .5f, {0, 0},  1.f, EFace::Turn,   0,   0,   ESpecial::None, 0},
    {TEXT("strafe"),    2.0f, {0, 1},  1.f, EFace::Fixed,  0,   0,   ESpecial::None, .4f},
    {TEXT("backpedal"), 2.0f, {-1, 0}, .65f, EFace::Fixed, 0,   0,   ESpecial::None, .4f},
    {TEXT("settle"),     .6f, {0, 0},  1.f, EFace::Fixed,  0,   0,   ESpecial::None, 0},
    {TEXT("turn_in_place_90"),  1.2f, {0, 0}, 1.f, EFace::Turn, 90,  540, ESpecial::None, .6f},
    {TEXT("turn_in_place_180"), 1.2f, {0, 0}, 1.f, EFace::Turn, 270, 0,   ESpecial::None, .6f},
    {TEXT("_to_hill"),   .6f, {0, 0},  1.f, EFace::Fixed,  0,   0,   ESpecial::Teleport, 0},
    {TEXT("hill"),      18.f, {1, 0},  1.f, EFace::Orient, 0,   0,   ESpecial::Hill, 0},
    {TEXT("_to_path"),   .6f, {0, 0},  1.f, EFace::Fixed,  0,   0,   ESpecial::Teleport, 0},
    {TEXT("corners"),   12.f, {0, 0},  1.f, EFace::Orient, 0,   0,   ESpecial::Path, 1.f},
};
constexpr int32 CaptureEvery = 4;           // 15 frames per second of 60 Hz game time: slow-motion sequences
const FVector Studio(0, -2100, 9000);
const FVector CourseOrigin = Studio + FVector(-2600, -1800, 0);
const FVector HillOrigin = Studio + FVector(-800, 2200, 0); // slope 15 degrees: up 800, top 400, down 800 (along +X)
const FVector PathOrigin = Studio + FVector(-2600, 600, 0);
const FVector2D PathPoints[] = {{400, 0}, {400, 400}, {800, 400}, {800, 0}, {1200, 0}, {1200, 400}, {1500, 400}};

struct FFrame
{
    float T = 0; int32 Seg = 0; bool bSkip = false;
    FVector Loc, Vel, MeshLoc; float ActorYaw = 0, VisYaw = 0;
    float RootYaw = 0, IK0 = 0, IK1 = 0, Pelvis = 0, IKWeight = 0;
    TArray<FVector> Contacts; TArray<float> H; TArray<FVector> Joints;
};
struct FLab
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<UTextRenderComponent> Title;
    TArray<TWeakObjectPtr<AActor>> Scene;
    TArray<FString> Subjects;
    int32 Subject = -1;
    TWeakObjectPtr<ACharacter> Body;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    TArray<int32> ContactBones, JointBones;
    FQuat RootRef = FQuat::Identity; FVector ForwardInComponent = FVector::ForwardVector;
    float Height = 180;
    TArray<FFrame> Frames;
    int32 Seg = -1; float SegTime = 0, Clock = 0; int32 SegFrame = 0, SkipFrames = 0, PathIndex = 0;
    bool bSegDone = false;
    enum class EState : uint8 { Build, Spawn, Warm, Run, Done } State = EState::Build;
    double StateStarted = 0, Started = 0;
    FString Directory, Tag;
    bool bCapture = true, bPass = true, bNet = false, bClient = false;
    TWeakObjectPtr<UWorld> WorldRef;
    double LastSeen = 0, LastClientTick = 0;
    int32 Captures = 0;
    TArray<TSharedPtr<FJsonValue>> Results;
};
FLab L;

void Fail(const FString& Why) { L.bPass = false; UE_LOG(LogCireLocoLab, Error, TEXT("CIRE_LOCO_LAB_CHECK_FAIL %s"), *Why); }
UWorld* World() { return L.Mode.IsValid() ? L.Mode->GetWorld() : L.WorldRef.Get(); }
bool IsHero() { return L.Subjects.IsValidIndex(L.Subject) && L.Subjects[L.Subject].StartsWith(TEXT("hero:")); }
FString SubjectId() { return L.Subjects.IsValidIndex(L.Subject) ? L.Subjects[L.Subject] : FString(); }
FString SafeName(const FString& In) { return In.Replace(TEXT(":"), TEXT("_")); }

AStaticMeshActor* Block(const FVector& Center, const FVector& Scale, const FRotator& Rotation, UMaterialInterface* Material, bool bPlane = false)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Actor = World()->SpawnActor<AStaticMeshActor>(Center, Rotation, Params);
    if (!Actor) return nullptr;
    Actor->SetMobility(EComponentMobility::Movable);
    auto* Mesh = Actor->GetStaticMeshComponent();
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, bPlane ? TEXT("/Engine/BasicShapes/Plane.Plane") : TEXT("/Engine/BasicShapes/Cube.Cube")));
    if (Material) Mesh->SetMaterial(0, Material);
    Actor->SetActorScale3D(Scale);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    L.Scene.Add(Actor);
    return Actor;
}

void BuildStudio(ACireController& Controller)
{
    UWorld* W = World();
    UMaterialInterface* Grid = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
    Block(Studio, FVector(100, 100, 1), FRotator::ZeroRotator, Grid, true);
    // Hill: 15 degree ramps (top surfaces meet the floor and the plateau exactly), 40 cm thick.
    const double Slope = FMath::DegreesToRadians(15.0), Run = 800, Rise = Run * FMath::Tan(Slope), Thick = 40;
    const FVector UpNormal(-FMath::Sin(Slope), 0, FMath::Cos(Slope)), DownNormal(FMath::Sin(Slope), 0, FMath::Cos(Slope));
    Block(HillOrigin + FVector(Run * .5, 0, Rise * .5) - UpNormal * Thick * .5, FVector(Run / FMath::Cos(Slope) / 100 + .02, 4, Thick / 100), FRotator(15, 0, 0), Grid);
    Block(HillOrigin + FVector(Run + 200, 0, Rise - Thick * .5), FVector(4.02, 4, Thick / 100), FRotator::ZeroRotator, Grid);
    Block(HillOrigin + FVector(Run + 400 + Run * .5, 0, Rise * .5) - DownNormal * Thick * .5, FVector(Run / FMath::Cos(Slope) / 100 + .02, 4, Thick / 100), FRotator(-15, 0, 0), Grid);
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (auto* Sun = W->SpawnActor<ADirectionalLight>(Studio + FVector(0, 0, 800), FRotator(-42, 135, 0), Params))
    {
        Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable); Sun->GetLightComponent()->SetIntensity(5.f);
        L.Scene.Add(Sun);
    }
    auto* Camera = W->SpawnActor<ACameraActor>(Params);
    L.Camera = Camera;
    auto* View = Camera->GetCameraComponent();
    View->SetAspectRatio(16.f / 9.f); View->bConstrainAspectRatio = true; View->SetFieldOfView(50);
    auto& Post = View->PostProcessSettings;
    Post.bOverride_AutoExposureMethod = true; Post.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true; Post.AutoExposureApplyPhysicalCameraExposure = false;
    Post.bOverride_AutoExposureBias = true; Post.AutoExposureBias = .6f;
    Post.bOverride_MotionBlurAmount = true; Post.MotionBlurAmount = 0;
    auto* Title = NewObject<UTextRenderComponent>(Camera);
    Camera->AddInstanceComponent(Title); Title->SetupAttachment(View);
    Title->SetRelativeLocation(FVector(250, -103, 56)); Title->SetRelativeRotation(FRotator(0, 180, 0));
    Title->SetWorldSize(4.2f); Title->SetTextRenderColor(FColor(245, 220, 170)); Title->RegisterComponent();
    L.Title = Title;
    Controller.SetViewTarget(Camera);
}

// ---- subject --------------------------------------------------------------------------------------------
USkeletalMeshComponent* VisualMesh(ACharacter* C)
{
    if (auto* H = Cast<ACireHero>(C); H && H->ChampionArt)
        if (auto* Skeletal = Cast<USkeletalMeshComponent>(H->ChampionArt->GetVisualMesh())) return Skeletal;
    return C ? C->GetMesh() : nullptr;
}

bool SpawnSubject()
{
    const FString Spec = SubjectId();
    FString Kind, Id;
    if (!Spec.Split(TEXT(":"), &Kind, &Id)) { Fail(TEXT("subject spec ") + Spec); return false; }
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector At = CourseOrigin + FVector(0, 0, 120);
    if (Kind == TEXT("hero"))
    {
        auto* H = World()->SpawnActor<ACireHero>(At, FRotator::ZeroRotator, Params);
        if (!H) { Fail(TEXT("hero spawn ") + Id); return false; }
        H->TeamId = 0;
        if (!H->DraftProfile(Id)) { Fail(TEXT("draft ") + Id); H->Destroy(); return false; }
        H->bBot = false; H->bAutoAttack = false; H->Target = nullptr; H->AttackSerial = 0;
        H->SetActorTickEnabled(false);
        H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        CireMovement::ApplyToHero(*H);
        if (H->ChampionArt) H->ChampionArt->UpdateVisuals(*H, 1.f / 60);
        if (!L.bNet && (!H->ChampionArt || !H->ChampionArt->IsApplied())) Fail(TEXT("champion art not applied: ") + Id); // a dedicated server draws nothing
        L.Body = H;
    }
    else
    {
        auto* M = World()->SpawnActor<ACireMonster>(At, FRotator::ZeroRotator, Params);
        if (!M) { Fail(TEXT("monster spawn ") + Id); return false; }
        M->Lane = 0; M->SetActorTickEnabled(false);
        CireNPCCombat::ConfigureArchetype(M, FName(*Id), 3, 0, 1);
        M->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        if (!L.bNet && (!M->MonsterArt || !M->MonsterArt->IsTripoApplied())) Fail(TEXT("monster body not applied: ") + Id);
        L.Body = M;
    }
    ACharacter* C = L.Body.Get();
    C->SetActorLocation(CourseOrigin + FVector(0, 0, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2), false, nullptr, ETeleportType::TeleportPhysics);
    L.Mesh = VisualMesh(C);
    if (!L.Mesh.IsValid() || !L.Mesh->GetSkeletalMeshAsset()) { Fail(TEXT("no skeletal body: ") + Spec); return false; }
    L.Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    L.Height = FMath::Clamp(static_cast<float>(C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2), 60.f, 900.f);
    L.Frames.Reset(); L.Seg = -1; L.SegTime = 0; L.Clock = 0; L.SegFrame = 0; L.SkipFrames = 0; L.PathIndex = 0;
    return true;
}

void ResolveBones()
{
    const FReferenceSkeleton& Ref = L.Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    CireLocomotion::FindContactBones(Ref, L.ContactBones);
    L.JointBones = L.ContactBones;
    for (int32 I = 0; I < Ref.GetNum(); ++I)
    {
        const FString Name = Ref.GetBoneName(I).ToString().ToLower();
        if ((Name == TEXT("pelvis") || Name == TEXT("head") || Name == TEXT("hand_l") || Name == TEXT("hand_r")) && !L.JointBones.Contains(I)) L.JointBones.Add(I);
    }
    if (L.JointBones.Num() == L.ContactBones.Num())
        for (int32 I = 0; I < Ref.GetNum(); ++I)
        {
            const FString Name = Ref.GetBoneName(I).ToString().ToLower();
            if ((Name.Contains(TEXT("pelvis")) || Name.Contains(TEXT("head"))) && !Name.Contains(TEXT("end")) && L.JointBones.Num() < L.ContactBones.Num() + 3) L.JointBones.Add(I);
        }
    L.RootRef = Ref.GetRefBonePose()[0].GetRotation();
    if (L.Mode.IsValid()) L.Mesh->PrimaryComponentTick.AddPrerequisite(L.Mode.Get(), L.Mode->PrimaryActorTick);
    L.ForwardInComponent = L.Mesh->GetComponentQuat().UnrotateVector(L.Body->GetActorForwardVector());
    FString Names;
    for (const int32 B : L.ContactBones) Names += Ref.GetBoneName(B).ToString() + TEXT(" ");
    UE_LOG(LogCireLocoLab, Display, TEXT("CIRE_LOCO_LAB_SUBJECT %s mesh=%s height=%.0f contacts=[%s] joints=%d"), *SubjectId(),
        *L.Mesh->GetSkeletalMeshAsset()->GetName(), L.Height, *Names.TrimEnd(), L.JointBones.Num());
}

float GroundBelow(const FVector& P)
{
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CireLocoLabGround), false, L.Body.Get());
    return World()->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 150), P - FVector(0, 0, 400), ECC_Visibility, Query) ? static_cast<float>(Hit.ImpactPoint.Z) : static_cast<float>(P.Z);
}

void Sample()
{
    ACharacter* C = L.Body.Get(); USkeletalMeshComponent* M = L.Mesh.Get();
    if (!C || !M || L.Seg < 0) return;
    FFrame F;
    F.T = L.Clock; F.Seg = L.Seg; F.bSkip = L.SkipFrames > 0;
    F.Loc = C->GetActorLocation(); F.Vel = C->GetVelocity(); F.ActorYaw = static_cast<float>(C->GetActorRotation().Yaw);
    F.MeshLoc = M->GetComponentLocation();
    CireLocomotion::FPoseFeel Feel;
    if (const auto* Combat = Cast<UCireCombatAnimInstance>(M->GetAnimInstance())) Feel = Combat->Feel;
    else if (const auto* Native = Cast<UCireMonsterAnimInstance>(M->GetAnimInstance())) Feel = Native->Feel;
    Feel.Resolve(M);
    F.RootYaw = Feel.RootYaw; F.IK0 = Feel.Foot[0]; F.IK1 = Feel.Foot[1]; F.Pelvis = Feel.Pelvis; F.IKWeight = Feel.IKWeight;
    if (!L.Frames.IsEmpty() && FVector::Dist(L.Frames.Last().Loc, F.Loc) > 300.) F.bSkip = true; // teleports / replicated snaps are not motion
    const TArray<FTransform>& Space = M->GetComponentSpaceTransforms();
    if (Space.IsEmpty()) return;
    const FQuat RootNow = Space[0].GetRotation();
    const FVector VisualForward = M->GetComponentQuat().RotateVector((RootNow * L.RootRef.Inverse()).RotateVector(L.ForwardInComponent));
    F.VisYaw = static_cast<float>(VisualForward.Rotation().Yaw);
    const float Scale = static_cast<float>(M->GetComponentScale().X);
    for (const int32 B : L.ContactBones)
    {
        if (!Space.IsValidIndex(B)) continue;
        const FVector P = M->GetComponentTransform().TransformPosition(Space[B].GetLocation());
        F.Contacts.Add(P); F.H.Add(static_cast<float>(P.Z) - GroundBelow(P));
    }
    for (const int32 B : L.JointBones)
        if (Space.IsValidIndex(B)) F.Joints.Add(RootNow.UnrotateVector(Space[B].GetLocation() - Space[0].GetLocation()) * Scale);
    L.Frames.Add(MoveTemp(F));
}

void Teleport(const FVector& Floor, float Yaw)
{
    ACharacter* C = L.Body.Get();
    C->GetCharacterMovement()->StopMovementImmediately();
    C->SetActorLocation(Floor + FVector(0, 0, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2), false, nullptr, ETeleportType::TeleportPhysics);
    C->SetActorRotation(FRotator(0, Yaw, 0));
    L.SkipFrames = 20;
}

void EnterSegment(int32 Index)
{
    L.Seg = Index; L.SegTime = 0; L.SegFrame = 0; L.bSegDone = false;
    const FSeg& S = Course[Index];
    if (S.Special == ESpecial::Teleport) Teleport(FString(S.Name) == TEXT("_to_hill") ? HillOrigin + FVector(-500, 0, 0) : PathOrigin, 0);
    L.PathIndex = 0;
}

void Drive(float Dt)
{
    ACharacter* C = L.Body.Get();
    const FSeg& S = Course[L.Seg];
    auto* Move = C->GetCharacterMovement();
    FVector Input(S.Dir.X, S.Dir.Y, 0);
    if (S.Special == ESpecial::Hill)
    {
        Input = FVector(1, 0, 0);
        if (C->GetActorLocation().X > HillOrigin.X + 2500) L.bSegDone = true;
    }
    else if (S.Special == ESpecial::Path)
    {
        const FVector Pos = C->GetActorLocation();
        while (L.PathIndex < UE_ARRAY_COUNT(PathPoints) && FVector::Dist2D(Pos, PathOrigin + FVector(PathPoints[L.PathIndex], 0)) < 45.) ++L.PathIndex;
        if (L.PathIndex >= UE_ARRAY_COUNT(PathPoints)) { Input = FVector::ZeroVector; L.bSegDone = true; }
        else Input = (PathOrigin + FVector(PathPoints[L.PathIndex], 0) - Pos).GetSafeNormal2D();
    }
    if (auto* H = Cast<ACireHero>(C))
    {
        CireMovement::ApplyToHero(*H);
        Move->MaxWalkSpeed = H->Mobility ? H->Mobility->MovementSpeed(false) : 520.f;
    }
    Move->bOrientRotationToMovement = S.Face == EFace::Orient;
    C->bUseControllerRotationYaw = false;
    if (S.Face == EFace::Fixed) C->SetActorRotation(FRotator(0, S.Yaw, 0));
    else if (S.Face == EFace::Turn)
    {
        const float Yaw = static_cast<float>(C->GetActorRotation().Yaw);
        const float Delta = FMath::FindDeltaAngleDegrees(Yaw, S.Yaw);
        const float Step = S.TurnRate <= 0 ? Delta : FMath::Clamp(Delta, -S.TurnRate * Dt, S.TurnRate * Dt);
        C->SetActorRotation(FRotator(0, Yaw + Step, 0));
    }
    if (!Input.IsNearlyZero()) C->AddMovementInput(Input.GetSafeNormal(), S.Scale);
    if (auto* H = Cast<ACireHero>(C); H && H->ChampionArt) H->ChampionArt->UpdateVisuals(*H, Dt);
}

void Frame(float Dt)
{
    ACharacter* C = L.Body.Get();
    if (!C || !L.Camera.IsValid()) return;
    const FVector Center = C->GetActorLocation() + FVector(0, 0, -C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + L.Height * .45f);
    const float Distance = FMath::Max(330.f, L.Height * 2.4f + 160.f);
    const FVector Eye = Center + FRotator(-14, -125, 0).Vector() * -Distance;
    L.Camera->SetActorLocation(Eye); L.Camera->SetActorRotation((Center - Eye).Rotation());
    if (L.Title.IsValid())
    {
        const FSeg& S = Course[L.Seg];
        L.Title->SetText(FText::FromString(FString::Printf(TEXT("%s  |  %s  |  t=%.2fs  |  %s  |  %.0f cm/s"), *SubjectId(), S.Name, L.SegTime,
            *L.Tag, C->GetVelocity().Size2D())));
    }
}

bool CaptureWindow()
{
    const FSeg& S = Course[L.Seg];
    if (!L.bCapture || L.SkipFrames > 0) return false;
    if (S.Special == ESpecial::Hill)
    {
        const double X = L.Body->GetActorLocation().X - HillOrigin.X;
        return (X > 150 && X < 650) || (X > 1350 && X < 1850);
    }
    return L.SegTime < S.Capture;
}

void Capture()
{
    const FString File = FPaths::Combine(L.Directory, TEXT("frames"), SafeName(SubjectId()), FString::Printf(TEXT("%02d_%s_%03d.png"), L.Seg, Course[L.Seg].Name, L.SegFrame / CaptureEvery));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    ++L.Captures;
}

// ---- metrics --------------------------------------------------------------------------------------------
TSharedPtr<FJsonObject> Metrics()
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("subject"), SubjectId());
    Out->SetNumberField(TEXT("height"), L.Height);
    const float Dt = 1.f / 60.f;
    const float Tol = FMath::Max(2.5f, L.Height * .03f);
    const int32 NC = L.ContactBones.Num();
    // Flat reference height of each contact when planted (sole thickness above the floor).
    TArray<float> FlatMin; FlatMin.Init(TNumericLimits<float>::Max(), NC);
    for (const FFrame& F : L.Frames)
        if (!F.bSkip && Course[F.Seg].Special != ESpecial::Hill)
            for (int32 C = 0; C < FMath::Min(NC, F.H.Num()); ++C) FlatMin[C] = FMath::Min(FlatMin[C], F.H[C]);
    TArray<TSharedPtr<FJsonValue>> Segments;
    for (int32 S = 0; S < UE_ARRAY_COUNT(Course); ++S)
    {
        if (Course[S].Name[0] == TEXT('_')) continue;
        TArray<int32> Idx;
        for (int32 I = 0; I < L.Frames.Num(); ++I) if (L.Frames[I].Seg == S && !L.Frames[I].bSkip) Idx.Add(I);
        if (Idx.Num() < 6) continue;
        double SpeedSum = 0, SlipSum = 0, YawRateMax = 0, YawRateSum = 0, JerkMax = 0, GroundErrSum = 0, GroundErrMax = 0, LagSum = 0;
        TArray<double> Jerks; // per-frame worst joint second difference (root space)
        int32 Planted = 0, SlipN = 0, Snaps = 0, Pops = 0, Frozen = 0, Moving = 0, GroundN = 0, LagN = 0, Flips = 0; float LastRate = 0;
        const float PopThreshold = 3.f * L.Height / 180.f;
        for (int32 K = 0; K < Idx.Num(); ++K)
        {
            const FFrame& F = L.Frames[Idx[K]];
            const float Speed = static_cast<float>(F.Vel.Size2D());
            SpeedSum += Speed;
            if (K == 0) continue;
            const FFrame& P = L.Frames[Idx[K] - 1];
            if (P.Seg != S || P.bSkip) continue;
            const float DYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(P.VisYaw, F.VisYaw));
            const float Rate = DYaw / Dt;
            YawRateMax = FMath::Max<double>(YawRateMax, Rate); YawRateSum += Rate;
            if (DYaw >= 15.f) ++Snaps;
            const float Signed = FMath::FindDeltaAngleDegrees(P.VisYaw, F.VisYaw) / Dt;
            if (FMath::Abs(Signed) > 60.f && FMath::Abs(LastRate) > 60.f && FMath::Sign(Signed) != FMath::Sign(LastRate)) ++Flips;
            if (FMath::Abs(Signed) > 60.f) LastRate = Signed;
            if (Speed > 60.f)
            {
                LagSum += FMath::Abs(FMath::FindDeltaAngleDegrees(F.VisYaw, F.ActorYaw)); ++LagN;
            }
            for (int32 C = 0; C < FMath::Min3(NC, F.Contacts.Num(), P.Contacts.Num()); ++C)
            {
                const bool bNow = F.H[C] < FlatMin[C] + Tol + (Course[S].Special == ESpecial::Hill ? 6.f : 0.f);
                const bool bThen = P.H[C] < FlatMin[C] + Tol + (Course[S].Special == ESpecial::Hill ? 6.f : 0.f);
                if (bNow) ++Planted;
                if (bNow && bThen)
                {
                    SlipSum += FVector::Dist2D(F.Contacts[C], P.Contacts[C]) / Dt; ++SlipN;
                    if (Course[S].Special == ESpecial::Hill)
                    {
                        const float Err = F.H[C] - FlatMin[C];
                        GroundErrSum += FMath::Abs(Err); GroundErrMax = FMath::Max<double>(GroundErrMax, FMath::Abs(Err)); ++GroundN;
                    }
                }
            }
            bool bSame = F.Joints.Num() == P.Joints.Num() && F.Joints.Num() > 0;
            for (int32 J = 0; J < FMath::Min(F.Joints.Num(), P.Joints.Num()); ++J) bSame &= F.Joints[J].Equals(P.Joints[J], .01);
            if (Speed > 30.f) { ++Moving; Frozen += bSame; }
            if (K >= 2)
            {
                const FFrame& Q = L.Frames[Idx[K] - 2];
                if (Q.Seg == S && !Q.bSkip)
                {
                    double Worst = 0;
                    for (int32 J = 0; J < FMath::Min3(F.Joints.Num(), P.Joints.Num(), Q.Joints.Num()); ++J)
                        Worst = FMath::Max(Worst, (F.Joints[J] - 2 * P.Joints[J] + Q.Joints[J]).Size());
                    JerkMax = FMath::Max(JerkMax, Worst); Jerks.Add(Worst);
                }
            }
        }
        // A pop is a discontinuity: a joint acceleration spike well above this segment's own motion (faster play
        // rates raise every frame's acceleration evenly and are not pops).
        if (!Jerks.IsEmpty())
        {
            TArray<double> Sorted = Jerks; Sorted.Sort();
            const double Median = Sorted[Sorted.Num() / 2];
            for (const double J : Jerks) Pops += J > FMath::Max<double>(PopThreshold, 4.0 * Median);
        }
        const double MeanSpeed = SpeedSum / Idx.Num(), MeanSlip = SlipN ? SlipSum / SlipN : 0;
        auto Seg = MakeShared<FJsonObject>();
        Seg->SetStringField(TEXT("segment"), Course[S].Name);
        Seg->SetNumberField(TEXT("frames"), Idx.Num());
        Seg->SetNumberField(TEXT("speed"), FMath::RoundToDouble(MeanSpeed));
        Seg->SetNumberField(TEXT("slip"), FMath::RoundToDouble(MeanSlip * 10) / 10);
        Seg->SetNumberField(TEXT("slideRatio"), MeanSpeed > 40 ? FMath::RoundToDouble(MeanSlip / MeanSpeed * 1000) / 1000 : -1);
        Seg->SetNumberField(TEXT("plantedFraction"), FMath::RoundToDouble(1000.0 * Planted / FMath::Max(1, (Idx.Num() - 1) * FMath::Max(1, NC))) / 1000);
        Seg->SetNumberField(TEXT("yawRateMax"), FMath::RoundToDouble(YawRateMax));
        Seg->SetNumberField(TEXT("yawRateMean"), FMath::RoundToDouble(YawRateSum / FMath::Max(1, Idx.Num() - 1)));
        Seg->SetNumberField(TEXT("yawSnaps"), Snaps);
        Seg->SetNumberField(TEXT("yawFlips"), Flips);
        Seg->SetNumberField(TEXT("visualLagDeg"), LagN ? FMath::RoundToDouble(LagSum / LagN * 10) / 10 : 0);
        Seg->SetNumberField(TEXT("jerkMax"), FMath::RoundToDouble(JerkMax * 100) / 100);
        Seg->SetNumberField(TEXT("pops"), Pops);
        Seg->SetNumberField(TEXT("frozenFrames"), Frozen);
        Seg->SetNumberField(TEXT("movingFrames"), Moving);
        if (GroundN) { Seg->SetNumberField(TEXT("groundErrMean"), FMath::RoundToDouble(GroundErrSum / GroundN * 10) / 10); Seg->SetNumberField(TEXT("groundErrMax"), FMath::RoundToDouble(GroundErrMax * 10) / 10); }
        Segments.Add(MakeShared<FJsonValueObject>(Seg));
        UE_LOG(LogCireLocoLab, Display, TEXT("CIRE_LOCO_LAB_METRIC %s %s speed=%.0f slip=%.1f ratio=%.3f yawMax=%.0f snaps=%d flips=%d lag=%.1f jerk=%.2f pops=%d frozen=%d/%d ground=%.1f/%.1f"),
            *SubjectId(), Course[S].Name, MeanSpeed, MeanSlip, MeanSpeed > 40 ? MeanSlip / MeanSpeed : -1.0, YawRateMax, Snaps, Flips, LagN ? LagSum / LagN : 0.0,
            JerkMax, Pops, Frozen, Moving, GroundN ? GroundErrSum / GroundN : 0.0, GroundErrMax);
    }
    Out->SetArrayField(TEXT("segments"), Segments);
    // Stride calibration of the body's locomotion: authored speed of every clip against where it is used.
    TArray<TSharedPtr<FJsonValue>> Clips;
    if (auto* H = Cast<ACireHero>(L.Body.Get()))
        if (auto* Single = H->GetMesh()->GetSingleNodeInstance())
            if (const UBlendSpace* Blend = Cast<UBlendSpace>(Single->GetAnimationAsset()))
            {
                const float Scale = static_cast<float>(H->GetMesh()->GetComponentScale().X);
                for (const FBlendSample& Sample : Blend->GetBlendSamples())
                {
                    const CireLocomotion::FGait& Gait = CireLocomotion::AnalyzeGait(Sample.Animation);
                    auto Row = MakeShared<FJsonObject>();
                    Row->SetStringField(TEXT("clip"), Sample.Animation ? Sample.Animation->GetName() : TEXT("?"));
                    Row->SetNumberField(TEXT("direction"), Sample.SampleValue.X); Row->SetNumberField(TEXT("axisSpeed"), Sample.SampleValue.Y);
                    Row->SetNumberField(TEXT("naturalSpeed"), FMath::RoundToDouble(Gait.Speed * Scale));
                    Row->SetNumberField(TEXT("length"), Gait.Length);
                    Clips.Add(MakeShared<FJsonValueObject>(Row));
                    UE_LOG(LogCireLocoLab, Display, TEXT("CIRE_LOCO_LAB_CLIP %s dir=%.0f axis=%.0f natural=%.0f len=%.2f %s"), *SubjectId(), Sample.SampleValue.X,
                        Sample.SampleValue.Y, Gait.Speed * Scale, Gait.Length, Sample.Animation ? *Sample.Animation->GetName() : TEXT("?"));
                }
            }
    if (auto* M = Cast<ACireMonster>(L.Body.Get()); M && M->MonsterArt)
    {
        const FVector2D Speeds = M->MonsterArt->GroundSpeeds();
        auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("walkNatural"), FMath::RoundToDouble(Speeds.X)); Row->SetNumberField(TEXT("runNatural"), FMath::RoundToDouble(Speeds.Y));
        Row->SetNumberField(TEXT("moveSpeed"), M->GetCharacterMovement()->MaxWalkSpeed);
        const float Scale = static_cast<float>(M->GetMesh()->GetComponentScale().X);
        const auto* Anim = Cast<UCireMonsterAnimInstance>(M->GetMesh()->GetAnimInstance());
        const float WalkGait = Anim ? CireLocomotion::AnalyzeGait(Anim->Walk.Sequence).Speed * Scale : 0.f, RunGait = Anim ? CireLocomotion::AnalyzeGait(Anim->Run.Sequence).Speed * Scale : 0.f;
        Row->SetNumberField(TEXT("walkMeasured"), FMath::RoundToDouble(WalkGait)); Row->SetNumberField(TEXT("runMeasured"), FMath::RoundToDouble(RunGait));
        Clips.Add(MakeShared<FJsonValueObject>(Row));
        UE_LOG(LogCireLocoLab, Display, TEXT("CIRE_LOCO_LAB_CLIP %s walkNatural=%.0f runNatural=%.0f walkMeasured=%.0f runMeasured=%.0f moveSpeed=%.0f"), *SubjectId(), Speeds.X, Speeds.Y,
            WalkGait, RunGait, M->GetCharacterMovement()->MaxWalkSpeed);
    }
    Out->SetArrayField(TEXT("clips"), Clips);
    TArray<AActor*> Attached; L.Body->GetAttachedActors(Attached, true, true);
    Out->SetNumberField(TEXT("attachedActors"), Attached.Num());
    if (!Attached.IsEmpty()) UE_LOG(LogCireLocoLab, Warning, TEXT("CIRE_LOCO_LAB_ATTACHED %s actors=%d first=%s"), *SubjectId(), Attached.Num(), *Attached[0]->GetName());
    return Out;
}

void WriteCsv()
{
    FString Csv = TEXT("t,seg,skip,x,y,z,vx,vy,actorYaw,visYaw,mx,my,mz,rootYaw,ik0,ik1,pelvis,ikw");
    for (int32 C = 0; C < L.ContactBones.Num(); ++C) Csv += FString::Printf(TEXT(",c%dx,c%dy,c%dz,c%dh"), C, C, C, C);
    Csv += TEXT("\n");
    for (const FFrame& F : L.Frames)
    {
        Csv += FString::Printf(TEXT("%.4f,%s,%d,%.2f,%.2f,%.2f,%.1f,%.1f,%.2f,%.2f,%.2f,%.2f,%.2f"), F.T, L.bClient ? TEXT("net") : Course[F.Seg].Name, F.bSkip ? 1 : 0,
            F.Loc.X, F.Loc.Y, F.Loc.Z, F.Vel.X, F.Vel.Y, F.ActorYaw, F.VisYaw, F.MeshLoc.X, F.MeshLoc.Y, F.MeshLoc.Z);
        Csv += FString::Printf(TEXT(",%.2f,%.2f,%.2f,%.2f,%.2f"), F.RootYaw, F.IK0, F.IK1, F.Pelvis, F.IKWeight);
        for (int32 C = 0; C < F.Contacts.Num(); ++C) Csv += FString::Printf(TEXT(",%.2f,%.2f,%.2f,%.2f"), F.Contacts[C].X, F.Contacts[C].Y, F.Contacts[C].Z, F.H[C]);
        Csv += TEXT("\n");
    }
    FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(L.Directory, TEXT("samples"), SafeName(SubjectId()) + TEXT(".csv")));
}

void Finish()
{
    if (L.State == FLab::EState::Done) return;
    L.State = FLab::EState::Done;
    auto Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("tag"), L.Tag);
    Root->SetBoolField(TEXT("locoFeel"), CireLocomotion::Enabled());
    Root->SetArrayField(TEXT("subjects"), L.Results);
    FString Text; auto Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Root, Writer);
    FFileHelper::SaveStringToFile(Text, *FPaths::Combine(L.Directory, TEXT("metrics.json")));
    UE_LOG(LogCireLocoLab, Display, TEXT("CIRE_LOCO_LAB_%s subjects=%d captures=%d directory=%s"), L.bPass ? TEXT("PASS") : TEXT("FAIL"), L.Results.Num(), L.Captures, *L.Directory);
    if (L.bNet)
    {   // the network check: keep serving while the remote client writes its samples
        const bool bPass = L.bPass;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bPass](float) { FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1); return false; }), 20.f);
        return;
    }
    FPlatformMisc::RequestExitWithStatus(false, L.bPass ? 0 : 1);
}

void EndSubject()
{
    WriteCsv();
    L.Results.Add(MakeShared<FJsonValueObject>(Metrics()));
    if (L.Body.IsValid()) L.Body->Destroy();
    L.Body.Reset(); L.Mesh.Reset();
}
}

bool CireLocomotionLab::Initialize(ACireGameMode* Mode)
{
    L = FLab();
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireLocomotionLab"))) return false;
    L.Mode = Mode; L.Started = FPlatformTime::Seconds();
    FString List = TEXT("hero:lancer,hero:knight,hero:bear,monster:ironbound_bruiser,monster:hollow_infantry,monster:dire_wolf,monster:rotting_shambler,monster:feral_mammoth,monster:hollow_siegebreaker");
    FParse::Value(FCommandLine::Get(), TEXT("CireLocoLabSubjects="), List, false);
    List.ParseIntoArray(L.Subjects, TEXT(","), true);
    L.Tag = CireLocomotion::Enabled() ? TEXT("after") : TEXT("before");
    FParse::Value(FCommandLine::Get(), TEXT("CireLocoLabTag="), L.Tag);
    L.bCapture = !FParse::Param(FCommandLine::Get(), TEXT("CireLocoLabNoCapture"));
    L.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LocomotionLab"),
        FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + L.Tag));
    L.bNet = FParse::Param(FCommandLine::Get(), TEXT("CireLocoLabNet"));
    if (L.bNet) L.bCapture = false; // the dedicated server renders nothing; the client samples
    L.WorldRef = Mode ? Mode->GetWorld() : nullptr;
    if (!Mode || Mode->GetNetMode() != (L.bNet ? NM_DedicatedServer : NM_Standalone) || !IFileManager::Get().MakeDirectory(*L.Directory, true)) { Fail(TEXT("match mode and output directory")); Finish(); return true; }
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    // Sample after every tick group (movement, animation evaluation and bone finalization) of the frame.
    static FDelegateHandle PostTick;
    FWorldDelegates::OnWorldPostActorTick.Remove(PostTick);
    PostTick = FWorldDelegates::OnWorldPostActorTick.AddLambda([](UWorld* W, ELevelTick, float) { if (W == World() && L.State == FLab::EState::Run) Sample(); });
    UE_LOG(LogCireLocoLab, Display, TEXT("CIRE_LOCO_LAB_START subjects=%d tag=%s locoFeel=%d fixedStep=%d"), L.Subjects.Num(), *L.Tag, CireLocomotion::Enabled() ? 1 : 0, FApp::UseFixedTimeStep() ? 1 : 0);
    return true;
}

bool CireLocomotionLab::Tick(ACireGameMode* Mode)
{
    if (L.Mode.Get() != Mode) return false;
    if (L.State == FLab::EState::Done) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - L.Started > 1800) { Fail(TEXT("lab exceeded 1800 seconds")); Finish(); return true; }
    UWorld* W = Mode->GetWorld();
    const float Dt = W->GetDeltaSeconds();
    switch (L.State)
    {
    case FLab::EState::Build:
    {
        auto* Controller = Cast<ACireController>(W->GetFirstPlayerController());
        if (!Controller || !Controller->GetPawn()) { L.StateStarted = Now; return true; }
        if (L.bNet && Now - L.StateStarted < 5.) return true; // the remote client finishes loading
        L.Controller = Controller;
        if (auto* Player = Cast<ACireHero>(Controller->GetPawn()))
        {
            Player->SetActorHiddenInGame(true); Player->SetActorEnableCollision(false); Player->SetActorTickEnabled(false); Player->GetCharacterMovement()->DisableMovement();
            if (L.bNet) Player->SetActorLocation(Studio + FVector(0, 0, 900), false, nullptr, ETeleportType::TeleportPhysics);
        }
        Controller->SetIgnoreMoveInput(true); Controller->SetIgnoreLookInput(true); Controller->bShowMouseCursor = false;
        if (Controller->GetHUD()) Controller->GetHUD()->bShowHUD = false;
        for (auto* M : Mode->Monsters) if (IsValid(M)) M->Destroy();
        Mode->Monsters.Reset();
        BuildStudio(*Controller);
        L.State = FLab::EState::Spawn;
        return true;
    }
    case FLab::EState::Spawn:
        if (++L.Subject >= L.Subjects.Num()) { Finish(); return true; }
        if (!SpawnSubject()) { if (L.Body.IsValid()) L.Body->Destroy(); L.Body.Reset(); return true; }
        L.State = FLab::EState::Warm; L.StateStarted = Now; L.Clock = 0;
        return true;
    case FLab::EState::Warm:
    {
        // Idle until shaders are compiled and the body has settled (visual state reaches rest).
        L.Clock += Dt;
        L.Seg = 0; Drive(Dt); Frame(Dt); L.Seg = -1;
        const bool bShaders = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - L.StateStarted < 240;
        if (bShaders || L.Clock < 1.5f) return true;
        ResolveBones();
        L.Clock = 0; EnterSegment(0);
        L.State = FLab::EState::Run;
        return true;
    }
    case FLab::EState::Run:
    {
        if (!L.Body.IsValid() || !L.Mesh.IsValid()) { Fail(TEXT("subject lost: ") + SubjectId()); L.State = FLab::EState::Spawn; return true; }
        L.Clock += Dt; L.SegTime += Dt; ++L.SegFrame;
        if (L.SkipFrames > 0) --L.SkipFrames;
        if (L.SegTime >= Course[L.Seg].Duration || L.bSegDone)
        {
            if (L.Seg + 1 >= UE_ARRAY_COUNT(Course)) { EndSubject(); L.State = FLab::EState::Spawn; return true; }
            EnterSegment(L.Seg + 1);
        }
        Drive(Dt);
        Frame(Dt);
        if (CaptureWindow() && L.SegFrame % CaptureEvery == 0) Capture();
        return true;
    }
    default: return true;
    }
}

// ---- network check: remote client ---------------------------------------------------------------------
namespace
{
FString ClientSubjectId(ACharacter* C)
{
    if (auto* H = Cast<ACireHero>(C)) return TEXT("hero:") + H->ChampionProfileId;
    if (auto* M = Cast<ACireMonster>(C)) return TEXT("monster:") + (M->NPCState ? M->NPCState->ArchetypeId.ToString() : FString(TEXT("?")));
    return TEXT("?");
}

ACharacter* FindRemoteSubject(UWorld* W)
{
    for (TActorIterator<ACireHero> It(W); It; ++It)
        if (It->bDrafted && !It->IsLocallyControlled() && !It->bDead && FVector::Dist2D(It->GetActorLocation(), Studio) < 6000.) return *It;
    for (TActorIterator<ACireMonster> It(W); It; ++It)
        if (It->Health > 0 && It->MonsterArt && It->MonsterArt->IsTripoApplied() && FVector::Dist2D(It->GetActorLocation(), Studio) < 6000.) return *It;
    return nullptr;
}

void EndClientSubject()
{
    if (L.Frames.Num() > 30)
    {
        WriteCsv();
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("subject"), SubjectId()); Row->SetNumberField(TEXT("height"), L.Height);
        Row->SetArrayField(TEXT("segments"), {});
        L.Results.Add(MakeShared<FJsonValueObject>(Row));
        UE_LOG(LogCireLocoLab, Display, TEXT("CIRE_LOCO_LAB_CLIENT_SUBJECT %s frames=%d"), *SubjectId(), L.Frames.Num());
    }
    L.Body.Reset(); L.Mesh.Reset(); L.Frames.Reset(); L.State = FLab::EState::Spawn;
}
}

bool CireLocomotionLab::TickClient(ACireController* Controller)
{
    static const bool bClient = FParse::Param(FCommandLine::Get(), TEXT("CireLocoLabClient"));
    if (!bClient || !Controller) return false;
    UWorld* W = Controller->GetWorld();
    const double Now = FPlatformTime::Seconds();
    if (!L.bClient)
    {
        if (Controller->GetNetMode() != NM_Client || !Controller->GetPawn()) return true;
        L = FLab(); L.bClient = true; L.bCapture = false; L.WorldRef = W; L.Started = Now; L.Tag = TEXT("net-client"); L.LastSeen = Now; L.LastClientTick = Now;
        L.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LocomotionLab"),
            FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-net-client")));
        IFileManager::Get().MakeDirectory(*L.Directory, true);
        Controller->SetIgnoreMoveInput(true); Controller->SetIgnoreLookInput(true);
        if (Controller->GetHUD()) Controller->GetHUD()->bShowHUD = false;
        BuildStudio(*Controller); // the same course geometry, locally (grounds the foot traces)
        L.State = FLab::EState::Spawn;
        static FDelegateHandle PostTick;
        FWorldDelegates::OnWorldPostActorTick.Remove(PostTick);
        PostTick = FWorldDelegates::OnWorldPostActorTick.AddLambda([](UWorld* In, ELevelTick, float) { if (L.bClient && In == World() && L.State == FLab::EState::Run) Sample(); });
        // The server closing the connection (or the client leaving the map) ends the run: this ticker finalizes it.
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
        {
            if (!L.bClient || L.State == FLab::EState::Done) return false;
            const double T = FPlatformTime::Seconds();
            if (T - L.LastClientTick > 3. || T - L.Started > 900.)
            {
                if (L.Frames.Num() > 0) EndClientSubject();
                Finish();
                return false;
            }
            return true;
        }), .5f);
        UE_LOG(LogCireLocoLab, Display, TEXT("CIRE_LOCO_LAB_CLIENT_START directory=%s"), *L.Directory);
        return true;
    }
    if (L.State == FLab::EState::Done) return true;
    if (W != L.WorldRef.Get() || Controller->GetNetMode() != NM_Client)
    {   // disconnected (the server finished): keep the last subject and stop
        if (L.Frames.Num() > 0) EndClientSubject();
        Finish();
        return true;
    }
    L.LastClientTick = Now;
    const float Dt = W->GetDeltaSeconds();
    ACharacter* Subject = FindRemoteSubject(W);
    if (!L.Body.IsValid() && L.Frames.Num() > 0) EndClientSubject(); // the server destroyed the subject
    if (Subject != L.Body.Get())
    {
        if (L.Body.IsValid() || L.Frames.Num() > 0) EndClientSubject();
        if (Subject)
        {
            L.Body = Subject; L.Subjects.Add(ClientSubjectId(Subject)); L.Subject = L.Subjects.Num() - 1;
            L.State = FLab::EState::Warm; L.StateStarted = Now; L.Clock = 0; L.Frames.Reset();
        }
    }
    if (Subject) L.LastSeen = Now;
    if (L.State == FLab::EState::Warm && L.Body.IsValid() && Now - L.StateStarted > 1.)
    {
        L.Mesh = VisualMesh(L.Body.Get());
        if (L.Mesh.IsValid() && L.Mesh->GetSkeletalMeshAsset())
        {
            L.Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            L.Height = FMath::Clamp(static_cast<float>(L.Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2), 60.f, 900.f);
            L.Subjects[L.Subject] = ClientSubjectId(L.Body.Get());
            ResolveBones(); L.Seg = 0; L.State = FLab::EState::Run;
        }
    }
    if (L.State == FLab::EState::Run) { L.Clock += Dt; Frame(Dt); }
    if ((!L.Results.IsEmpty() && Now - L.LastSeen > 12.) || Now - L.Started > 900.)
    {
        if (L.Body.IsValid() || L.Frames.Num() > 0) EndClientSubject();
        Finish();
    }
    return true;
}
