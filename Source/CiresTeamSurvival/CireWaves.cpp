// wave-director: authoritative wave runtime, neutral challenge packs and bot lane defence.
#include "CireWaves.h"
#include "CireGame.h"
#include "CireItems.h" // progression-shop: ready flags on the inventory
#include "CireLanePath.h"
#include "CireTownMap.h" // medieval-kingdom
#include "CireLoot.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireThreat.h"
#include "CireSummon.h"
#include "CireDeveloperTools.h"
#include "CireNav.h" // nav-paths
#include "CireMonsterExpansion.h" // monster-expansion
#include "CireLeash.h" // layout-wiring
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireWaves, Log, All);

namespace
{
struct FTrack
{
    int32 Serial = 0;
    float SpawnedAt = 0, Size = 1, Reward = 1;
    bool bMustClear = true, bEscortee = false, bGuard = false;
    TWeakObjectPtr<ACireMonster> Charge;
    FVector Anchor = FVector::ZeroVector;
    float SampleAt = 0, StuckFor = 0, SuppressUntil = 0, GhostRefreshAt = 0, ForcedAt = 0;
    int32 Nudges = 0;
    bool bForcedMarch = false;
    float BestProgress = -1.f, BestProgressAt = 0.f; // layout-wiring: cm along its own path, and when it last improved
    FCireWaveUnitInfo Info; // economy hook
};
struct FWaveRecord
{
    FString Label, Type;
    float StartedAt = 0, LastSpawnAt = 0;
    bool bMustClear = true;
    int32 WaveNumber = 0, WaveInCycle = 0, Cycle = 1;
    ECireWaveType WaveType = ECireWaveType::Normal;
};
struct FOrder
{
    FCireWaveUnit Unit;
    int32 Slot = 0, Serial = 0;
    bool bMustClear = true;
    float Reward = 1;
    ECireNPCRank Rank = ECireNPCRank::Normal; // monster-races: row rank plus campaign promotion
    bool bBonus = false; // monster-expansion: bonus loot wave creature
};
struct FBotState
{
    bool bRetreating = false; float RetreatUntil = 0;
    FVector Anchor = FVector::ZeroVector; float AnchorAt = 0, DetourUntil = 0; FVector Detour = FVector::ZeroVector; int32 Side = 1;
};
struct FRuntime
{
    FCireWaveConfig Config;
    bool bInitialized = false;
    TArray<FOrder> Queue;
    float SpawnTimer = 0, SpawnInterval = .6f;
    int32 Serial = 0, CurrentSerial = 0;
    TMap<int32, FWaveRecord> Records;
    TMap<TWeakObjectPtr<ACireMonster>, FTrack> Tracks;
    TSet<TWeakObjectPtr<ACireMonster>> SeenPacks;
    TMap<TWeakObjectPtr<ACireHero>, FBotState> Bots;
    int32 LeakCostSpawned[2] = {0, 0};
    int32 BossesSpawned = 0;
    // layout-wiring: per realm, the running slot of the path split and the boss spot rotation.
    int32 PathSlot[2] = {0, 0}, BossSlot[2] = {0, 0};
    TMap<int32, int32> PathSpawned[2]; // path index -> units sent down it (probes and the F8 readout)
    int32 Nudges = 0, Marches = 0, Despawns = 0; // nav-paths: rescue totals for the navigation probe
    TSet<TWeakObjectPtr<ACireHero>> Ready; // breather Ready presses since the last wave started
    int32 LastWaveNumber = 0;
    // monster-expansion: rare spawns and bonus waves this cycle and this match.
    int32 RaresThisCycle = 0, BonusThisCycle = 0, RaresTotal = 0, BonusTotal = 0;
};
/** Phase clock and breather from the pacing block (normal matches only; smoke/probes keep their own timing). */
void ApplyPacing(ACireGameMode* Mode, const FCireWaveConfig& C)
{
    if (Mode->bSmoke) return;
    Mode->Clock.SetDurations({C.PrepSeconds, C.ArenaSeconds, C.RecoverySeconds});
    Mode->RecoverySeconds = C.RecoverySeconds;
    Mode->WaveBreatherSeconds = C.BreatherSeconds;
}
TMap<TWeakObjectPtr<UWorld>, FRuntime> Runtimes;
FCireWaveConfig FileConfig;
bool bFileLoaded = false;

const FCireWaveConfig& GlobalConfig()
{
    if (!bFileLoaded)
    {
        bFileLoaded = true;
        FString Error;
        if (!CireWaveDirector::LoadFile(FileConfig, &Error))
        {
            UE_LOG(LogCireWaves, Warning, TEXT("CIRE_WAVES_DEFAULTS %s; using built-in defaults"), *Error);
            FileConfig = CireWaveDirector::Defaults();
        }
    }
    return FileConfig;
}
FRuntime* Find(const UWorld* World)
{
    return World ? Runtimes.Find(TWeakObjectPtr<UWorld>(const_cast<UWorld*>(World))) : nullptr;
}
FRuntime& Get(const ACireGameMode* Mode)
{
    for (auto It = Runtimes.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    FRuntime& R = Runtimes.FindOrAdd(Mode->GetWorld());
    if (!R.bInitialized) { R.bInitialized = true; R.Config = GlobalConfig(); }
    return R;
}
const FTrack* TrackOf(const ACireMonster* M)
{
    if (!M) return nullptr;
    const FRuntime* R = Find(M->GetWorld());
    return R ? R->Tracks.Find(TWeakObjectPtr<ACireMonster>(const_cast<ACireMonster*>(M))) : nullptr;
}
bool AliveUnit(const ACireMonster* M) { return IsValid(M) && !M->IsActorBeingDestroyed() && M->Health > 0; }
bool WaveUnit(const ACireMonster* M) { return AliveUnit(M) && M->PackId < 0 && M->Lane >= 0 && M->Lane < 2; }
float Now(const UObject* O) { return O && O->GetWorld() ? O->GetWorld()->GetTimeSeconds() : 0.f; }
int32 LeakCost(const ACireMonster* M) { return M->LeakCostOverride > 0 ? M->LeakCostOverride : M->bBoss ? 10 : 1; }
float Reach(const ACireMonster* M)
{
    const auto* A = M->NPCState ? M->NPCState->Archetype() : nullptr;
    return A ? A->AttackRange : M->CombatArchetype >= 2 ? 650.f : 170.f;
}
void Publish(ACireGameMode* Mode)
{
    auto* S = Mode->GetGameState<ACireGameState>();
    if (!S) return;
    const auto Sum = CireWaveDirector::Summary(Mode);
    if (S->WaveLabel != Sum.Current || S->NextWaveLabel != Sum.Next) { S->WaveLabel = Sum.Current; S->NextWaveLabel = Sum.Next; S->ForceNetUpdate(); }
}
void Ghost(ACireMonster* M, FTrack& T)
{
    const float Time = Now(M);
    if (T.GhostRefreshAt > Time) return;
    T.GhostRefreshAt = Time + 1.f;
    for (TActorIterator<ACharacter> It(M->GetWorld()); It; ++It)
        if (*It != M && (Cast<ACireHero>(*It) || Cast<ACireMonster>(*It)))
            M->GetCapsuleComponent()->IgnoreActorWhenMoving(*It, true);
}
/** rules-conformance: undo Ghost() when a forced marcher is attacked and turns to fight. */
void Unghost(ACireMonster* M)
{
    if (M && M->GetCapsuleComponent()) M->GetCapsuleComponent()->ClearMoveIgnoreActors();
}
/** Teleport a unit onto its route, Step cm closer to the castle than its nearest route point. layout-wiring: its OWN path. */
void NudgeAlong(ACireMonster* M, float Step)
{
    UWorld* World = M->GetWorld();
    const int32 Team = FMath::Clamp(M->Lane, 0, 1);
    const float Length = FMath::Max(1.f, CireLanePath::PathLengthOf(World, Team, M->LanePath));
    const float Progress = CireLanePath::PathProgress(World, Team, M->LanePath, M->GetActorLocation());
    FVector Target = CireLanePath::PointAlongPath(World, Team, M->LanePath, FMath::Min(.995f, Progress + Step / Length), M->GetActorLocation().Z);
    if (CireTownMap::IsActive()) Target.Z = CireTownMap::Ground(World, FVector2D(Target)) + M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.f; // medieval-kingdom: landscape
    else Target.Z = FMath::Max(Target.Z, 100.);
    M->GetCharacterMovement()->StopMovementImmediately();
    M->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
    CireLanePath::InitializeProgress(M);
}
// Diagnostics: -CireWaveNoRescue keeps detection/logging but disables nudges and the failsafe,
// so a soak can show what would have stalled under the pre-director behaviour.
bool NoRescue()
{
    static const bool bValue = FParse::Param(FCommandLine::Get(), TEXT("CireWaveNoRescue"));
    return bValue;
}
bool PlayerSide(const ACireHero* Attacker)
{
    if (!IsValid(Attacker)) return false;
    if (const auto* Summon = Cast<ACireSummon>(Attacker)) return IsValid(Summon->GetOwnerHero()) && !Summon->GetOwnerHero()->bBot;
    return !Attacker->bBot;
}
}

// ---------------------------------------------------------------- config
const FCireWaveConfig& CireWaveDirector::Config(const UWorld* World)
{
    if (const FRuntime* R = Find(World)) return R->Config;
    return GlobalConfig();
}

void CireWaveDirector::Initialize(ACireGameMode* Mode)
{
    if (!Mode) return;
    Runtimes.Remove(Mode->GetWorld());
    bFileLoaded = false; // pick up Waves.json edits made between sessions
    FRuntime& R = Get(Mode);
    CireRaces::BeginMatch(Mode); // monster-races: this match's seeded skill draw
    if (auto* S = Mode->GetGameState<ACireGameState>()) S->WavesPerCycle = R.Config.WavesPerCycle;
    ApplyPacing(Mode, R.Config);
    Publish(Mode);
    UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_READY waves=%d per_cycle=%d cycles=%d breather=%.1f failsafe=%d max=%.0f stuck=%.1f"), R.Config.Waves.Num(),
        R.Config.WavesPerCycle, R.Config.Cycles, R.Config.BreatherSeconds, R.Config.bStallFailsafe ? 1 : 0, R.Config.MaxWaveSeconds, R.Config.StuckSeconds);
}

bool CireWaveDirector::ApplyLive(ACireGameMode* Mode, const FCireWaveConfig& In, FString* Error)
{
    if (!Mode || !Mode->HasAuthority()) { if (Error) *Error = TEXT("Only the authoritative server can change waves."); return false; }
    FCireWaveConfig C = In;
    if (!Validate(C, Error, true)) return false;
    FRuntime& R = Get(Mode);
    R.Config = MoveTemp(C);
    if (auto* S = Mode->GetGameState<ACireGameState>()) S->WavesPerCycle = R.Config.WavesPerCycle;
    if (!Mode->bSmoke)
    {
        ApplyPacing(Mode, R.Config);
        Mode->WaveTimer = FMath::Min(Mode->WaveTimer, R.Config.BreatherSeconds);
    }
    Publish(Mode);
    UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_APPLIED waves=%d per_cycle=%d breather=%.1f (takes effect from the next wave)"), R.Config.Waves.Num(), R.Config.WavesPerCycle, R.Config.BreatherSeconds);
    return true;
}

FCireWaveDef CireWaveDirector::ResolveWave(const FCireWaveConfig& C, int32 WaveInCycle, int32 Cycle)
{
    if (C.Waves.IsEmpty()) return Template(ECireWaveType::Normal);
    Cycle = FMath::Clamp(Cycle, 0, 100);
    FCireWaveDef W = C.Waves[WaveIndex(C, WaveInCycle, Cycle) % C.Waves.Num()];
    const float Health = 1.f + C.CycleHealthGrowth * Cycle, Damage = 1.f + C.CycleDamageGrowth * Cycle;
    int32 Total = 0;
    for (auto& U : W.Units)
    {
        U.HealthScale = FMath::Clamp(U.HealthScale * Health, .1f, 200.f);
        U.DamageScale = FMath::Clamp(U.DamageScale * Damage, .05f, 100.f);
        if (!U.bBoss && !U.bEscortee) U.Count = FMath::Clamp(U.Count + C.CycleExtraUnits * Cycle, 1, 20);
        Total += U.Count;
    }
    // monster-races: slot rows take the unit of the wave's race (rotation per cycle, mixed races alternate by row),
    // the rotation's lap picks the palette (reskin set), and late-cycle lane bosses become mythic.
    const int32 Laps = C.Campaign.RaceRotation.IsEmpty() ? 0 : RotationIndex(C, WaveInCycle, Cycle) / C.Campaign.RaceRotation.Num();
    for (int32 Row = 0; Row < W.Units.Num(); ++Row)
    {
        auto& U = W.Units[Row];
        const FName Race = RaceFor(C, W, Cycle, Row, WaveInCycle);
        if (!U.Slot.IsNone()) if (const FName Id = CireRaces::UnitFor(Race, U.Slot, Cycle); !Id.IsNone()) U.Archetype = Id;
        if (U.Palette < 0) U.Palette = C.Campaign.bReskinOnWrap ? Laps : 0;
        if (U.bBoss && C.Campaign.MythicBossFromCycle > 0 && Cycle + 1 >= C.Campaign.MythicBossFromCycle) U.Rank = ECireNPCRank::Mythic;
    }
    // Never exceed the per-lane spawn budget, even after many looping cycles.
    while (Total > 30)
    {
        bool bTrimmed = false;
        for (auto& U : W.Units) if (Total > 30 && U.Count > 1 && !U.bBoss && !U.bEscortee) { --U.Count; --Total; bTrimmed = true; }
        if (!bTrimmed) break;
    }
    return W;
}

// ---------------------------------------------------------------- spawning
namespace
{
void QueueWave(FRuntime& R, const FCireWaveDef& W, int32 Serial, bool bFast, int32 Cycle = 0, bool bBonus = false, bool bVariants = false)
{
    TMap<FName, int32> VariantCounters; // monster-expansion: per race-slot counter for Bestiary.json race variants
    // Escortees lead, bosses close the column; other rows interleave for a mixed wave.
    TArray<FCireWaveUnit> Lead, Mixed, Bosses;
    for (const auto& U : W.Units) (U.bEscortee ? Lead : U.bBoss ? Bosses : Mixed).Add(U);
    int32 Slot = 0, Promotable = 0;
    // monster-races: from the campaign's promotion cycles every Nth normal attacker spawns veteran/elite/champion.
    const FCireCampaign& K = R.Config.Campaign;
    const int32 CycleNumber = Cycle + 1;
    const ECireNPCRank Promotion = K.ChampionFromCycle > 0 && CycleNumber >= K.ChampionFromCycle ? ECireNPCRank::Champion :
        K.EliteFromCycle > 0 && CycleNumber >= K.EliteFromCycle ? ECireNPCRank::Elite :
        K.VeteranFromCycle > 0 && CycleNumber >= K.VeteranFromCycle ? ECireNPCRank::Veteran : ECireNPCRank::Normal;
    // rules-conformance: champions arrive alongside elites (every other promotion) once both tiers are unlocked, so the
    // Champion rank shows in a default 3-cycle match without every promoted unit jumping two tiers.
    const bool bChampionsAlternate = Promotion == ECireNPCRank::Champion && K.EliteFromCycle > 0 && CycleNumber >= K.EliteFromCycle;
    int32 Promoted = 0;
    auto Emit = [&](const FCireWaveUnit& U)
    {
        FOrder O; O.Unit = U; O.Slot = Slot++; O.Serial = Serial; O.bMustClear = W.bMustClear; O.Reward = W.RewardMultiplier; O.Rank = U.EffectiveRank();
        // monster-expansion: bonus-wave creatures (an authored bonus_loot wave, or the occasional bonus wave) never block
        // the cycle and are never promoted; rares keep their own look instead of a promotion.
        O.bBonus = bBonus || W.Type == ECireWaveType::BonusLoot;
        if (O.bBonus) O.bMustClear = false;
        if (bVariants && !O.bBonus && !U.bRare && !U.bBoss && !U.bNonAttacking) O.Unit.Archetype = CireMonsterExpansion::VariantFor(U.Archetype, VariantCounters);
        if (O.Rank == ECireNPCRank::Normal && !U.bBoss && !U.bNonAttacking && !O.bBonus && !U.bRare && Promotion != ECireNPCRank::Normal && ++Promotable % FMath::Max(1, K.PromoteEvery) == 0)
            O.Rank = bChampionsAlternate && (Promoted++ % 2 == 1) ? ECireNPCRank::Elite : Promotion;
        R.Queue.Add(O);
    };
    for (const auto& U : Lead) for (int32 I = 0; I < U.Count; ++I) Emit(U);
    for (bool bAny = true; bAny;)
    {
        bAny = false;
        for (auto& U : Mixed) if (U.Count > 0) { Emit(U); --U.Count; bAny = true; }
    }
    for (const auto& U : Bosses) for (int32 I = 0; I < U.Count; ++I) Emit(U);
    R.SpawnInterval = bFast ? FMath::Min(W.SpawnInterval, .05f) : W.SpawnInterval;
    R.SpawnTimer = 0;
}

ACireMonster* SpawnUnit(ACireGameMode* Mode, FRuntime& R, const FOrder& O, int32 Team)
{
    auto* S = Mode->GetGameState<ACireGameState>();
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // layout-wiring: every Monster Spawn of the realm owns its paths; units are split across all of them deterministically
    // (CireLanePath::PathForSlot: spawns share a wave evenly, each spawn splits evenly or by path weight). Bosses appear at
    // the Boss markers in turn (else the route's "boss" spot, else the breach) and march the path that starts nearest.
    UWorld* World = Mode->GetWorld();
    const FCireBattlefieldRoutes& Routes = CireLanePath::Get(World);
    int32 Path = 0;
    FVector Breach;
    if (O.Unit.bBoss)
    {
        Breach = CireLanePath::BossSpawnAt(World, Team, R.BossSlot[Team]++);
        Path = CireLanePath::NearestPathStart(Routes, Team, CireLanePath::ToLocal(Team, Breach));
    }
    else
    {
        // Guards of an escort wave march the path of the escortee they defend.
        const ACireMonster* Escortee = nullptr;
        if (!O.Unit.bNonAttacking)
            for (const auto& Pair : R.Tracks)
                if (Pair.Value.Serial == O.Serial && Pair.Value.bEscortee && AliveUnit(Pair.Key.Get()) && Pair.Key->Lane == Team) { Escortee = Pair.Key.Get(); break; }
        Path = Escortee ? Escortee->LanePath : CireLanePath::PathForSlot(Routes, Team, R.PathSlot[Team]++);
        Breach = CireLanePath::PathStart(World, Team, Path);
    }
    // pacing: waves appear SpawnAlongRoute of the way down the road (0 = the breach gate).
    const FVector Start = R.Config.SpawnAlongRoute > .001f ? CireLanePath::PointAlongPath(World, Team, Path, R.Config.SpawnAlongRoute, Breach.Z) : Breach;
    // The column forms up behind the spawn, turned to the path's first leg (a spawn facing its street).
    const TArray<FVector2D>& Points = CireLanePath::PathPoints(Routes, Team, Path);
    const FVector2D Ahead = Points.Num() > 1 ? (Points[1] - Points[0]).GetSafeNormal() : FVector2D(-1, 0);
    const FVector2D Right(-Ahead.Y, Ahead.X);
    const int32 Row = (O.Slot / 3) % 2, File = (O.Slot % 3) - 1;
    const FVector2D Offset = -Ahead * (Row * 90.f) + Right * (File * 170.f);
    FVector Default = CireLanePath::ClampToLane(World, Team, Start + FVector(Offset, 0), 80);
    // layout-wiring: a spawn Eric places in a narrow alley must not form its column inside a wall. The formation slot is
    // put on the navmesh next to it, else the unit stands on the spawn itself.
    if (CireNav::HasNavigation(World))
    {
        FVector OnNav;
        if (CireNav::Project(World, Default, OnNav, FVector(120, 120, 400), 45.f)) Default = FVector(OnNav.X, OnNav.Y, FMath::Max(Default.Z, OnNav.Z + 100.f));
        else if (CireNav::Project(World, Start, OnNav, FVector(200, 200, 400), 45.f)) Default = FVector(OnNav.X, OnNav.Y, FMath::Max(Start.Z, OnNav.Z + 100.f));
    }
    const FVector Position = CireDeveloperTools::SpawnPosition(Mode->GetWorld(), Team, O.Slot, Default);
    auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), Position, FRotator(0, FMath::RadiansToDegrees(FMath::Atan2(Ahead.Y, Ahead.X)), 0), Params);
    if (!M) { UE_LOG(LogCireWaves, Error, TEXT("CIRE_WAVES_SPAWN_FAILED archetype=%s"), *O.Unit.Archetype.ToString()); return nullptr; }
    M->Lane = Team;
    M->LanePath = Path; M->bPathLeash = true; // layout-wiring: its own path, and the leash that snaps it back to it
    R.PathSpawned[Team].FindOrAdd(Path)++;
    const FCireWaveUnit& U = O.Unit;
    const int32 GlobalWave = S ? S->Wave : 1;
    CireNPCCombat::ConfigureArchetype(M, U.Archetype, GlobalWave, 0, Mode->Clock.Round(), U.bBoss);
    M->MaxHealth = M->Health = FMath::Clamp(M->MaxHealth * U.HealthScale, 1.f, 1.e8f);
    M->Damage = FMath::Clamp(M->Damage * U.DamageScale, 1.f, 100000.f);
    if (U.LeakCost > 0) M->LeakCostOverride = U.LeakCost;
    // monster-races: rank (the old elite flag is rank elite: x1.6 health, x1.25 damage, Elite classification), palette
    // reskin and this monster's drawn, wave-gated skills.
    {
        ECireNPCRank Rank = O.Rank;
        if (M->NPCState && M->NPCState->Classification == ECireNPCClass::Boss && Rank < ECireNPCRank::Warlord) Rank = ECireNPCRank::Warlord;
        if (U.bRare && !O.bBonus && Rank < ECireNPCRank::Elite) Rank = ECireNPCRank::Elite; // monster-expansion: rares are at least elite (+1 skill)
        CireRaces::ApplyRank(M, Rank, FMath::Max(0, U.Palette));
        CireRaces::ApplyLoadout(M, R.Config.Skills, GlobalWave, U.SkillCount, U.SkillTier);
    }
    // monster-expansion: rare look/stats/plate, or a fleeing bonus creature with its escape clock.
    float SpecialSize = 1.f;
    if (O.bBonus) SpecialSize = CireMonsterExpansion::ApplyBonus(M, R.Config.Bonus);
    else if (U.bRare) SpecialSize = CireMonsterExpansion::ApplyRare(M, R.Config.Rare, GlobalWave);
    FTrack T;
    T.Serial = O.Serial; T.SpawnedAt = Now(M); T.Size = U.SizeScale * SpecialSize; T.Reward = O.Reward; T.bMustClear = O.bMustClear;
    T.bEscortee = U.bEscortee; T.Anchor = Position; T.SampleAt = T.SpawnedAt + 1.f;
    if (const FWaveRecord* Rec = R.Records.Find(O.Serial))
    {
        T.Info.bValid = true; T.Info.WaveNumber = Rec->WaveNumber; T.Info.WaveInCycle = Rec->WaveInCycle; T.Info.Cycle = Rec->Cycle; T.Info.Type = Rec->WaveType;
    }
    else { T.Info.bValid = true; T.Info.WaveNumber = GlobalWave; T.Info.Cycle = Mode->Clock.Round(); }
    T.Info.bArmored = U.bNonAttacking; T.Info.bEscortee = U.bEscortee; T.Info.bBoss = U.bBoss;
    T.Info.bElite = !U.bBoss && O.Rank >= ECireNPCRank::Elite;
    T.Info.bRare = U.bRare && !O.bBonus; T.Info.bBonus = O.bBonus; // monster-expansion
    if (U.bNonAttacking && !O.bBonus)
    {
        // Non-attacking marchers reuse the armored-escort behaviour: they ignore combat,
        // walk through heroes and must be killed before they reach the castle.
        CireNPCCombat::Interrupt(M); CireThreat::Clear(M);
        M->bArmoredEscort = true; M->bEngaged = false; M->CombatArchetype = 0;
        if (M->LeakCostOverride <= 0) M->LeakCostOverride = 1;
        M->MonsterName = U.bEscortee ? FString::Printf(TEXT("Escorted %s"), *M->MonsterName) : FString::Printf(TEXT("Armored %s"), *M->MonsterName);
        if (U.bEscortee) M->BaseMoveSpeed = FMath::Min(M->BaseMoveSpeed, 165.f);
        M->GetCharacterMovement()->MaxWalkSpeed = M->BaseMoveSpeed;
        M->EscortCollisionRefreshAt = 0;
    }
    else
    {
        // Attackers of an escort wave defend the escortee of their own lane.
        for (const auto& Pair : R.Tracks)
            if (Pair.Value.Serial == O.Serial && Pair.Value.bEscortee && AliveUnit(Pair.Key.Get()) && Pair.Key->Lane == Team)
            { T.Charge = Pair.Key; T.bGuard = true; break; }
    }
    M->SpawnPosition = Position;
    CireLanePath::InitializeProgress(M);
    M->ForceNetUpdate();
    Mode->Monsters.Add(M);
    R.Tracks.Add(M, T);
    if (!O.bBonus) R.LeakCostSpawned[Team] += LeakCost(M); // monster-expansion: bonus creatures escape, they never leak
    if (M->bBoss) ++R.BossesSpawned;
    if (Team == 0 && (O.bBonus || U.bRare))
        UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_SPECIAL kind=%s archetype=%s name=\"%s\" health=%.0f"), O.bBonus ? TEXT("bonus") : TEXT("rare"), *U.Archetype.ToString(), *M->GetNPCDisplayName(), M->MaxHealth);
    return M;
}
}

bool CireWaveDirector::StartWave(ACireGameMode* Mode, bool bLive)
{
    auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S || Mode->Clock.Phase() != Cires::MatchPhase::Survival || Mode->CycleWavesSpawned >= S->WavesPerCycle) return false;
    FRuntime& R = Get(Mode);
    const int32 WaveInCycle = Mode->CycleWavesSpawned;
    FCireWaveDef W = ResolveWave(R.Config, WaveInCycle, Mode->Clock.Round() - 1);
    ++S->Wave; ++Mode->CycleWavesSpawned;
    // monster-expansion: a rare creature may join this wave (both lanes; deterministic per match seed and wave).
    const bool bRareJoined = bLive && !Mode->bSmoke && RollRare(R.Config, W, S->Wave, CireRaces::MatchSeed(Mode->GetWorld()), R.RaresThisCycle);
    if (bRareJoined) { ++R.RaresThisCycle; ++R.RaresTotal; }
    S->NextWaveSeconds = 0;
    const int32 Serial = ++R.Serial;
    R.CurrentSerial = Serial;
    FWaveRecord& Rec = R.Records.Add(Serial);
    Rec.Label = W.Label; Rec.Type = TypeName(W.Type); Rec.StartedAt = Now(Mode); Rec.LastSpawnAt = Rec.StartedAt; Rec.bMustClear = W.bMustClear;
    Rec.WaveNumber = S->Wave; Rec.WaveInCycle = Mode->CycleWavesSpawned; Rec.Cycle = Mode->Clock.Round(); Rec.WaveType = W.Type;
    R.LastWaveNumber = S->Wave; R.Ready.Reset();
    // monster-races: the wave's race rides with its label and on the replicated game state.
    const int32 Cycle = Mode->Clock.Round() - 1;
    const FString Race = RaceLabel(R.Config, W, Cycle, WaveInCycle);
    if (!Race.IsEmpty()) Rec.Label = FString::Printf(TEXT("%s (%s)"), *W.Label, *Race);
    S->WaveRace = RaceFor(R.Config, W, Cycle, 0, WaveInCycle);
    QueueWave(R, W, Serial, Mode->bSmoke, Cycle, false, bLive && !Mode->bSmoke);
    const TCHAR* Lead = W.Type == ECireWaveType::Armored ? TEXT("ARMORED | They will not fight back. Stop them before the gate!") :
        W.Type == ECireWaveType::ArmoredEscort ? TEXT("ARMORED ESCORT | Break the escorted tank; its guards will defend it.") :
        W.Type == ECireWaveType::Boss ? TEXT("SIEGE | A lane boss marches with this wave. A leak costs 10 lives.") : TEXT("DEFEND THE GATES");
    S->Announcement = FString::Printf(TEXT("%s | Wave %d of %d: %s"), Lead, Mode->CycleWavesSpawned, S->WavesPerCycle, *Rec.Label);
    if (bRareJoined) S->Announcement += TEXT(" | A RARE creature marches with it!"); // monster-expansion
    Publish(Mode);
    UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_START round=%d wave=%d cycle=%d/%d type=%s label=\"%s\" units=%d race=%s"), S->Round, S->Wave, Mode->CycleWavesSpawned,
        S->WavesPerCycle, TypeName(W.Type), *W.Label, W.UnitsPerLane(), *S->WaveRace.ToString());
    return true;
}

bool CireWaveDirector::SkipTo(ACireGameMode* Mode, int32 WaveInCycle, bool bSpawnNow, FString* Error)
{
    auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S || Mode->Clock.Phase() != Cires::MatchPhase::Survival) { if (Error) *Error = TEXT("Skipping waves works during the survival phase."); return false; }
    if (WaveInCycle < 1 || WaveInCycle > S->WavesPerCycle) { if (Error) *Error = FString::Printf(TEXT("Choose a wave from 1 to %d."), S->WavesPerCycle); return false; }
    Mode->CycleWavesSpawned = WaveInCycle - 1;
    S->CycleWavesDone = Mode->CycleWavesSpawned;
    Mode->WaveTimer = bSpawnNow ? 0.f : FMath::Min(Mode->WaveTimer, 3.f);
    Publish(Mode);
    if (bSpawnNow && !StartWave(Mode)) { if (Error) *Error = TEXT("The wave could not start."); return false; }
    return true;
}

bool CireWaveDirector::SpawnNow(ACireGameMode* Mode, const FCireWaveDef& In, FString* Error)
{
    if (!Mode || Mode->Clock.Phase() != Cires::MatchPhase::Survival) { if (Error) *Error = TEXT("Test spawns work during the survival phase."); return false; }
    FCireWaveConfig Probe; Probe.Waves = {In};
    if (!Validate(Probe, Error, true)) return false;
    FRuntime& R = Get(Mode);
    const int32 Serial = ++R.Serial;
    FWaveRecord& Rec = R.Records.Add(Serial);
    Rec.Label = Probe.Waves[0].Label + TEXT(" (test)"); Rec.Type = TypeName(Probe.Waves[0].Type); Rec.StartedAt = Rec.LastSpawnAt = Now(Mode);
    if (const auto* S = Mode->GetGameState<ACireGameState>()) { Rec.WaveNumber = FMath::Max(1, S->Wave); Rec.WaveInCycle = FMath::Max(1, Mode->CycleWavesSpawned); }
    Rec.Cycle = Mode->Clock.Round(); Rec.WaveType = Probe.Waves[0].Type;
    // monster-races: a test spawn resolves its race slots like a real wave of the current cycle.
    Probe.Skills = R.Config.Skills; Probe.Campaign = R.Config.Campaign;
    const FCireWaveDef Resolved = ResolveWave(Probe, 0, FMath::Max(0, Mode->Clock.Round() - 1));
    FCireWaveDef Spawned = Probe.Waves[0];
    for (int32 I = 0; I < Spawned.Units.Num(); ++I) { Spawned.Units[I].Archetype = Resolved.Units[I].Archetype; Spawned.Units[I].Palette = Resolved.Units[I].Palette; }
    QueueWave(R, Spawned, Serial, Mode->bSmoke, Mode->Clock.Round() - 1);
    UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_TEST_SPAWN label=\"%s\" units=%d"), *Probe.Waves[0].Label, Probe.Waves[0].UnitsPerLane());
    return true;
}

// ---------------------------------------------------------------- per-tick
void CireWaveDirector::TickSurvival(ACireGameMode* Mode, float Delta)
{
    if (!Mode || !Mode->HasAuthority()) return;
    FRuntime& R = Get(Mode);
    const float Time = Now(Mode);
    UWorld* World = Mode->GetWorld();
    // Challenge packs start neutral the first time the director sees them.
    for (auto* M : Mode->Monsters)
        if (IsValid(M) && M->PackId >= 0 && !R.SeenPacks.Contains(M)) { R.SeenPacks.Add(M); MakeNeutral(M); }
    // Spawn queue: one slot per interval, both lanes together.
    if (!R.Queue.IsEmpty())
    {
        R.SpawnTimer -= Delta;
        int32 Budget = 8;
        while (!R.Queue.IsEmpty() && R.SpawnTimer <= 0 && Budget-- > 0)
        {
            const FOrder O = R.Queue[0];
            R.Queue.RemoveAt(0);
            for (int32 Team = 0; Team < 2; ++Team) SpawnUnit(Mode, R, O, Team);
            if (FWaveRecord* Rec = R.Records.Find(O.Serial)) Rec->LastSpawnAt = Time;
            R.SpawnTimer += R.SpawnInterval;
        }
        if (R.Queue.IsEmpty()) R.SpawnTimer = 0;
    }
    const auto& C = R.Config;
    TArray<ACireMonster*> Despawn;
    for (auto It = R.Tracks.CreateIterator(); It; ++It)
    {
        ACireMonster* M = It.Key().Get();
        if (!AliveUnit(M)) { It.RemoveCurrent(); continue; }
        FTrack& T = It.Value();
        const FWaveRecord* Rec = R.Records.Find(T.Serial);
        const float Age = Time - (Rec ? Rec->LastSpawnAt : T.SpawnedAt);
        // Stall failsafe: leftovers WITH NO THREAT march to the castle; after a grace period they despawn.
        // rules-conformance (Eric: threat is lost only on death or an explicit ability): a unit that holds
        // threat keeps fighting past the stall limit, and a marching unit that is attacked stops and fights.
        // layout-wiring: threat a leashed unit cannot pursue (its holders stand outside the leash zone) does not hold it back.
        const bool bHasThreat = IsValid(M->Victim) || (!M->Threat.IsEmpty() && !CireLeash::Applies(M));
        if (C.bStallFailsafe && !NoRescue() && Age > C.MaxWaveSeconds)
        {
            if (bHasThreat)
            {
                if (T.bForcedMarch)
                {
                    T.bForcedMarch = false; Unghost(M);
                    UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_RESCUE_MARCH_ENDED %s lane=%d (attacked: it fights its threat holder)"), *M->GetNPCDisplayName(), M->Lane);
                }
            }
            else if (C.FailsafeAction == ECireWaveFailsafe::Despawn || (T.bForcedMarch && Time - T.ForcedAt > C.FailsafeGraceSeconds))
            { Despawn.Add(M); continue; }
            else if (!T.bForcedMarch)
            {
                T.bForcedMarch = true; T.ForcedAt = Time; ++R.Marches;
                CireNPCCombat::Interrupt(M); M->bEngaged = false;
                NoteFailsafe(FString::Printf(TEXT("march %s lane=%d age=%.0f"), *M->GetNPCDisplayName(), M->Lane, Age));
                UE_LOG(LogCireWaves, Warning, TEXT("CIRE_WAVES_RESCUE_MARCH %s lane=%d age=%.0f"), *M->GetNPCDisplayName(), M->Lane, Age);
            }
        }
        if (T.bForcedMarch) Ghost(M, T);
        // rules-conformance: no lane leash. A wave unit chases its threat holder at any distance (the old 18 m
        // "lane leash" dropped the target); the navmesh path ends at the nearest reachable point when needed.
        // Outside its realm (knocked back, launched): return to the route.
        if (!NoRescue() && (!CireLanePath::Contains(World, M->Lane, M->GetActorLocation(), 0) || M->GetActorLocation().Z < -500))
        { NudgeAlong(M, 0); T.Anchor = M->GetActorLocation(); T.StuckFor = 0; continue; }
        // layout-wiring: the Play Bounds polygon flags a unit that left the playable town; it is set back onto its path.
        if (!NoRescue() && !CireLanePath::InsidePlayBounds(World, M->Lane, M->GetActorLocation()) && CireLanePath::DistanceToUnitPath(M, M->GetActorLocation()) > 300.f)
        {
            UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_OUT_OF_BOUNDS %s lane=%d at=(%.0f,%.0f): back onto its path"), *M->GetNPCDisplayName(), M->Lane, M->GetActorLocation().X, M->GetActorLocation().Y);
            if (CireLeash::IsReturning(M)) CireLeash::Rescue(M); else NudgeAlong(M, 0);
            T.Anchor = M->GetActorLocation(); T.StuckFor = 0; continue;
        }
        // Stuck detection, sampled once per second.
        if (Time < T.SampleAt) continue;
        T.SampleAt = Time + 1.f;
        const bool bPaused = CireProgression::IsPaused(M) || !M->CastingAbility.IsEmpty();
        const bool bFighting = IsValid(M->Victim) && !M->Victim->bDead &&
            FVector::DistSquared2D(M->GetActorLocation(), M->Victim->GetActorLocation()) <= FMath::Square(Reach(M) + 120.f);
        const bool bGuardWaiting = T.bGuard && T.Charge.IsValid() && AliveUnit(T.Charge.Get()) &&
            FVector::DistSquared2D(M->GetActorLocation(), T.Charge->GetActorLocation()) < FMath::Square(320.f);
        // layout-wiring: a marcher that keeps moving without getting anywhere (circling a market stall, looping on a navmesh
        // detour, wandering off its path) is stuck too: after three stuck periods without progress along its own path it
        // gets the same nudge. Fighting, returning (leash), paused and escort-guard units are left alone.
        if (!bPaused && !IsValid(M->Victim) && !bGuardWaiting && !CireLeash::IsReturning(M) && M->SpecialSpawn != 2)
        {
            const float Along = CireLanePath::PathProgress(World, M->Lane, M->LanePath, M->GetActorLocation()) * CireLanePath::PathLengthOf(World, M->Lane, M->LanePath);
            if (T.BestProgress < 0.f || Along > T.BestProgress + 100.f) { T.BestProgress = FMath::Max(T.BestProgress, Along); T.BestProgressAt = Time; }
            else if (Time - T.BestProgressAt > C.StuckSeconds * 3.f && !NoRescue())
            {
                const FVector From = M->GetActorLocation();
                NudgeAlong(M, 450.f);
                ++T.Nudges; ++R.Nudges; T.StuckFor = 0; T.Anchor = M->GetActorLocation(); T.BestProgressAt = Time;
                UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_STALL_NUDGE %s lane=%d path=%d from=(%.0f,%.0f) to=(%.0f,%.0f) off_path=%.0f (moving without progress)"), *M->GetNPCDisplayName(), M->Lane,
                    M->LanePath, From.X, From.Y, M->GetActorLocation().X, M->GetActorLocation().Y, CireLanePath::DistanceToUnitPath(M, From));
                continue;
            }
        }
        if (bPaused || bFighting || bGuardWaiting || FVector::DistSquared2D(M->GetActorLocation(), T.Anchor) > FMath::Square(60.f))
        { T.Anchor = M->GetActorLocation(); T.StuckFor = 0; continue; }
        T.StuckFor += 1.f;
        if (T.StuckFor < C.StuckSeconds) continue;
        if (NoRescue())
        {
            if (FMath::IsNearlyEqual(T.StuckFor, C.StuckSeconds, .5f) || FMath::Fmod(T.StuckFor, 60.f) < 1.f)
                UE_LOG(LogCireWaves, Warning, TEXT("CIRE_WAVES_STUCK_DETECTED %s lane=%d at=(%.0f,%.0f) for=%.0fs victim=%s victim_dist=%.0f"), *M->GetNPCDisplayName(), M->Lane,
                    M->GetActorLocation().X, M->GetActorLocation().Y, T.StuckFor, IsValid(M->Victim) ? *M->Victim->HeroName : TEXT("-"),
                    IsValid(M->Victim) ? FVector::Dist2D(M->GetActorLocation(), M->Victim->GetActorLocation()) : -1.f);
            continue;
        }
        const FVector From = M->GetActorLocation();
        // layout-wiring: a unit stuck on its way back to its path is set down at its return point (the leash rescue).
        if (CireLeash::IsReturning(M))
        {
            CireLeash::Rescue(M); ++T.Nudges; ++R.Nudges; T.StuckFor = 0; T.Anchor = M->GetActorLocation();
            continue;
        }
        const bool bChasing = bHasThreat && (IsValid(M->Victim) || !CireLeash::Applies(M)); // layout-wiring: a leashed unit may hold threat it does not pursue
        if (bChasing)
        {
            // rules-conformance: a stuck chaser keeps its target; it repaths (to the nearest reachable point)
            // instead of dropping threat and being teleported down the road.
            CireNav::Forget(M);
            ++T.Nudges; T.StuckFor = 0; T.Anchor = M->GetActorLocation();
            UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_STUCK_REPATH %s lane=%d at=(%.0f,%.0f) victim=%s (threat kept)"), *M->GetNPCDisplayName(), M->Lane,
                From.X, From.Y, IsValid(M->Victim) ? *M->Victim->HeroName : TEXT("-"));
            continue;
        }
        NudgeAlong(M, 450.f);
        ++T.Nudges; ++R.Nudges; T.StuckFor = 0; T.Anchor = M->GetActorLocation();
        UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_STUCK_NUDGE %s lane=%d from=(%.0f,%.0f) to=(%.0f,%.0f) chasing=0 nudges=%d"), *M->GetNPCDisplayName(), M->Lane,
            From.X, From.Y, M->GetActorLocation().X, M->GetActorLocation().Y, T.Nudges);
    }
    for (ACireMonster* M : Despawn)
    {
        UE_LOG(LogCireWaves, Warning, TEXT("CIRE_WAVES_RESCUE_DESPAWN %s lane=%d"), *M->GetNPCDisplayName(), M->Lane);
        ++R.Despawns;
        NoteFailsafe(FString::Printf(TEXT("despawn %s lane=%d"), *M->GetNPCDisplayName(), M->Lane));
        Forget(M); Mode->Monsters.Remove(M); CireThreat::Clear(M); CireNPCCombat::Interrupt(M); M->Destroy();
    }
    // Wave units the director did not spawn (legacy/probe/dev paths) still get a failsafe clock.
    for (auto* M : Mode->Monsters)
        if (WaveUnit(M) && !R.Tracks.Contains(M))
        { FTrack T; T.SpawnedAt = Time; T.Anchor = M->GetActorLocation(); T.SampleAt = Time + 1.f; T.Serial = -1; R.Tracks.Add(M, T); }
}

void CireWaveDirector::OnPhaseChanged(ACireGameMode* Mode, int32 NewPhase)
{
    if (!Mode) return;
    FRuntime& R = Get(Mode);
    R.Queue.Reset(); R.SpawnTimer = 0; R.Ready.Reset();
    if (NewPhase == 0) { R.Records.Reset(); R.CurrentSerial = 0; R.RaresThisCycle = 0; R.BonusThisCycle = 0; } // monster-expansion: per-cycle caps
    Publish(Mode);
}

bool CireWaveDirector::HasPendingSpawns(const ACireGameMode* Mode)
{
    const FRuntime* R = Mode ? Find(Mode->GetWorld()) : nullptr;
    return R && !R->Queue.IsEmpty();
}
bool CireWaveDirector::IsWaveActive(const ACireGameMode* Mode)
{
    if (!Mode) return false;
    if (HasPendingSpawns(Mode)) return true;
    for (auto* M : Mode->Monsters) if (WaveUnit(M)) return true;
    return false;
}
bool CireWaveDirector::BlocksNextWave(const ACireGameMode* Mode)
{
    if (!Mode) return false;
    if (HasPendingSpawns(Mode)) return true;
    for (auto* M : Mode->Monsters)
        if (WaveUnit(M)) { const FTrack* T = TrackOf(M); if (!T || T->bMustClear) return true; }
    return false;
}
float CireWaveDirector::CurrentWaveAge(const ACireGameMode* Mode)
{
    const FRuntime* R = Mode ? Find(Mode->GetWorld()) : nullptr;
    const FWaveRecord* Rec = R ? R->Records.Find(R->CurrentSerial) : nullptr;
    return Rec ? Now(Mode) - Rec->StartedAt : 0.f;
}
FString CireWaveDirector::CurrentWaveType(const ACireGameMode* Mode)
{
    const FRuntime* R = Mode ? Find(Mode->GetWorld()) : nullptr;
    const FWaveRecord* Rec = R ? R->Records.Find(R->CurrentSerial) : nullptr;
    return Rec ? Rec->Type : FString(TEXT("none"));
}
float CireWaveDirector::RewardMultiplier(const ACireMonster* M) { const FTrack* T = TrackOf(M); return T ? T->Reward : 1.f; }
float CireWaveDirector::StuckSeconds(const ACireMonster* M) { const FTrack* T = TrackOf(M); return T ? T->StuckFor : 0.f; }
float CireWaveDirector::SizeScale(const ACireMonster* M) { const FTrack* T = TrackOf(M); return T && T->Serial >= 0 ? T->Size : 0.f; }
bool CireWaveDirector::IsForcedMarch(const ACireMonster* M) { const FTrack* T = TrackOf(M); return T && T->bForcedMarch; }
bool CireWaveDirector::AggroSuppressed(const ACireMonster* M) { const FTrack* T = TrackOf(M); return T && (T->bForcedMarch || T->SuppressUntil > Now(M)); }
ACireMonster* CireWaveDirector::EscortCharge(const ACireMonster* M)
{
    const FTrack* T = TrackOf(M);
    ACireMonster* Charge = T && T->bGuard ? T->Charge.Get() : nullptr;
    return AliveUnit(Charge) ? Charge : nullptr;
}
// Suppresses proximity aggro (acquiring a NEW target) for a while; it never removes existing threat.
void CireWaveDirector::SuppressAggro(ACireMonster* M, float Seconds)
{
    FRuntime* R = M ? Find(M->GetWorld()) : nullptr;
    if (FTrack* T = R ? R->Tracks.Find(TWeakObjectPtr<ACireMonster>(M)) : nullptr) T->SuppressUntil = FMath::Max(T->SuppressUntil, Now(M) + Seconds);
}
TMap<int32, int32> CireWaveDirector::PathSpawnCounts(const ACireGameMode* Mode, int32 Team)
{
    const FRuntime* R = Mode ? Find(Mode->GetWorld()) : nullptr;
    return R ? R->PathSpawned[FMath::Clamp(Team, 0, 1)] : TMap<int32, int32>();
}
void CireWaveDirector::RescueCounts(const ACireGameMode* Mode, int32& Nudges, int32& Marches, int32& Despawns)
{
    const FRuntime* R = Mode ? Find(Mode->GetWorld()) : nullptr;
    Nudges = R ? R->Nudges : 0; Marches = R ? R->Marches : 0; Despawns = R ? R->Despawns : 0;
}
// monster-races ------------------------------------------------------------------------------------------
int32 CireWaveDirector::WaveIndex(const FCireWaveConfig& C, int32 WaveInCycle, int32 Cycle)
{
    WaveInCycle = FMath::Max(0, WaveInCycle);
    return C.bCampaignOrder ? FMath::Clamp(Cycle, 0, 100) * FMath::Max(1, C.WavesPerCycle) + WaveInCycle : WaveInCycle;
}
int32 CireWaveDirector::RotationIndex(const FCireWaveConfig& C, int32 WaveInCycle, int32 Cycle)
{
    Cycle = FMath::Clamp(Cycle, 0, 100);
    return C.Campaign.bRotatePerWave ? Cycle * FMath::Max(1, C.WavesPerCycle) + FMath::Clamp(WaveInCycle, 0, FMath::Max(1, C.WavesPerCycle) - 1) : Cycle;
}
FName CireWaveDirector::RaceFor(const FCireWaveConfig& C, const FCireWaveDef& W, int32 Cycle, int32 Row, int32 WaveInCycle)
{
    if (!W.Race.IsNone()) return W.Race;
    if (C.Campaign.RaceRotation.IsEmpty()) return TEXT("hollow");
    TArray<FString> Parts;
    C.Campaign.RaceRotation[RotationIndex(C, WaveInCycle, Cycle) % C.Campaign.RaceRotation.Num()].ParseIntoArray(Parts, TEXT("+"), true);
    return Parts.IsEmpty() ? FName(TEXT("hollow")) : FName(*Parts[FMath::Max(0, Row) % Parts.Num()].TrimStartAndEnd());
}
FString CireWaveDirector::RaceLabel(const FCireWaveConfig& C, const FCireWaveDef& W, int32 Cycle, int32 WaveInCycle)
{
    TArray<FString> Names;
    for (int32 Row = 0; Row < FMath::Max(1, W.Units.Num()); ++Row)
        if (const FCireRace* Race = CireRaces::FindRace(RaceFor(C, W, Cycle, Row, WaveInCycle))) Names.AddUnique(Race->Short);
    return FString::Join(Names, TEXT(" + "));
}
void CireWaveDirector::AdoptSummon(ACireGameMode* Mode, ACireMonster* Summon, ACireMonster* Parent)
{
    if (!Mode || !IsValid(Summon)) return;
    FRuntime& R = Get(Mode);
    FTrack T;
    if (const FTrack* P = TrackOf(Parent)) { T = *P; T.bEscortee = false; T.bGuard = false; T.Charge.Reset(); T.bForcedMarch = false; T.Nudges = 0; T.StuckFor = 0; }
    else T.Serial = -1;
    T.SpawnedAt = Now(Summon); T.Anchor = Summon->GetActorLocation(); T.SampleAt = T.SpawnedAt + 1.f; T.Size = 1.f;
    R.Tracks.Add(Summon, T);
}

void CireWaveDirector::Forget(const ACireMonster* M)
{
    if (FRuntime* R = M ? Find(M->GetWorld()) : nullptr) R->Tracks.Remove(TWeakObjectPtr<ACireMonster>(const_cast<ACireMonster*>(M)));
}
void CireWaveDirector::SmokeCounters(const ACireGameMode* Mode, int32& Leak0, int32& Leak1, int32& Bosses)
{
    const FRuntime* R = Mode ? Find(Mode->GetWorld()) : nullptr;
    Leak0 = R ? R->LeakCostSpawned[0] : 0; Leak1 = R ? R->LeakCostSpawned[1] : 0; Bosses = R ? R->BossesSpawned : 0;
}

// ---------------------------------------------------------------- neutral packs
void CireWaveDirector::MakeNeutral(ACireMonster* M)
{
    if (!IsValid(M) || !M->HasAuthority() || M->PackId < 0) return;
    CireThreat::Clear(M); M->bEngaged = false; M->bNeutral = true; M->ForceNetUpdate();
}
bool CireWaveDirector::IsNeutral(const ACireMonster* M) { return IsValid(M) && M->bNeutral; }
bool CireWaveDirector::AllowDamage(ACireMonster* M, ACireHero* Attacker)
{
    if (!IsValid(M) || !M->bNeutral) return true;
    // Bots (and their summons) never open on a neutral pack; only a player's attack does.
    if (!PlayerSide(Attacker)) return false;
    auto* Mode = M->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode) return true;
    for (auto* Other : Mode->Monsters)
        if (IsValid(Other) && Other->PackId == M->PackId && Other->Lane == M->Lane && Other->bNeutral)
        {
            Other->bNeutral = false; Other->ForceNetUpdate();
            CireThreat::Engage(Other, Attacker); CireThreat::Select(Other);
        }
    UE_LOG(LogCireWaves, Display, TEXT("CIRE_PACK_AGGRO pack=%d lane=%d by=%s"), M->PackId, M->Lane, *Attacker->HeroName);
    return true;
}
void CireWaveDirector::OnMonsterDamaged(ACireMonster* M, ACireHero* Attacker)
{
    if (!IsValid(M) || !IsValid(Attacker) || M->PackId >= 0) return;
    const FTrack* T = TrackOf(M);
    const FRuntime* R = Find(M->GetWorld());
    if (!T || !T->bEscortee || !R) return;
    // Guards of this escortee turn on whoever is breaking their charge.
    for (const auto& Pair : R->Tracks)
        if (Pair.Value.bGuard && Pair.Value.Charge.Get() == M && AliveUnit(Pair.Key.Get()))
            CireThreat::Damage(Pair.Key.Get(), Attacker, 25.f);
}
void CireWaveDirector::OnPackReset(ACireMonster* M)
{
    if (IsValid(M) && M->PackId >= 0) MakeNeutral(M);
}

// ---------------------------------------------------------------- bots
namespace
{
bool TeammateFightingPack(const ACireMonster* M, const ACireHero* Bot)
{
    const ACireHero* V = M->Victim;
    return IsValid(V) && V != Bot && !V->bDead && V->TeamId == Bot->TeamId && FVector::DistSquared2D(V->GetActorLocation(), Bot->GetActorLocation()) < FMath::Square(2600.f);
}
}
bool CireWaveDirector::ShouldBotRetreat(ACireHero* Bot)
{
    if (!IsValid(Bot) || !Bot->HasAuthority()) return false;
    auto* Mode = Bot->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode || Mode->Clock.Phase() != Cires::MatchPhase::Survival) return false;
    FRuntime& R = Get(Mode);
    FBotState& B = R.Bots.FindOrAdd(Bot);
    const float Fraction = Bot->Health / FMath::Max(1.f, Bot->MaxHealth), Time = Now(Bot);
    if (!B.bRetreating && Fraction < .28f)
    {
        bool bThreatened = false;
        for (auto* M : Mode->Monsters) if (AliveUnit(M) && M->Victim == Bot) { bThreatened = true; break; }
        if (bThreatened) { B.bRetreating = true; B.RetreatUntil = Time + 9.f; }
    }
    else if (B.bRetreating && (Fraction > .6f || Time > B.RetreatUntil)) B.bRetreating = false;
    return B.bRetreating;
}

FVector CireWaveDirector::BotSteer(ACireHero* Bot, const FVector& Goal)
{
    const FVector Straight = IsValid(Bot) ? (Goal - Bot->GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector;
    // nav-paths: bots path on the navmesh in every phase (lane, targets, healers, castle hold, arena).
    // The road detour below remains the fallback where no navmesh covers the bot.
    if (IsValid(Bot))
    {
        bool bUsedNav = false;
        const FVector Dir = CireNav::Steer(Bot, Goal, &bUsedNav);
        if (bUsedNav) return Dir;
    }
    auto* Mode = IsValid(Bot) ? Bot->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr;
    if (!Mode || Mode->Clock.Phase() != Cires::MatchPhase::Survival) return Straight;
    FRuntime& R = Get(Mode);
    FBotState& B = R.Bots.FindOrAdd(Bot);
    const float Time = Now(Bot);
    const FVector P = Bot->GetActorLocation();
    if (B.DetourUntil > Time)
    {
        const FVector Dir = (B.Detour - P).GetSafeNormal2D();
        if (FVector::DistSquared2D(B.Detour, P) > FMath::Square(60.f) && !Dir.IsNearlyZero()) return Dir;
        B.DetourUntil = 0;
    }
    // Progress check every 1.2 s while the bot is trying to move somewhere.
    if (FVector::DistSquared2D(P, B.Anchor) > FMath::Square(45.f) || Time - B.AnchorAt > 30.f) { B.Anchor = P; B.AnchorAt = Time; return Straight; }
    if (Time - B.AnchorAt < 1.2f) return Straight;
    // Stuck on geometry: detour along the road (routes are kept clear of props), heading toward
    // the goal's side of the route; if already on the road, sidestep.
    UWorld* World = Bot->GetWorld();
    const int32 Team = FMath::Clamp(Bot->TeamId, 0, 1);
    const float Length = FMath::Max(1.f, CireLanePath::RouteLength(World, Team));
    const float Mine = CireLanePath::RouteProgress(World, Team, P), Theirs = CireLanePath::RouteProgress(World, Team, Goal);
    const FVector OnRoad = CireLanePath::PointAlongRoute(World, Team, Mine, P.Z);
    if (FVector::DistSquared2D(OnRoad, P) > FMath::Square(140.f))
        B.Detour = CireLanePath::PointAlongRoute(World, Team, FMath::Clamp(Mine + FMath::Sign(Theirs - Mine) * 250.f / Length, 0.f, 1.f), P.Z);
    else
    {
        B.Side = -B.Side;
        B.Detour = P + FVector::CrossProduct(Straight, FVector::UpVector) * 260.f * B.Side + Straight * 80.f;
    }
    B.DetourUntil = Time + 1.6f; B.Anchor = P; B.AnchorAt = Time;
    return (B.Detour - P).GetSafeNormal2D();
}

AActor* CireWaveDirector::ChooseBotTarget(ACireHero* Bot)
{
    if (!IsValid(Bot)) return nullptr;
    auto* Mode = Bot->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode) return nullptr;
    UWorld* World = Bot->GetWorld();
    const int32 Team = FMath::Clamp(Bot->TeamId, 0, 1);
    const float RouteLength = FMath::Max(1.f, CireLanePath::RouteLength(World, Team));
    bool bWaveThreat = false;
    for (auto* M : Mode->Monsters) if (WaveUnit(M) && M->Lane == Bot->TeamId) { bWaveThreat = true; break; }
    AActor* Best = nullptr; double BestScore = TNumericLimits<double>::Max();
    for (auto* M : Mode->Monsters)
    {
        if (!AliveUnit(M) || M->Lane != Bot->TeamId || !Bot->IsHostile(M)) continue;
        double Score = FVector::Dist2D(Bot->GetActorLocation(), M->GetActorLocation());
        if (M->PackId >= 0)
        {
            // Packs: never neutral ones; hostile ones only in self-defence, or to help a
            // teammate the pack is fighting while no wave threatens the route.
            if (M->bNeutral) continue;
            const bool bSelf = M->Victim == Bot;
            if (!bSelf && (bWaveThreat || !TeammateFightingPack(M, Bot))) continue;
            Score += bSelf ? 0.0 : 400.0;
        }
        else
        {
            // Leak prevention: the closer to the castle, the more urgent.
            const float Progress = CireLanePath::RouteProgress(World, Team, M->GetActorLocation());
            Score += (1.f - Progress) * RouteLength * .45f;
            if (M->bBoss || M->bArmoredEscort) Score -= 700.0;
            if (IsValid(M->Victim) && M->Victim->TeamId == Bot->TeamId) Score -= 350.0;
        }
        if (Score < BestScore) { BestScore = Score; Best = M; }
    }
    return Best;
}

bool CireWaveDirector::BotDestination(ACireHero* Bot, FVector& Out)
{
    if (!IsValid(Bot)) return false;
    auto* Mode = Bot->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode || Mode->Clock.Phase() != Cires::MatchPhase::Survival) return false;
    UWorld* World = Bot->GetWorld();
    const int32 Team = FMath::Clamp(Bot->TeamId, 0, 1);
    int32 Index = 0, Seen = 0;
    for (auto* H : Mode->Heroes) if (IsValid(H) && H->TeamId == Bot->TeamId) { if (H == Bot) Index = Seen; ++Seen; }
    const FRuntime* R = Find(World);
    const FBotState* B = R ? R->Bots.Find(Bot) : nullptr;
    if (B && B->bRetreating)
    {
        // Regroup on a living healer when there is one; otherwise fall back toward the gate.
        for (auto* H : Mode->Heroes)
            if (IsValid(H) && H != Bot && !H->bDead && H->bDrafted && H->TeamId == Bot->TeamId && H->HasChampionRole(TEXT("healer")))
            { Out = H->GetActorLocation(); return FVector::DistSquared2D(Out, Bot->GetActorLocation()) > FMath::Square(220.f); }
        Out = Mode->BasePosition(Team) + FVector(350, 0, 0);
        return FVector::DistSquared2D(Out, Bot->GetActorLocation()) > FMath::Square(200.f);
    }
    // Hold the castle approach in a loose line across the road.
    const FVector Anchor = CireLanePath::PointAlongRoute(World, Team, Config(World).BotHoldAt, Bot->GetActorLocation().Z); // world-scale: data (was .80)
    Out = CireLanePath::ClampToLane(World, Team, Anchor + FVector(((Index / 5) % 2) * -160.f, (Index % 5 - 2) * 150.f, 0), 120);
    return FVector::DistSquared2D(Out, Bot->GetActorLocation()) > FMath::Square(180.f);
}

// ---------------------------------------------------------------- monster-expansion: rare spawns and bonus waves
bool CireWaveDirector::RollRare(const FCireWaveConfig& C, FCireWaveDef& W, int32 GlobalWave, int32 Seed, int32 RaresThisCycle, bool bForce)
{
    const FCireRareSpawnRules& Rr = C.Rare;
    const bool bEligibleType = W.Type == ECireWaveType::Normal || W.Type == ECireWaveType::CasterPack || W.Type == ECireWaveType::MeleePack ||
        W.Type == ECireWaveType::RangedPack || W.Type == ECireWaveType::HybridPack || W.Type == ECireWaveType::Custom;
    if (Rr.Pool.IsEmpty() || W.Units.Num() >= 8) return false;
    FRandomStream Stream(static_cast<int32>(HashCombine(GetTypeHash(Seed), GetTypeHash(GlobalWave * 7919 + 17))));
    const float Roll = Stream.FRand();
    const int32 Pick = Stream.RandRange(0, Rr.Pool.Num() - 1);
    if (!bForce && (!Rr.bEnabled || !bEligibleType || GlobalWave < Rr.FromWave || RaresThisCycle >= Rr.MaxPerCycle || Roll >= Rr.Chance)) return false;
    if (W.UnitsPerLane() >= 30) return false;
    FCireWaveUnit U; U.Archetype = Rr.Pool[Pick]; U.Count = 1; U.bRare = true;
    // The rare inherits the wave's pressure (its health/damage rows), then Rare.health/damage on top (ApplyRare).
    if (!W.Units.IsEmpty()) { U.HealthScale = W.Units[0].HealthScale; U.DamageScale = W.Units[0].DamageScale; }
    W.Units.Add(U);
    return true;
}

float CireWaveDirector::OnWaveCleared(ACireGameMode* Mode, int32 WaveInCycle, bool bForce)
{
    auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S || !Mode->HasAuthority() || Mode->Clock.Phase() != Cires::MatchPhase::Survival) return 0.f;
    FRuntime& R = Get(Mode);
    const FCireBonusWaveRules& B = R.Config.Bonus;
    if (!bForce)
    {
        // Never after the cycle's last wave (prep follows), never in smoke runs, capped per cycle.
        if (Mode->bSmoke || !B.bEnabled || WaveInCycle >= S->WavesPerCycle || S->Wave < B.FromWave || R.BonusThisCycle >= B.MaxPerCycle) return 0.f;
        FRandomStream Stream(static_cast<int32>(HashCombine(GetTypeHash(CireRaces::MatchSeed(Mode->GetWorld())), GetTypeHash(S->Wave * 104729 + 3))));
        if (Stream.FRand() >= B.Chance) return 0.f;
    }
    return StartBonusWave(Mode) ? B.ExtraBreatherSeconds : 0.f;
}

bool CireWaveDirector::StartBonusWave(ACireGameMode* Mode, FString* Error)
{
    auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S || Mode->Clock.Phase() != Cires::MatchPhase::Survival) { if (Error) *Error = TEXT("Bonus waves run during the survival phase."); return false; }
    FRuntime& R = Get(Mode);
    FCireWaveDef W = R.Config.Bonus.Wave;
    if (W.Units.IsEmpty()) { if (Error) *Error = TEXT("The bonus wave has no creatures."); return false; }
    W.Type = ECireWaveType::BonusLoot; W.bMustClear = false;
    const int32 Serial = ++R.Serial;
    FWaveRecord& Rec = R.Records.Add(Serial);
    Rec.Label = W.Label + TEXT(" (bonus)"); Rec.Type = TypeName(W.Type); Rec.StartedAt = Rec.LastSpawnAt = Now(Mode); Rec.bMustClear = false;
    Rec.WaveNumber = FMath::Max(1, S->Wave); Rec.WaveInCycle = FMath::Max(1, Mode->CycleWavesSpawned); Rec.Cycle = Mode->Clock.Round(); Rec.WaveType = W.Type;
    QueueWave(R, W, Serial, Mode->bSmoke, Mode->Clock.Round() - 1, true);
    ++R.BonusThisCycle; ++R.BonusTotal;
    S->Announcement = FString::Printf(TEXT("BONUS LOOT WAVE | %s: treasure creatures flee down your lane. Catch them before they escape (%.0f s)!"), *W.Label, R.Config.Bonus.EscapeSeconds);
    S->ForceNetUpdate();
    UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_BONUS_START label=\"%s\" units=%d escape=%.0f extra_breather=%.0f wave=%d"), *W.Label, W.UnitsPerLane(),
        R.Config.Bonus.EscapeSeconds, R.Config.Bonus.ExtraBreatherSeconds, S->Wave);
    return true;
}

void CireWaveDirector::SpecialCounts(const ACireGameMode* Mode, int32& Rares, int32& BonusWaves)
{
    const FRuntime* R = Mode ? Find(Mode->GetWorld()) : nullptr;
    Rares = R ? R->RaresTotal : 0; BonusWaves = R ? R->BonusTotal : 0;
}

// ---------------------------------------------------------------- HUD summary
FCireWaveSummary CireWaveDirector::Summary(const ACireGameMode* Mode)
{
    FCireWaveSummary Out;
    const auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S) return Out;
    const FCireWaveConfig& C = Config(Mode->GetWorld());
    const FRuntime* R = Find(Mode->GetWorld());
    const FWaveRecord* Rec = R ? R->Records.Find(R->CurrentSerial) : nullptr;
    if (Rec && Mode->CycleWavesSpawned > 0) Out.Current = FString::Printf(TEXT("%d. %s"), Mode->CycleWavesSpawned, *Rec->Label);
    if (Mode->CycleWavesSpawned < S->WavesPerCycle)
    {
        const FCireWaveDef Next = ResolveWave(C, Mode->CycleWavesSpawned, Mode->Clock.Round() - 1);
        const FString Race = RaceLabel(C, Next, Mode->Clock.Round() - 1, Mode->CycleWavesSpawned); // monster-races
        Out.Next = Race.IsEmpty() ? FString::Printf(TEXT("%d. %s (%s)"), Mode->CycleWavesSpawned + 1, *Next.Label, *TypeLabel(Next.Type)) :
            FString::Printf(TEXT("%d. %s (%s, %s)"), Mode->CycleWavesSpawned + 1, *Next.Label, *TypeLabel(Next.Type), *Race);
    }
    else Out.Next = TEXT("Preparation");
    return Out;
}

#if !UE_BUILD_SHIPPING
void CireWaveDirector::DebugAge(ACireGameMode* Mode, float Seconds)
{
    if (!Mode) return;
    FRuntime& R = Get(Mode);
    for (auto& Pair : R.Records) { Pair.Value.StartedAt -= Seconds; Pair.Value.LastSpawnAt -= Seconds; }
    for (auto& Pair : R.Tracks)
    {
        auto& T = Pair.Value;
        T.SpawnedAt -= Seconds; T.SampleAt -= Seconds; T.ForcedAt -= Seconds; T.SuppressUntil -= Seconds; T.GhostRefreshAt -= Seconds;
    }
    for (auto& Pair : R.Bots) { Pair.Value.AnchorAt -= Seconds; Pair.Value.DetourUntil -= Seconds; Pair.Value.RetreatUntil -= Seconds; }
}
#endif

// ---------------------------------------------------------------- pacing / economy hooks
int32 CireWaveDirector::CurrentWaveIndex(const ACireGameMode* Mode)
{
    const auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    return S ? S->Wave : 0;
}
FCireWaveUnitInfo CireWaveDirector::UnitFlags(const ACireMonster* M)
{
    const FTrack* T = TrackOf(M);
    if (T && T->Info.bValid) return T->Info;
    FCireWaveUnitInfo Out;
    if (IsValid(M) && M->PackId < 0 && M->Lane >= 0)
    {
        // Wave units spawned outside the director (legacy/dev paths): best effort from the actor.
        Out.bValid = true; Out.bArmored = M->bArmoredEscort; Out.bBoss = M->bBoss;
        if (const auto* Mode = M->GetWorld()->GetAuthGameMode<ACireGameMode>()) { Out.WaveNumber = CurrentWaveIndex(Mode); Out.Cycle = Mode->Clock.Round(); Out.WaveInCycle = Mode->CycleWavesSpawned; }
    }
    return Out;
}
float CireWaveDirector::MarchSpeed(const ACireMonster* M)
{
    if (!IsValid(M) || M->PackId >= 0 || IsValid(M->Victim)) return 1.f;
    const FTrack* T = TrackOf(M);
    if (!T) return 1.f;
    const FCireWaveConfig& C = Config(M->GetWorld());
    // world-scale: non-attacking marchers never stop to fight: they keep their own (hero-like) pace the whole way, and
    // escort guards walking beside their escortee keep that pace too, so the escort does not break formation.
    if (M->bArmoredEscort || (T->bGuard && AliveUnit(T->Charge.Get()))) return FMath::Max(C.MarchSpeedMultiplier, C.MarcherSpeed);
    // world-scale: hurry across the empty outer districts; march at the normal pace once a defender is near.
    if (C.RallySpeed > C.MarchSpeedMultiplier)
    {
        const auto* Mode = M->GetWorld()->GetAuthGameMode<ACireGameMode>();
        if (!Mode) return C.MarchSpeedMultiplier;
        const FVector P = M->GetActorLocation();
        for (const ACireHero* H : Mode->Heroes)
            if (IsValid(H) && !H->bDead && H->TeamId == M->Lane && FVector::DistSquared2D(H->GetActorLocation(), P) < FMath::Square(C.RallyRadius))
                return C.MarchSpeedMultiplier;
        return C.RallySpeed;
    }
    return C.MarchSpeedMultiplier;
}
bool CireWaveDirector::IsBreather(const ACireGameMode* Mode)
{
    const auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    return S && Mode->Clock.Phase() == Cires::MatchPhase::Survival && S->CycleWavesDone < S->WavesPerCycle &&
        Mode->CycleWavesSpawned == S->CycleWavesDone && !BlocksNextWave(Mode);
}
bool CireWaveDirector::SetPlayerReady(ACireHero* Hero, bool bReady)
{
    auto* Mode = IsValid(Hero) ? Hero->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr;
    if (!Mode || Hero->bBot || !IsBreather(Mode)) return false;
    FRuntime& R = Get(Mode);
    if (bReady) R.Ready.Add(Hero); else R.Ready.Remove(Hero);
    UpdateBreatherReady(Mode);
    return true;
}
bool CireWaveDirector::UpdateBreatherReady(ACireGameMode* Mode)
{
    auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S) return false;
    FRuntime& R = Get(Mode);
    int32 Humans = 0, Ready = 0;
    for (auto* H : Mode->Heroes)
        if (IsValid(H) && !H->bBot && H->bDrafted) { ++Humans; if (R.Ready.Contains(H)) ++Ready; }
    // progression-shop: mirror each hero's ready state for the Skill Shop's team panel (bots auto-ready).
    for (auto* H : Mode->Heroes)
        if (IsValid(H) && H->Inventory)
        {
            const bool bReady = IsBreather(Mode) && (H->bBot || R.Ready.Contains(H));
            if (H->Inventory->bReadyToContinue != bReady) H->Inventory->bReadyToContinue = bReady;
        }
    if (S->BreatherReady != Ready || S->BreatherPlayers != Humans) { S->BreatherReady = Ready; S->BreatherPlayers = Humans; S->ForceNetUpdate(); }
    // Bots-only matches (soak) keep the full breather: early continue needs at least one human.
    return R.Config.bEarlyContinue && Humans > 0 && Ready >= Humans && IsBreather(Mode);
}
