#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Engine-independent rules. Call mutations from the authoritative game server.
namespace Cires
{
enum class PrimaryStat : std::uint8_t { Strength, Agility, Intelligence };
enum class SkillKind : std::uint8_t { Active, Passive, Ultimate };
enum class SkillDraftRole : std::uint8_t { Any, Tank, Damage, Support };
// Skill role tags and champion role sets are bit masks so hybrids/cross-class
// skills can belong to several roles. RoleNone is never a valid tag set.
using RoleMask = std::uint8_t;
constexpr RoleMask RoleNone = 0;
constexpr RoleMask RoleTank = 1;
constexpr RoleMask RoleDamage = 2;
constexpr RoleMask RoleSupport = 4;
constexpr RoleMask RoleAll = RoleTank | RoleDamage | RoleSupport;
enum class MatchPhase : std::uint8_t { Survival = 0, Intermission = 1, Arena = 2, Finished = 3, Recovery = 4 };

struct StatBlock
{
    int Strength = 10;
    int Agility = 10;
    int Intelligence = 10;
};

// Strength scaling (Eric's ruling, 25 September 2026): each STR point gives 10 maximum
// health (it was 25) plus 0.1 armor and 0.1 spell ward. Level-1 health is kept: every
// champion gets a flat base of (25 - 10) x its authored starting STR, so only the
// per-point growth changed. See Docs/Progression.md "Strength scaling".
constexpr double HealthPerStrength = 10.0;
constexpr double ArmorPerStrength = 0.1;
constexpr double WardPerStrength = 0.1;
constexpr double LegacyHealthPerStrength = 25.0;
// Flat base health that preserves the old level-1 health for a champion drafted with this STR.
double StartingBaseHealth(int startingStrength);

struct CombatTuning
{
    double WeaponDamage = 10.0;
    double BaseHealth = 0.0;
    double BaseMana = 0.0;
    double MaxEnergy = 100.0;
    double PureCooldownReduction = 0.0;
};

struct DerivedStats
{
    double MaxHealth = 0.0;
    double MaxMana = 0.0;
    double MaxEnergy = 0.0;
    double Armor = 0.0;   // from STR only; items and auras add on top in the damage path
    double Ward = 0.0;
    double AttackSpeedMultiplier = 1.0;
    double BasicAttackDamage = 0.0;
    double CooldownMultiplier = 1.0;
};

DerivedStats CalculateStats(const StatBlock& stats, PrimaryStat primary,
                           const CombatTuning& tuning = {});
double CooldownSeconds(double baseSeconds, double pureCooldownReduction);

struct SkillDefinition
{
    std::string Id;
    std::string Name;
    SkillKind Kind = SkillKind::Active;
};

struct Progression
{
    int Level = 1;
    PrimaryStat Primary = PrimaryStat::Strength;
    SkillDraftRole DraftRole = SkillDraftRole::Any;
    // Additional roles a hybrid champion fills (subset of RoleAll). Offers draw
    // from every skill tagged with the primary role or any secondary role.
    RoleMask SecondaryRoles = RoleNone;
    StatBlock Stats;
    // Flat health on top of STR x HealthPerStrength, set once at draft from the
    // starting STR (StartingBaseHealth); level growth never changes it.
    double BaseHealth = 0.0;
    // Every champion starts with one skill point: the opening choice is due at level 1.
    int NextAugmentLevel = 1;
    std::vector<SkillDefinition> LearnedSkills;
};

// Basic attack is innate and separate from the eight learned slots.
constexpr int MaxActiveSkills = 6;
constexpr int MaxPassives = 1;
constexpr int MaxUltimates = 1;
constexpr int MaxSkills = MaxActiveSkills + MaxPassives + MaxUltimates;
constexpr double MaxCooldownReduction = 0.60;
// Level at which the Nth learned skill (0-based) is offered: 1 (opening point), then 3, 6 ... 21.
int BreakpointForSkill(int learnedCount);
// The opening offer (no skills learned yet) is four ACTIVE, non-ultimate skills from
// the champion's PRIMARY role only (hybrids open in their primary role).
bool IsOpeningSkill(const std::string& id, SkillDraftRole primary);
bool IsOpeningOffer(const Progression& progression);
bool GainLevels(Progression& progression, int count = 1);
bool HasPassive(const Progression& progression);
bool HasUltimate(const Progression& progression);
int CountSkills(const Progression& progression, SkillKind kind);
bool HasPendingAugment(const Progression& progression);

struct AugmentOffer
{
    int BreakpointLevel = 0;
    std::vector<SkillDefinition> Choices;
    std::string Error;
    // Always four unique choices, including four passives when only the passive
    // slot remains. LearnSkill checks category capacity against progression.
    bool IsValid() const;
};

// The seed and sorted IDs produce stable offers across standard libraries.
// Insufficient pools fail explicitly; categories at capacity are never offered.
// Only a final passive-only slot allows four passives instead of one or two.
AugmentOffer GenerateAugmentOffer(const Progression& progression,
                                 const std::vector<SkillDefinition>& pool,
                                 std::uint64_t seed);
// Pass the server-stored offer. Never trust an offer supplied by a client.
bool LearnSkill(Progression& progression, const AugmentOffer& offer,
                const std::string& selectedId);

struct PhaseDurations
{
    double Intermission = 60.0;
    double Arena = 90.0;
    double Recovery = 15.0;
};

struct PhaseEvent
{
    MatchPhase From;
    MatchPhase To;
    int Round;
    bool ArenaTimedOut = false;
};

class MatchClock
{
public:
    explicit MatchClock(PhaseDurations durations = {});
    MatchPhase Phase() const { return Current; }
    int Round() const { return RoundNumber; }
    // Survival is objective-driven and returns -1, never a countdown.
    double RemainingSeconds() const;
    PhaseDurations GetDurations() const { return Durations; }
    // Transactional live tuning: preserve phase, round and elapsed time. A
    // duration shortened below elapsed transitions through the next Advance.
    bool SetDurations(PhaseDurations durations);
    // Rejects negative/nonfinite deltas and deltas over one day. Crosses at most
    // one timed boundary per call, so hitches cannot skip phase side effects.
    std::vector<PhaseEvent> Advance(double seconds);
    // The authoritative wave controller calls this only after the cycle clears.
    bool BeginIntermission();
    // Arena resolution gives both teams a recovery break before the next cycle.
    bool ResolveArena();
    void Finish();
private:
    double Duration() const;
    PhaseDurations Durations;
    MatchPhase Current = MatchPhase::Survival;
    double Elapsed = 0.0;
    int RoundNumber = 1;
};

struct TeamRewards
{
    int ArenaWins = 0;
    double PowerMultiplier = 1.0;
    double LootMultiplier = 1.0;
};

// Persistent match rewards: +3% power / +8% loot per win, capped at +12% / +40%.
void AwardArenaWin(TeamRewards& rewards);

// Ordinary creeps remove one life; bosses remove ten. Zero is terminal.
int RemainingLivesAfterLeak(int currentLives, bool boss);

struct ChallengeReward
{
    int Gold = 0;
    int Experience = 0;
    int StatTomePoints = 0;
    bool GreaterStatTome = false;
    bool RareDrop = false;
};

// tier 1..10; loot multiplier 1..1.4. Reward rolls are deterministic.
ChallengeReward RollChallengeReward(int tier, double lootMultiplier,
                                    std::uint64_t seed);
double ChallengeHealthMultiplier(int tier, int round);
double ChallengeDamageMultiplier(int tier, int round);

// Starter pool intentionally contains more than six distinct active abilities.
std::vector<SkillDefinition> StarterSkillPool();
// Any is reserved for catalog inspection/legacy rules fixtures. Runtime heroes
// use one explicit bucket; supportive classes share the Support/Healer bucket.
std::vector<SkillDefinition> StarterSkillPool(SkillDraftRole role);
bool IsSkillAllowedForRole(const std::string& id, SkillDraftRole role);
const char* DraftRoleName(SkillDraftRole role);
// Authoritative per-skill role tags. Unknown IDs return RoleNone (fail closed).
RoleMask SkillRoleTags(const std::string& id);
RoleMask RoleBit(SkillDraftRole role);
// Any -> RoleAll (catalog inspection); otherwise primary bit | secondary roles.
RoleMask EffectiveRoleMask(const Progression& progression);
bool IsSkillAllowedForRoles(const std::string& id, RoleMask roles);
std::vector<SkillDefinition> OpeningSkillPool(SkillDraftRole primary);

// Class baseline traits, by main (primary) role. Engine code applies them through
// the authoritative damage / stat pipeline; these functions are the maths.
namespace Traits
{
    constexpr double SupportDamageMultiplier = 0.80;  // Support: -20% damage to enemies
    constexpr double SupportAttackSpeedBonus = 0.10;  // Support: +10% attack speed
    constexpr double SupportMendingShare = 0.50;      // Support: 50% of damage dealt heals the lowest ally
    constexpr double TankFlatReduction = 10.0;        // Tank: -10 per incoming instance, after armor, floor 0
    constexpr double DpsBaseCriticalChance = 0.10;    // DPS: 10% base critical chance
    double OutgoingDamageMultiplier(SkillDraftRole primary);
    double AttackSpeedBonus(SkillDraftRole primary);
    // Tank natural defense, applied last (after guard, armor and spell ward). Nonfinite -> 0.
    double ApplyIncomingFlatReduction(double amount, SkillDraftRole primary);
    double BaseCriticalChance(SkillDraftRole primary, double tuningBase);
    double MendingHealAmount(double damageDealt, SkillDraftRole primary);
    struct PartyMember { int Id = 0; double Health = 0; double MaxHealth = 0; bool Alive = true; };
    // Lowest health fraction among living members (MaxHealth > 0), the healer included.
    // Ties go to the lowest absolute health, then the lowest Id. -1 when nobody qualifies.
    int SelectMendingTarget(const std::vector<PartyMember>& party);
}
std::vector<SkillDefinition> StarterSkillPoolForRoles(RoleMask roles);

// Per-level ability scaling with no level cap (Content/Data/Abilities.json "curve").
// Effect grows logarithmically (diminishing, unbounded unless EffectCap > 0), costs
// rise toward a capped multiplier, cooldowns decay toward a floor fraction and an
// absolute minimum. Level 1 always equals the base values.
namespace Abilities
{
    struct Curve
    {
        double EffectGrowth = 0.35;         // effect x (1 + g ln(1 + (L-1)/h))
        double EffectHalfLevels = 4.0;      // h
        double EffectCap = 0.0;             // absolute effect cap (0 = none), e.g. 30 for a 30% reduction
        double CostCapMultiplier = 1.5;     // cost -> base x cap as L -> infinity
        double CostRampLevels = 10.0;       // cost x (1 + (cap-1)(L-1)/(L-1+ramp))
        double CooldownFloorFraction = 0.6; // cooldown -> base x floor as L -> infinity
        double CooldownDecayLevels = 15.0;  // cooldown x (floor + (1-floor) e^-((L-1)/decay))
        double MinCooldownSeconds = 1.0;    // absolute cooldown floor (skills with a base cooldown)
    };
    struct Base { double Effect = 0, ManaCost = 0, EnergyCost = 0, Cooldown = 0, CastTime = 0; };
    struct LevelStats { int Level = 1; double Effect = 0, ManaCost = 0, EnergyCost = 0, Cooldown = 0, CastTime = 0; };
    bool ValidCurve(const Curve& curve);
    bool ValidBase(const Base& base);
    // level < 1 is treated as 1; invalid inputs return the base unchanged.
    LevelStats Scale(const Base& base, const Curve& curve, int level);
}

// Crowd control, heal cuts and executes (server authority; engine: CireCrowdControl).
namespace CC
{
    constexpr double DiminishingWindowSeconds = 18.0;
    // PvP chain-CC: 1st full, 2nd half, 3rd quarter, then immune until the window lapses.
    double DiminishedDuration(double baseSeconds, int priorApplicationsInWindow);
    enum class VoidZone { None, Inner, Outer };
    // Inner circle (distance <= inner) stuns; the ring (inner < distance <= outer) slows.
    VoidZone ClassifyVoidZone(double distance, double innerRadius, double outerRadius);
    // Cuts are fractions 0..1 (0.5 = -50%); received and done cuts multiply.
    double ApplyHealingCut(double amount, double receivedCut, double doneCut);
    double ArmorAfterBreak(double armor, double breakFraction);
    enum class ExecuteTarget { Monster, Boss, Hero };
    constexpr double ExecutionerIntervalSeconds = 300.0;
    constexpr double ExecuteHeroMaxHealthFraction = 0.30;
    // Executioner / Decimating Strike damage: lethal to ordinary monsters, a normal hit
    // on bosses, and max(normal, 30% of max health) against champions (PvP).
    double ExecuteDamage(ExecuteTarget target, double targetHealth, double targetMaxHealth, double normalDamage);
}
// Dodge-roll synergy maths (engine: CireRollSkills).
namespace Roll
{
    constexpr int MaxMomentumStacks = 5;
    // Remaining cooldown after a roll cuts it by percent (0..100).
    double ReducedCooldown(double remainingSeconds, double percent);
    // Distance from point P to the segment AB in the ground plane (roll trails).
    double PointSegmentDistance2D(double px, double py, double ax, double ay, double bx, double by);
    double MomentumMultiplier(int stacks, double perStackPercent);
    // Roll recovery shortened by cutPercent (Shadow Dance): readyAt measured from startedAt.
    double ShortenedReadyAt(double startedAt, double readyAt, double cutPercent);
    // Blur: a roll in [0,1) dodges when below chancePercent/100.
    bool BlurDodges(double roll01, double chancePercent);
}
} // namespace Cires
