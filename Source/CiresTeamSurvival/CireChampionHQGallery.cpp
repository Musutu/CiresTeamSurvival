// champion-hq: review gallery for the HQ champion bodies (see CireChampionHQGallery.h).
#include "CireChampionHQGallery.h"

#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireChampionArt.h"
#include "CireChampionActions.h"
#include "CireChampionRoster.h"
#include "CireLanePath.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireChampionHQGallery, Log, All);

namespace
{
enum class EShot : uint8 { Lineup, Idle, Run, Attack, Cast, Heavy, Raw };
struct FStage { FString Profile; EShot Shot = EShot::Idle; float Settle = 1.5f; };
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<AActor>> Scene;
    TWeakObjectPtr<ACireHero> Focus;
    TArray<FStage> Stages;
    TArray<FString> Profiles, Captures;
    FString Directory;
    FVector Hold, Forward = FVector(1, 0, 0), Right = FVector(0, 1, 0);
    double Started = 0, StageStarted = 0;
    int32 Stage = -1;
    bool bCaptured = false, bDone = false, bPass = true, bBuilt = false;
};
FGallery G;

const TCHAR* ShotName(EShot S)
{
    switch (S)
    {
    case EShot::Lineup: return TEXT("lineup");
    case EShot::Idle: return TEXT("idle");
    case EShot::Run: return TEXT("run");
    case EShot::Attack: return TEXT("attack");
    case EShot::Cast: return TEXT("cast");
    case EShot::Raw: return TEXT("raw");
    default: return TEXT("heavy");
    }
}
UWorld* World() { return G.Mode.IsValid() ? G.Mode->GetWorld() : nullptr; }
void Fail(const FString& Why) { G.bPass = false; UE_LOG(LogCireChampionHQGallery, Error, TEXT("CIRE_CHAMPION_HQ_GALLERY_CHECK_FAIL %s"), *Why); }
void Finish()
{
    if (G.bDone) return;
    G.bDone = true;
    for (const FString& File : G.Captures) if (IFileManager::Get().FileSize(*File) < 1024) Fail(TEXT("capture not written: ") + File);
    UE_LOG(LogCireChampionHQGallery, Display, TEXT("CIRE_CHAMPION_HQ_GALLERY_%s captures=%d directory=%s"), G.bPass ? TEXT("PASS") : TEXT("FAIL"), G.Captures.Num(), *G.Directory);
    FPlatformMisc::RequestExitWithStatus(false, G.bPass ? 0 : 1);
}
float FloorZ(const FVector& P)
{
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CireChampionHQGalleryFloor), false);
    for (const auto& Actor : G.Scene) if (Actor.IsValid()) Query.AddIgnoredActor(Actor.Get());
    return World()->LineTraceSingleByObjectType(Hit, P + FVector(0, 0, 2000), P - FVector(0, 0, 4000), FCollisionObjectQueryParams(ECC_WorldStatic), Query) ? Hit.ImpactPoint.Z : P.Z;
}
FVector Ground(float Along, float Side) { const FVector P = G.Hold + G.Forward * Along + G.Right * Side; return FVector(P.X, P.Y, FloorZ(P)); }
void Look(const FVector& Eye, const FVector& Target, float Fov)
{
    if (!G.Camera.IsValid()) return;
    G.Camera->SetActorLocation(Eye); G.Camera->SetActorRotation((Target - Eye).Rotation());
    G.Camera->GetCameraComponent()->SetFieldOfView(Fov);
}
void ClearScene()
{
    for (auto& Actor : G.Scene) if (Actor.IsValid()) Actor->Destroy();
    G.Scene.Reset(); G.Focus.Reset();
    if (G.Mode.IsValid()) G.Mode->Heroes.RemoveAll([](ACireHero* H) { return !IsValid(H) || H->IsActorBeingDestroyed(); });
}
ACireHero* Hero(const FString& Profile, float Along, float Side, float Yaw)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector At = Ground(Along, Side);
    auto* H = World()->SpawnActor<ACireHero>(At + FVector(0, 0, 200), FRotator(0, Yaw, 0), Params);
    if (!H) { Fail(TEXT("spawn ") + Profile); return nullptr; }
    G.Scene.Add(H); H->TeamId = 0;
    if (!H->DraftProfile(Profile)) Fail(TEXT("draft ") + Profile);
    H->Health = H->MaxHealth = 1.e6f; H->Mana = H->MaxMana = 1.e5f; H->Energy = 100; H->Offers.Reset();
    H->SetActorLocation(FVector(At.X, At.Y, At.Z + H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
    H->SetActorRotation(FRotator(0, Yaw, 0));
    H->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    H->GetMesh()->SetForcedLOD(1); // LOD 0 for the review
    G.Mode->Heroes.Add(H);
    return H;
}
float FeetZ(ACireHero* H) { return static_cast<float>(H->GetActorLocation().Z - H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()); }
float BodyHeight(ACireHero* H)
{
    if (!H) return 180.f;
    USkeletalMeshComponent* Mesh = H->GetMesh();
    if (Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->GetBoneIndex(TEXT("head")) != INDEX_NONE)
        return FMath::Clamp(static_cast<float>(Mesh->GetBoneLocation(TEXT("head")).Z - FeetZ(H)) * 1.12f, 60.f, 500.f);
    return H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f;
}
float SoleAboveFeet(ACireHero* H)
{
    USkeletalMeshComponent* Mesh = H ? H->GetMesh() : nullptr;
    if (!Mesh || Mesh->GetBoneIndex(TEXT("foot_l")) == INDEX_NONE) return 0.f;
    float Z = MAX_flt;
    for (const TCHAR* Bone : {TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r")})
        if (Mesh->GetBoneIndex(Bone) != INDEX_NONE) Z = FMath::Min(Z, static_cast<float>(Mesh->GetBoneLocation(Bone).Z));
    return Z - FeetZ(H);
}
void Report(ACireHero* H, const FString& Tag)
{
    if (!H || !H->GetMesh()) return;
    USkeletalMeshComponent* Mesh = H->GetMesh();
    const USkeletalMesh* Body = Mesh->GetSkeletalMeshAsset();
    const UMaterialInterface* Mat = Mesh->GetNumMaterials() > 0 ? Mesh->GetMaterial(0) : nullptr;
    FVector Blend = FVector::ZeroVector; FString Asset(TEXT("none"));
    if (UAnimSingleNodeInstance* Single = Mesh->GetSingleNodeInstance())
    { Blend = Single->GetFilterLastOutput(); if (Single->GetAnimationAsset()) Asset = Single->GetAnimationAsset()->GetName(); }
    FString Layer;
    if (const auto* C = Cast<UCireCombatAnimInstance>(Mesh->GetAnimInstance()))
        Layer = FString::Printf(TEXT(" attackW=%.2f seq=%s air=%.2f roll=%.2f seat=%.2f relax=%.2f falling=%d"), C->AttackWeight, C->AttackSequence ? *C->AttackSequence->GetName() : TEXT("none"),
            C->AirWeight, C->RollProgress, C->SeatWeight, C->RelaxArms, H->GetCharacterMovement()->IsFalling() ? 1 : 0);
    UE_LOG(LogCireChampionHQGallery, Display, TEXT("CIRE_CHAMPION_HQ_GALLERY_BODY %s %s mesh=%s material=%s applied=%d foot_bones_above_floor=%.1f height=%.1f speed=%.0f anim=%s blend=%s%s"),
        *H->ChampionProfileId, *Tag, Body ? *Body->GetName() : TEXT("none"), Mat ? *Mat->GetName() : TEXT("none"),
        H->ChampionArt && H->ChampionArt->IsApplied() ? 1 : 0, SoleAboveFeet(H), BodyHeight(H), H->GetVelocity().Size2D(), *Asset, *Blend.ToString(), *Layer);
}

void EnterStage(const FStage& S)
{
    ClearScene();
    const float Face = (-G.Forward).Rotation().Yaw; // the camera stands on the -Forward side
    if (S.Shot == EShot::Lineup)
    {
        const int32 N = G.Profiles.Num();
        float MaxH = 0.f;
        for (int32 I = 0; I < N; ++I)
            if (ACireHero* H = Hero(G.Profiles[I], 0.f, (I - (N - 1) * .5f) * 170.f, Face))
            { if (H->ChampionArt) H->ChampionArt->UpdateVisuals(*H, .1f); MaxH = FMath::Max(MaxH, BodyHeight(H)); Report(H, TEXT("lineup")); }
        const float Width = N * 170.f + 200.f;
        const FVector Mid = Ground(0, 0) + FVector(0, 0, MaxH * .5f);
        Look(Mid - G.Forward * (Width * .5f / FMath::Tan(FMath::DegreesToRadians(25.f))) + FVector(0, 0, MaxH * .15f), Mid, 50.f);
        return;
    }
    ACireHero* H = Hero(S.Profile, 0.f, 0.f, Face - 30.f); // 3/4 front
    if (H && S.Shot == EShot::Raw)
    {   // The bound body playing its native idle clip on a plain skeletal mesh actor, next to the champion.
        H->ChampionArt->UpdateVisuals(*H, .1f);
        USkeletalMesh* Body = H->GetMesh()->GetSkeletalMeshAsset();
        FString IdlePath = Body ? Body->GetPathName() : FString();
        IdlePath = FPaths::GetPath(IdlePath) + TEXT("/Animations/") + (Body ? Body->GetName() : FString()) + TEXT("_idle.") + (Body ? Body->GetName() : FString()) + TEXT("_idle");
        UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, *IdlePath, nullptr, LOAD_Quiet | LOAD_NoWarn);
        if (auto* A = World()->SpawnActor<ASkeletalMeshActor>(H->GetMesh()->GetComponentLocation() + G.Right * 140.f, H->GetMesh()->GetComponentRotation()))
        {
            A->GetSkeletalMeshComponent()->SetSkeletalMesh(Body); A->GetSkeletalMeshComponent()->SetWorldScale3D(H->GetMesh()->GetComponentScale());
            if (Idle) A->GetSkeletalMeshComponent()->PlayAnimation(Idle, true);
            G.Scene.Add(A);
            UE_LOG(LogCireChampionHQGallery, Display, TEXT("CIRE_CHAMPION_HQ_GALLERY_RAW %s idle=%s"), *S.Profile, Idle ? *Idle->GetName() : TEXT("missing"));
        }
        // And the same body playing the champion's BlendSpace at rest, on a plain actor (no combat layer).
        UAnimSingleNodeInstance* HeroNode = H->GetMesh()->GetSingleNodeInstance();
        if (UAnimationAsset* Blend = HeroNode ? HeroNode->GetAnimationAsset() : nullptr)
            if (auto* B = World()->SpawnActor<ASkeletalMeshActor>(H->GetMesh()->GetComponentLocation() + G.Right * 280.f, H->GetMesh()->GetComponentRotation()))
            {
                B->GetSkeletalMeshComponent()->SetSkeletalMesh(Body); B->GetSkeletalMeshComponent()->SetWorldScale3D(H->GetMesh()->GetComponentScale());
                B->GetSkeletalMeshComponent()->PlayAnimation(Blend, true);
                if (auto* N = B->GetSkeletalMeshComponent()->GetSingleNodeInstance()) N->SetBlendSpacePosition(FVector::ZeroVector);
                G.Scene.Add(B);
            }
    }
    if (!H) return;
    G.Focus = H;
    // Let the art component bind before the pose is forced.
    if (H->ChampionArt) H->ChampionArt->UpdateVisuals(*H, .1f);
    Report(H, ShotName(S.Shot));
    const float Height = BodyHeight(H);
    const FVector Pivot = FVector(H->GetActorLocation().X, H->GetActorLocation().Y, FeetZ(H) + Height * .5f) + (S.Shot == EShot::Raw ? G.Right * 140.f : FVector::ZeroVector);
    const float Distance = (Height * 2.9f + 80.f) * (S.Shot == EShot::Raw ? 1.7f : 1.f); // 40 deg horizontal = ~23 deg vertical at 16:9
    const FVector Toward = (-G.Forward).RotateAngleAxis(-12.f, FVector::UpVector);
    Look(Pivot + Toward * Distance + FVector(0, 0, Height * .18f), Pivot, 40.f);
    bool bOk = true;
    switch (S.Shot)
    {
    case EShot::Attack: bOk = CireChampionActions::Hold(*H, CireChampionActions::ClipName(*H, TEXT("attack")), 1.f); break;
    case EShot::Cast: bOk = CireChampionActions::Hold(*H, CireChampionActions::ClipName(*H, TEXT("spell")), 1.f); break;
    case EShot::Heavy: bOk = CireChampionActions::Hold(*H, TEXT("war_cry"), 1.f); break;
    default: break;
    }
    if (!bOk) UE_LOG(LogCireChampionHQGallery, Warning, TEXT("CIRE_CHAMPION_HQ_GALLERY_NOCLIP %s %s"), *S.Profile, ShotName(S.Shot));
}
void TickStage(const FStage& S, float DeltaSeconds)
{
    ACireHero* H = G.Focus.Get();
    if (!H || S.Shot != EShot::Run) return;
    // Run in place: the movement component sees full-speed input toward the camera side, the actor is pinned.
    // Run in place: no movement mode, so the velocity the art reads is not integrated into the position.
    UCharacterMovementComponent* Move = H->GetCharacterMovement();
    if (Move->MovementMode != MOVE_None) Move->SetMovementMode(MOVE_None);
    Move->Velocity = H->GetActorForwardVector() * FMath::Max(400.f, Move->MaxWalkSpeed);
}
void Capture(const FStage& S)
{
    Report(G.Focus.Get(), TEXT("capture"));
    const FString File = FPaths::Combine(G.Directory, FString::Printf(TEXT("%02d_%s_%s.png"), G.Stage + 1, S.Shot == EShot::Lineup ? TEXT("all") : *S.Profile, ShotName(S.Shot)));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    G.Captures.Add(File);
    UE_LOG(LogCireChampionHQGallery, Display, TEXT("CIRE_CHAMPION_HQ_GALLERY_CAPTURE %s"), *File);
}

bool Build(ACireGameMode& Mode, ACireController& Controller)
{
    if (auto* Player = Cast<ACireHero>(Controller.GetPawn()))
    { Player->TeamId = 0; Player->Draft(0); Player->SetActorHiddenInGame(true); Player->SetActorEnableCollision(false); Player->SetActorLocation(FVector(0, 0, -50000)); }
    Controller.SetIgnoreMoveInput(true); Controller.SetIgnoreLookInput(true); Controller.bShowMouseCursor = false;
    if (Controller.GetHUD()) Controller.GetHUD()->bShowHUD = false;
    for (auto* M : Mode.Monsters) if (IsValid(M)) M->Destroy();
    Mode.Monsters.Reset();
    UWorld* W = Mode.GetWorld();
    G.Hold = CireLanePath::PointAlongRoute(W, 0, .58f);
    const FVector Ahead = CireLanePath::PointAlongRoute(W, 0, .60f);
    G.Forward = (Ahead - G.Hold).GetSafeNormal2D(); if (G.Forward.IsNearlyZero()) G.Forward = FVector(-1, 0, 0);
    G.Right = FVector::CrossProduct(FVector::UpVector, G.Forward);
    G.Camera = W->SpawnActor<ACameraActor>();
    if (!G.Camera.IsValid()) return false;
    auto* Camera = G.Camera->GetCameraComponent();
    Camera->SetAspectRatio(16.f / 9.f); Camera->bConstrainAspectRatio = true;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true; Camera->PostProcessSettings.MotionBlurAmount = 0;
    Controller.SetViewTarget(G.Camera.Get());
    if (G.Profiles.IsEmpty()) for (const auto& P : CireChampionRoster::All()) G.Profiles.Add(P.Id);
    G.Stages.Add({FString(), EShot::Lineup, 3.f});
    for (const FString& P : G.Profiles)
        for (EShot Shot : {EShot::Raw, EShot::Idle, EShot::Run, EShot::Attack, EShot::Cast, EShot::Heavy})
            G.Stages.Add({P, Shot, Shot == EShot::Run ? 1.2f : 1.4f});
    G.bBuilt = true;
    UE_LOG(LogCireChampionHQGallery, Display, TEXT("CIRE_CHAMPION_HQ_GALLERY_READY stages=%d"), G.Stages.Num());
    return true;
}
}

bool CireChampionHQGallery::Initialize(ACireGameMode* Mode)
{
    G = FGallery();
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireChampionHQGallery"))) return false;
    G.Mode = Mode; G.Started = FPlatformTime::Seconds();
    FString Profiles;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireChampionHQProfiles="), Profiles, false)) Profiles.ParseIntoArray(G.Profiles, TEXT(","), true);
    FString Out;
    G.Directory = FParse::Value(FCommandLine::Get(), TEXT("CireChampionHQOut="), Out, false) ? Out :
        FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ChampionHQ"), TEXT("Gallery"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if (!Mode || Mode->GetNetMode() != NM_Standalone || !IFileManager::Get().MakeDirectory(*G.Directory, true)) { Fail(TEXT("standalone match and capture directory")); Finish(); return true; }
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    return true;
}

bool CireChampionHQGallery::Tick(ACireGameMode* Mode)
{
    if (G.Mode.Get() != Mode) return false;
    if (G.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - G.Started > 1500) { Fail(TEXT("gallery exceeded 1500 seconds")); Finish(); return true; }
    if (!G.bBuilt)
    {
        auto* Controller = Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if (Controller && Controller->GetPawn() && Controller->GetHUD() && !Build(*Mode, *Controller)) { Fail(TEXT("build")); Finish(); }
        return true;
    }
    if (G.Stage < 0 && GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - G.Started < 300) return true;
    if (G.Stage < 0 || (G.bCaptured && Now - G.StageStarted > G.Stages[G.Stage].Settle + .6))
    {
        if (G.Stage + 1 >= G.Stages.Num()) { ClearScene(); Finish(); return true; }
        ++G.Stage; G.bCaptured = false; G.StageStarted = Now;
        EnterStage(G.Stages[G.Stage]);
        return true;
    }
    TickStage(G.Stages[G.Stage], Mode->GetWorld()->GetDeltaSeconds());
    // Wait for shaders / textures of a freshly spawned body before the shot.
    if (!G.bCaptured && Now - G.StageStarted >= G.Stages[G.Stage].Settle &&
        (!GShaderCompilingManager || GShaderCompilingManager->GetNumRemainingJobs() == 0 || Now - G.StageStarted > 30))
    { Capture(G.Stages[G.Stage]); G.bCaptured = true; }
    return true;
}
#else
bool CireChampionHQGallery::Initialize(ACireGameMode*) { return false; }
bool CireChampionHQGallery::Tick(ACireGameMode*) { return false; }
#endif
