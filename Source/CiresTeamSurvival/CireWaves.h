#pragma once
// wave-director: data-driven wave composition (Content/Data/Waves.json), the
// authoritative wave runtime (spawn queue, clear rules, stuck detection, stall
// failsafe), neutral challenge packs, and bot lane-defence priorities.
// See Docs/Waves.md.
#include "CoreMinimal.h"

class ACireGameMode;
class ACireMonster;
class ACireHero;
class UWorld;

enum class ECireWaveType : uint8
{
    Normal, Armored, ArmoredEscort, Boss, CasterPack, MeleePack, RangedPack, HybridPack, Custom, Count
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
    bool operator==(const FCireWaveUnit& O) const;
};

struct CIRESTEAMSURVIVAL_API FCireWaveDef
{
    FString Label = TEXT("Wave");
    ECireWaveType Type = ECireWaveType::Normal;
    TArray<FCireWaveUnit> Units;
    /** Seconds between individual spawns within this wave (both lanes spawn together). */
    float SpawnInterval = .6f;
    /** Extra delay before this wave, on top of the global breather. */
    float DelayBefore = 0.f;
    /** When false the next wave may start once this one has fully spawned (its units still count for the cycle clear). */
    bool bMustClear = true;
    /** Kill reward (XP/gold) multiplier for this wave's units. */
    float RewardMultiplier = 1.f;
    int32 UnitsPerLane() const;
    bool operator==(const FCireWaveDef& O) const;
};

struct CIRESTEAMSURVIVAL_API FCireWaveConfig
{
    /** Seconds between a cleared wave and the next spawn. */
    float BreatherSeconds = 8.f;
    /** Cleared waves per cycle before prep -> arena -> recovery. Waves[] wraps if shorter. */
    int32 WavesPerCycle = 5;
    /** 0 = loop cycles forever with scaling; N = the match ends after cycle N (most lives wins). */
    int32 Cycles = 0;
    /** Per completed cycle: health/damage multiplier growth and extra units per composition row. */
    float CycleHealthGrowth = .15f, CycleDamageGrowth = .10f;
    int32 CycleExtraUnits = 1;
    /** Stall failsafe: a wave older than this (seconds after its last spawn) has its leftovers march, then despawn. */
    bool bStallFailsafe = true;
    float MaxWaveSeconds = 210.f;
    ECireWaveFailsafe FailsafeAction = ECireWaveFailsafe::March;
    float FailsafeGraceSeconds = 45.f;
    /** Stuck detection: a wave unit that makes no progress for this long is nudged along its route. */
    float StuckSeconds = 5.f;
    TArray<FCireWaveDef> Waves;
    bool operator==(const FCireWaveConfig& O) const;
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
    /** Starts the next wave of the cycle (queues its spawns). */
    CIRESTEAMSURVIVAL_API bool StartWave(ACireGameMode* Mode);
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
    /** Clears the director's per-monster bookkeeping (death/leak/despawn). */
    CIRESTEAMSURVIVAL_API void Forget(const ACireMonster* Monster);

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
    /** Survival movement for a bot heading to Goal: the straight line, or a detour along the
     *  prop-free road when the bot has stopped making progress (bots have no navmesh). */
    CIRESTEAMSURVIVAL_API FVector BotSteer(ACireHero* Bot, const FVector& Goal);

    // ---- HUD ----
    CIRESTEAMSURVIVAL_API FCireWaveSummary Summary(const ACireGameMode* Mode);

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
