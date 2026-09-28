// bosses-spacing: -CireBossSpacingProbe (Tools/RunBossSpacingProbe.py). Runs in a real match world (the town with Eric's
// MapLayout.json when -CireTown -CireUseMapLayout, else the Citadel):
//   1. CROWD before/after: 16 melee monsters engage one (unkillable) hero on the lane road, once with the legacy spacing
//      (capsule 38, no reach bonus, no separation) and once with UnitSpacing.json; over the last seconds it measures the
//      pairs whose drawn footprints overlap and the mean nearest-neighbour gap. After must overlap less.
//   2. GIANTS: every outdoor world boss is 5x its legacy size with the Large-agent capsule, stands on the navmesh and has a
//      Large-agent path from its lair to the realm's player spawn (it can chase you through the streets).
//   3. MARCH: a 5x wave boss (Siege Host) marches the lane road through the town for 45 s without stalling.
//   4. CAPTURES (-CireBossSpacingShots, rendering): the marching boss and a world boss with the raid bar, in all 4 HUD themes.
// Logs CIRE_BOSS_SPACING_PROBE_PASS / _FAIL; writes Saved/BossSpacing/<stamp>/probe.txt (+ PNGs).
#include "CireUnitSpacing.h"

#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireNav.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireOutdoorBosses.h"
#include "CireThreat.h"
#include "CireTownMap.h"
#include "CireUITheme.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireBossSpacingProbe, Log, All);

namespace
{
enum class EStage : uint8 { Setup, CrowdBefore, CrowdAfter, Giants, March, Shots, Done };
struct FSpacingProbe
{
    bool bEnabled = false, bPass = true, bShots = false;
    EStage Stage = EStage::Setup;
    float Clock = 0, StageAt = 0, NextSample = 0;
    int32 Team = 0, Shot = 0;
    FString Directory;
    TArray<FString> Lines;
    TArray<ACireMonster*> Crowd;
    ACireHero* Victim = nullptr;
    FVector Centre = FVector::ZeroVector;
    TArray<CireUnitSpacing::FCrowd> Samples;
    CireUnitSpacing::FCrowd Before, After;
    ACireMonster* Boss = nullptr;
    float BossStart = 0, LastMoveAt = 0, LongestStall = 0;
    FVector LastPos = FVector::ZeroVector;
    FCireUnitSpacing Saved;
};
FSpacingProbe SP;
void SPNote(const FString& Line) { UE_LOG(LogCireBossSpacingProbe, Display, TEXT("%s"), *Line); SP.Lines.Add(Line); }
void SPFail(const FString& Why) { SP.bPass = false; UE_LOG(LogCireBossSpacingProbe, Error, TEXT("CIRE_BOSS_SPACING_PROBE_CHECK_FAIL %s"), *Why); SP.Lines.Add(TEXT("FAIL ") + Why); }
void SPFinish()
{
    SP.Stage = EStage::Done;
    CireUnitSpacing::Set(SP.Saved);
    SPNote(FString::Printf(TEXT("CIRE_BOSS_SPACING_PROBE_%s"), SP.bPass ? TEXT("PASS") : TEXT("FAIL")));
    FFileHelper::SaveStringToFile(FString::Join(SP.Lines, TEXT("\n")) + TEXT("\n"), *(SP.Directory / TEXT("probe.txt")));
    FPlatformMisc::RequestExitWithStatus(false, SP.bPass ? 0 : 1);
}
FVector OnNav(UWorld* World, const FVector& At, float Radius = 48.f)
{
    FVector Out;
    if (CireNav::Project(World, At + FVector(0, 0, 100), Out, FVector(300, 300, 1500), Radius)) return Out;
    return At;
}
ACireMonster* SpawnUnit(ACireGameMode* Mode, const FVector& Floor, FName Id, bool bLaneBoss)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), Floor + FVector(0, 0, 100), FRotator::ZeroRotator, Params);
    if (!M) return nullptr;
    M->Lane = SP.Team;
    CireNPCCombat::ConfigureArchetype(M, Id, 1, 0, 1, bLaneBoss);
    M->SetActorLocation(Floor + FVector(0, 0, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f));
    M->SpawnPosition = M->GetActorLocation();
    Mode->Monsters.Add(M);
    return M;
}
void ClearCrowd(ACireGameMode* Mode)
{
    for (ACireMonster* M : SP.Crowd) if (IsValid(M)) { Mode->Monsters.Remove(M); M->Destroy(); }
    SP.Crowd.Reset();
}
void StartCrowd(ACireGameMode* Mode, bool bLegacy)
{
    CireUnitSpacing::Set(bLegacy ? FCireUnitSpacing::Legacy() : SP.Saved);
    UWorld* World = Mode->GetWorld();
    const FName Melee = CireNPCArchetypes::Get().LegacyKinds.IsValidIndex(0) ? CireNPCArchetypes::Get().LegacyKinds[0] : NAME_None;
    SP.Victim->SetActorLocation(SP.Centre + FVector(0, 0, SP.Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f));
    SP.Victim->Health = SP.Victim->MaxHealth = 1.e8f;
    for (int32 I = 0; I < 16; ++I)
    {
        const float A = I * 2.f * PI / 16.f, R = 520.f + (I % 2) * 140.f;
        ACireMonster* M = SpawnUnit(Mode, OnNav(World, SP.Centre + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0)), Melee, false);
        if (!M) continue;
        M->Damage = 1.f; // the crowd is about spacing, not killing
        CireThreat::Engage(M, SP.Victim);
        SP.Crowd.Add(M);
    }
    SP.Samples.Reset(); SP.StageAt = SP.Clock; SP.NextSample = SP.Clock + 7.f;
}
CireUnitSpacing::FCrowd Average(const TArray<CireUnitSpacing::FCrowd>& S)
{
    CireUnitSpacing::FCrowd A; if (S.IsEmpty()) return A;
    float Pairs = 0;
    for (const auto& C : S) { A.Units = FMath::Max(A.Units, C.Units); Pairs += C.OverlapPairs; A.MeanNearest += C.MeanNearest / S.Num(); A.MeanOverlapDepth += C.MeanOverlapDepth / S.Num(); }
    A.OverlapPairs = FMath::RoundToInt(Pairs / S.Num());
    return A;
}
bool TickCrowd(ACireGameMode* Mode, const TCHAR* Label, CireUnitSpacing::FCrowd& Out)
{
    SP.Victim->Health = SP.Victim->MaxHealth;
    SP.Victim->SetActorLocation(SP.Centre + FVector(0, 0, SP.Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f));
    for (ACireMonster* M : SP.Crowd) if (IsValid(M) && M->Victim != SP.Victim) CireThreat::Engage(M, SP.Victim);
    if (SP.Clock >= SP.NextSample) { SP.Samples.Add(CireUnitSpacing::Measure(SP.Crowd)); SP.NextSample += .5f; }
    if (SP.Clock - SP.StageAt < 12.f) return false;
    Out = Average(SP.Samples);
    int32 Engaged = 0; for (ACireMonster* M : SP.Crowd) Engaged += IsValid(M) && M->Victim == SP.Victim && FVector::Dist2D(M->GetActorLocation(), SP.Centre) < 500.f;
    SPNote(FString::Printf(TEXT("CIRE_BOSS_SPACING_PROBE_CROWD %s units=%d engaged_near=%d overlap_pairs=%d mean_overlap=%.0fcm mean_nearest=%.0fcm samples=%d"), Label, Out.Units, Engaged,
        Out.OverlapPairs, Out.MeanOverlapDepth, Out.MeanNearest, SP.Samples.Num()));
    if (Engaged < 10) SPFail(FString::Printf(TEXT("%s: only %d of the crowd reached the hero"), Label, Engaged));
    ClearCrowd(Mode);
    return true;
}
void CheckGiants(ACireGameMode* Mode)
{
    UWorld* World = Mode->GetWorld();
    int32 Count = 0;
    for (ACireMonster* M : Mode->Monsters)
    {
        if (!IsValid(M) || M->Health <= 0 || !CireOutdoorBosses::IsOutdoorBoss(M)) continue;
        ++Count;
        const UCapsuleComponent* Cap = M->GetCapsuleComponent();
        const int32 Realm = M->Lane < 0 ? 0 : M->Lane;
        FVector Nav; const bool bNav = CireNav::Project(World, M->GetActorLocation() - FVector(0, 0, Cap->GetScaledCapsuleHalfHeight()) + FVector(0, 0, 60), Nav, FVector(200, 200, 600), CireNav::AgentRadius(M));
        const FVector Home = OnNav(World, CireLanePath::PlayerSpawnTransform(World, Realm, 0).GetLocation(), CireNav::AgentRadius(M));
        const FCireNavPath Path = CireNav::FindPath(World, bNav ? Nav : M->GetActorLocation(), Home, CireNav::AgentRadius(M), false);
        const bool bPath = Path.bValid && !Path.bPartial;
        SPNote(FString::Printf(TEXT("CIRE_BOSS_SPACING_PROBE_GIANT realm=%d boss=%s scale=%.2f capsule=%.0f/%.0f drawn_height=%.0fm on_nav=%d path_to_spawn=%d length=%.0fm"), Realm,
            *(M->NPCState ? M->NPCState->ArchetypeId.ToString() : FString()), M->GetActorScale3D().X, Cap->GetScaledCapsuleRadius(), Cap->GetScaledCapsuleHalfHeight(),
            CireUnitSpacing::BaseHalfHeight * 2.f * M->GetActorScale3D().Z / 100.f, bNav ? 1 : 0, bPath ? 1 : 0, Path.Length / 100.f));
        if (!CireUnitSpacing::IsBossBody(M) || M->GetActorScale3D().X < 4.f) SPFail(TEXT("an outdoor boss is not drawn 5x"));
        if (Cap->GetScaledCapsuleRadius() > 72.5f || Cap->GetScaledCapsuleHalfHeight() > 150.5f) SPFail(TEXT("an outdoor boss capsule exceeds the Large nav agent"));
        if (!bNav) SPFail(TEXT("an outdoor boss stands off the navmesh"));
        if (!bPath) SPFail(TEXT("an outdoor boss has no Large-agent path to the player spawn"));
    }
    SPNote(FString::Printf(TEXT("CIRE_BOSS_SPACING_PROBE_GIANTS outdoor_bosses=%d"), Count));
}
ACireMonster* NearestOutdoorBoss(ACireGameMode* Mode, int32 Realm)
{
    for (ACireMonster* M : Mode->Monsters) if (IsValid(M) && M->Health > 0 && CireOutdoorBosses::IsOutdoorBoss(M) && M->Lane == Realm) return M;
    return nullptr;
}
ACireHero* LocalHero(UWorld* World)
{
    APlayerController* PC = World->GetFirstPlayerController();
    return PC ? Cast<ACireHero>(PC->GetPawn()) : nullptr;
}
/** Puts the local hero Back cm behind Subject (seen from Dir), facing it, and targets it (raid bar selected). */
void Frame(UWorld* World, ACireMonster* Subject, const FVector& Dir, float Back)
{
    ACireHero* Hero = LocalHero(World);
    if (!Hero || !Subject) return;
    const FVector Floor = OnNav(World, Subject->GetActorLocation() - Dir.GetSafeNormal2D() * Back);
    Hero->SetActorLocation(Floor + FVector(0, 0, Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
    const FRotator Face = (Subject->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal2D().Rotation();
    Hero->SetActorRotation(Face);
    if (APlayerController* PC = World->GetFirstPlayerController()) PC->SetControlRotation(FRotator(-8.f, Face.Yaw, 0));
    Hero->Target = Subject;
}
}

void CireUnitSpacing::InitializeProbe(ACireGameMode* Mode)
{
    SP = FSpacingProbe();
    SP.bEnabled = Mode && FParse::Param(FCommandLine::Get(), TEXT("CireBossSpacingProbe"));
    if (!SP.bEnabled) return;
    SP.bShots = FParse::Param(FCommandLine::Get(), TEXT("CireBossSpacingShots"));
    SP.Saved = Get();
    if (!FParse::Value(FCommandLine::Get(), TEXT("CireBossSpacingDir="), SP.Directory))
        SP.Directory = FPaths::ProjectSavedDir() / TEXT("BossSpacing") / FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
    SP.Directory = FPaths::ConvertRelativePathToFull(SP.Directory);
    IFileManager::Get().MakeDirectory(*SP.Directory, true);
    Mode->BotFillTimer = 0;
    SPNote(FString::Printf(TEXT("CIRE_BOSS_SPACING_PROBE_READY town=%d shots=%d"), CireTownMap::IsActive() ? 1 : 0, SP.bShots ? 1 : 0));
}

bool CireUnitSpacing::TickProbe(ACireGameMode* Mode, float Delta)
{
    if (!SP.bEnabled || !Mode) return false;
    if (SP.Stage == EStage::Done) return true;
    UWorld* World = Mode->GetWorld();
    auto* State = Mode->GetGameState<ACireGameState>();
    SP.Clock += Delta;
    Mode->WaveTimer = 1.e6f; // no waves: only the probe's units, the packs and the world bosses
    ACireHero* Local = LocalHero(World);
    for (auto* H : Mode->Heroes) if (IsValid(H) && !H->bBot && H != Local) { H->Draft(2); H->bBot = true; H->bAutoAttack = false; }
    if (Local && !Local->bDrafted && Mode->bBotsFilled) Local->Draft(2);
    // Everyone but the probe's hero stands aside: undrafted bots aggro nothing and nothing aggroes them.
    if (SP.Stage != EStage::Setup)
        for (auto* H : Mode->Heroes) if (IsValid(H) && H != SP.Victim && H != Local) { H->bDrafted = false; H->Target = nullptr; H->bAutoAttack = false; }
    if (Local && SP.Stage != EStage::Setup) { Local->bBot = false; Local->bAutoAttack = false; Local->Health = Local->MaxHealth; if (SP.Stage != EStage::Shots) Local->bDrafted = false; }
    switch (SP.Stage)
    {
    case EStage::Setup:
    {
        if (!Mode->bBotsFilled || !CireNav::IsReady(World) || !State || State->Phase != 0)
        {
            if (SP.Clock > (CireTownMap::IsActive() ? 900.f : 150.f)) { SPFail(TEXT("setup timed out")); SPFinish(); }
            return true;
        }
        SP.Team = Local ? FMath::Clamp(Local->TeamId, 0, 1) : 0;
        SP.Centre = OnNav(World, CireLanePath::PointAlongRoute(World, SP.Team, .45f, 110));
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SP.Victim = World->SpawnActor<ACireHero>(SP.Centre + FVector(0, 0, 100), FRotator::ZeroRotator, Params);
        if (!SP.Victim) { SPFail(TEXT("no probe hero")); SPFinish(); return true; }
        SP.Victim->TeamId = SP.Team; SP.Victim->Draft(0); SP.Victim->HeroName = TEXT("Spacing probe tank"); SP.Victim->bAutoAttack = false;
        Mode->Heroes.Add(SP.Victim);
        SPNote(FString::Printf(TEXT("CIRE_BOSS_SPACING_PROBE_SETUP team=%d centre=(%.0f,%.0f,%.0f) after: radius=%.0f melee_bonus=%.0f pad=%.0f boss=%.1fx"), SP.Team, SP.Centre.X, SP.Centre.Y, SP.Centre.Z,
            SP.Saved.MonsterCapsuleRadius, SP.Saved.MeleeReachBonus, SP.Saved.SeparationPadding, SP.Saved.BossSizeMultiplier));
        StartCrowd(Mode, true);
        SP.Stage = EStage::CrowdBefore;
        return true;
    }
    case EStage::CrowdBefore:
        if (TickCrowd(Mode, TEXT("before(legacy)"), SP.Before)) { StartCrowd(Mode, false); SP.Stage = EStage::CrowdAfter; }
        return true;
    case EStage::CrowdAfter:
        if (!TickCrowd(Mode, TEXT("after"), SP.After)) return true;
        SPNote(FString::Printf(TEXT("CIRE_BOSS_SPACING_PROBE_OVERLAP before=%d after=%d (%.0f%% fewer) nearest %.0f -> %.0f cm"), SP.Before.OverlapPairs, SP.After.OverlapPairs,
            SP.Before.OverlapPairs > 0 ? 100.f * (SP.Before.OverlapPairs - SP.After.OverlapPairs) / SP.Before.OverlapPairs : 0.f, SP.Before.MeanNearest, SP.After.MeanNearest));
        if (SP.After.OverlapPairs >= SP.Before.OverlapPairs && SP.Before.OverlapPairs > 0) SPFail(TEXT("the new spacing does not reduce overlapping units"));
        if (SP.After.MeanNearest <= SP.Before.MeanNearest) SPFail(TEXT("the new spacing does not spread units further apart"));
        Mode->Heroes.Remove(SP.Victim); SP.Victim->Destroy(); SP.Victim = nullptr;
        CireUnitSpacing::Set(SP.Saved);
        SP.Stage = EStage::Giants; SP.StageAt = SP.Clock;
        return true;
    case EStage::Giants:
    {
        CheckGiants(Mode);
        const FName WaveBoss = CireNPCArchetypes::Get().WaveBoss;
        SP.Boss = SpawnUnit(Mode, OnNav(World, CireLanePath::SpawnPosition(World, SP.Team), 72.f), WaveBoss, true);
        if (!SP.Boss) { SPFail(TEXT("no wave boss")); SPFinish(); return true; }
        SP.BossStart = CireLanePath::RouteProgress(World, SP.Team, SP.Boss->GetActorLocation()) * CireLanePath::RouteLength(World, SP.Team);
        SP.LastPos = SP.Boss->GetActorLocation(); SP.LastMoveAt = SP.Clock; SP.LongestStall = 0;
        SPNote(FString::Printf(TEXT("CIRE_BOSS_SPACING_PROBE_MARCH_START boss=%s scale=%.2f capsule=%.0f/%.0f nav_agent_radius=%.0f"), *WaveBoss.ToString(), SP.Boss->GetActorScale3D().X,
            SP.Boss->GetCapsuleComponent()->GetScaledCapsuleRadius(), SP.Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), CireNav::AgentRadius(SP.Boss)));
        SP.Stage = EStage::March; SP.StageAt = SP.Clock;
        return true;
    }
    case EStage::March:
    {
        if (!IsValid(SP.Boss) || SP.Boss->Health <= 0) { SPFail(TEXT("the marching boss vanished")); SPFinish(); return true; }
        SP.Boss->Health = SP.Boss->MaxHealth;
        if (FVector::Dist2D(SP.Boss->GetActorLocation(), SP.LastPos) > 60.f) { SP.LastPos = SP.Boss->GetActorLocation(); SP.LastMoveAt = SP.Clock; }
        SP.LongestStall = FMath::Max(SP.LongestStall, SP.Clock - SP.LastMoveAt);
        // Mid-march captures: the giant in the town street with its raid bar.
        if (SP.bShots && SP.Clock - SP.StageAt > 18.f && SP.Shot < 4)
        {
            if (Local) { Local->bDrafted = true; Frame(World, SP.Boss, SP.Boss->GetVelocity().IsNearlyZero() ? FVector(1, 0, 0) : -SP.Boss->GetVelocity(), 2000.f); }
            static const TCHAR* Themes[] = {TEXT("GildedCitadel"), TEXT("Ironbound"), TEXT("ArcaneVeil"), TEXT("VerdantBloom")};
            static float ArmedAt = -1.f;
            if (ArmedAt < 0) { CireUITheme::SetActive(Themes[SP.Shot]); ArmedAt = SP.Clock + .8f; }
            else if (SP.Clock >= ArmedAt)
            {
                const FString File = SP.Directory / FString::Printf(TEXT("siege_boss_%s.png"), Themes[SP.Shot]);
                FScreenshotRequest::RequestScreenshot(File, true, false, false, FIntRect(), true);
                SPNote(TEXT("CIRE_BOSS_SPACING_PROBE_SHOT ") + File);
                ++SP.Shot; ArmedAt = -1.f;
            }
        }
        if (SP.Clock - SP.StageAt < 45.f) return true;
        const float Now = CireLanePath::RouteProgress(World, SP.Team, SP.Boss->GetActorLocation()) * CireLanePath::RouteLength(World, SP.Team);
        SPNote(FString::Printf(TEXT("CIRE_BOSS_SPACING_PROBE_MARCH advanced=%.1fm in 45s longest_stall=%.1fs at=(%.0f,%.0f)"), (Now - SP.BossStart) / 100.f, SP.LongestStall,
            SP.Boss->GetActorLocation().X, SP.Boss->GetActorLocation().Y));
        if (Now - SP.BossStart < 2000.f) SPFail(TEXT("the giant wave boss did not march 20 m along the town road"));
        if (SP.LongestStall > 6.f) SPFail(TEXT("the giant wave boss stalled on the road"));
        SP.Stage = SP.bShots ? EStage::Shots : EStage::Done; SP.StageAt = SP.Clock; SP.Shot = 0;
        if (!SP.bShots) SPFinish();
        return true;
    }
    case EStage::Shots:
    {
        // A world boss in its lair, raid bar on top, per theme.
        if (SP.Shot >= 4) { if (SP.Clock - SP.StageAt > 2.f) { CireUITheme::SetActive(CireUITheme::DefaultId()); SPFinish(); } return true; } // let the last PNG write
        ACireMonster* World0 = NearestOutdoorBoss(Mode, SP.Team);
        if (!World0 || !Local) { SPNote(TEXT("CIRE_BOSS_SPACING_PROBE_SHOT skipped: no world boss")); SPFinish(); return true; }
        Local->bDrafted = true;
        Frame(World, World0, FVector(1, .4f, 0), 1600.f);
        static const TCHAR* Themes[] = {TEXT("GildedCitadel"), TEXT("Ironbound"), TEXT("ArcaneVeil"), TEXT("VerdantBloom")};
        static float ArmedAt = -1.f;
        if (SP.Shot == 0 && SP.Clock - SP.StageAt < 2.f) return true;
        if (ArmedAt < 0) { CireUITheme::SetActive(Themes[SP.Shot]); ArmedAt = SP.Clock + .8f; return true; }
        if (SP.Clock < ArmedAt) return true;
        const FString File = SP.Directory / FString::Printf(TEXT("world_boss_%s.png"), Themes[SP.Shot]);
        FScreenshotRequest::RequestScreenshot(File, true, false, false, FIntRect(), true);
        SPNote(TEXT("CIRE_BOSS_SPACING_PROBE_SHOT ") + File);
        ArmedAt = -1.f;
        if (++SP.Shot >= 4) SP.StageAt = SP.Clock;
        return true;
    }
    default: return true;
    }
}
#endif
