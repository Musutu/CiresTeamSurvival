// ui-themes: real wave-fight HUD captures. Runs on top of the wave soak (-CireWaveSoak, the local
// player becomes a bot) with rendering on: whenever a crowd of monsters is fighting near the player,
// the nearest one is targeted and a 1080p screenshot is taken (nameplates, cast bars, target/boss
// frames, threat). -CireUIWaveShots=N (default 4). Output: Saved/UIWaveCapture/<utc>/.
// Tools/RunUIWaveCapture.py drives it per theme (-CireUITheme=<Id>).
#include "CireUIWaveCapture.h"
#include "CireGame.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
struct FCapture
{
    bool bInit = false, bEnabled = false, bDone = false;
    int32 Wanted = 4, Taken = 0;
    double LastShot = -100, Armed = -1;
    FString Directory;
} G;
}

void CireUIWaveCapture::Tick(ACireGameMode* Mode)
{
    if (!G.bInit)
    {
        G.bInit = true;
        G.bEnabled = FParse::Param(FCommandLine::Get(), TEXT("CireUIWaveCapture"));
        FParse::Value(FCommandLine::Get(), TEXT("CireUIWaveShots="), G.Wanted);
        G.Wanted = FMath::Clamp(G.Wanted, 1, 20);
        G.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UIWaveCapture"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
        if (G.bEnabled) IFileManager::Get().MakeDirectory(*G.Directory, true);
    }
    if (!G.bEnabled || G.bDone || !Mode || !Mode->GetWorld()) return;
    APlayerController* PC = Mode->GetWorld()->GetFirstPlayerController();
    ACireHero* Hero = PC ? Cast<ACireHero>(PC->GetPawn()) : nullptr;
    if (!Hero || Hero->bDead) return;
    const double Now = Mode->GetWorld()->GetRealTimeSeconds();
    if (G.Armed > 0)
    {
        if (Now < G.Armed) return;
        const FString File = FPaths::Combine(G.Directory, FString::Printf(TEXT("wave_fight_%02d.png"), G.Taken + 1));
        FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
        UE_LOG(LogTemp, Display, TEXT("CIRE_UI_WAVE_CAPTURE %s"), *File);
        G.Armed = -1; G.LastShot = Now;
        if (++G.Taken >= G.Wanted)
        {
            G.bDone = true;
            UE_LOG(LogTemp, Display, TEXT("CIRE_UI_WAVE_CAPTURE_PASS captures=%d directory=%s"), G.Taken, *G.Directory);
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        return;
    }
    if (Now - G.LastShot < 7.0) return;
    // Only during an active wave: the breather opens the Skill Shop, which would cover the HUD.
    const auto* State = Mode->GetGameState<ACireGameState>();
    if (!State || State->Phase != 0 || (FMath::IsFinite(State->NextWaveSeconds) && State->NextWaveSeconds > .05f && State->NextWaveSeconds < 3600.f)) return;
    if (const auto* C = Cast<ACireController>(PC); C && C->bShop) return;
    // A crowd: at least five living lane monsters around the player.
    ACireMonster* Nearest = nullptr; double Best = MAX_dbl; int32 Near = 0;
    for (TActorIterator<ACireMonster> It(Mode->GetWorld()); It; ++It)
    {
        if (It->Health <= 0 || It->Lane != Hero->TeamId) continue;
        const double D = FVector::DistSquared(It->GetActorLocation(), Hero->GetActorLocation());
        if (D < FMath::Square(1600.)) { ++Near; if (D < Best) { Best = D; Nearest = *It; } }
    }
    if (Near < 5 || !Nearest) return;
    Hero->Target = Nearest; // target frame, selected plate and threat meter show this enemy
    G.Armed = Now + .4;
}
