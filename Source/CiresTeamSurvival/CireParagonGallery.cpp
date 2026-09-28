// paragon-champions: review gallery for the Paragon champions (see CireParagonGallery.h).
#include "CireParagonGallery.h"

#include "CireGame.h"
#include "CireAbilityDB.h"
#include "CireChampionArt.h"
#include "CireChampionRoster.h"
#include "CireCreatureArt.h"
#include "CireLanePath.h"
#include "CireMonsterAnim.h"
#include "CireNPCCombat.h"
#include "CireParagonChampions.h"
#include "CireSignatureSkills.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Animation/AnimSequence.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireParagonGallery, Log, All);

namespace
{
struct FPgStage { FString Hero, Ability, Name; float Settle = 2.f; };
struct FPgGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<AActor>> Scene;
    TWeakObjectPtr<ACireHero> Focus;
    TWeakObjectPtr<ACireMonster> Dummy;
    TArray<FPgStage> Stages;
    TArray<FString> Captures, Only;
    FString Directory;
    FVector Hold, Forward = FVector(1, 0, 0), Right = FVector(0, 1, 0);
    double Started = 0, StageStarted = 0;
    int32 Stage = -1, Casts = 0, CastFails = 0, Clips = 0;
    bool bCaptured = false, bDone = false, bPass = true, bBuilt = false;
};
FPgGallery PG;

UWorld* PgWorld() { return PG.Mode.IsValid() ? PG.Mode->GetWorld() : nullptr; }
void PgFail(const FString& Why) { PG.bPass = false; UE_LOG(LogCireParagonGallery, Error, TEXT("CIRE_PARAGON_GALLERY_CHECK_FAIL %s"), *Why); }
void PgFinish()
{
    if (PG.bDone) return;
    PG.bDone = true;
    for (const FString& File : PG.Captures) if (IFileManager::Get().FileSize(*File) < 1024) PgFail(TEXT("capture not written: ") + File);
    UE_LOG(LogCireParagonGallery, Display, TEXT("CIRE_PARAGON_GALLERY_%s captures=%d casts=%d castFails=%d clips=%d directory=%s"), PG.bPass ? TEXT("PASS") : TEXT("FAIL"),
        PG.Captures.Num(), PG.Casts, PG.CastFails, PG.Clips, *PG.Directory);
    FPlatformMisc::RequestExitWithStatus(false, PG.bPass ? 0 : 1);
}
float PgFloorZ(const FVector& P)
{
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CireParagonGalleryFloor), false);
    for (const auto& Actor : PG.Scene) if (Actor.IsValid()) Query.AddIgnoredActor(Actor.Get());
    return PgWorld()->LineTraceSingleByObjectType(Hit, P + FVector(0, 0, 2000), P - FVector(0, 0, 4000), FCollisionObjectQueryParams(ECC_WorldStatic), Query) ? Hit.ImpactPoint.Z : P.Z;
}
FVector PgGround(float Along, float Side) { const FVector P = PG.Hold + PG.Forward * Along + PG.Right * Side; return FVector(P.X, P.Y, PgFloorZ(P)); }
void PgLook(const FVector& Eye, const FVector& Target, float Fov = 55.f)
{
    if (!PG.Camera.IsValid()) return;
    PG.Camera->SetActorLocation(Eye); PG.Camera->SetActorRotation((Target - Eye).Rotation());
    PG.Camera->GetCameraComponent()->SetFieldOfView(Fov);
}
void PgClear()
{
    for (auto& Actor : PG.Scene) if (Actor.IsValid()) Actor->Destroy();
    PG.Scene.Reset(); PG.Focus.Reset(); PG.Dummy.Reset();
    if (PG.Mode.IsValid())
    {
        PG.Mode->Monsters.RemoveAll([](ACireMonster* M) { return !IsValid(M) || M->IsActorBeingDestroyed(); });
        PG.Mode->Heroes.RemoveAll([](ACireHero* H) { return !IsValid(H) || H->IsActorBeingDestroyed(); });
    }
}
ACireHero* PgHeroAt(const FString& Profile, float Along, float Side, float Yaw)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector At = PgGround(Along, Side);
    auto* H = PgWorld()->SpawnActor<ACireHero>(At + FVector(0, 0, 200), FRotator(0, Yaw, 0), Params);
    if (!H) { PgFail(TEXT("spawn ") + Profile); return nullptr; }
    PG.Scene.Add(H); H->TeamId = 0;
    if (!H->DraftProfile(Profile)) PgFail(TEXT("draft ") + Profile);
    H->Health = H->MaxHealth = 1.e6f; H->Mana = H->MaxMana = 1.e5f; H->Energy = 100; H->Offers.Reset();
    H->SetActorLocation(FVector(At.X, At.Y, At.Z + H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
    H->SetActorRotation(FRotator(0, Yaw, 0));
    H->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    PG.Mode->Heroes.Add(H);
    return H;
}
ACireMonster* PgDummy(float Along, float Side)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    const FVector At = PgGround(Along, Side);
    auto* M = PgWorld()->SpawnActor<ACireMonster>(At + FVector(0, 0, 140), (-PG.Forward).Rotation(), Params);
    if (!M) return nullptr;
    PG.Scene.Add(M); M->Lane = 0; PG.Mode->Monsters.Add(M);
    CireNPCCombat::ConfigureArchetype(M, TEXT("hollow_infantry"), 4, 0, 1);
    M->Health = M->MaxHealth = 1.e6f;
    M->SetActorLocation(FVector(At.X, At.Y, At.Z + M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
    M->SetActorTickEnabled(false); M->GetCharacterMovement()->DisableMovement();
    return M;
}
void PgPose(const FString& Stage)
{
    ACireHero* H = PG.Focus.Get(); if (!H) return;
    USkeletalMeshComponent* Mesh = H->GetMesh();
    const UCireCreatureArt* Body = H->ChampionArt ? H->ChampionArt->GetCreature() : nullptr;
    const UAnimSequence* Clip = Body ? Body->GetActionClip() : nullptr;
    const float Feet = static_cast<float>(H->GetActorLocation().Z - H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FBoxSphereBounds B = Mesh->Bounds;
    UE_LOG(LogCireParagonGallery, Display, TEXT("CIRE_PARAGON_GALLERY_POSE %s mesh=%s kind=%s action=%s top=%.0f bottom=%.0f"), *Stage,
        Mesh->GetSkeletalMeshAsset() ? *Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("none"), Body ? *Body->GetKind() : TEXT("none"),
        Clip ? *Clip->GetName() : TEXT("none"), B.Origin.Z + B.BoxExtent.Z - Feet, B.Origin.Z - B.BoxExtent.Z - Feet);
    if (!Mesh->GetSkeletalMeshAsset() || !Body || Body->GetKind() != TEXT("monster_native")) PgFail(Stage + TEXT(": the hero is not on its Paragon body"));
    if (!PG.Stages[PG.Stage].Ability.IsEmpty() && Clip) ++PG.Clips;
}
void PgCapture(const FPgStage& S)
{
    PgPose(S.Name);
    const FString File = FPaths::Combine(PG.Directory, FString::Printf(TEXT("%s.png"), *S.Name));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    PG.Captures.Add(File);
    UE_LOG(LogCireParagonGallery, Display, TEXT("CIRE_PARAGON_GALLERY_CAPTURE stage=%s file=%s"), *S.Name, *File);
}
void PgEnter(const FPgStage& S)
{
    PgClear();
    const float Face = (-PG.Forward).Rotation().Yaw;
    ACireHero* H = PgHeroAt(S.Hero, 0, 0, Face);
    PG.Focus = H;
    if (!H) return;
    const FVector C = H->GetActorLocation();
    const float Height = FMath::Max(120.f, H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f);
    if (S.Ability.IsEmpty())
    {
        // Three-quarter close-up: the whole body, facing the camera.
        PgLook(C + (-PG.Forward) * (Height * 2.3f + 160.f) + PG.Right * (Height * .9f) + FVector(0, 0, Height * .25f), C + FVector(0, 0, Height * .05f), 45);
        return;
    }
    ACireMonster* Dummy = PgDummy(-520, 0); PG.Dummy = Dummy;
    const FVector Aim = Dummy ? Dummy->GetActorLocation() - FVector(0, 0, Dummy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) : PgGround(-520, 0);
    H->Skills = {S.Ability}; H->Cooldowns = {0}; H->GlobalCooldown = 0; H->Mana = H->MaxMana; H->Energy = 100;
    const FCireAbilityDef* Def = CireAbilityDB::Find(S.Ability);
    const bool bAlly = Def && (Def->Targeting == TEXT("ally") || Def->Targeting == TEXT("self"));
    H->Target = bAlly ? static_cast<AActor*>(H) : static_cast<AActor*>(Dummy); H->bHasCastAim = true; H->CastAimPoint = Aim;
    const bool bCast = CireSignatureSkills::Cast(H, 0, S.Ability);
    H->bHasCastAim = false;
    ++PG.Casts; if (!bCast) ++PG.CastFails;
    UE_LOG(LogCireParagonGallery, Display, TEXT("CIRE_PARAGON_GALLERY_CAST %s %s %s"), *S.Hero, *S.Ability, bCast ? TEXT("ok") : *H->Notice);
    const FVector Mid = (C + Aim) * .5f;
    PgLook(Mid + PG.Right * 900.f + (-PG.Forward) * 200.f + FVector(0, 0, 420), Mid + FVector(0, 0, 60), 60);
}
bool PgBuild(ACireGameMode& Mode, ACireController& Controller)
{
    if (auto* Player = Cast<ACireHero>(Controller.GetPawn()))
    { Player->TeamId = 0; Player->Draft(0); Player->SetActorHiddenInGame(true); Player->SetActorEnableCollision(false); Player->SetActorLocation(FVector(0, 0, -50000)); }
    Controller.SetIgnoreMoveInput(true); Controller.SetIgnoreLookInput(true); Controller.bShowMouseCursor = false;
    if (Controller.GetHUD()) Controller.GetHUD()->bShowHUD = false;
    for (auto* M : Mode.Monsters) if (IsValid(M)) M->Destroy();
    Mode.Monsters.Reset();
    UWorld* W = Mode.GetWorld();
    PG.Hold = CireLanePath::PointAlongRoute(W, 0, .58f);
    const FVector Ahead = CireLanePath::PointAlongRoute(W, 0, .60f);
    PG.Forward = (Ahead - PG.Hold).GetSafeNormal2D(); if (PG.Forward.IsNearlyZero()) PG.Forward = FVector(-1, 0, 0);
    PG.Right = FVector::CrossProduct(FVector::UpVector, PG.Forward);
    PG.Camera = W->SpawnActor<ACameraActor>();
    if (!PG.Camera.IsValid()) return false;
    auto* Camera = PG.Camera->GetCameraComponent();
    Camera->SetAspectRatio(16.f / 9.f); Camera->bConstrainAspectRatio = true;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true; Camera->PostProcessSettings.MotionBlurAmount = 0;
    Controller.SetViewTarget(PG.Camera.Get());
    for (const FString& Id : CireParagonChampions::HeroIds())
    {
        if (!PG.Only.IsEmpty() && !PG.Only.Contains(Id)) continue;
        PG.Stages.Add({Id, FString(), Id + TEXT("_0_front"), 2.2f});
        int32 N = 0;
        for (const FString& A : CireParagonChampions::OwnAbilities(Id)) PG.Stages.Add({Id, A, FString::Printf(TEXT("%s_%d_%s"), *Id, ++N, *A), .45f});
    }
    PG.bBuilt = true;
    UE_LOG(LogCireParagonGallery, Display, TEXT("CIRE_PARAGON_GALLERY_READY heroes=%d stages=%d"), CireParagonChampions::HeroIds().Num(), PG.Stages.Num());
    return PG.Stages.Num() > 0;
}
}

bool CireParagonGallery::Initialize(ACireGameMode* Mode)
{
    PG = FPgGallery();
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireParagonGallery"))) return false;
    PG.Mode = Mode; PG.Started = FPlatformTime::Seconds();
    FString Only;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireParagonGalleryOnly="), Only, false)) Only.ParseIntoArray(PG.Only, TEXT(","), true);
    PG.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ParagonGallery"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if (!Mode || Mode->GetNetMode() != NM_Standalone || !IFileManager::Get().MakeDirectory(*PG.Directory, true)) { PgFail(TEXT("standalone match and capture directory")); PgFinish(); return true; }
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    return true;
}

bool CireParagonGallery::Tick(ACireGameMode* Mode)
{
    if (PG.Mode.Get() != Mode) return false;
    if (PG.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - PG.Started > 2400) { PgFail(TEXT("gallery exceeded 2400 seconds")); PgFinish(); return true; }
    if (!PG.bBuilt)
    {
        auto* Controller = Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if (Controller && Controller->GetPawn() && Controller->GetHUD() && !PgBuild(*Mode, *Controller)) { PgFail(TEXT("build (no installed Paragon heroes?)")); PgFinish(); }
        return true;
    }
    if (PG.Stage < 0 && GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - PG.Started < 600) return true;
    if (PG.Stage < 0 || (PG.bCaptured && Now - PG.StageStarted > PG.Stages[PG.Stage].Settle + .6))
    {
        if (PG.Stage + 1 >= PG.Stages.Num()) { PgClear(); PgFinish(); return true; }
        ++PG.Stage; PG.bCaptured = false; PG.StageStarted = Now;
        PgEnter(PG.Stages[PG.Stage]);
        return true;
    }
    if (!PG.bCaptured && Now - PG.StageStarted >= PG.Stages[PG.Stage].Settle) { PgCapture(PG.Stages[PG.Stage]); PG.bCaptured = true; }
    return true;
}
