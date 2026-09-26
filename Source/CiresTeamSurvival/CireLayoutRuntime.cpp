// layout-wiring: the map layout at match time. See CireLayoutRuntime.h.
#include "CireLayoutRuntime.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireMapLayout.h"
#include "CireNPCCombat.h"
#include "CireRouteEditMode.h"
#include "CireThreat.h"
#include "CireTownMap.h"
#include "CireVendors.h"
#include "CireWaves.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireLayoutRuntime, Log, All);

namespace
{
TMap<TWeakObjectPtr<ACireHero>, float> OutOfBounds;
constexpr float BoundsGraceSeconds = 3.f;
}

void CireLayoutRuntime::TickHero(ACireHero* H, float Delta)
{
    if (!IsValid(H) || !H->HasAuthority() || H->bDead || !H->bDrafted) return;
    auto* Mode = H->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode || CireRouteEditMode::IsActive() || CireTownMap::IsExplore()) return;
    // The town phases only: the arena is elsewhere and recovery teleports heroes home.
    const auto Phase = Mode->Clock.Phase();
    if (Phase != Cires::MatchPhase::Survival && Phase != Cires::MatchPhase::Intermission) { OutOfBounds.Remove(H); return; }
    const int32 Team = FMath::Clamp(H->TeamId, 0, 1);
    if (CireLanePath::InsidePlayBounds(H->GetWorld(), Team, H->GetActorLocation())) { OutOfBounds.Remove(H); return; }
    float& Seconds = OutOfBounds.FindOrAdd(H);
    if (Seconds == 0.f)
    {
        H->Notice = TEXT("OUT OF BOUNDS | Return to the town or you will be pulled back.");
        UE_LOG(LogCireLayoutRuntime, Display, TEXT("CIRE_LAYOUT_OUT_OF_BOUNDS hero=%s at=(%.0f,%.0f)"), *H->HeroName, H->GetActorLocation().X, H->GetActorLocation().Y);
    }
    Seconds += Delta;
    if (Seconds < BoundsGraceSeconds) return;
    FVector Back = CireLanePath::ClampToPlayBounds(H->GetWorld(), Team, H->GetActorLocation(), 250.f);
    if (CireTownMap::IsActive()) Back.Z = CireTownMap::Ground(H->GetWorld(), FVector2D(Back)) + H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f;
    H->GetCharacterMovement()->StopMovementImmediately();
    H->SetActorLocation(Back, false, nullptr, ETeleportType::TeleportPhysics);
    H->Notice = TEXT("Pulled back inside the play bounds.");
    OutOfBounds.Remove(H);
}

float CireLayoutRuntime::ClampBoom(const ACireHero* H, const FVector& Pivot, const FRotator& Rotation, float Boom)
{
    if (!H || !H->GetWorld()) return Boom;
    const auto& Routes = CireLanePath::Get(H->GetWorld());
    if (Routes.PlayBounds.Num() < 3 || CireRouteEditMode::IsActive()) return Boom;
    const int32 Team = CireTownMap::RealmAt(Pivot);
    auto Inside = [&](float Length)
    {
        const FVector Camera = Pivot - Rotation.Vector() * Length;
        return CireLanePath::InsidePlayBounds(Routes, CireLanePath::ToLocal(Team, Camera));
    };
    if (Inside(Boom) || !Inside(0.f)) return Boom; // inside, or the hero himself stands outside (TickHero handles him)
    float Lo = 0.f, Hi = Boom;
    for (int32 I = 0; I < 8; ++I) { const float Mid = (Lo + Hi) * .5f; (Inside(Mid) ? Lo : Hi) = Mid; }
    return FMath::Max(Lo, FMath::Min(Boom, 150.f));
}

int32 CireLayoutRuntime::RespawnVendors(UWorld* World)
{
    if (!World) return 0;
    for (TActorIterator<ACireVendor> It(World); It; ++It) It->Destroy();
    ACireWorld* Owner = nullptr;
    for (TActorIterator<ACireWorld> It(World); It; ++It) { Owner = *It; break; }
    if (!Owner) return 0;
    CireVendors::SpawnAll(Owner); // re-reads Vendors.json and TownVendors.json
    int32 Count = 0;
    for (TActorIterator<ACireVendor> It(World); It; ++It) ++Count;
    return Count;
}

bool CireLayoutRuntime::RestartOnLayout(ACireGameMode* Mode, FString& Out)
{
    if (!Mode || !Mode->HasAuthority()) { Out = TEXT("Only the host can restart the match."); return false; }
    UWorld* World = Mode->GetWorld();
    FCireBattlefieldRoutes Routes; FString Error, Source;
    if (!CireLanePath::LoadActive(Routes, &Error, &Source) || !CireLanePath::ApplyLive(World, Routes, &Error)) { Out = FString::Printf(TEXT("Layout not applied: %s"), *Error); return false; }
    // Clear the field: every monster, the wave director's bookkeeping, and the packs.
    for (int32 I = Mode->Monsters.Num() - 1; I >= 0; --I)
        if (ACireMonster* M = Mode->Monsters[I]; IsValid(M)) { CireWaveDirector::Forget(M); CireThreat::Clear(M); CireNPCCombat::Interrupt(M); M->Destroy(); }
    Mode->Monsters.Reset(); Mode->RewardedPacks.Reset();
    // The match starts over on the new layout: survival, round 1, full lives, the first wave's delay.
    Mode->Clock = Cires::MatchClock(Mode->Clock.GetDurations());
    Mode->CycleWavesSpawned = 0; Mode->bCyclesComplete = false;
    if (auto* S = Mode->GetGameState<ACireGameState>())
    {
        S->Phase = 0; S->Round = 1; S->Wave = 0; S->CycleWavesDone = 0; S->EmberLives = S->DuskLives = 100; S->SecondsLeft = -1;
        S->Announcement = FString::Printf(TEXT("LAYOUT RESTART | %s"), Routes.LayoutName.IsEmpty() ? *Source : *Routes.LayoutName);
    }
    CireWaveDirector::Initialize(Mode);
    Mode->WaveTimer = CireWaveDirector::Config(World).FirstWaveDelay;
    Mode->SpawnPacks();
    // Heroes at their player spawns, facing as authored.
    int32 Slot[2] = {0, 0};
    for (ACireHero* H : Mode->Heroes)
    {
        if (!IsValid(H)) continue;
        const int32 Team = FMath::Clamp(H->TeamId, 0, 1);
        const FTransform At = CireLanePath::PlayerSpawnTransform(World, Team, Slot[Team]++);
        H->HomePosition = At.GetLocation();
        H->ReviveAt(At.GetLocation());
        H->SetActorRotation(At.Rotator());
        if (AController* C = H->GetController()) C->SetControlRotation(At.Rotator());
    }
    const int32 Vendors = RespawnVendors(World);
    Out = FString::Printf(TEXT("Restarted on %s: %d/%d paths, %d/%d packs, %d merchants"), *Source, CireLanePath::PathCount(World, 0), CireLanePath::PathCount(World, 1),
        CireLanePath::BayCount(World, 0), CireLanePath::BayCount(World, 1), Vendors);
    UE_LOG(LogCireLayoutRuntime, Display, TEXT("CIRE_LAYOUT_RESTART %s"), *Out);
    return true;
}

void CireLayoutRuntime::RequestRestart(APlayerController* C)
{
    if (!C) return;
    auto* Hero = Cast<ACireHero>(C->GetPawn());
    auto* Mode = C->GetWorld() ? C->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr;
    FString Out;
    if (!Mode) Out = TEXT("Only the host can restart on the layout (Alt+F5).");
    else RestartOnLayout(Mode, Out);
    if (Hero) Hero->Notice = Out;
}

bool CireLayoutRuntime::LaunchTestMatch(FString& Out)
{
    const FString Exe = FPlatformProcess::ExecutablePath();
    const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    const FString Map = CireTownMap::IsActive() ? TEXT("-CireTown") : TEXT("-CireProcedural");
    const FString Args = FString::Printf(TEXT("\"%s\" /Game/Maps/Citadel -game -windowed -ResX=1600 -ResY=900 -NoLiveCoding -CireNoReplay -CireUseMapLayout -CireLayoutTest %s"), *Project, *Map);
    uint32 Pid = 0;
    FProcHandle Handle = FPlatformProcess::CreateProc(*Exe, *Args, true, false, false, &Pid, 0, nullptr, nullptr);
    if (!Handle.IsValid()) { Out = FString::Printf(TEXT("Could not start %s"), *Exe); return false; }
    FPlatformProcess::CloseProc(Handle);
    Out = FString::Printf(TEXT("A match on this layout is starting in a new window (%s town)."), CireTownMap::IsActive() ? TEXT("Medieval Kingdom") : TEXT("procedural"));
    UE_LOG(LogCireLayoutRuntime, Display, TEXT("CIRE_LAYOUT_TEST_LAUNCHED pid=%u args=%s"), Pid, *Args);
    return true;
}

bool CireLayoutRuntime::IsLayoutTest() { return FParse::Param(FCommandLine::Get(), TEXT("CireLayoutTest")); }

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs LayoutCommand(TEXT("cire.Layout"),
    TEXT("cire.Layout restart: re-read Content/Data/MapLayout.json and restart the match on it (host). cire.Layout source: what the match runs."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        auto* Mode = World ? World->GetAuthGameMode<ACireGameMode>() : nullptr;
        FString Out;
        if (Args.Num() == 1 && Args[0] == TEXT("restart")) CireLayoutRuntime::RestartOnLayout(Mode, Out);
        else Out = FString::Printf(TEXT("The match runs %s. Use: cire.Layout restart"), *CireLanePath::ActiveSource());
        UE_LOG(LogCireLayoutRuntime, Display, TEXT("%s"), *Out);
    }));
#endif
