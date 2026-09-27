#pragma once
// outdoor-bosses: the WORLD BOSSES of the open town (Eric, playtest 5, 2026-09-26: "the boss markers i placed should be
// OUTDOOR bosses that are always there, each marker is a different boss"). Docs/OutdoorBosses.md.
//
// Every Boss Spawn marker of MapLayout.json holds one persistent race boss in each realm (the same boss on a marker and on
// its mirrored twin), spawned at match start. Outdoor bosses live in the open world like the jungle packs: NEUTRAL until a
// player attacks them (the challenge-elite ruling; bots and wave monsters never engage them), a boss bounty (10x the mob
// value, Eric's gold ruling) and boss-grade personal loot (the lane-boss table plus the challenge-pack completion table).
// They are leashed to their marker by the existing leash (MonsterLeash.json, the boss radius): kited past it they evade
// home, immune and regenerating, keeping their threat. A dead boss comes back after respawnSeconds (0 = stays dead).
//
// Which boss sits on which marker: the marker's own "kind" (the layout editor's inspector: OUTDOOR BOSS < >), else
// Content/Data/OutdoorBosses.json "byMarker" (marker name -> boss id), else the "roster" in marker order.
//
// Wave bosses and the Rare Spawn / Bonus Loot creatures are unchanged; with outdoor bosses on, wave bosses no longer spawn
// at the Boss markers (those are the outdoor bosses' lairs) but at their wave's spawn ("waveBossesAtMarkers": false).
#include "CoreMinimal.h"

class ACireGameMode;
class ACireMonster;
struct FCireRouteSpot;
struct FCireBattlefieldRoutes;
struct FCireMapLayout;
struct FCireMapMarker;

struct CIRESTEAMSURVIVAL_API FCireOutdoorBossRules
{
    bool bEnabled = true;
    /** Seconds before a slain outdoor boss returns to its marker (<= 0: it stays dead for the match). */
    float RespawnSeconds = 300.f;
    /** false: wave bosses spawn at their wave's monster spawn (the Boss markers are the outdoor bosses' lairs). */
    bool bWaveBossesAtMarkers = false;
    /** Strength: configured like a wave boss of at least this global wave (the live wave when later), times the multipliers. */
    int32 StrengthWave = 12;
    float HealthMultiplier = 1.5f, DamageMultiplier = 1.f;
    /** Challenge tier of the boss (the nameplate T#, and the tier the pack-completion loot table rolls at). */
    int32 LootTier = 4;
    /** Multiplier on the boss bounty (Eric: a boss is worth 10x the current mob value). */
    float GoldMultiplier = 1.f;
    /** Marker name ("Boss 1") -> boss archetype id, and the fallback order for markers without one. */
    TMap<FString, FName> ByMarker;
    TArray<FName> Roster;
};

namespace CireOutdoorBosses
{
    // ---- data ----
    CIRESTEAMSURVIVAL_API const FCireOutdoorBossRules& Rules();
    CIRESTEAMSURVIVAL_API bool Reload(FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireOutdoorBossRules& Out, FString& Error);
    /** Tests: use these rules instead of the file (nullptr: back to OutdoorBosses.json). */
    CIRESTEAMSURVIVAL_API void OverrideRules(const FCireOutdoorBossRules* Rules);
    /** Every race boss (Races.json warlord and colossus of each race, in race order): the ids a marker may hold. */
    CIRESTEAMSURVIVAL_API TArray<FName> BossIds();
    CIRESTEAMSURVIVAL_API bool IsBossId(FName Id);
    CIRESTEAMSURVIVAL_API FString BossName(FName Id);
    /** The boss of a marker: its kind, else byMarker[name], else roster[index] (index = its order among the realm's boss markers). */
    CIRESTEAMSURVIVAL_API FName Resolve(const FString& Kind, const FString& MarkerName, int32 Index, const FCireOutdoorBossRules& Rules);
    CIRESTEAMSURVIVAL_API FName ResolveSpot(const FCireBattlefieldRoutes& Routes, int32 Realm, int32 Index);
    /** The layout editor's view of a Boss Spawn marker: the boss it will hold (same order as the compile), and whether the
     *  marker chose it itself (false: OutdoorBosses.json picked it). */
    CIRESTEAMSURVIVAL_API FName ResolveMarker(const FCireMapLayout& Layout, const FCireMapMarker& Marker, bool* bOutOwn = nullptr);

    // ---- identity ----
    /** Outdoor bosses use their own pack-id block (round 0, never used by the jungle packs): realm x 50000 + 40000 + index. */
    CIRESTEAMSURVIVAL_API int32 PackIdFor(int32 Realm, int32 Index);
    CIRESTEAMSURVIVAL_API bool IsOutdoorPackId(int32 PackId);
    CIRESTEAMSURVIVAL_API int32 IndexOf(int32 PackId);
    /** Valid on every peer (PackId replicates). */
    CIRESTEAMSURVIVAL_API bool IsOutdoorBoss(const ACireMonster* Monster);

    // ---- match runtime (server) ----
    /** Spawns every marker's boss in both realms (those already alive are kept). Returns how many were spawned. */
    CIRESTEAMSURVIVAL_API int32 SpawnAll(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API ACireMonster* SpawnOne(ACireGameMode* Mode, int32 Realm, int32 Index);
    /** A boss died: its respawn timer starts (or it stays dead). */
    CIRESTEAMSURVIVAL_API void OnKilled(ACireGameMode* Mode, ACireMonster* Monster);
    /** Respawn timers. */
    CIRESTEAMSURVIVAL_API void Tick(ACireGameMode* Mode, float DeltaSeconds);
    /** Forget every timer (a layout restart spawns them all again). */
    CIRESTEAMSURVIVAL_API void Reset(ACireGameMode* Mode);
    /** Seconds until the boss of this pack id returns (< 0: alive, or it stays dead). */
    CIRESTEAMSURVIVAL_API float RespawnIn(const ACireGameMode* Mode, int32 PackId);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
    /** -CireOutdoorBossProbe (Tools/RunOutdoorBossProbe.py): Eric's layout on the town, both realms. */
    CIRESTEAMSURVIVAL_API void InitializeProbe(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API void TickProbe(ACireGameMode* Mode, float DeltaSeconds);
#endif
}
