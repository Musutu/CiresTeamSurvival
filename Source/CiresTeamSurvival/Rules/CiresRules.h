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
    int NextAugmentLevel = 3;
    std::vector<SkillDefinition> LearnedSkills;
};

// Basic attack is innate and separate from the eight learned slots.
constexpr int MaxActiveSkills = 6;
constexpr int MaxPassives = 1;
constexpr int MaxUltimates = 1;
constexpr int MaxSkills = MaxActiveSkills + MaxPassives + MaxUltimates;
constexpr double MaxCooldownReduction = 0.60;
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
std::vector<SkillDefinition> StarterSkillPoolForRoles(RoleMask roles);
} // namespace Cires
