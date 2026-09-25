#pragma once
// wave-director: data-driven wave composition (Content/Data/Waves.json), the
// authoritative wave runtime (spawn queue, clear rules, stuck detection, stall
// failsafe), neutral challenge packs, and bot lane-defence priorities.
// See Docs/Waves.md.
#include "CoreMinimal.h"
#include "CireRaces.h" // monster-races: ranks, skill progression, campaign

class ACireGameMode;
class ACireMonster;
class ACireHero;
class UWorld;

enum class ECireWaveType : uint8
{
    Normal, Armored, ArmoredEscort, Boss, CasterPack, MeleePack, RangedPack, HybridPack, Custom,
    BonusLoot, // monster-expansion: fleeing treasure creatures, big payout, never costs lives (Waves.json "bonusWave")
    Count
};

enum class ECireWaveFailsafe : uint8 { March, Despawn };

/** One composition row: Count units of Archetype per lane. */
struct CIRESTEAMSURVIVAL_API FCireWaveUnit
{
    FName Archetype = TEXT("hollow_infantry");
    int32 Count = 1;
    float HealthScale = 1.f, DamageScale = 1.f, SizeScale = 1.f;
    /** Elite: classified Elite, gold nameplate marker, tougher (x1.6 health on top of HealthScale). */
    bool bElite = false;
    /** Marches the route and never attacks or aggroes (armored / escortee). Must be stopped. */
    bool bNonAttacking = false;
    /** The escortee of an Armored Escort wave: attackers in the same wave defend it. */
    bool bEscortee = false;
    /** Lane boss: leaks for its archetype leak cost (10 by default). */
    bool bBoss = false;
    /** Lives lost when this unit reaches the castle; 0 = archetype/default rule. */
    int32 LeakCost = 0;
    // monster-races
    /** Race slot (line, bruiser, tank, caster, ranged, special, warlord, colossus, boss): the row follows the wave's
     *  race. None = the explicit Archetype. Archetype keeps the hollow unit of the slot so old callers still work. */
    FName Slot;
    /** Rank colour and strength (Elite = the legacy elite flag). */
    ECireNPCRank Rank = ECireNPCRank::Normal;
    /** Race palette variant (reskin set); -1 = campaign default (rotation lap). */
    int32 Palette = -1;
    /** Skill count / tier overrides; -1 / 0 = the wave schedule (Waves.json skillProgression). */
    int32 SkillCount = -1;
    int32 SkillTier = 0;
    /** monster-expansion: Rare Spawn (tougher, glowing, "Rare" plate, much better personal loot; Waves.json "rareSpawn"). */
    bool bRare = false;
    ECireNPCRank EffectiveRank() const { return bElite && Rank < ECireNPCRank::Elite ? ECireNPCRank::Elite : Rank; }
    bool operator==(const FCireWaveUnit& O) const;
};

struct CIRESTEAMSURVIVAL_API FCireWaveDef
{
    FString Label = TEXT("Wave");
    ECireWaveType Type = ECireWaveType::Normal;
    TArray<FCireWaveUnit> Units;
    /** Seconds between individual spawns within this wave (both lanes spawn together). */
    float SpawnInterval = .4f;
    /** Extra delay before this wave, on top of the global breather. */
    float DelayBefore = 0.f;
    /** When false the next wave may start once this one has fully spawned (its units still count for the cycle clear). */
    bool bMustClear = true;
    /** Kill reward (XP/gold) multiplier for this wave's units. */
    float RewardMultiplier = 1.f;
    /** monster-races: race of this wave's slot rows; None = the campaign rotation for the cycle. */
    FName Race;
    int32 UnitsPerLane() const;
    bool operator==(const FCireWaveDef& O) const;
};

/** monster-expansion: Waves.json "rareSpawn". A rare creature occasionally joins a normal wave (both lanes). */
struct CIRESTEAMSURVIVAL_API FCireRareSpawnRules
{
    bool bEnabled = true;
    /** Chance per eligible wave (normal / pack / hybrid / custom types). */
    float Chance = .3f;
    /** First global wave that can roll a rare (early waves stay readable). */
    int32 FromWave = 2;
    int32 MaxPerCycle = 2;
    /** On top of the creature's archetype and the wave's scaling. */
    float Health = 1.8f, Damage = 1.2f, Size = 1.15f; // x elite (1.6 health, 1.25 damage): about 4x a mob of its wave
    /** Kill bounty in mob values (a normal mob is 1). */
    float Bounty = 5.f;
    /** Rare creature archetypes (Bestiary.json); one is drawn per rare. */
    TArray<FName> Pool;
    bool operator==(const FCireRareSpawnRules& O) const;
};

/** monster-expansion: Waves.json "bonusWave". After a cleared wave (never the cycle's last) a short Bonus Loot
 *  Wave may run during the breather: greedy creatures flee down the lane and escape after EscapeSeconds. */
struct CIRESTEAMSURVIVAL_API FCireBonusWaveRules
{
    bool bEnabled = true;
    float Chance = .4f;
    int32 FromWave = 2;
    int32 MaxPerCycle = 1;
    /** Added to the breather when the bonus wave runs (the match grows by at most this). */
    float ExtraBreatherSeconds = 6.f;
    /** Seconds before a bonus creature escapes with its loot (no lives lost). */
    float EscapeSeconds = 26.f;
    /** A bonus creature bolts away from a champion closer than this. */
    float FleeRadius = 950.f;
    /** Kill bounty in mob values. */
    float Bounty = 4.f;
    FCireWaveDef Wave;
    FCireBonusWaveRules(); // Wave = the goblin hoard template (CireWaveDirector::BonusTemplate)
    bool operator==(const FCireBonusWaveRules& O) const;
};

struct CIRESTEAMSURVIVAL_API FCireWaveConfig
{
    /** Seconds between a cleared wave and the next spawn: the Skill Shop window (progression-shop reads it). */
    float BreatherSeconds = 12.f; // balance (pacing): 15 -> 12
    /** Pacing (Waves.json "pacing"): where on the route waves appear (0 = breach gate, 0.7 max), the march-speed
     *  multiplier while a wave unit is not fighting, the first wave's delay and the phase clock after a cycle. */
    float SpawnAlongRoute = 0.f, MarchSpeedMultiplier = 1.4f, FirstWaveDelay = 8.f; // balance (pacing): march 1.25 -> 1.4
    float PrepSeconds = 25.f, ArenaSeconds = 60.f, RecoverySeconds = 8.f; // balance (pacing): prep 30 -> 25, recovery 10 -> 8
    /** Breather ends early (1 s) once every human player has pressed Ready; bots are always ready. */
    bool bEarlyContinue = true;
    /** Cleared waves per cycle before prep -> arena -> recovery. Waves[] wraps if shorter. */
    int32 WavesPerCycle = 5;
    /** 0 = loop cycles forever with scaling; N = the match ends after cycle N (most lives wins). */
    int32 Cycles = 3; // balance (pacing): a full match is three cycles (about 25-30 minutes), not an endless loop
    /** Per completed cycle: health/damage multiplier growth and extra units per composition row. */
    float CycleHealthGrowth = .08f, CycleDamageGrowth = .10f; // balance (pacing): health growth .10 -> .08
    int32 CycleExtraUnits = 0;
    /** Stall failsafe: a wave older than this (seconds after its last spawn) has its leftovers march, then despawn. */
    bool bStallFailsafe = true;
    float MaxWaveSeconds = 100.f; // balance (pacing): 120 -> 100, stragglers held whole cycles
    ECireWaveFailsafe FailsafeAction = ECireWaveFailsafe::March;
    float FailsafeGraceSeconds = 20.f; // balance (pacing): 30 -> 20
    /** Stuck detection: a wave unit that makes no progress for this long is nudged along its route. */
    float StuckSeconds = 5.f;
    TArray<FCireWaveDef> Waves;
    /** monster-races: when monsters get skills, and which race each cycle fields. */
    FCireSkillProgression Skills;
    FCireCampaign Campaign;
    /** monster-expansion: rare spawns and the bonus loot wave. */
    FCireRareSpawnRules Rare;
    FCireBonusWaveRules Bonus;
    bool operator==(const FCireWaveConfig& O) const;
};

/** Economy hook: what a wave unit was when it spawned. Valid until ACireGameMode::MonsterKilled/Leak return. */
struct CIRESTEAMSURVIVAL_API FCireWaveUnitInfo
{
    bool bValid = false;
    int32 WaveNumber = 0;   // global wave number (1-based, never resets)
    int32 WaveInCycle = 0;  // 1..WavesPerCycle
    int32 Cycle = 1;        // match round
    ECireWaveType Type = ECireWaveType::Normal;
    bool bArmored = false;  // non-attacking marcher (armored wave or escortee)
    bool bEscortee = false, bBoss = false, bElite = false;
    bool bRare = false, bBonus = false; // monster-expansion
};

/** Replicated one-line summary for the HUD match plate. */
struct CIRESTEAMSURVIVAL_API FCireWaveSummary
{
    FString Current, Next;
};

namespace CireWaveDirector
{
    // ---- data ----
    CIRESTEAMSURVIVAL_API const TCHAR* TypeName(ECireWaveType Type);
    CIRESTEAMSURVIVAL_API FString TypeLabel(ECireWaveType Type);
    CIRESTEAMSURVIVAL_API bool ParseType(const FString& Name, ECireWaveType& Out);
    /** Template composition for a wave type (Custom returns one infantry row). */
    CIRESTEAMSURVIVAL_API FCireWaveDef Template(ECireWaveType Type);
    /** Eric's default progression: normal, normal, armored, armored escort, boss. */
    CIRESTEAMSURVIVAL_API FCireWaveConfig Defaults();
    /** Clamps every value to sane limits; returns false (with a reason) if anything is structurally invalid. */
    CIRESTEAMSURVIVAL_API bool Validate(FCireWaveConfig& Config, FString* Error = nullptr, bool bClamp = true);
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireWaveConfig& Out, FString& Error);
    CIRESTEAMSURVIVAL_API FString ToJson(const FCireWaveConfig& Config);
    CIRESTEAMSURVIVAL_API FString DataPath();
    CIRESTEAMSURVIVAL_API bool LoadFile(FCireWaveConfig& Out, FString* Error = nullptr, const FString& Path = FString());
    CIRESTEAMSURVIVAL_API bool SaveFile(const FCireWaveConfig& Config, FString* Error = nullptr, const FString& Path = FString());

    // ---- authoritative runtime (server) ----
    CIRESTEAMSURVIVAL_API const FCireWaveConfig& Config(const UWorld* World = nullptr);
    CIRESTEAMSURVIVAL_API void Initialize(ACireGameMode* Mode);
    /** Live edit: validated, applied from the next wave. */
    CIRESTEAMSURVIVAL_API bool ApplyLive(ACireGameMode* Mode, const FCireWaveConfig& Config, FString* Error = nullptr);
    /** Wave definition used for global wave number (1-based), with the cycle scaling applied. */
    CIRESTEAMSURVIVAL_API FCireWaveDef ResolveWave(const FCireWaveConfig& Config, int32 WaveInCycle, int32 Cycle);
    /** Starts the next wave of the cycle (queues its spawns). bLive = the match flow (ACireGameMode::SpawnWave): only live
     *  waves roll rare spawns and race variants (monster-expansion), so direct test/developer starts stay deterministic. */
    CIRESTEAMSURVIVAL_API bool StartWave(ACireGameMode* Mode, bool bLive = false);
    /** Developer: make wave N (1-based within the cycle) the next one; optionally spawn it now. */
    CIRESTEAMSURVIVAL_API bool SkipTo(ACireGameMode* Mode, int32 WaveInCycle, bool bSpawnNow, FString* Error = nullptr);
    /** Developer: spawn a specific definition immediately as an extra wave. */
    CIRESTEAMSURVIVAL_API bool SpawnNow(ACireGameMode* Mode, const FCireWaveDef& Wave, FString* Error = nullptr);
    /** Called every GameMode tick during survival: spawn queue, clear rule, stuck detection, failsafe. */
    CIRESTEAMSURVIVAL_API void TickSurvival(ACireGameMode* Mode, float Delta);
    /** Phase entry (clears queues, resets per-cycle state on survival). */
    CIRESTEAMSURVIVAL_API void OnPhaseChanged(ACireGameMode* Mode, int32 NewPhase);
    /** True when any wave unit is alive or waiting to spawn. */
    CIRESTEAMSURVIVAL_API bool IsWaveActive(const ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API bool HasPendingSpawns(const ACireGameMode* Mode);
    /** True while a must-clear wave unit is alive or spawns are queued (gates the next wave). */
    CIRESTEAMSURVIVAL_API bool BlocksNextWave(const ACireGameMode* Mode);
    /** Director size multiplier for a wave unit (0 = not director-spawned). */
    CIRESTEAMSURVIVAL_API float SizeScale(const ACireMonster* Monster);
    /** Wave unit may not acquire new victims right now (dropped an unreachable target, or forced march). */
    CIRESTEAMSURVIVAL_API bool AggroSuppressed(const ACireMonster* Monster);
    /** Smoke bookkeeping: leak cost spawned per lane and lane bosses spawned. */
    CIRESTEAMSURVIVAL_API void SmokeCounters(const ACireGameMode* Mode, int32& Leak0, int32& Leak1, int32& Bosses);
    CIRESTEAMSURVIVAL_API float CurrentWaveAge(const ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API FString CurrentWaveType(const ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API float RewardMultiplier(const ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API float StuckSeconds(const ACireMonster* Monster);
    /** Monster under the failsafe's forced march (ignores combat). */
    CIRESTEAMSURVIVAL_API bool IsForcedMarch(const ACireMonster* Monster);
    /** Escort guard: the escortee this unit defends (null when none/dead). */
    CIRESTEAMSURVIVAL_API ACireMonster* EscortCharge(const ACireMonster* Monster);
    /** nav-paths: the unit may not acquire victims for Seconds (its victim was unreachable on the navmesh). */
    CIRESTEAMSURVIVAL_API void SuppressAggro(ACireMonster* Monster, float Seconds);
    /** nav-paths: stuck nudges, failsafe marches and failsafe despawns so far in this world. */
    CIRESTEAMSURVIVAL_API void RescueCounts(const ACireGameMode* Mode, int32& Nudges, int32& Marches, int32& Despawns);
    /** Clears the director's per-monster bookkeeping (death/leak/despawn). */
    CIRESTEAMSURVIVAL_API void Forget(const ACireMonster* Monster);
    // ---- monster-races ----
    /** Race fielded by a wave row: the wave's own race, else the campaign rotation for the cycle (0-based). */
    CIRESTEAMSURVIVAL_API FName RaceFor(const FCireWaveConfig& Config, const FCireWaveDef& Wave, int32 Cycle, int32 Row = 0);
    /** Human label of a wave's race ("The Drowned Deep", or "Hollow + Blightwood"). */
    CIRESTEAMSURVIVAL_API FString RaceLabel(const FCireWaveConfig& Config, const FCireWaveDef& Wave, int32 Cycle);
    /** A summoned unit joins its summoner's wave bookkeeping (clear rule, failsafe, size). */
    CIRESTEAMSURVIVAL_API void AdoptSummon(ACireGameMode* Mode, ACireMonster* Summon, ACireMonster* Parent);
    /** F8 editor: next race of a wave (campaign rotation, then every race in Races.json order). */
    CIRESTEAMSURVIVAL_API void CycleWaveRace(FCireWaveDef& Wave);
    /** F8 editor: next unit of a row: the race slots (line..special, warlord, colossus, boss), then each unit of Race explicitly. */
    CIRESTEAMSURVIVAL_API void CycleRowUnit(FCireWaveUnit& Row, FName Race);
    /** F8 editor: next rank (normal, veteran, elite, champion, warlord, mythic). */
    CIRESTEAMSURVIVAL_API void CycleRowRank(FCireWaveUnit& Row);
    /** F8 editor: short label of a row's unit ("Caster: Tidecaller" or "Tidecaller"). */
    CIRESTEAMSURVIVAL_API FString RowUnitLabel(const FCireWaveUnit& Row, FName Race, int32 Cycle = 0);

    // ---- monster-expansion: rare spawns and bonus loot waves ----
    /** Template of the bonus loot wave (goblin hoard). */
    CIRESTEAMSURVIVAL_API FCireWaveDef BonusTemplate();
    /** Called when a wave clears: may start a bonus loot wave. Returns extra breather seconds (0 = none). */
    CIRESTEAMSURVIVAL_API float OnWaveCleared(ACireGameMode* Mode, int32 WaveInCycle, bool bForce = false);
    /** Starts the configured bonus wave now (developer / tests). */
    CIRESTEAMSURVIVAL_API bool StartBonusWave(ACireGameMode* Mode, FString* Error = nullptr);
    /** Rare spawns / bonus waves started this match (server; tests and soaks). */
    CIRESTEAMSURVIVAL_API void SpecialCounts(const ACireGameMode* Mode, int32& Rares, int32& BonusWaves);
    /** Rare roll for a wave (deterministic per match seed and wave number). Appends the rare row when it hits. */
    CIRESTEAMSURVIVAL_API bool RollRare(const FCireWaveConfig& Config, FCireWaveDef& Wave, int32 GlobalWave, int32 Seed, int32 RaresThisCycle, bool bForce = false);

    // ---- neutral challenge packs ----
    /** Challenge-pack units start neutral; a player's attack turns the whole pack hostile. */
    CIRESTEAMSURVIVAL_API void MakeNeutral(ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API bool IsNeutral(const ACireMonster* Monster);
    /** Server hook from ACireMonster::TakeDamage (before damage applies). Returns false to reject the hit. */
    CIRESTEAMSURVIVAL_API bool AllowDamage(ACireMonster* Monster, ACireHero* Attacker);
    /** Server hook after damage applied: pack aggro and escort defence. */
    CIRESTEAMSURVIVAL_API void OnMonsterDamaged(ACireMonster* Monster, ACireHero* Attacker);
    /** Pack returned home (leash complete): becomes neutral again. */
    CIRESTEAMSURVIVAL_API void OnPackReset(ACireMonster* Monster);

    // ---- bots ----
    /** Lane-defence target choice for a survival-phase bot; null = none. */
    CIRESTEAMSURVIVAL_API AActor* ChooseBotTarget(ACireHero* Bot);
    /** Where an idle / retreating bot should stand during survival. Returns false if it should stay put. */
    CIRESTEAMSURVIVAL_API bool BotDestination(ACireHero* Bot, FVector& Out);
    CIRESTEAMSURVIVAL_API bool ShouldBotRetreat(ACireHero* Bot);
    /** Survival movement for a bot heading to Goal: the navmesh path (nav-paths, every phase); without
     *  a navmesh the straight line, or a detour along the prop-free road when the bot stops making progress. */
    CIRESTEAMSURVIVAL_API FVector BotSteer(ACireHero* Bot, const FVector& Goal);

    // ---- HUD ----
    CIRESTEAMSURVIVAL_API FCireWaveSummary Summary(const ACireGameMode* Mode);

    // ---- pacing / economy hooks ----
    /** Global wave number of the most recently started wave (1-based; 0 before the first wave). */
    CIRESTEAMSURVIVAL_API int32 CurrentWaveIndex(const ACireGameMode* Mode);
    /** Spawn-time wave facts for a unit (armored/boss/elite flags, wave number). bValid=false for non-wave units. */
    CIRESTEAMSURVIVAL_API FCireWaveUnitInfo UnitFlags(const ACireMonster* Monster);
    /** March-speed multiplier for a wave unit that is walking the route (1 while fighting, for packs and non-wave units). */
    CIRESTEAMSURVIVAL_API float MarchSpeed(const ACireMonster* Monster);
    /** True while survival is counting down to the next wave (the Skill Shop window). */
    CIRESTEAMSURVIVAL_API bool IsBreather(const ACireGameMode* Mode);
    /** A human player's Ready toggle for the breather (server; CireController ServerAction 10). */
    CIRESTEAMSURVIVAL_API bool SetPlayerReady(ACireHero* Hero, bool bReady);
    /** Updates the replicated ready counts; true when the breather should end early. */
    CIRESTEAMSURVIVAL_API bool UpdateBreatherReady(ACireGameMode* Mode);

    // ---- soak / diagnostics (development) ----
    CIRESTEAMSURVIVAL_API bool IsSoak();
    CIRESTEAMSURVIVAL_API void InitializeSoak(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API bool TickSoak(ACireGameMode* Mode, float Delta);
    CIRESTEAMSURVIVAL_API bool TickGallery(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API void NoteFailsafe(const FString& What);
    CIRESTEAMSURVIVAL_API void DumpWave(ACireGameMode* Mode, const TCHAR* Reason);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
    /** Tests only: pretend Seconds more have passed for every tracked wave/unit timer. */
    CIRESTEAMSURVIVAL_API void DebugAge(ACireGameMode* Mode, float Seconds);
#endif
}
