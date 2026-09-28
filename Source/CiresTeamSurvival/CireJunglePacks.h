#pragma once
// jungle-packs: challenge packs as "jungle mobs" (Docs/JunglePacks.md). A pack has a TIER (1..4: how many abilities each
// monster uses, and its stat / reward curve), a PACK TYPE (a monster race, or Mixed) and a COMPOSITION of 3..8 monsters
// (pack-formations): 1..3 tanks, 1..3 healers and 1..6 DPS (melee, physical ranged or ranged caster), in a preset formation. The default composition is generated deterministically from the pack's seed,
// type and tier; the layout editor can override the counts (clamped to the rules).
//
// Data: Content/Data/JunglePacks.json (built-in fallback). The monster pool is every combat archetype the wave system can
// spawn (Races.json units, Bestiary.json race variants and rare creatures), grouped by race, each unit in one pack role.
// Units whose own kit is short of the tier-4 floor borrow abilities from their race (MergeInto, logged in the audit), so
// every unit fills T3 (5) and T4 (complete kit >= kitFloor). Borrowed abilities are flagged and never used by waves.
#include "CoreMinimal.h"

struct FCireNPCArchetype;
struct FCireNPCDatabase;
class FJsonValue;
class ACireMonster;
class ACireGameMode;

/** Pack role of a monster. Dps is the composition's "any DPS" slot (its kind is drawn from the race's DPS units); a
 *  unit's own role (RoleOf) is always Tank, Healer, Melee, Ranged (physical) or Caster (ranged caster). */
enum class ECirePackRole : uint8 { Tank = 0, Healer = 1, Dps = 2, Melee = 3, Ranged = 4, Caster = 5 };

/** pack-formations: 3..8 monsters. Dps = DPS of any kind (automatic), Melee / Ranged / Casters = DPS of that kind. */
struct CIRESTEAMSURVIVAL_API FCirePackComposition
{
    int32 Tanks = 1, Healers = 1, Dps = 1, Melee = 0, Ranged = 0, Casters = 0;
    FCirePackComposition() = default;
    FCirePackComposition(int32 InTanks, int32 InHealers, int32 InDps, int32 InMelee = 0, int32 InRanged = 0, int32 InCasters = 0)
        : Tanks(InTanks), Healers(InHealers), Dps(InDps), Melee(InMelee), Ranged(InRanged), Casters(InCasters) {}
    int32 DpsTotal() const { return Dps + Melee + Ranged + Casters; }
    int32 Total() const { return Tanks + Healers + DpsTotal(); }
    bool IsZero() const { return Tanks <= 0 && Healers <= 0 && DpsTotal() <= 0; }
    int32 Count(ECirePackRole Role) const { return const_cast<FCirePackComposition*>(this)->CountRef(Role); }
    int32& CountRef(ECirePackRole Role)
    {
        switch (Role)
        {
        case ECirePackRole::Tank: return Tanks;
        case ECirePackRole::Healer: return Healers;
        case ECirePackRole::Dps: return Dps;
        case ECirePackRole::Melee: return Melee;
        case ECirePackRole::Ranged: return Ranged;
        default: return Casters;
        }
    }
    bool operator==(const FCirePackComposition& O) const
    { return Tanks == O.Tanks && Healers == O.Healers && Dps == O.Dps && Melee == O.Melee && Ranged == O.Ranged && Casters == O.Casters; }
    bool operator!=(const FCirePackComposition& O) const { return !(*this == O); }
};

/** pack-formations: one slot of a preset formation, in formation units (1 = about one monster spacing): Forward toward
 *  the pack's facing (the front, where the tanks stand), Right to the pack's right. Role is Tank, Healer or Dps. */
struct CIRESTEAMSURVIVAL_API FCireFormationSlot
{
    ECirePackRole Role = ECirePackRole::Dps;
    float Forward = 0.f, Right = 0.f;
};

/** pack-formations: challenge-mob stats (JunglePacks.json "stats"). Health = tankHealth x the role's share x the tier's
 *  health x globalHealth (x the leader multiplier); damage = baseDamage x the tier's damage x globalDamage. */
struct CIRESTEAMSURVIVAL_API FCirePackStats
{
    float BaseDamage = 200.f, TankHealth = 1500.f;
    float HealerShare = .5f, MeleeShare = .75f, RangedShare = .65f, CasterShare = .65f;
    float GlobalHealth = 1.f, GlobalDamage = 1.f;
};

/** One tier of the jungle curve (JunglePacks.json "tiers"). */
struct CIRESTEAMSURVIVAL_API FCirePackTier
{
    int32 Tier = 1;
    /** Abilities each monster uses (-1 = the complete kit). */
    int32 Abilities = 2;
    /** pack-formations: per-tier health / damage multipliers on the challenge-mob stats (FCirePackStats), and the gold one. */
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
    /** pack-formations: challenge-mob stats and the preset formations by pack size (index 3..8; JunglePacks.json "formations"). */
    FCirePackStats Stats;
    TArray<FCireFormationSlot> Formations[9];
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
    constexpr int32 MinTier = 1, MaxTier = 4, MinSize = 3, MaxSize = 8;
    constexpr int32 MinTanks = 1, MaxTanks = 3, MinHealers = 1, MaxHealers = 3, MinDps = 1, MaxDps = 6;
    extern CIRESTEAMSURVIVAL_API const FName Mixed;

    // ---- rules (world-free) ----------------------------------------------------------------------------------------
    CIRESTEAMSURVIVAL_API const FCireJungleRules& Rules();
    CIRESTEAMSURVIVAL_API FCireJungleRules BuiltInRules();
    /** pack-formations: live edit of the stats and per-tier health / damage (F8 / layout editor). Rescales every living pack
     *  monster (health fraction kept). SaveStats writes them into Content/Data/JunglePacks.json. */
    CIRESTEAMSURVIVAL_API void SetStats(UWorld* World, const FCirePackStats& Stats, const float TierHealth[4], const float TierDamage[4]);
    CIRESTEAMSURVIVAL_API bool SaveStats(FString* Error = nullptr);
    /** Health / damage of a pack unit of Role (its own role: Tank, Healer, Melee, Ranged, Caster) at Tier. */
    CIRESTEAMSURVIVAL_API float UnitHealth(ECirePackRole Role, int32 Tier, bool bLeader);
    CIRESTEAMSURVIVAL_API float UnitDamage(ECirePackRole Role, int32 Tier);
    CIRESTEAMSURVIVAL_API bool IsDps(ECirePackRole Role);
    /** "[t,h,d]" (automatic DPS only) or "[t,h,d,melee,ranged,caster]"; CompFromJson reads either (3 or 6 numbers). */
    CIRESTEAMSURVIVAL_API FString CompJson(const FCirePackComposition& C);
    CIRESTEAMSURVIVAL_API bool CompFromJson(const TArray<TSharedPtr<FJsonValue>>& Values, FCirePackComposition& Out);
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
    /** Each count clamped to its range, then the total brought down to 8 (DPS first: any, melee, ranged, casters; then
     *  healers, then tanks). At least 1 tank, 1 healer and 1 DPS always. */
    CIRESTEAMSURVIVAL_API FCirePackComposition Clamp(const FCirePackComposition& C);
    /** The default composition of a pack: deterministic for (Seed, Type, Tier); always valid; grows with the tier. */
    CIRESTEAMSURVIVAL_API FCirePackComposition DefaultComposition(uint32 Seed, FName Type, int32 Tier);
    /** Override (all zero = automatic) resolved against the default. */
    CIRESTEAMSURVIVAL_API FCirePackComposition Resolve(const FCirePackComposition* Override, uint32 Seed, FName Type, int32 Tier);
    /** "T3 Drowned Deep: 2 tank . 1 healer . 3 DPS (1 caster) . 5 abilities each". */
    CIRESTEAMSURVIVAL_API FString Summary(int32 Tier, FName Type, const FCirePackComposition& C);
    /** Seed of a pack from its realm-local position (twins share it). */
    CIRESTEAMSURVIVAL_API uint32 SeedFor(const FVector2D& Local);

    // ---- the monster pool (needs the NPC database) -----------------------------------------------------------------
    /** Pack role of an archetype: Tank role -> tank; owns a HealAlly ability -> healer; Caster / Support -> caster DPS;
     *  Ranged -> ranged (physical) DPS; else melee DPS. */
    CIRESTEAMSURVIVAL_API ECirePackRole RoleOf(const FCireNPCArchetype& Archetype);
    CIRESTEAMSURVIVAL_API const TCHAR* RoleName(ECirePackRole Role);
    /** Every pack-eligible unit (bosses and non-combat creatures excluded), grouped by race (creatures: their family). */
    CIRESTEAMSURVIVAL_API TArray<FCirePackUnit> Inventory();
    /** Units of a type and role (Dps: every DPS kind). Mixed: every race's units. A DPS kind the race lacks falls back to
     *  the race's other DPS; bOutStandIn: the race has none at all, so a cross-race stand-in pool (Mixed) is returned. */
    CIRESTEAMSURVIVAL_API TArray<FName> Pool(FName Type, ECirePackRole Role, bool* bOutStandIn = nullptr);
    /** The archetypes of a pack, tanks first, then healers, then DPS (deterministic for the seed). OutRoles: the role each
     *  member fills (its RoleOf, or Caster for a caster body in a caster-DPS slot). */
    CIRESTEAMSURVIVAL_API TArray<FName> Members(uint32 Seed, FName Type, const FCirePackComposition& C, TArray<ECirePackRole>* OutRoles = nullptr);
    /** Non-basic abilities of the unit's complete kit (own + borrowed). */
    CIRESTEAMSURVIVAL_API int32 KitSize(const FCireNPCArchetype& Archetype);
    /** The abilities a pack unit uses at a tier: its own kit first (core, then the seeded order), then borrowed ones. */
    CIRESTEAMSURVIVAL_API TArray<FName> TierLoadout(const FCireNPCArchetype& Archetype, int32 Tier, int32 Seed, bool bNoHeals = false);
    /** pack-formations: a caster body (role caster / support): it can fill a ranged-caster DPS slot even when it also heals
     *  (then it fights without its heals). The roster's casters nearly all heal, so this is where caster DPS come from. */
    CIRESTEAMSURVIVAL_API bool IsCasterBody(const FCireNPCArchetype& Archetype);
    /** Abilities a unit filling Role uses at Tier (a healer body in a caster-DPS slot drops its heals). */
    CIRESTEAMSURVIVAL_API int32 LoadoutCount(const FCireNPCArchetype& Archetype, int32 Tier, ECirePackRole Role);
    /** The pack role a spawned pack monster fills (recorded by ApplyTier; else its archetype's RoleOf). */
    CIRESTEAMSURVIVAL_API ECirePackRole PackRoleOf(const ACireMonster* Monster);
    /** Called by CireNPCArchetypes::Reload after Races.json and Bestiary.json: tops every pack unit's kit up to the floor
     *  from its race (flagged borrowed) and records the audit. */
    CIRESTEAMSURVIVAL_API void MergeInto(FCireNPCDatabase& Database);
    /** Audit lines of the last merge: fills and stand-ins. */
    CIRESTEAMSURVIVAL_API const TArray<FString>& Audit();

    // ---- runtime (server) ------------------------------------------------------------------------------------------
    /** pack-formations: the preset formation of a pack of Roles.Num() members (JunglePacks.json "formations", sizes 3..8):
     *  each member's pack-local offset in cm (X forward = the facing, Y right). Members take a slot of their role class
     *  (tank / healer / DPS) first, leftovers the free slots front first. Spacing grows with the pack radius. */
    CIRESTEAMSURVIVAL_API TArray<FVector2D> FormationOffsets(const TArray<ECirePackRole>& Roles, float Radius);
    /** Centimetres per formation unit for a pack of Radius. */
    CIRESTEAMSURVIVAL_API float FormationSpacing(float Radius);
    /** pack-formations: world yaw a pack faces: toward the nearest point of its realm's monster paths (the path players and
     *  waves come along); a pack sitting on the path faces the hero base. */
    CIRESTEAMSURVIVAL_API float FacingYaw(const UWorld* World, int32 Team, const FVector& Center);
    /** Start async loads of the bodies (mesh + role clips) of every unit the realm's packs can draw, so a pack of a race
     *  not seen yet does not hitch the frame it spawns in (server and clients; each archetype once). */
    CIRESTEAMSURVIVAL_API void PrewarmBodies(const UWorld* World);
    /** Apply the tier: ability loadout (TierLoadout), the challenge-mob stats (UnitHealth / UnitDamage) and the numeral. */
    CIRESTEAMSURVIVAL_API void ApplyTier(ACireMonster* Monster, int32 Tier, int32 Seed, bool bLeader = false, TOptional<ECirePackRole> Role = {});

#if !UE_BUILD_SHIPPING
    /** -CireJungleProbe (Tools/RunJunglePackProbe.py): Eric's committed layout spawns T1..T4 through the real schedule, then
     *  40 packs of mixed tiers, types and sizes (3..8) in both realms, checked. */
    CIRESTEAMSURVIVAL_API void InitializeProbe(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API bool TickProbe(ACireGameMode* Mode, float DeltaSeconds);
    /** Native tests (part of CireRouteEditor::RunTests): tiers, compositions, pool, loadouts. */
    CIRESTEAMSURVIVAL_API bool RunTests(TArray<FString>& Failures);
#endif
}
