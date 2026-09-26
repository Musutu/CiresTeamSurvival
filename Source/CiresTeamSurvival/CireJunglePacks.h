#pragma once
// jungle-packs: challenge packs as "jungle mobs" (Docs/JunglePacks.md). A pack has a TIER (1..4: how many abilities each
// monster uses, and its stat / reward curve), a PACK TYPE (a monster race, or Mixed) and a COMPOSITION of 3..6 monsters:
// 1..2 tanks, 1..2 healers and 1..3 DPS. The default composition is generated deterministically from the pack's seed,
// type and tier; the layout editor can override the counts (clamped to the rules).
//
// Data: Content/Data/JunglePacks.json (built-in fallback). The monster pool is every combat archetype the wave system can
// spawn (Races.json units, Bestiary.json race variants and rare creatures), grouped by race, each unit in one pack role.
// Units whose own kit is short of the tier-4 floor borrow abilities from their race (MergeInto, logged in the audit), so
// every unit fills T3 (5) and T4 (complete kit >= kitFloor). Borrowed abilities are flagged and never used by waves.
#include "CoreMinimal.h"

struct FCireNPCArchetype;
struct FCireNPCDatabase;
class ACireMonster;
class ACireGameMode;

/** Pack role of a monster. */
enum class ECirePackRole : uint8 { Tank = 0, Healer = 1, Dps = 2 };

struct CIRESTEAMSURVIVAL_API FCirePackComposition
{
    int32 Tanks = 1, Healers = 1, Dps = 1;
    int32 Total() const { return Tanks + Healers + Dps; }
    int32 Count(ECirePackRole Role) const { return Role == ECirePackRole::Tank ? Tanks : Role == ECirePackRole::Healer ? Healers : Dps; }
    bool operator==(const FCirePackComposition& O) const { return Tanks == O.Tanks && Healers == O.Healers && Dps == O.Dps; }
    bool operator!=(const FCirePackComposition& O) const { return !(*this == O); }
};

/** One tier of the jungle curve (JunglePacks.json "tiers"). */
struct CIRESTEAMSURVIVAL_API FCirePackTier
{
    int32 Tier = 1;
    /** Abilities each monster uses (-1 = the complete kit). */
    int32 Abilities = 2;
    /** Multipliers on top of the challenge curve (health already grows with tier in Cires::ChallengeHealthMultiplier). */
    float Health = 1.f, Damage = 1.f, Gold = 1.f;
    /** When packs of this tier first appear (round = match cycle, wave within it). */
    int32 UnlockRound = 1, UnlockWave = 1;
};

struct CIRESTEAMSURVIVAL_API FCireJungleRules
{
    TArray<FCirePackTier> Tiers;
    /** Every pack unit's complete kit is topped up to at least this many non-basic abilities (T4 > T3). */
    int32 KitFloor = 6;
    /** The pack's first tank leads it (warlord colours, pack-leader bounty and loot) with this health multiplier. */
    float LeaderHealth = 1.5f;
    /** Creatures outside every race borrow from (and stand in for) this race. */
    TMap<FName, FName> Families;
    /** Archetypes never used in packs (non-combat bonus creatures). */
    TSet<FName> Excluded;
};

/** One entry of the pack pool (the inventory in Docs/JunglePacks.md). */
struct CIRESTEAMSURVIVAL_API FCirePackUnit
{
    FName Id, Race;
    ECirePackRole Role = ECirePackRole::Dps;
    /** Non-basic abilities the unit owns, and those borrowed to reach the kit floor. */
    int32 OwnKit = 0, Borrowed = 0;
    /** Bestiary creature (variant or rare) rather than a Races.json unit. */
    bool bCreature = false;
};

namespace CireJunglePacks
{
    constexpr int32 MinTier = 1, MaxTier = 4, MinSize = 3, MaxSize = 6;
    constexpr int32 MinTanks = 1, MaxTanks = 2, MinHealers = 1, MaxHealers = 2, MinDps = 1, MaxDps = 3;
    extern CIRESTEAMSURVIVAL_API const FName Mixed;

    // ---- rules (world-free) ----------------------------------------------------------------------------------------
    CIRESTEAMSURVIVAL_API const FCireJungleRules& Rules();
    CIRESTEAMSURVIVAL_API FCireJungleRules BuiltInRules();
    CIRESTEAMSURVIVAL_API bool ParseRules(const FString& Json, FCireJungleRules& Out, FString& Error);
    CIRESTEAMSURVIVAL_API const FCirePackTier& TierRules(int32 Tier);
    CIRESTEAMSURVIVAL_API int32 ClampTier(int32 Tier);
    /** Abilities per monster at a tier for a unit whose complete kit has KitSize non-basic abilities (T4: the whole kit). */
    CIRESTEAMSURVIVAL_API int32 AbilityCount(int32 Tier, int32 KitSize);
    /** "2", "3", "5", "full kit". */
    CIRESTEAMSURVIVAL_API FString AbilityLabel(int32 Tier);

    /** Pack types in editor order: the races (Races.json raceOrder), then Mixed. */
    CIRESTEAMSURVIVAL_API TArray<FName> PackTypes();
    /** A known race id or Mixed; anything else (empty, unknown) reads as Mixed. */
    CIRESTEAMSURVIVAL_API FName NormalizeType(const FString& Type);
    CIRESTEAMSURVIVAL_API FString TypeLabel(FName Type);        // "Drowned Deep", "Mixed"
    CIRESTEAMSURVIVAL_API int32 TypeIndex(FName Type);          // index into PackTypes() (Mixed = last)
    CIRESTEAMSURVIVAL_API FName TypeAt(int32 Index);

    CIRESTEAMSURVIVAL_API bool IsValid(const FCirePackComposition& C);
    /** Each count clamped to its range, then the total brought down to 6 (DPS first, then healers, then tanks). */
    CIRESTEAMSURVIVAL_API FCirePackComposition Clamp(const FCirePackComposition& C);
    /** The default composition of a pack: deterministic for (Seed, Type, Tier); always valid; grows with the tier. */
    CIRESTEAMSURVIVAL_API FCirePackComposition DefaultComposition(uint32 Seed, FName Type, int32 Tier);
    /** Override (all zero = automatic) resolved against the default. */
    CIRESTEAMSURVIVAL_API FCirePackComposition Resolve(const FCirePackComposition* Override, uint32 Seed, FName Type, int32 Tier);
    /** "T3 Drowned Deep: 2 tank . 1 healer . 3 DPS . 5 abilities each". */
    CIRESTEAMSURVIVAL_API FString Summary(int32 Tier, FName Type, const FCirePackComposition& C);
    /** Seed of a pack from its realm-local position (twins share it). */
    CIRESTEAMSURVIVAL_API uint32 SeedFor(const FVector2D& Local);

    // ---- the monster pool (needs the NPC database) -----------------------------------------------------------------
    /** Pack role of an archetype: Tank role -> tank; owns a HealAlly ability -> healer; else DPS. */
    CIRESTEAMSURVIVAL_API ECirePackRole RoleOf(const FCireNPCArchetype& Archetype);
    CIRESTEAMSURVIVAL_API const TCHAR* RoleName(ECirePackRole Role);
    /** Every pack-eligible unit (bosses and non-combat creatures excluded), grouped by race (creatures: their family). */
    CIRESTEAMSURVIVAL_API TArray<FCirePackUnit> Inventory();
    /** Units of a type and role. Mixed: every race's units. bOutStandIn: the race has none, so a cross-race stand-in pool
     *  (Mixed of that role) is returned and flagged. */
    CIRESTEAMSURVIVAL_API TArray<FName> Pool(FName Type, ECirePackRole Role, bool* bOutStandIn = nullptr);
    /** The archetypes of a pack, tanks first, then healers, then DPS (deterministic for the seed). */
    CIRESTEAMSURVIVAL_API TArray<FName> Members(uint32 Seed, FName Type, const FCirePackComposition& C, TArray<ECirePackRole>* OutRoles = nullptr);
    /** Non-basic abilities of the unit's complete kit (own + borrowed). */
    CIRESTEAMSURVIVAL_API int32 KitSize(const FCireNPCArchetype& Archetype);
    /** The abilities a pack unit uses at a tier: its own kit first (core, then the seeded order), then borrowed ones. */
    CIRESTEAMSURVIVAL_API TArray<FName> TierLoadout(const FCireNPCArchetype& Archetype, int32 Tier, int32 Seed);
    /** Called by CireNPCArchetypes::Reload after Races.json and Bestiary.json: tops every pack unit's kit up to the floor
     *  from its race (flagged borrowed) and records the audit. */
    CIRESTEAMSURVIVAL_API void MergeInto(FCireNPCDatabase& Database);
    /** Audit lines of the last merge: fills and stand-ins. */
    CIRESTEAMSURVIVAL_API const TArray<FString>& Audit();

    // ---- runtime (server) ------------------------------------------------------------------------------------------
    /** Formation slot of member Index (Count members, the role order of Members) inside a pack of Radius (realm-local
     *  offset from the centre): tanks in front, DPS on the flanks, healers behind. */
    CIRESTEAMSURVIVAL_API FVector2D FormationOffset(int32 Index, const TArray<ECirePackRole>& Roles, float Radius, uint32 Seed);
    /** Start async loads of the bodies (mesh + role clips) of every unit the realm's packs can draw, so a pack of a race
     *  not seen yet does not hitch the frame it spawns in (server and clients; each archetype once). */
    CIRESTEAMSURVIVAL_API void PrewarmBodies(const UWorld* World);
    /** Apply the tier: ability loadout (TierLoadout), damage multiplier and the skill tier numeral. */
    CIRESTEAMSURVIVAL_API void ApplyTier(ACireMonster* Monster, int32 Tier, int32 Seed);

#if !UE_BUILD_SHIPPING
    /** -CireJungleProbe (Tools/RunJunglePackProbe.py): 40 packs of mixed tiers and types in both realms, checked. */
    CIRESTEAMSURVIVAL_API void InitializeProbe(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API bool TickProbe(ACireGameMode* Mode, float DeltaSeconds);
    /** Native tests (part of CireRouteEditor::RunTests): tiers, compositions, pool, loadouts. */
    CIRESTEAMSURVIVAL_API bool RunTests(TArray<FString>& Failures);
#endif
}
