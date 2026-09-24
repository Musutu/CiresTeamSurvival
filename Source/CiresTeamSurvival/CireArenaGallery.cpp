#include "CireArenaGallery.h"
#if !UE_BUILD_SHIPPING
#include "CireArenas.h"
#include "CireGame.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "ContentStreaming.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireArenaGallery, Log, All);

namespace
{
struct FShot { FString File; FString Arena; double FrameMs = 0; int32 Frames = 0; };
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<ACireHero>> Extras;
    TArray<int32> Arenas;
    FString Directory;
    TArray<FShot> Shots;
    int32 Current = -1, Stage = 0;
    bool bDebugView = false; FVector DebugFrom = FVector::ZeroVector, DebugTo = FVector::ZeroVector;
    double Started = 0, StageAt = 0, FrameSum = 0; int32 FrameCount = 0;
    bool bPass = true, bDone = false;
} G;

void Finish(bool bPass)
{
    if (G.bDone) return; G.bDone = true;
    for (const FShot& S : G.Shots)
        UE_LOG(LogCireArenaGallery, Display, TEXT("CIRE_ARENA_GALLERY_SHOT arena=%s file=%s frame_ms=%.2f frames=%d"), *S.Arena, *S.File, S.FrameMs, S.Frames);
    UE_LOG(LogCireArenaGallery, Display, TEXT("CIRE_ARENA_GALLERY_%s captures=%d arenas=%d directory=%s"), bPass ? TEXT("PASS") : TEXT("FAIL"), G.Shots.Num(), G.Arenas.Num(), *G.Directory);
    FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
}

ACireHero* Extra(UWorld* World, int32 Index)
{
    while (G.Extras.Num() <= Index) G.Extras.Add(nullptr);
    if (!G.Extras[Index].IsValid())
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* H = World->SpawnActor<ACireHero>(ACireHero::StaticClass(), FVector(0, 0, 5000), FRotator::ZeroRotator, P);
        if (!H) return nullptr;
        H->TeamId = Index % 2; H->Draft(Index % 3); H->bBot = false; H->bAutoAttack = false;
        H->HeroName = FString::Printf(TEXT("%s %d"), H->TeamId == 0 ? TEXT("Ember") : TEXT("Dusk"), Index / 2 + 1);
        H->GetCharacterMovement()->DisableMovement();
        G.Extras[Index] = H;
    }
    return G.Extras[Index].Get();
}

void Place(ACireHero* H, FVector At, float Yaw)
{
    if (!H) return;
    H->SetActorLocation(At + FVector(0, 0, H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2), false, nullptr, ETeleportType::TeleportPhysics);
    H->SetActorRotation(FRotator(0, Yaw, 0));
}

void Look(const FVector& From, const FVector& To, float Fov)
{
    G.Camera->SetActorLocation(From); G.Camera->SetActorRotation((To - From).Rotation());
    G.Camera->GetCameraComponent()->SetFieldOfView(Fov);
}

void BeginArena(ACireGameMode* Mode)
{
    UWorld* World = Mode->GetWorld();
    const int32 Index = G.Arenas[G.Current];
    CireArenas::Force(World, Index, true);
    Mode->ArenaIndex = Index;
    if (auto* State = Mode->GetGameState<ACireGameState>()) { State->ArenaIndex = Index; State->Phase = 1; }
    const FVector O = CireArenas::Origin();
    const auto* A = CireArenas::Get(Index);
    // The player's champion at an Ember spawn, walking out; a few champions of both teams mid-field for scale.
    if (auto* Player = Cast<ACireHero>(G.Controller->GetPawn()))
    {
        const FVector S = CireArenas::SpawnLocation(Index, 0, 2, 0) + FVector(700, 0, 0);
        Place(Player, S, 0);
    }
    const FVector2D H = A->HalfExtents;
    const FVector Mid[] = {FVector(-H.X * .35f, H.Y * .25f, 0), FVector(H.X * .25f, -H.Y * .15f, 0), FVector(-H.X * .5f, -H.Y * .45f, 0),
                           FVector(H.X * .45f, H.Y * .4f, 0), FVector(-H.X * .15f, -H.Y * .7f, 0), FVector(H.X * .6f, -H.Y * .6f, 0)};
    for (int32 I = 0; I < 6; ++I) Place(Extra(World, I), O + Mid[I], I % 2 == 0 ? 0.f : 180.f);
    G.Stage = 0; G.StageAt = FPlatformTime::Seconds(); G.FrameSum = 0; G.FrameCount = 0;
    UE_LOG(LogCireArenaGallery, Display, TEXT("CIRE_ARENA_GALLERY_ARENA index=%d id=%s"), Index, *A->Id.ToString());
}

void Capture(const TCHAR* Kind)
{
    const auto* A = CireArenas::Get(G.Arenas[G.Current]);
    const FString File = FPaths::Combine(G.Directory, FString::Printf(TEXT("%02d_%s_%s.png"), G.Current + 1, *A->Id.ToString(), Kind));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    if (G.Camera.IsValid())
    {
        // Diagnostics: what the camera actually sees at the centre of the frame.
        const FVector From = G.Camera->GetActorLocation(), Dir = G.Camera->GetActorForwardVector();
        FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(CireArenaGalleryProbe), true);
        const bool bHit = G.Camera->GetWorld()->LineTraceSingleByChannel(Hit, From, From + Dir * 60000, ECC_Visibility, Q);
        UE_LOG(LogCireArenaGallery, Display, TEXT("CIRE_ARENA_GALLERY_VIEW kind=%s camera=%s dir=%s target=%s hit=%d at=%s actor=%s component=%s"), Kind,
            *From.ToString(), *Dir.ToString(), *GetNameSafe(G.Controller.IsValid() ? G.Controller->GetViewTarget() : nullptr), bHit ? 1 : 0,
            *Hit.ImpactPoint.ToString(), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()));
    }
    FShot S; S.File = File; S.Arena = A->Id.ToString(); S.Frames = G.FrameCount; S.FrameMs = G.FrameCount ? G.FrameSum / G.FrameCount * 1000.0 : 0;
    G.Shots.Add(S); G.FrameSum = 0; G.FrameCount = 0;
}
}

bool CireArenaGallery::Initialize(ACireGameMode* Mode)
{
    G = FGallery();
    FString Only;
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireArenaGallery")) && !FParse::Value(FCommandLine::Get(), TEXT("CireArenaGallery="), Only)) return false;
    FParse::Value(FCommandLine::Get(), TEXT("CireArenaGallery="), Only);
    G.Mode = Mode; G.Started = FPlatformTime::Seconds();
    G.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ArenaGallery"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    IFileManager::Get().MakeDirectory(*G.Directory, true);
    const auto& P = CireArenas::Pool(true);
    TArray<FString> Ids; Only.ParseIntoArray(Ids, TEXT("+"));  // '+' separated: FParse::Value stops at commas
    for (int32 I = 0; I < P.Arenas.Num(); ++I)
        if (Ids.IsEmpty() ? (!P.Arenas[I].bFallbackOnly || FParse::Param(FCommandLine::Get(), TEXT("CireArenaGalleryFallback"))) : Ids.Contains(P.Arenas[I].Id.ToString()))
            G.Arenas.Add(I);
    FString View; // development: -CireArenaGalleryView=x_y_z_tx_ty_tz (arena-local) adds a close-up capture
    if (FParse::Value(FCommandLine::Get(), TEXT("CireArenaGalleryView="), View))
    {
        TArray<FString> N; View.ParseIntoArray(N, TEXT("_"));
        if (N.Num() == 6) { G.bDebugView = true; G.DebugFrom = FVector(FCString::Atof(*N[0]), FCString::Atof(*N[1]), FCString::Atof(*N[2])); G.DebugTo = FVector(FCString::Atof(*N[3]), FCString::Atof(*N[4]), FCString::Atof(*N[5])); }
    }
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    if (G.Arenas.IsEmpty()) { UE_LOG(LogCireArenaGallery, Error, TEXT("CIRE_ARENA_GALLERY_ERROR no arenas match")); Finish(false); }
    return true;
}

bool CireArenaGallery::Tick(ACireGameMode* Mode)
{
    if (G.Mode.Get() != Mode) return false;
    if (G.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - G.Started > 60 + 60.0 * G.Arenas.Num()) { UE_LOG(LogCireArenaGallery, Error, TEXT("CIRE_ARENA_GALLERY_ERROR timed out")); Finish(false); return true; }
    UWorld* World = Mode->GetWorld();
    if (!G.Controller.IsValid())
    {
        auto* C = Cast<ACireController>(World->GetFirstPlayerController());
        auto* Hero = C ? Cast<ACireHero>(C->GetPawn()) : nullptr;
        if (!Hero || !C->GetHUD()) return true;
        Hero->TeamId = 0; Hero->bDrafted = true; Hero->Draft(1);
        Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->GetCharacterMovement()->DisableMovement();
        C->SetIgnoreMoveInput(true); C->SetIgnoreLookInput(true); C->bShowMouseCursor = false;
        G.Camera = World->SpawnActor<ACameraActor>();
        auto* Cam = G.Camera->GetCameraComponent(); Cam->SetAspectRatio(16.f / 9.f); Cam->bConstrainAspectRatio = true;
        Cam->PostProcessSettings.bOverride_MotionBlurAmount = true; Cam->PostProcessSettings.MotionBlurAmount = 0;
        C->SetViewTarget(G.Camera.Get());
        // Put the authoritative clock in the arena phase (the gallery holds the match tick, so it stays there):
        // champions outside their realm are only allowed while the clock says Arena.
        if (Mode->Clock.BeginIntermission()) Mode->Clock.Advance(61);
        G.Controller = C; G.Current = 0; BeginArena(Mode);
        return true;
    }
    G.FrameSum += World->GetDeltaSeconds(); ++G.FrameCount;
    auto* State = Mode->GetGameState<ACireGameState>();
    const int32 Index = G.Arenas[G.Current];
    const auto* A = CireArenas::Get(Index);
    const FVector O = CireArenas::Origin();
    const FVector2D H = A->HalfExtents;
    const double Age = Now - G.StageAt;
    if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - G.Started < 500) { G.StageAt = Now; return true; }
    if (G.Stage == 0)
    {
        if (State && Age > .5 && State->Phase != 2) State->Phase = 2; // prep -> arena: the HUD raises the arena banner
        G.Controller->GetHUD()->bShowHUD = false;
        Look(O + FVector(-H.X - 1500, -H.Y - 2300, 2900), O + FVector(250, 150, 0), 70);
        if (Age > 1 && Age < 1.3) IStreamingManager::Get().StreamAllResources(1.f);
        if (Age > 5) { Capture(TEXT("overview")); G.Stage = 1; G.StageAt = Now; }
    }
    else if (G.Stage == 1)
    {
        // The real third-person framing: default 650 cm boom, -20 degree pitch, 80 degree FOV, pivot above the shoulders.
        auto* Player = Cast<ACireHero>(G.Controller->GetPawn());
        const FVector Pivot = Player->GetActorLocation() + FVector(0, 0, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * .85f);
        const FRotator View(-20, 0, 0);
        Look(Pivot - View.Vector() * 650, Pivot, 80);
        G.Controller->GetHUD()->bShowHUD = true;
        if (Age > 1 && Age < 1.3) IStreamingManager::Get().StreamAllResources(1.f);
        if (Age > 4.5) { Capture(TEXT("gameplay")); G.Stage = 2; G.StageAt = Now; }
    }
    else if (G.Stage == 2)
    {
        // Vista: from the Ember flank toward the far side, low, to judge the backdrop and the light.
        G.Controller->GetHUD()->bShowHUD = false;
        Look(O + FVector(-H.X * .55f, H.Y * .8f, 330), O + FVector(H.X * .35f, -H.Y * 1.6f, 120), 75);
        if (Age > 1 && Age < 1.3) IStreamingManager::Get().StreamAllResources(1.f);
        if (Age > 3.5) { Capture(TEXT("vista")); G.Stage = 3; G.StageAt = Now; }
    }
    else if (G.Stage == 3)
    {
        // Top-down plan view: the blocker layout at a glance (matches the minimap).
        Look(O + FVector(0, 0, 9000), O + FVector(1, 0, 0), 50);
        if (Age > 1 && Age < 1.3) IStreamingManager::Get().StreamAllResources(1.f);
        if (Age > 3) { Capture(TEXT("plan")); G.Stage = G.bDebugView ? 4 : 5; G.StageAt = Now; }
    }
    else if (G.Stage == 4)
    {
        Look(O + G.DebugFrom, O + G.DebugTo, 60);
        if (Age > 1 && Age < 1.3) IStreamingManager::Get().StreamAllResources(1.f);
        if (Age > 3) { Capture(TEXT("debug")); G.Stage = 5; G.StageAt = Now; }
    }
    else if (Age > 1)
    {
        if (G.Current + 1 >= G.Arenas.Num() && Age < 2.5) return true;
        if (++G.Current >= G.Arenas.Num())
        {
            CireArenas::ReleaseForce(World);
            bool bFiles = true;
            for (const FShot& S : G.Shots) bFiles &= IFileManager::Get().FileSize(*S.File) > 10000;
            Finish(G.bPass && bFiles && G.Shots.Num() == G.Arenas.Num() * (G.bDebugView ? 5 : 4));
            return true;
        }
        BeginArena(Mode);
    }
    return true;
}
#endif
