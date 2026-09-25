// wave-director: headless full-match soak (-CireWaveSoak). Bots only, real match
// clock and real wave flow. Logs a stall report whenever a wave outlives its
// expected duration and writes a summary when the requested cycles complete.
#include "CireWaves.h"
#include "CireUIWaveCapture.h" // ui-themes
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireLoot.h"
#include "CireMonsterArt.h"
#include "CireNPCState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "CireHUD.h"
#include "CireCombatEvents.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireWaveSoak, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
struct FSoak
{
    bool bEnabled = false, bDone = false, bPlayerConverted = false;
    int32 TargetCycles = 3;
    float LimitSeconds = 3600, Elapsed = 0, NextReport = 0, NextStallDump = 0;
    int32 LastPhase = -1, LastWave = 0, LastCleared = 0, Transitions = 0;
    float WaveSpawnedAt = 0, LongestWave = 0, LongestPhaseWait = 0, PhaseEnteredAt = 0;
    int32 WavesSpawned = 0, WavesCleared = 0, StallDumps = 0, Failsafes = 0;
    TArray<FString> Lines;
    TMap<FString, int32> TypeCounts;
    float MeanLevelSeen = 1.f, CycleStartedAt = 0; // pacing: power spikes and cycle length
};
FSoak Soak;

FString MonsterLine(const ACireGameMode* Mode, const ACireMonster* M)
{
    const UWorld* World = M->GetWorld();
    const FVector P = M->GetActorLocation();
    const float Progress = CireLanePath::RouteProgress(World, FMath::Clamp(M->Lane, 0, 1), P);
    const ACireHero* V = M->Victim;
    const auto* Art = M->MonsterArt.Get();
    return FString::Printf(TEXT("  %s lane=%d pack=%d hp=%.0f/%.0f dmg=%.1f pos=(%.0f,%.0f,%.0f) route=%.2f wp=%d vel=%.0f victim=%s victim_dist=%.0f victim_dead=%d threat=%d engaged=%d leash=%.1f paused=%d escort=%d boss=%d cast=%s swing=%d neutral=%d stuck=%.1f"),
        *M->GetNPCDisplayName(), M->Lane, M->PackId, M->Health, M->MaxHealth, M->Damage, P.X, P.Y, P.Z, Progress, M->LaneWaypointIndex,
        M->GetVelocity().Size2D(), V ? *V->HeroName : TEXT("-"), V ? FVector::Dist2D(P, V->GetActorLocation()) : -1.f, V ? (V->bDead ? 1 : 0) : 0,
        M->Threat.Num(), M->bEngaged ? 1 : 0, M->LeashTimer, CireProgression::IsPaused(M) ? 1 : 0, M->bArmoredEscort ? 1 : 0, M->bBoss ? 1 : 0,
        M->CastingAbility.IsEmpty() ? TEXT("-") : *M->CastingAbility, Art && Art->HasPendingSwing() ? 1 : 0, M->bNeutral ? 1 : 0,
        CireWaveDirector::StuckSeconds(M));
}
void Log(const FString& Line)
{
    UE_LOG(LogCireWaveSoak, Display, TEXT("%s"), *Line);
    Soak.Lines.Add(Line);
}
}

bool CireWaveDirector::IsSoak() { return Soak.bEnabled; }

void CireWaveDirector::InitializeSoak(ACireGameMode* Mode)
{
    Soak = FSoak();
    Soak.bEnabled = Mode && FParse::Param(FCommandLine::Get(), TEXT("CireWaveSoak"));
    if (!Soak.bEnabled) return;
    FParse::Value(FCommandLine::Get(), TEXT("CireWaveSoakCycles="), Soak.TargetCycles);
    FParse::Value(FCommandLine::Get(), TEXT("CireWaveSoakSeconds="), Soak.LimitSeconds);
    Soak.TargetCycles = FMath::Clamp(Soak.TargetCycles, 1, 20);
    Soak.LimitSeconds = FMath::Clamp(Soak.LimitSeconds, 60.f, 6. * 3600.);
    Mode->BotFillTimer = 0;
    CireWaveDirector::Config(Mode->GetWorld());
    // monster-races: -CireWaveSoakRotation=drowned_deep,blightwood,... soaks chosen races (one per cycle).
    FString Rotation;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireWaveSoakRotation="), Rotation, false))
    {
        FCireWaveConfig C = CireWaveDirector::Config(Mode->GetWorld());
        Rotation.ParseIntoArray(C.Campaign.RaceRotation, TEXT(","), true);
        FString Error;
        if (!CireWaveDirector::ApplyLive(Mode, C, &Error)) Log(TEXT("CIRE_WAVE_SOAK_ROTATION_ERROR ") + Error);
        else Log(TEXT("CIRE_WAVE_SOAK_ROTATION ") + FString::Join(C.Campaign.RaceRotation, TEXT(",")));
    }
    Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_READY cycles=%d limit=%.0f failsafe=%d waves_per_cycle=%d"), Soak.TargetCycles, Soak.LimitSeconds,
        CireWaveDirector::Config().bStallFailsafe ? 1 : 0, CireWaveDirector::Config().WavesPerCycle));
}

void CireWaveDirector::NoteFailsafe(const FString& What)
{
    if (!Soak.bEnabled) return;
    ++Soak.Failsafes;
    Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_RESCUE %s"), *What));
}

void CireWaveDirector::DumpWave(ACireGameMode* Mode, const TCHAR* Reason)
{
    if (!Mode) return;
    auto* S = Mode->GetGameState<ACireGameState>();
    int32 Alive = 0;
    for (auto* M : Mode->Monsters) if (IsValid(M) && M->PackId < 0 && M->Health > 0) ++Alive;
    const FString Header = FString::Printf(TEXT("CIRE_WAVE_STALL_REPORT reason=%s round=%d wave=%d phase=%d alive=%d age=%.1f"), Reason,
        S ? S->Round : 0, S ? S->Wave : 0, S ? S->Phase : -1, Alive, CireWaveDirector::CurrentWaveAge(Mode));
    UE_LOG(LogCireWaveSoak, Warning, TEXT("%s"), *Header);
    Soak.Lines.Add(Header);
    for (auto* M : Mode->Monsters)
        if (IsValid(M) && M->Health > 0 && M->PackId < 0)
        {
            const FString Line = MonsterLine(Mode, M);
            UE_LOG(LogCireWaveSoak, Warning, TEXT("%s"), *Line);
            Soak.Lines.Add(Line);
        }
    for (auto* H : Mode->Heroes)
        if (IsValid(H))
        {
            const FString Line = FString::Printf(TEXT("  HERO %s team=%d bot=%d dead=%d hp=%.0f/%.0f pos=(%.0f,%.0f) target=%s"), *H->HeroName, H->TeamId, H->bBot ? 1 : 0,
                H->bDead ? 1 : 0, H->Health, H->MaxHealth, H->GetActorLocation().X, H->GetActorLocation().Y,
                IsValid(H->Target) ? *H->Target->GetName() : TEXT("-"));
            Soak.Lines.Add(Line);
        }
}

bool CireWaveDirector::TickSoak(ACireGameMode* Mode, float Delta)
{
    if (!Soak.bEnabled || Soak.bDone || !Mode) return false;
    CireUIWaveCapture::Tick(Mode); // ui-themes: HUD captures during real wave fights (-CireUIWaveCapture)
    auto* S = Mode->GetGameState<ACireGameState>();
    if (!S) return false;
    // The local standalone player is converted into a bot so the match runs unattended.
    // -CireWaveSoakPlayer=idle keeps a drafted human-like player who never acts (stands at
    // the gate), which is how monsters meet a real, distracted player.
    if (!Soak.bPlayerConverted)
        for (auto* H : Mode->Heroes)
            if (IsValid(H) && !H->bBot)
            {
                FString PlayerMode = TEXT("bot");
                FParse::Value(FCommandLine::Get(), TEXT("CireWaveSoakPlayer="), PlayerMode);
                H->Draft(2); H->HeroName = TEXT("Soak player");
                if (PlayerMode != TEXT("idle")) { H->bBot = true; H->bAutoAttack = true; }
                Soak.bPlayerConverted = true;
                Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_PLAYER mode=%s team=%d"), *PlayerMode, H->TeamId));
            }
    Soak.Elapsed += Delta;
    const float Now = Soak.Elapsed;
    if (S->Phase != Soak.LastPhase)
    {
        if (Soak.LastPhase >= 0)
        {
            ++Soak.Transitions;
            Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_PHASE %d->%d round=%d t=%.1f waited=%.1f"), Soak.LastPhase, S->Phase, S->Round, Now, Now - Soak.PhaseEnteredAt));
            if (S->Phase == 0) { Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_CYCLE round=%d took=%.1f"), S->Round - 1, Now - Soak.CycleStartedAt)); Soak.CycleStartedAt = Now; }
        }
        Soak.LastPhase = S->Phase; Soak.PhaseEnteredAt = Now;
    }
    if (S->Wave != Soak.LastWave)
    {
        Soak.LastWave = S->Wave; ++Soak.WavesSpawned; Soak.WaveSpawnedAt = Now; Soak.NextStallDump = Now + 150;
        const FString Type = CireWaveDirector::CurrentWaveType(Mode);
        Soak.TypeCounts.FindOrAdd(Type)++;
        Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_SPAWN wave=%d type=%s round=%d t=%.1f"), S->Wave, *Type, S->Round, Now));
    }
    if (S->CycleWavesDone != Soak.LastCleared)
    {
        if (S->CycleWavesDone > Soak.LastCleared)
        {
            ++Soak.WavesCleared;
            Soak.LongestWave = FMath::Max(Soak.LongestWave, Now - Soak.WaveSpawnedAt);
            Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_CLEAR wave=%d took=%.1f lives=%d/%d"), S->Wave, Now - Soak.WaveSpawnedAt, S->EmberLives, S->DuskLives));
        }
        Soak.LastCleared = S->CycleWavesDone;
    }
    // Pacing evidence: every time the mean hero level rises a whole level, and each full cycle's length.
    {
        float Sum = 0; int32 N = 0;
        for (auto* H : Mode->Heroes) if (IsValid(H) && H->bDrafted) { Sum += H->Level; ++N; }
        const float Mean = N ? Sum / N : 1.f;
        if (FMath::FloorToFloat(Mean) > FMath::FloorToFloat(Soak.MeanLevelSeen))
            Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_LEVEL mean=%.2f t=%.1f wave=%d"), Mean, Now, S->Wave));
        Soak.MeanLevelSeen = FMath::Max(Soak.MeanLevelSeen, Mean);
    }
    bool bWaveAlive = false;
    for (auto* M : Mode->Monsters) if (IsValid(M) && M->PackId < 0 && M->Health > 0) { bWaveAlive = true; break; }
    if (S->Phase == 0 && bWaveAlive && Now >= Soak.NextStallDump)
    {
        ++Soak.StallDumps; Soak.NextStallDump = Now + 60;
        DumpWave(Mode, TEXT("soak_slow_wave"));
    }
    if (Now >= Soak.NextReport)
    {
        Soak.NextReport = Now + 30;
        int32 Alive = 0, Packs = 0;
        for (auto* M : Mode->Monsters) if (IsValid(M) && M->Health > 0) { if (M->PackId < 0) ++Alive; else ++Packs; }
        int32 DeadHeroes = 0; for (auto* H : Mode->Heroes) if (IsValid(H) && H->bDead) ++DeadHeroes;
        Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_STATUS t=%.0f phase=%d round=%d wave=%d cleared=%d/%d alive=%d packs=%d dead_heroes=%d lives=%d/%d"), Now, S->Phase, S->Round,
            S->Wave, S->CycleWavesDone, S->WavesPerCycle, Alive, Packs, DeadHeroes, S->EmberLives, S->DuskLives));
    }
    const bool bCyclesDone = S->Round > Soak.TargetCycles;
    const bool bFinished = S->Phase == 3;
    if (bCyclesDone || bFinished || Now >= Soak.LimitSeconds)
    {
        Soak.bDone = true;
        const bool bPass = bCyclesDone && !bFinished && Soak.WavesCleared >= Soak.TargetCycles * FMath::Max(1, S->WavesPerCycle);
        FString Types; for (const auto& Pair : Soak.TypeCounts) Types += FString::Printf(TEXT("%s:%d "), *Pair.Key, Pair.Value);
        { const FCireRaceStats R = CireRaces::Stats(); // monster-races: race skill usage over the soak
          Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_RACES skill_casts=%d rider_hits=%d summoned=%d first_skill_wave=%d earliest_cast_wave=%d races=%s"),
              R.Casts, R.RiderHits, R.Summoned, CireWaveDirector::Config(Mode->GetWorld()).Skills.FirstSkillWave, R.EarliestCastWave, *R.Races)); }
        Log(FString::Printf(TEXT("CIRE_WAVE_SOAK_%s rounds=%d spawned=%d cleared=%d transitions=%d longest_wave=%.1f stall_dumps=%d rescues=%d lives=%d/%d t=%.0f types=%s"),
            bPass ? TEXT("PASS") : TEXT("FAIL"), S->Round - 1, Soak.WavesSpawned, Soak.WavesCleared, Soak.Transitions, Soak.LongestWave, Soak.StallDumps, Soak.Failsafes,
            S->EmberLives, S->DuskLives, Now, *Types.TrimEnd()));
        FString Path;
        if (!FParse::Value(FCommandLine::Get(), TEXT("CireWaveSoakSummary="), Path))
            Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WaveSoak"), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT(".txt"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        FFileHelper::SaveStringToFile(FString::Join(Soak.Lines, TEXT("\n")) + TEXT("\n"), *Path);
        FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
    }
    return false;
}

// wave-director: -CireWaveGallery renders the F8 Waves editor and a neutral (yellow) challenge
// pack, then the same pack provoked, to Saved/WaveGallery/<stamp>/ (run -RenderOffscreen).
namespace
{
struct FGallery
{
    bool bEnabled = false, bDone = false;
    int32 Stage = 0;
    double Started = 0, StageAt = 0;
    FString Directory;
    TArray<FString> Files;
};
FGallery Gallery;
void Shot(const FString& Name)
{
    const FString File = FPaths::Combine(Gallery.Directory, Name);
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    Gallery.Files.Add(File);
    UE_LOG(LogCireWaveSoak, Display, TEXT("CIRE_WAVE_GALLERY_CAPTURE file=%s"), *File);
}
}
bool CireWaveDirector::TickGallery(ACireGameMode* Mode)
{
    if (!Gallery.bEnabled && !Gallery.bDone && Gallery.Started == 0)
    {
        Gallery.Started = FPlatformTime::Seconds();
        Gallery.bEnabled = Mode && FParse::Param(FCommandLine::Get(), TEXT("CireWaveGallery"));
        if (!Gallery.bEnabled) { Gallery.bDone = true; return false; }
        Gallery.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WaveGallery"), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
        IFileManager::Get().MakeDirectory(*Gallery.Directory, true);
        Mode->WaveTimer = 1.e6f; Mode->bBotsFilled = true; Mode->BotFillTimer = 1.e6f;
    }
    if (!Gallery.bEnabled || Gallery.bDone) return false;
    auto* Controller = Mode->GetWorld()->GetFirstPlayerController();
    auto* Hero = Controller ? Cast<ACireHero>(Controller->GetPawn()) : nullptr;
    auto* HUD = Controller ? Cast<ACireHUD>(Controller->GetHUD()) : nullptr;
    if (!Hero || !HUD) return false;
    if (!Hero->bDrafted) { Hero->Draft(0); Hero->HeroName = TEXT("Eric"); }
    const double Now = FPlatformTime::Seconds();
    ACireMonster* Leader = nullptr; TArray<ACireMonster*> Pack;
    for (auto* M : Mode->Monsters) if (IsValid(M) && M->PackId >= 0 && M->Lane == Hero->TeamId) { Pack.Add(M); if (M->GetNPCClassification() == ECireNPCClass::Boss) Leader = M; }
    auto Frame = [&]()
    {
        if (Pack.IsEmpty()) return;
        FVector Center = FVector::ZeroVector; for (auto* M : Pack) Center += M->GetActorLocation(); Center /= Pack.Num();
        const FVector Stand = Center + FVector(-650, -180, 0);
        Hero->SetActorLocation(FVector(Stand.X, Stand.Y, Center.Z + 10), false, nullptr, ETeleportType::TeleportPhysics);
        const FRotator Face = (Center - Stand).GetSafeNormal2D().Rotation();
        Hero->SetActorRotation(Face); Controller->SetControlRotation(FRotator(-18, Face.Yaw, 0));
    };
    switch (Gallery.Stage)
    {
    case 0:
        if (Now - Gallery.Started < 6) return false;
        HUD->SetSkillOfferOpen(false); HUD->DebugOptionsPage(5, 7, true); Gallery.StageAt = Now; ++Gallery.Stage; return false;
    case 1:
        if (Now - Gallery.StageAt < 2.5) return false;
        Shot(TEXT("01_wave_editor.png")); Gallery.StageAt = Now; ++Gallery.Stage; return false;
    case 2:
        if (Now - Gallery.StageAt < 1) return false;
        HUD->DebugOptionsPage(0, 0, false); HUD->SetSkillOfferOpen(false); Frame(); Hero->Target = Leader; Gallery.StageAt = Now; ++Gallery.Stage; return false;
    case 3:
        Frame(); HUD->SetSkillOfferOpen(false);
        if (Now - Gallery.StageAt < 7) return false; // let the district banner fade
        {
            int32 Neutral = 0; for (auto* M : Pack) Neutral += M->bNeutral ? 1 : 0;
            UE_LOG(LogCireWaveSoak, Display, TEXT("CIRE_WAVE_GALLERY_PACK neutral=%d/%d"), Neutral, Pack.Num());
        }
        Shot(TEXT("02_neutral_pack.png")); Gallery.StageAt = Now; ++Gallery.Stage; return false;
    case 4:
        if (Now - Gallery.StageAt < 1) return false;
        if (Leader) CireCombat::ApplyDamage(Hero, Leader, 1, TEXT("Gallery provoke"));
        Hero->Health = Hero->MaxHealth = 100000; Gallery.StageAt = Now; ++Gallery.Stage; return false;
    case 5:
        if (Now - Gallery.StageAt < 1.2) return false;
        {
            int32 Neutral = 0; for (auto* M : Pack) Neutral += M->bNeutral ? 1 : 0;
            UE_LOG(LogCireWaveSoak, Display, TEXT("CIRE_WAVE_GALLERY_PROVOKED neutral=%d/%d"), Neutral, Pack.Num());
        }
        Shot(TEXT("03_provoked_pack.png")); Gallery.StageAt = Now; ++Gallery.Stage; return false;
    default:
        if (Now - Gallery.StageAt < 2) return false;
        {
            bool bOK = Gallery.Files.Num() == 3;
            for (const FString& F : Gallery.Files) bOK &= IFileManager::Get().FileSize(*F) > 1024;
            UE_LOG(LogCireWaveSoak, Display, TEXT("CIRE_WAVE_GALLERY_%s dir=%s"), bOK ? TEXT("DONE") : TEXT("INCOMPLETE"), *Gallery.Directory);
            Gallery.bDone = true;
            FPlatformMisc::RequestExitWithStatus(false, bOK ? 0 : 1);
        }
        return false;
    }
}
#else
bool CireWaveDirector::TickGallery(ACireGameMode*) { return false; }
bool CireWaveDirector::IsSoak() { return false; }
void CireWaveDirector::InitializeSoak(ACireGameMode*) {}
bool CireWaveDirector::TickSoak(ACireGameMode*, float) { return false; }
void CireWaveDirector::NoteFailsafe(const FString&) {}
void CireWaveDirector::DumpWave(ACireGameMode*, const TCHAR*) {}
#endif
