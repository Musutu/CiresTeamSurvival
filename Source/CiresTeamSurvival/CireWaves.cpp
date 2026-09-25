// wave-director: authoritative wave runtime, neutral challenge packs and bot lane defence.
#include "CireWaves.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireLoot.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireThreat.h"
#include "CireSummon.h"
#include "CireDeveloperTools.h"
#include "CireNav.h" // nav-paths
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
};
struct FWaveRecord
{
    FString Label, Type;
    float StartedAt = 0, LastSpawnAt = 0;
    bool bMustClear = true;
};
struct FOrder
{
    FCireWaveUnit Unit;
    int32 Slot = 0, Serial = 0;
    bool bMustClear = true;
    float Reward = 1;
    ECireNPCRank Rank = ECireNPCRank::Normal; // monster-races: row rank plus campaign promotion
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
    int32 Nudges = 0, Marches = 0, Despawns = 0; // nav-paths: rescue totals for the navigation probe
};
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
/** Teleport a unit onto its route, Step cm closer to the castle than its nearest route point. */
void NudgeAlong(ACireMonster* M, float Step)
{
    UWorld* World = M->GetWorld();
    const int32 Team = FMath::Clamp(M->Lane, 0, 1);
    const float Length = FMath::Max(1.f, CireLanePath::RouteLength(World, Team));
    const float Progress = CireLanePath::RouteProgress(World, Team, M->GetActorLocation());
    FVector Target = CireLanePath::PointAlongRoute(World, Team, FMath::Min(.995f, Progress + Step / Length), M->GetActorLocation().Z);
    Target.Z = FMath::Max(Target.Z, 100.);
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
    if (!Mode->bSmoke) Mode->WaveBreatherSeconds = R.Config.BreatherSeconds;
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
        Mode->WaveBreatherSeconds = R.Config.BreatherSeconds;
        Mode->WaveTimer = FMath::Min(Mode->WaveTimer, R.Config.BreatherSeconds);
    }
    Publish(Mode);
    UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_APPLIED waves=%d per_cycle=%d breather=%.1f (takes effect from the next wave)"), R.Config.Waves.Num(), R.Config.WavesPerCycle, R.Config.BreatherSeconds);
    return true;
}

FCireWaveDef CireWaveDirector::ResolveWave(const FCireWaveConfig& C, int32 WaveInCycle, int32 Cycle)
{
    if (C.Waves.IsEmpty()) return Template(ECireWaveType::Normal);
    FCireWaveDef W = C.Waves[FMath::Max(0, WaveInCycle) % C.Waves.Num()];
    Cycle = FMath::Clamp(Cycle, 0, 100);
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
    const int32 Laps = C.Campaign.RaceRotation.IsEmpty() ? 0 : Cycle / C.Campaign.RaceRotation.Num();
    for (int32 Row = 0; Row < W.Units.Num(); ++Row)
    {
        auto& U = W.Units[Row];
        const FName Race = RaceFor(C, W, Cycle, Row);
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
void QueueWave(FRuntime& R, const FCireWaveDef& W, int32 Serial, bool bFast, int32 Cycle = 0)
{
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
    auto Emit = [&](const FCireWaveUnit& U)
    {
        FOrder O; O.Unit = U; O.Slot = Slot++; O.Serial = Serial; O.bMustClear = W.bMustClear; O.Reward = W.RewardMultiplier; O.Rank = U.EffectiveRank();
        if (O.Rank == ECireNPCRank::Normal && !U.bBoss && !U.bNonAttacking && Promotion != ECireNPCRank::Normal && ++Promotable % FMath::Max(1, K.PromoteEvery) == 0)
            O.Rank = Promotion;
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
    const FVector Start = CireLanePath::SpawnPosition(Mode->GetWorld(), Team);
    const FVector Default = CireLanePath::ClampToLane(Mode->GetWorld(), Team, Start + FVector(((O.Slot / 3) % 2) * 90.f, ((O.Slot % 3) - 1) * 170.f, 0), 80);
    const FVector Position = CireDeveloperTools::SpawnPosition(Mode->GetWorld(), Team, O.Slot, Default);
    auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), Position, FRotator(0, 180, 0), Params);
    if (!M) { UE_LOG(LogCireWaves, Error, TEXT("CIRE_WAVES_SPAWN_FAILED archetype=%s"), *O.Unit.Archetype.ToString()); return nullptr; }
    M->Lane = Team;
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
        CireRaces::ApplyRank(M, Rank, FMath::Max(0, U.Palette));
        CireRaces::ApplyLoadout(M, R.Config.Skills, GlobalWave, U.SkillCount, U.SkillTier);
    }
    FTrack T;
    T.Serial = O.Serial; T.SpawnedAt = Now(M); T.Size = U.SizeScale; T.Reward = O.Reward; T.bMustClear = O.bMustClear;
    T.bEscortee = U.bEscortee; T.Anchor = Position; T.SampleAt = T.SpawnedAt + 1.f;
    if (U.bNonAttacking)
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
    R.LeakCostSpawned[Team] += LeakCost(M);
    if (M->bBoss) ++R.BossesSpawned;
    return M;
}
}

bool CireWaveDirector::StartWave(ACireGameMode* Mode)
{
    auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S || Mode->Clock.Phase() != Cires::MatchPhase::Survival || Mode->CycleWavesSpawned >= S->WavesPerCycle) return false;
    FRuntime& R = Get(Mode);
    const int32 WaveInCycle = Mode->CycleWavesSpawned;
    const FCireWaveDef W = ResolveWave(R.Config, WaveInCycle, Mode->Clock.Round() - 1);
    ++S->Wave; ++Mode->CycleWavesSpawned;
    S->NextWaveSeconds = 0;
    const int32 Serial = ++R.Serial;
    R.CurrentSerial = Serial;
    FWaveRecord& Rec = R.Records.Add(Serial);
    Rec.Label = W.Label; Rec.Type = TypeName(W.Type); Rec.StartedAt = Now(Mode); Rec.LastSpawnAt = Rec.StartedAt; Rec.bMustClear = W.bMustClear;
    // monster-races: the wave's race rides with its label and on the replicated game state.
    const int32 Cycle = Mode->Clock.Round() - 1;
    const FString Race = RaceLabel(R.Config, W, Cycle);
    if (!Race.IsEmpty()) Rec.Label = FString::Printf(TEXT("%s (%s)"), *W.Label, *Race);
    S->WaveRace = RaceFor(R.Config, W, Cycle, 0);
    QueueWave(R, W, Serial, Mode->bSmoke, Cycle);
    const TCHAR* Lead = W.Type == ECireWaveType::Armored ? TEXT("ARMORED | They will not fight back. Stop them before the gate!") :
        W.Type == ECireWaveType::ArmoredEscort ? TEXT("ARMORED ESCORT | Break the escorted tank; its guards will defend it.") :
        W.Type == ECireWaveType::Boss ? TEXT("SIEGE | A lane boss marches with this wave. A leak costs 10 lives.") : TEXT("DEFEND THE GATES");
    S->Announcement = FString::Printf(TEXT("%s | Wave %d of %d: %s"), Lead, Mode->CycleWavesSpawned, S->WavesPerCycle, *Rec.Label);
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
        // Stall failsafe: leftovers stop fighting and march; after a grace period they despawn.
        if (C.bStallFailsafe && !NoRescue() && Age > C.MaxWaveSeconds)
        {
            if (C.FailsafeAction == ECireWaveFailsafe::Despawn || (T.bForcedMarch && Time - T.ForcedAt > C.FailsafeGraceSeconds))
            { Despawn.Add(M); continue; }
            if (!T.bForcedMarch)
            {
                T.bForcedMarch = true; T.ForcedAt = Time; ++R.Marches;
                CireNPCCombat::Interrupt(M); CireThreat::Clear(M); M->bEngaged = false;
                NoteFailsafe(FString::Printf(TEXT("march %s lane=%d age=%.0f"), *M->GetNPCDisplayName(), M->Lane, Age));
                UE_LOG(LogCireWaves, Warning, TEXT("CIRE_WAVES_RESCUE_MARCH %s lane=%d age=%.0f"), *M->GetNPCDisplayName(), M->Lane, Age);
            }
        }
        if (T.bForcedMarch) Ghost(M, T);
        // Wave units hold their lane: a victim far off the route is dropped.
        if (!NoRescue() && IsValid(M->Victim) && !M->bArmoredEscort && FVector::DistSquared2D(M->GetActorLocation(), M->Victim->GetActorLocation()) > FMath::Square(1800.f))
        { CireThreat::Clear(M); T.SuppressUntil = Time + 3.f; }
        // Outside its realm (knocked back, launched): return to the route.
        if (!NoRescue() && (!CireLanePath::Contains(World, M->Lane, M->GetActorLocation(), 0) || M->GetActorLocation().Z < -500))
        { NudgeAlong(M, 0); T.Anchor = M->GetActorLocation(); T.StuckFor = 0; continue; }
        // Stuck detection, sampled once per second.
        if (Time < T.SampleAt) continue;
        T.SampleAt = Time + 1.f;
        const bool bPaused = CireProgression::IsPaused(M) || !M->CastingAbility.IsEmpty();
        const bool bFighting = IsValid(M->Victim) && !M->Victim->bDead &&
            FVector::DistSquared2D(M->GetActorLocation(), M->Victim->GetActorLocation()) <= FMath::Square(Reach(M) + 120.f);
        const bool bGuardWaiting = T.bGuard && T.Charge.IsValid() && AliveUnit(T.Charge.Get()) &&
            FVector::DistSquared2D(M->GetActorLocation(), T.Charge->GetActorLocation()) < FMath::Square(320.f);
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
        const bool bChasing = IsValid(M->Victim);
        if (bChasing) { CireThreat::Clear(M); T.SuppressUntil = Time + 6.f; }
        NudgeAlong(M, 450.f);
        ++T.Nudges; ++R.Nudges; T.StuckFor = 0; T.Anchor = M->GetActorLocation();
        UE_LOG(LogCireWaves, Display, TEXT("CIRE_WAVES_STUCK_NUDGE %s lane=%d from=(%.0f,%.0f) to=(%.0f,%.0f) chasing=%d nudges=%d"), *M->GetNPCDisplayName(), M->Lane,
            From.X, From.Y, M->GetActorLocation().X, M->GetActorLocation().Y, bChasing ? 1 : 0, T.Nudges);
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
    R.Queue.Reset(); R.SpawnTimer = 0;
    if (NewPhase == 0) { R.Records.Reset(); R.CurrentSerial = 0; }
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
// nav-paths: a wave unit whose victim the navmesh cannot reach drops it and holds the lane for a while.
void CireWaveDirector::SuppressAggro(ACireMonster* M, float Seconds)
{
    FRuntime* R = M ? Find(M->GetWorld()) : nullptr;
    if (FTrack* T = R ? R->Tracks.Find(TWeakObjectPtr<ACireMonster>(M)) : nullptr) T->SuppressUntil = FMath::Max(T->SuppressUntil, Now(M) + Seconds);
}
void CireWaveDirector::RescueCounts(const ACireGameMode* Mode, int32& Nudges, int32& Marches, int32& Despawns)
{
    const FRuntime* R = Mode ? Find(Mode->GetWorld()) : nullptr;
    Nudges = R ? R->Nudges : 0; Marches = R ? R->Marches : 0; Despawns = R ? R->Despawns : 0;
}
// monster-races ------------------------------------------------------------------------------------------
FName CireWaveDirector::RaceFor(const FCireWaveConfig& C, const FCireWaveDef& W, int32 Cycle, int32 Row)
{
    if (!W.Race.IsNone()) return W.Race;
    if (C.Campaign.RaceRotation.IsEmpty()) return TEXT("hollow");
    TArray<FString> Parts;
    C.Campaign.RaceRotation[FMath::Max(0, Cycle) % C.Campaign.RaceRotation.Num()].ParseIntoArray(Parts, TEXT("+"), true);
    return Parts.IsEmpty() ? FName(TEXT("hollow")) : FName(*Parts[FMath::Max(0, Row) % Parts.Num()].TrimStartAndEnd());
}
FString CireWaveDirector::RaceLabel(const FCireWaveConfig& C, const FCireWaveDef& W, int32 Cycle)
{
    TArray<FString> Names;
    for (int32 Row = 0; Row < FMath::Max(1, W.Units.Num()); ++Row)
        if (const FCireRace* Race = CireRaces::FindRace(RaceFor(C, W, Cycle, Row))) Names.AddUnique(Race->Short);
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
    const FVector Anchor = CireLanePath::PointAlongRoute(World, Team, .80f, Bot->GetActorLocation().Z);
    Out = CireLanePath::ClampToLane(World, Team, Anchor + FVector(((Index / 5) % 2) * -160.f, (Index % 5 - 2) * 150.f, 0), 120);
    return FVector::DistSquared2D(Out, Bot->GetActorLocation()) > FMath::Square(180.f);
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
        const FString Race = RaceLabel(C, Next, Mode->Clock.Round() - 1); // monster-races
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
