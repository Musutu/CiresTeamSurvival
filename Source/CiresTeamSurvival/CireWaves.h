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
    /** waves-modes: 1-based pack this row belongs to (0 = a legacy row, no pack). Pack waves are expanded into these rows
     *  by ResolveWave; Hybrid presets pick fight-back packs by this number. */
    int32 Pack = 0;
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
    /** waves-modes (Waves.json "packs"): 0 = legacy rows spawned as authored. N > 0 = the wave spawns N packs of
     *  PackSizeMin..PackSizeMax monsters (plus the global pack-size modifier); the non-boss rows are the pack recipe
     *  (their counts are weights) and boss / escortee rows march after / ahead of the packs as authored. */
    int32 Packs = 0, PackSizeMin = 5, PackSizeMax = 5;
    /** waves-modes (Waves.json "damage"): false = this wave's monsters never attack (they path to the castle like an
     *  armored wave) except the packs listed in FightBackPacks (the Hybrid mode). Armored units never attack. */
    bool bDealsDamage = true;
    TArray<int32> FightBackPacks;
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

/** monster-expansion: Waves.json "bonusWave". bonus-loot (playtest 6): a Bonus Loot Stage REPLACES a non-boss wave
 *  with ReplaceChance (low); its greedy creatures run the route to the castle, never attack, bolt from champions and
 *  escape EscapeSeconds after they are first attacked (or at the castle). The stage rolls a loot tier (CireBonusStage).
 *  The older breather bonus wave (Chance, after a cleared wave) is kept for tools and is off by default. */
struct CIRESTEAMSURVIVAL_API FCireBonusWaveRules
{
    bool bEnabled = true;
    float Chance = 0.f; // bonus-loot: the breather bonus wave is off by default (was .4); stages replace waves instead
    /** bonus-loot: chance per eligible (non-boss) wave that a Bonus Loot Stage replaces it. */
    float ReplaceChance = .08f;
    /** bonus-loot: the escape clock starts only when a creature is first attacked (false = at spawn, the old rule). */
    bool bEscapeTimerOnHit = true;
    int32 FromWave = 2;
    int32 MaxPerCycle = 1;
    /** Added to the breather when the bonus wave runs (the match grows by at most this). */
    float ExtraBreatherSeconds = 6.f;
    /** Seconds before a bonus creature escapes with its loot (no lives lost). bonus-loot: doubled 26 -> 52. */
    float EscapeSeconds = 52.f;
    /** A bonus creature bolts away from a champion closer than this. */
    float FleeRadius = 950.f;
    /** Kill bounty in mob values. bonus-loot: 4 -> 1 (the stage's tier chest is the reward). */
    float Bounty = 1.f;
    FCireWaveDef Wave;
    FCireBonusWaveRules(); // Wave = the goblin hoard template (CireWaveDirector::BonusTemplate)
    bool operator==(const FCireBonusWaveRules& O) const;
};

/** waves-modes (Waves.json "match"): the 25-wave match, its PvP rounds and Sudden Death.
 *  feat/arena-flow reads this through CireWaveDirector::Schedule / IsPvpAfterWave (Docs/RESUME-waves-modes.md). */
struct CIRESTEAMSURVIVAL_API FCireMatchSchedule
{
    /** Regular waves in a match; waves after this are Sudden Death waves. 0 = no Sudden Death. */
    int32 TotalWaves = 25;
    /** Global wave numbers (1-based) after which a PvP arena round runs, ascending. Default 5, 10, 15, 20. */
    TArray<int32> PvpAfterWaves = {5, 10, 15, 20};
    /** Sudden Death waves: monster health and damage multipliers (on top of all other scaling). */
    float SuddenDeathHealth = 2.f, SuddenDeathDamage = 2.f;
    /** Sudden Death replays the last N regular waves in a loop (their compositions, doubled). */
    int32 SuddenDeathLoop = 5;
    bool operator==(const FCireMatchSchedule& O) const;
};

/** waves-modes: wave monster movement and the armored traits (Waves.json "monsters"). */
struct CIRESTEAMSURVIVAL_API FCireWaveMonsterRules
{
    /** Every wave monster's speed (Eric: -20% across the board, more packs per wave). */
    float Speed = .8f;
    /** Armored (non-attacking) wave units: extra speed multiplier, slow immunity, stun duration multiplier. */
    float ArmoredSpeed = .5f;
    bool bArmoredSlowImmune = true;
    float ArmoredStunMultiplier = 2.f;
    /** Pack waves: seconds between one pack's last spawn and the next pack's first (so packs read as groups). */
    float PackGapSeconds = 1.5f;
    bool operator==(const FCireWaveMonsterRules& O) const;
};

/** waves-modes: the in-game live scale Eric sets while playing (F8 > Waves, cire.WaveScale). Applied to new spawns and
 *  rescaled onto the living wave monsters when changed. */
struct CIRESTEAMSURVIVAL_API FCireWaveScale
{
    float Health = 1.f, Damage = 1.f, Speed = 1.f;
    bool operator==(const FCireWaveScale& O) const;
};

/** waves-modes: a saveable game-mode preset (Content/Data/WavePresets.json). Hosting lists them as game types. */
struct CIRESTEAMSURVIVAL_API FCireWavePreset
{
    FName Id = TEXT("standard");
    FString Label = TEXT("Standard"), Description;
    /** Default for every wave: monsters attack heroes (true) or just path to the castle (false). */
    bool bDefaultDamage = true;
    /** Default fight-back packs when a wave's damage is off (Hybrid). */
    TArray<int32> DefaultFightBack;
    /** Per global wave (1-based) overrides: damage on/off and fight-back packs. */
    struct FWave
    {
        int32 Wave = 1; bool bDamage = true; TArray<int32> FightBack;
        bool operator==(const FWave& O) const { return Wave == O.Wave && bDamage == O.bDamage && FightBack == O.FightBack; }
    };
    TArray<FWave> Waves;
    FCireWaveScale Scale;
    /** Difficulty modifier: added to every pack's size (-3..+3). */
    int32 PackSizeBonus = 0;
    /** Optional PvP schedule override (empty = Waves.json "match"). */
    TArray<int32> PvpAfterWaves;
    /** Shipped preset (Standard / Hero TD/PvP / Hybrid): it can be saved over but always comes back if missing. */
    bool bBuiltIn = false;
    /** kit-editor: the Hero Creator kit profile this game type uses (empty / unknown = "Standard"). */
    FString KitProfile;
    /** game-profiles: the other bundle keys (layout, tuningProfile, economyProfile, packProfile, movementProfile,
     *  matchProfile, spacingProfile, worldEdit) -> profile name; missing = Default (CireProfiles.h). */
    TMap<FString, FString> Bundle;
    /** ability-tuner "allowTuning" kept through a save (-1 = absent). */
    int32 AllowTuning = -1;
    bool operator==(const FCireWavePreset& O) const;
};

struct CIRESTEAMSURVIVAL_API FCireWaveConfig
{
    /** Seconds between a cleared wave and the next spawn: the Skill Shop window (progression-shop reads it). */
    float BreatherSeconds = 12.f; // balance (pacing): 15 -> 12
    /** Pacing (Waves.json "pacing"): where on the route waves appear (0 = breach gate, 0.7 max), the march-speed
     *  multiplier while a wave unit is not fighting, the first wave's delay and the phase clock after a cycle. */
    float SpawnAlongRoute = 0.f, MarchSpeedMultiplier = 1.4f, FirstWaveDelay = 8.f; // balance (pacing): march 1.25 -> 1.4
    /** world-scale (the realm is 3x longer, pacing must not triple): while no living defender of its lane is within
     *  RallyRadius cm, a marching wave unit moves at RallySpeed x its base speed (never below MarchSpeedMultiplier), so
     *  columns cross the empty outer districts quickly and slow to the normal march as they meet the heroes.
     *  MarcherSpeed: non-attacking marchers (armored waves, escortees) never stop to fight, so they always move at this
     *  multiple (about hero running speed) and still reach the castle inside the stall failsafe on the long road.
     *  BotHoldAt is the route fraction (0 = rift, 1 = castle gate) where idle bots hold their defensive line. */
    float RallySpeed = 3.f, RallyRadius = 2500.f, BotHoldAt = .2f, MarcherSpeed = 3.f;
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
    /** rules-conformance (Waves.json "waveOrder"): "campaign" = Waves[] is played straight through the match (cycle 2
     *  continues at Waves[WavesPerCycle]) and wraps; "cycle" (default) = every cycle replays Waves[] from the start. */
    bool bCampaignOrder = false;
    /** monster-races: when monsters get skills, and which race each cycle fields. */
    FCireSkillProgression Skills;
    FCireCampaign Campaign;
    /** monster-expansion: rare spawns and the bonus loot wave. */
    FCireRareSpawnRules Rare;
    FCireBonusWaveRules Bonus;
    /** waves-modes: match schedule, monster movement / armored rules, live scale, pack-size modifier, active preset. */
    FCireMatchSchedule Match;
    FCireWaveMonsterRules Monsters;
    FCireWaveScale Live;
    int32 PackSizeBonus = 0;
    FName Preset = TEXT("standard");
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
    int32 Pack = 0;             // waves-modes: 1-based pack in its wave (0 = none)
    bool bPassive = false;      // waves-modes: damage off for its wave/pack (never attacks; not armored for gold)
    bool bSuddenDeath = false;  // waves-modes: spawned in a Sudden Death wave
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
    /** layout-wiring: units sent down each path of a realm this match (path index -> count). */
    CIRESTEAMSURVIVAL_API TMap<int32, int32> PathSpawnCounts(const ACireGameMode* Mode, int32 Team);
    /** nav-paths: stuck nudges, failsafe marches and failsafe despawns so far in this world. */
    CIRESTEAMSURVIVAL_API void RescueCounts(const ACireGameMode* Mode, int32& Nudges, int32& Marches, int32& Despawns);
    /** Clears the director's per-monster bookkeeping (death/leak/despawn). */
    CIRESTEAMSURVIVAL_API void Forget(const ACireMonster* Monster);
    // ---- monster-races ----
    /** Race fielded by a wave row: the wave's own race, else the campaign rotation for the cycle (0-based). */
    CIRESTEAMSURVIVAL_API FName RaceFor(const FCireWaveConfig& Config, const FCireWaveDef& Wave, int32 Cycle, int32 Row = 0, int32 WaveInCycle = 0);
    /** rules-conformance: index into Waves[] (campaign order) and into the race rotation (per wave or per cycle). */
    CIRESTEAMSURVIVAL_API int32 WaveIndex(const FCireWaveConfig& Config, int32 WaveInCycle, int32 Cycle);
    CIRESTEAMSURVIVAL_API int32 RotationIndex(const FCireWaveConfig& Config, int32 WaveInCycle, int32 Cycle);
    /** Human label of a wave's race ("The Drowned Deep", or "Hollow + Blightwood"). */
    CIRESTEAMSURVIVAL_API FString RaceLabel(const FCireWaveConfig& Config, const FCireWaveDef& Wave, int32 Cycle, int32 WaveInCycle = 0);
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
    /** bonus-loot: does a Bonus Loot Stage replace this wave? Never a boss wave (or a wave with a boss row), never before
     *  bonusWave.fromWave, at most maxPerCycle per cycle; deterministic per match seed and wave. */
    CIRESTEAMSURVIVAL_API bool RollBonusStage(const FCireWaveConfig& C, const FCireWaveDef& W, int32 GlobalWave, int32 Seed, int32 BonusThisCycle, bool bForce = false);
    /** bonus-loot: the next StartWave becomes a Bonus Loot Stage (developer / tests; -1 = tier rolled, 1..3 = forced tier). */
    CIRESTEAMSURVIVAL_API void ForceNextBonusStage(ACireGameMode* Mode, int32 Tier = -1);
    /** Rare spawns / bonus waves started this match (server; tests and soaks). */
    CIRESTEAMSURVIVAL_API void SpecialCounts(const ACireGameMode* Mode, int32& Rares, int32& BonusWaves);
    /** Rare roll for a wave (deterministic per match seed and wave number). Appends the rare row when it hits. */
    CIRESTEAMSURVIVAL_API bool RollRare(const FCireWaveConfig& Config, FCireWaveDef& Wave, int32 GlobalWave, int32 Seed, int32 RaresThisCycle, bool bForce = false);

    // ---- waves-modes: match schedule (consumed by feat/arena-flow; see Docs/RESUME-waves-modes.md) ----
    /** The live match schedule (the active preset's PvP override applied). World = null reads Waves.json. */
    CIRESTEAMSURVIVAL_API FCireMatchSchedule Schedule(const UWorld* World = nullptr);
    /** True when a PvP arena round follows global wave GlobalWave (1-based). */
    CIRESTEAMSURVIVAL_API bool IsPvpAfterWave(const UWorld* World, int32 GlobalWave);
    /** 1-based PvP round that follows GlobalWave (1..4 by default), 0 = none. */
    CIRESTEAMSURVIVAL_API int32 PvpRoundAfterWave(const UWorld* World, int32 GlobalWave);
    /** Global wave of the next PvP round at or after GlobalWave (0 = none left). */
    CIRESTEAMSURVIVAL_API int32 NextPvpWave(const UWorld* World, int32 GlobalWave);
    /** True for waves after the schedule's TotalWaves. */
    CIRESTEAMSURVIVAL_API bool IsSuddenDeath(const FCireWaveConfig& Config, int32 GlobalWave);
    /** Global wave number (1-based) of wave WaveInCycle (0-based) of Cycle (0-based). */
    CIRESTEAMSURVIVAL_API int32 GlobalWaveOf(const FCireWaveConfig& Config, int32 WaveInCycle, int32 Cycle);

    // ---- waves-modes: wave-type roll hook (feat/bonus-loot extends this) ----
    /** Called for every live wave just before it is queued: may replace the planned wave (a bonus loot stage or another
     *  special type). Default: returns Planned unchanged. Rules: never replace a Boss wave; deterministic per Seed and
     *  GlobalWave. Tests call it directly. */
    CIRESTEAMSURVIVAL_API FCireWaveDef RollWaveType(const FCireWaveConfig& Config, const FCireWaveDef& Planned, int32 GlobalWave, int32 Seed);

    // ---- waves-modes: armored traits, live scale, damage toggle ----
    /** Movement multiplier for a wave monster: Monsters.Speed x Live.Speed (x ArmoredSpeed when armored). 1 otherwise. */
    CIRESTEAMSURVIVAL_API float SpeedFactor(const ACireMonster* Monster);
    /** Armored wave units ignore slows (CireCrowdControl::Slow and the movement slow factor). */
    CIRESTEAMSURVIVAL_API bool IsSlowImmune(const ACireMonster* Monster);
    /** Stun duration multiplier (armored: 2x). 1 for everything else. */
    CIRESTEAMSURVIVAL_API float StunMultiplier(const AActor* Target);
    /** Damage-off wave unit: marches to the castle and never attacks (CireNPCCombat marcher branch). */
    CIRESTEAMSURVIVAL_API bool IsPassive(const ACireMonster* Monster);
    /** Live scale: validated, stored on the runtime config and rescaled onto living wave units. */
    CIRESTEAMSURVIVAL_API bool SetLiveScale(ACireGameMode* Mode, const FCireWaveScale& Scale, FString* Error = nullptr);

    // ---- waves-modes: presets / game types (Content/Data/WavePresets.json) ----
    CIRESTEAMSURVIVAL_API FString PresetsPath();
    /** Shipped presets: Standard, Hero TD/PvP, Hybrid. */
    CIRESTEAMSURVIVAL_API TArray<FCireWavePreset> BuiltInPresets();
    CIRESTEAMSURVIVAL_API bool ParsePresets(const FString& Json, TArray<FCireWavePreset>& Out, FString& Error);
    CIRESTEAMSURVIVAL_API FString PresetsToJson(const TArray<FCireWavePreset>& Presets);
    /** All presets (file, else built-ins; built-ins missing from the file are added). Cached; bReload re-reads. */
    CIRESTEAMSURVIVAL_API const TArray<FCireWavePreset>& Presets(bool bReload = false);
    CIRESTEAMSURVIVAL_API const FCireWavePreset* FindPreset(FName Id);
    /** Writes the preset (per-wave damage / fight-back, scale, pack modifier, PvP override) into Config. */
    CIRESTEAMSURVIVAL_API void ApplyPreset(FCireWaveConfig& Config, const FCireWavePreset& Preset);
    /** Captures Config's current settings as a preset. */
    CIRESTEAMSURVIVAL_API FCireWavePreset CapturePreset(const FCireWaveConfig& Config, FName Id, const FString& Label, const FString& Description = FString());
    /** Adds or replaces a preset (by Id) and saves the file (Path empty = PresetsPath()). */
    CIRESTEAMSURVIVAL_API bool SavePreset(const FCireWavePreset& Preset, FString* Error = nullptr, const FString& Path = FString());
    /** Host: select the match's game type (before the first wave). Replicates on ACireGameState::WavePreset. */
    CIRESTEAMSURVIVAL_API bool SelectPreset(ACireGameMode* Mode, FName Id, FString* Error = nullptr);
    /** game-profiles: an in-memory game type (probe fixtures); listed by Presets() after the file's. */
    CIRESTEAMSURVIVAL_API void RegisterRuntimePreset(const FCireWavePreset& Preset);

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
