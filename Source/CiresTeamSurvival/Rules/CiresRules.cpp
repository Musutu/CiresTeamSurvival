#include "CiresRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace Cires
{
namespace
{
class StableRandom
{
public:
    explicit StableRandom(std::uint64_t seed) : State(seed) {}
    std::uint64_t Next()
    {
        std::uint64_t value = (State += UINT64_C(0x9e3779b97f4a7c15));
        value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
        return value ^ (value >> 31);
    }
    std::uint64_t Bounded(std::uint64_t bound)
    {
        if (bound < 2) return 0;
        const std::uint64_t threshold = (UINT64_C(0) - bound) % bound;
        std::uint64_t value;
        do { value = Next(); } while (value < threshold);
        return value % bound;
    }
    bool Chance(double chance)
    {
        return static_cast<double>(Bounded(1000000)) / 1000000.0 < chance;
    }
private:
    std::uint64_t State;
};

template <typename T>
void Shuffle(std::vector<T>& values, StableRandom& random)
{
    for (std::size_t i = values.size(); i > 1; --i)
    {
        const auto other = static_cast<std::size_t>(random.Bounded(i));
        std::swap(values[i - 1], values[other]);
    }
}

double Nonnegative(double value)
{
    return std::isfinite(value) ? std::max(0.0, value) : 0.0;
}

bool ValidKind(SkillKind kind)
{
    return kind == SkillKind::Active || kind == SkillKind::Passive || kind == SkillKind::Ultimate;
}

bool ValidProgression(const Progression& progression)
{
    if (progression.DraftRole != SkillDraftRole::Any && progression.DraftRole != SkillDraftRole::Tank &&
        progression.DraftRole != SkillDraftRole::Damage && progression.DraftRole != SkillDraftRole::Support) return false;
    if ((progression.SecondaryRoles & ~RoleAll) != 0 ||
        (progression.DraftRole == SkillDraftRole::Any && progression.SecondaryRoles != RoleNone)) return false;
    const auto count = static_cast<int>(progression.LearnedSkills.size());
    if (count > MaxSkills || progression.Level < 1 ||
        progression.NextAugmentLevel != BreakpointForSkill(count) ||
        (count > 0 && progression.Level < BreakpointForSkill(count - 1)) || progression.Stats.Strength < 0 ||
        progression.Stats.Agility < 0 || progression.Stats.Intelligence < 0 ||
        (progression.Primary != PrimaryStat::Strength &&
         progression.Primary != PrimaryStat::Agility &&
         progression.Primary != PrimaryStat::Intelligence)) return false;
    std::set<std::string> ids;
    for (const auto& skill : progression.LearnedSkills)
    {
        if (skill.Id.empty() || !ValidKind(skill.Kind) ||
            !ids.insert(skill.Id).second) return false;
    }
    return CountSkills(progression, SkillKind::Active) <= MaxActiveSkills &&
        CountSkills(progression, SkillKind::Passive) <= MaxPassives &&
        CountSkills(progression, SkillKind::Ultimate) <= MaxUltimates;
}

bool AlreadyLearned(const Progression& progression, const std::string& id)
{
    return std::any_of(progression.LearnedSkills.begin(), progression.LearnedSkills.end(),
        [&](const SkillDefinition& skill) { return skill.Id == id; });
}

// Any is the legacy unrestricted catalog mode (custom fixture pools included);
// every explicit role set filters through the authoritative tag table.
bool AllowedForProgression(const Progression& progression, const std::string& id)
{
    return progression.DraftRole == SkillDraftRole::Any ||
        IsSkillAllowedForRoles(id, EffectiveRoleMask(progression));
}

int Capacity(SkillKind kind)
{
    return kind == SkillKind::Active ? MaxActiveSkills :
        kind == SkillKind::Passive ? MaxPassives : MaxUltimates;
}

bool HasCapacity(const Progression& progression, SkillKind kind)
{
    return ValidKind(kind) && CountSkills(progression, kind) < Capacity(kind);
}

bool OnlyPassiveRemains(const Progression& progression)
{
    return !HasCapacity(progression, SkillKind::Active) &&
        !HasCapacity(progression, SkillKind::Ultimate) &&
        HasCapacity(progression, SkillKind::Passive);
}
} // namespace

DerivedStats CalculateStats(const StatBlock& stats, PrimaryStat primary,
                           const CombatTuning& tuning)
{
    const double strength = std::max(0, stats.Strength);
    const double agility = std::max(0, stats.Agility);
    const double intelligence = std::max(0, stats.Intelligence);
    const double damageStat = primary == PrimaryStat::Agility ? agility :
        primary == PrimaryStat::Intelligence ? intelligence : strength;
    DerivedStats result;
    result.MaxHealth = Nonnegative(tuning.BaseHealth) + strength * 25.0;
    result.MaxMana = Nonnegative(tuning.BaseMana) + intelligence * 30.0;
    result.MaxEnergy = Nonnegative(tuning.MaxEnergy);
    result.AttackSpeedMultiplier = 1.0 + agility * 0.01;
    result.BasicAttackDamage = Nonnegative(tuning.WeaponDamage) + damageStat;
    result.CooldownMultiplier = 1.0 - std::clamp(
        Nonnegative(tuning.PureCooldownReduction), 0.0, MaxCooldownReduction);
    return result;
}

double CooldownSeconds(double baseSeconds, double pureCooldownReduction)
{
    return Nonnegative(baseSeconds) * (1.0 - std::clamp(
        Nonnegative(pureCooldownReduction), 0.0, MaxCooldownReduction));
}

bool GainLevels(Progression& progression, int count)
{
    if (!ValidProgression(progression) || count <= 0) return false;
    const auto delta = static_cast<std::int64_t>(count);
    const auto limit = std::numeric_limits<int>::max();
    const auto strength = static_cast<std::int64_t>(progression.Stats.Strength) +
        delta * (progression.Primary == PrimaryStat::Strength ? 2 : 1);
    const auto agility = static_cast<std::int64_t>(progression.Stats.Agility) +
        delta * (progression.Primary == PrimaryStat::Agility ? 2 : 1);
    const auto intelligence = static_cast<std::int64_t>(progression.Stats.Intelligence) +
        delta * (progression.Primary == PrimaryStat::Intelligence ? 2 : 1);
    if (static_cast<std::int64_t>(progression.Level) + delta > limit ||
        strength > limit || agility > limit || intelligence > limit) return false;
    progression.Level += count;
    progression.Stats = {static_cast<int>(strength), static_cast<int>(agility),
                         static_cast<int>(intelligence)};
    return true;
}

bool HasPassive(const Progression& progression)
{
    return CountSkills(progression, SkillKind::Passive) > 0;
}

bool HasUltimate(const Progression& progression)
{
    return CountSkills(progression, SkillKind::Ultimate) > 0;
}

int CountSkills(const Progression& progression, SkillKind kind)
{
    return static_cast<int>(std::count_if(progression.LearnedSkills.begin(), progression.LearnedSkills.end(),
        [kind](const SkillDefinition& skill) { return skill.Kind == kind; }));
}

bool HasPendingAugment(const Progression& progression)
{
    return ValidProgression(progression) &&
        progression.LearnedSkills.size() < static_cast<std::size_t>(MaxSkills) &&
        progression.Level >= progression.NextAugmentLevel;
}

bool AugmentOffer::IsValid() const
{
    if (!Error.empty() || BreakpointLevel < 1 || (BreakpointLevel != 1 && BreakpointLevel % 3 != 0) ||
        Choices.size() != 4) return false;
    std::set<std::string> ids;
    for (const auto& choice : Choices)
        if (choice.Id.empty() || !ValidKind(choice.Kind) ||
            !ids.insert(choice.Id).second) return false;
    return true;
}

AugmentOffer GenerateAugmentOffer(const Progression& progression,
                                 const std::vector<SkillDefinition>& pool,
                                 std::uint64_t seed)
{
    AugmentOffer offer;
    if (!HasPendingAugment(progression))
    {
        offer.Error = "No unclaimed skill breakpoint.";
        return offer;
    }
    offer.BreakpointLevel = progression.NextAugmentLevel;
    if (IsOpeningOffer(progression))
    {
        // Opening point: four actives from the primary role's opening pool, never passives/ultimates.
        std::vector<SkillDefinition> opening;
        std::set<std::string> seen;
        for (const auto& skill : pool)
        {
            if (skill.Id.empty() || !ValidKind(skill.Kind) || !seen.insert(skill.Id).second)
            {
                offer.Error = "Skill catalog has an empty, duplicate, or invalid definition.";
                return offer;
            }
            if (skill.Kind == SkillKind::Active && IsOpeningSkill(skill.Id, progression.DraftRole)) opening.push_back(skill);
        }
        if (opening.size() < 4)
        {
            offer.Error = "Need four opening actives for this role.";
            return offer;
        }
        std::sort(opening.begin(), opening.end(), [](const SkillDefinition& a, const SkillDefinition& b) { return a.Id < b.Id; });
        StableRandom openingRandom(seed);
        Shuffle(opening, openingRandom);
        offer.Choices.assign(opening.begin(), opening.begin() + 4);
        return offer;
    }
    std::vector<SkillDefinition> active;
    std::vector<SkillDefinition> passive;
    std::set<std::string> ids;
    for (const auto& skill : pool)
    {
        if (skill.Id.empty() || !ValidKind(skill.Kind) || !ids.insert(skill.Id).second)
        {
            offer.Error = "Skill catalog has an empty, duplicate, or invalid definition.";
            return offer;
        }
        if (!AllowedForProgression(progression, skill.Id) ||
            AlreadyLearned(progression, skill.Id) || !HasCapacity(progression, skill.Kind)) continue;
        (skill.Kind == SkillKind::Passive ? passive : active).push_back(skill);
    }
    const auto byId = [](const SkillDefinition& a, const SkillDefinition& b)
        { return a.Id < b.Id; };
    std::sort(active.begin(), active.end(), byId);
    std::sort(passive.begin(), passive.end(), byId);
    StableRandom random(seed);
    if (OnlyPassiveRemains(progression))
    {
        if (passive.size() < 4)
        {
            offer.Error = "Need four unlearned passives for the final passive slot.";
            return offer;
        }
        Shuffle(passive, random);
        offer.Choices.insert(offer.Choices.end(), passive.begin(), passive.begin() + 4);
        return offer;
    }
    int passiveCount = 0;
    if (!HasPassive(progression))
    {
        const bool canOfferOne = !passive.empty() && active.size() >= 3;
        const bool canOfferTwo = passive.size() >= 2 && active.size() >= 2;
        if (!canOfferOne && !canOfferTwo)
        {
            offer.Error = "Need four unlearned skills including one or two passives.";
            return offer;
        }
        passiveCount = canOfferOne && canOfferTwo ?
            1 + static_cast<int>(random.Bounded(2)) : (canOfferTwo ? 2 : 1);
    }
    if (active.size() < static_cast<std::size_t>(4 - passiveCount))
    {
        offer.Error = "Need four unlearned skills from categories with remaining capacity.";
        return offer;
    }
    Shuffle(active, random);
    Shuffle(passive, random);
    offer.Choices.insert(offer.Choices.end(), active.begin(), active.begin() + (4 - passiveCount));
    offer.Choices.insert(offer.Choices.end(), passive.begin(), passive.begin() + passiveCount);
    Shuffle(offer.Choices, random);
    return offer;
}

bool LearnSkill(Progression& progression, const AugmentOffer& offer,
                const std::string& selectedId)
{
    if (!HasPendingAugment(progression) || !offer.IsValid() ||
        offer.BreakpointLevel != progression.NextAugmentLevel) return false;
    const bool passiveOnly = OnlyPassiveRemains(progression);
    const bool opening = IsOpeningOffer(progression);
    std::set<std::string> ids;
    const SkillDefinition* selected = nullptr;
    int passives = 0;
    for (const auto& skill : offer.Choices)
    {
        if (skill.Id.empty() || !AllowedForProgression(progression, skill.Id) ||
            !HasCapacity(progression, skill.Kind) || AlreadyLearned(progression, skill.Id) ||
            !ids.insert(skill.Id).second) return false;
        if (opening && (skill.Kind != SkillKind::Active || !IsOpeningSkill(skill.Id, progression.DraftRole))) return false;
        if (skill.Kind == SkillKind::Passive) ++passives;
        if (skill.Id == selectedId) selected = &skill;
    }
    if (!selected || (opening ? passives != 0 : passiveOnly ? passives != 4 :
        (HasPassive(progression) ? passives != 0 : (passives < 1 || passives > 2))))
        return false;
    progression.LearnedSkills.push_back(*selected);
    progression.NextAugmentLevel = BreakpointForSkill(static_cast<int>(progression.LearnedSkills.size()));
    return true;
}

MatchClock::MatchClock(PhaseDurations durations) : Durations(durations)
{
    if (!std::isfinite(Durations.Intermission) || Durations.Intermission <= 0) Durations.Intermission = 60;
    if (!std::isfinite(Durations.Arena) || Durations.Arena <= 0) Durations.Arena = 90;
    if (!std::isfinite(Durations.Recovery) || Durations.Recovery <= 0) Durations.Recovery = 15;
    // Keep malformed development settings from causing unbounded transition loops.
    Durations.Intermission = std::max(1.0, Durations.Intermission);
    Durations.Arena = std::max(1.0, Durations.Arena);
    Durations.Recovery = std::max(1.0, Durations.Recovery);
}

double MatchClock::Duration() const
{
    switch (Current)
    {
    case MatchPhase::Intermission: return Durations.Intermission;
    case MatchPhase::Arena: return Durations.Arena;
    case MatchPhase::Recovery: return Durations.Recovery;
    default: return 0;
    }
}

bool MatchClock::SetDurations(PhaseDurations durations)
{
    const auto Valid=[](double value){return std::isfinite(value)&&value>=1.0&&value<=3600.0;};
    if(!Valid(durations.Intermission)||!Valid(durations.Arena)||!Valid(durations.Recovery))return false;
    Durations=durations;
    return true;
}

double MatchClock::RemainingSeconds() const
{
    return Current == MatchPhase::Survival ? -1.0 : std::max(0.0, Duration() - Elapsed);
}

std::vector<PhaseEvent> MatchClock::Advance(double seconds)
{
    std::vector<PhaseEvent> events;
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400 ||
        Current == MatchPhase::Finished || Current == MatchPhase::Survival) return events;
    if (seconds + 1.0e-9 >= RemainingSeconds())
    {
        const auto previous = Current;
        const int previousRound = RoundNumber;
        bool timedOut = false;
        if (Current == MatchPhase::Intermission) Current = MatchPhase::Arena;
        else if (Current == MatchPhase::Arena)
        {
            Current = MatchPhase::Recovery;
            timedOut = true;
        }
        else
        {
            Current = MatchPhase::Survival;
            ++RoundNumber;
        }
        Elapsed = 0.0;
        events.push_back({previous, Current, previousRound, timedOut});
    }
    else Elapsed += seconds;
    return events;
}

bool MatchClock::BeginIntermission()
{
    if (Current != MatchPhase::Survival) return false;
    Current = MatchPhase::Intermission;
    Elapsed = 0;
    return true;
}

bool MatchClock::ResolveArena()
{
    if (Current != MatchPhase::Arena) return false;
    Current = MatchPhase::Recovery;
    Elapsed = 0;
    return true;
}

void MatchClock::Finish()
{
    Current = MatchPhase::Finished;
    Elapsed = 0;
}

void AwardArenaWin(TeamRewards& rewards)
{
    rewards.ArenaWins = std::clamp(rewards.ArenaWins, 0, 1000000) + 1;
    rewards.PowerMultiplier = 1.0 + 0.03 * std::min(rewards.ArenaWins, 4);
    rewards.LootMultiplier = 1.0 + 0.08 * std::min(rewards.ArenaWins, 5);
}

int RemainingLivesAfterLeak(int currentLives, bool boss)
{
    return std::max(0, std::max(0, currentLives) - (boss ? 10 : 1));
}

ChallengeReward RollChallengeReward(int tier, double lootMultiplier, std::uint64_t seed)
{
    tier = std::clamp(tier, 1, 10);
    lootMultiplier = std::clamp(Nonnegative(lootMultiplier), 1.0, 1.4);
    StableRandom random(seed);
    ChallengeReward reward;
    const double variance = 0.9 + static_cast<double>(random.Bounded(21)) * 0.01;
    reward.Gold = static_cast<int>(std::lround((35 + tier * 25) * lootMultiplier * variance));
    reward.Experience = 100 + tier * 75;
    reward.GreaterStatTome = random.Chance(std::min(0.70, (0.15 + tier * 0.035) * lootMultiplier));
    reward.StatTomePoints = reward.GreaterStatTome ? 2 + tier / 3 : 1;
    reward.RareDrop = random.Chance(std::min(0.35, tier * 0.025 * lootMultiplier));
    return reward;
}

double ChallengeHealthMultiplier(int tier, int round)
{
    tier = std::clamp(tier, 1, 10);
    round = std::clamp(round, 1, 100);
    return (1.0 + 0.65 * (tier - 1)) * (1.0 + 0.12 * (round - 1));
}

double ChallengeDamageMultiplier(int tier, int round)
{
    tier = std::clamp(tier, 1, 10);
    round = std::clamp(round, 1, 100);
    return (1.0 + 0.40 * (tier - 1)) * (1.0 + 0.08 * (round - 1));
}

std::vector<SkillDefinition> StarterSkillPool()
{
    return {
        {"iron_guard", "Iron Guard", SkillKind::Active},
        {"shield_slam", "Shield Slam", SkillKind::Active},
        {"war_cry", "War Cry", SkillKind::Active},
        {"chain_spark", "Chain Spark", SkillKind::Active},
        {"ember_lance", "Ember Lance", SkillKind::Active},
        {"venom_ground", "Venom Ground", SkillKind::Active},
        {"cinder_cone", "Cinder Cone", SkillKind::Active},
        {"grave_line", "Grave Line", SkillKind::Active},
        {"ashen_square", "Ashen Ward", SkillKind::Active},
        {"blight_sigil", "Blight Sigil", SkillKind::Active},

        {"frost_bind", "Frost Bind", SkillKind::Active},
        {"cleaving_strike", "Cleaving Strike", SkillKind::Active},
        {"piercing_shot", "Piercing Shot", SkillKind::Active},
        {"shadow_step", "Shadow Step", SkillKind::Active},
        {"restoring_light", "Restoring Light", SkillKind::Active},
        {"sanctuary", "Sanctuary", SkillKind::Active},
        {"purify", "Purify", SkillKind::Active},
        {"summoned_wall", "Runestone Wall", SkillKind::Active},
        {"protection_dome", "Aegis Dome", SkillKind::Active},
        {"oathbound_guardian", "Oathbound Guardian", SkillKind::Active},
        {"spectral_pack", "Spectral Pack", SkillKind::Active},
        {"second_wind", "Second Wind", SkillKind::Active},
        {"stone_skin", "Stone Skin", SkillKind::Passive},
        {"battle_rhythm", "Battle Rhythm", SkillKind::Passive},
        {"deep_reserves", "Deep Reserves", SkillKind::Passive},
        {"soul_conduit", "Soul Conduit", SkillKind::Passive},
        {"bastion_of_dawn", "Bastion of Dawn", SkillKind::Ultimate},
        {"cataclysm", "Cataclysm", SkillKind::Ultimate},
        {"executioners_verdict", "Executioner's Verdict", SkillKind::Ultimate},
        {"renewal", "Renewal", SkillKind::Ultimate},
        {"last_stand", "Last Stand", SkillKind::Ultimate},
        {"challenge_of_iron", "Challenge of Iron", SkillKind::Ultimate},
        {"seismic_reprisal", "Seismic Reprisal", SkillKind::Ultimate},
        {"starfall", "Starfall", SkillKind::Ultimate},
        {"spectral_hunt", "Spectral Hunt", SkillKind::Ultimate},
        {"mass_aegis", "Mass Aegis", SkillKind::Ultimate},
        {"wellspring", "Wellspring", SkillKind::Ultimate}
    };
}

const char* DraftRoleName(SkillDraftRole role)
{
    switch (role)
    {
    case SkillDraftRole::Tank: return "Tank";
    case SkillDraftRole::Damage: return "DPS";
    case SkillDraftRole::Support: return "Support / Healer";
    default: return "Unrestricted";
    }
}

RoleMask SkillRoleTags(const std::string& id)
{
    // One table is the single source of truth for draft filtering, the HUD role
    // badges, and the AstraAbilities.json "roles" mirror (checked by tests).
    // Tank healing stays self-only; ally heals/cleanses are Support-only; DPS
    // receives no monster taunts. Passives are universal so every role keeps
    // four alternatives for the final passive-only offer.
    struct Entry { const char* Id; RoleMask Roles; };
    static const Entry table[] = {
        {"iron_guard", RoleAll}, {"shield_slam", RoleTank}, {"war_cry", RoleTank},
        {"chain_spark", RoleDamage | RoleSupport}, {"ember_lance", RoleDamage | RoleSupport},
        {"venom_ground", RoleDamage}, {"cinder_cone", RoleDamage}, {"grave_line", RoleDamage},
        {"ashen_square", RoleDamage}, {"blight_sigil", RoleDamage},
        {"frost_bind", RoleAll}, {"cleaving_strike", RoleTank | RoleDamage},
        {"piercing_shot", RoleDamage}, {"shadow_step", RoleTank | RoleDamage},
        {"restoring_light", RoleSupport}, {"sanctuary", RoleSupport}, {"purify", RoleSupport},
        {"summoned_wall", RoleAll}, {"protection_dome", RoleAll}, {"oathbound_guardian", RoleAll},
        {"spectral_pack", RoleDamage}, {"second_wind", RoleAll},
        {"stone_skin", RoleAll}, {"battle_rhythm", RoleAll}, {"deep_reserves", RoleAll}, {"soul_conduit", RoleAll},
        {"bastion_of_dawn", RoleTank | RoleSupport}, {"cataclysm", RoleDamage},
        {"executioners_verdict", RoleDamage}, {"renewal", RoleSupport},
        {"last_stand", RoleTank}, {"challenge_of_iron", RoleTank}, {"seismic_reprisal", RoleTank},
        {"starfall", RoleDamage}, {"spectral_hunt", RoleDamage},
        {"mass_aegis", RoleSupport}, {"wellspring", RoleSupport}};
    for (const auto& entry : table)
        if (id == entry.Id) return entry.Roles;
    return RoleNone;
}

int BreakpointForSkill(int learnedCount)
{
    return learnedCount <= 0 ? 1 : 3 * learnedCount;
}

bool IsOpeningOffer(const Progression& progression)
{
    return progression.LearnedSkills.empty();
}

bool IsOpeningSkill(const std::string& id, SkillDraftRole primary)
{
    // Role-defining actives. Tanks: threat, guard, cleave, wall. Supports: heals,
    // cleanse and the protective dome. DPS: every damage-tagged active that is not
    // universal. Legacy "Any" progressions may open with any non-ultimate active.
    static const std::set<std::string> tank = {"shield_slam", "war_cry", "iron_guard", "cleaving_strike", "summoned_wall"};
    static const std::set<std::string> support = {"restoring_light", "sanctuary", "purify", "protection_dome"};
    if (primary == SkillDraftRole::Any) return !id.empty();
    const RoleMask tags = SkillRoleTags(id);
    if (tags == RoleNone) return false;
    switch (primary)
    {
    case SkillDraftRole::Tank: return tank.count(id) != 0;
    case SkillDraftRole::Support: return support.count(id) != 0;
    case SkillDraftRole::Damage: return (tags & RoleDamage) != 0 && tags != RoleAll;
    default: return false;
    }
}

std::vector<SkillDefinition> OpeningSkillPool(SkillDraftRole primary)
{
    auto pool = StarterSkillPool();
    pool.erase(std::remove_if(pool.begin(), pool.end(), [primary](const SkillDefinition& skill)
        { return skill.Kind != SkillKind::Active || !IsOpeningSkill(skill.Id, primary); }), pool.end());
    return pool;
}

namespace Traits
{
double OutgoingDamageMultiplier(SkillDraftRole primary)
{
    return primary == SkillDraftRole::Support ? SupportDamageMultiplier : 1.0;
}
double AttackSpeedBonus(SkillDraftRole primary)
{
    return primary == SkillDraftRole::Support ? SupportAttackSpeedBonus : 0.0;
}
double ApplyIncomingFlatReduction(double amount, SkillDraftRole primary)
{
    if (!std::isfinite(amount) || amount <= 0) return 0.0;
    return primary == SkillDraftRole::Tank ? std::max(0.0, amount - TankFlatReduction) : amount;
}
double BaseCriticalChance(SkillDraftRole primary, double tuningBase)
{
    const double base = std::isfinite(tuningBase) ? std::clamp(tuningBase, 0.0, 1.0) : 0.0;
    return primary == SkillDraftRole::Damage ? std::max(base, DpsBaseCriticalChance) : base;
}
double MendingHealAmount(double damageDealt, SkillDraftRole primary)
{
    if (primary != SkillDraftRole::Support || !std::isfinite(damageDealt) || damageDealt <= 0) return 0.0;
    return damageDealt * SupportMendingShare;
}
int SelectMendingTarget(const std::vector<PartyMember>& party)
{
    int best = -1;
    double bestFraction = 0, bestHealth = 0;
    for (int i = 0; i < static_cast<int>(party.size()); ++i)
    {
        const auto& m = party[i];
        if (!m.Alive || !std::isfinite(m.Health) || !std::isfinite(m.MaxHealth) || m.MaxHealth <= 0 || m.Health <= 0) continue;
        const double fraction = std::min(1.0, m.Health / m.MaxHealth);
        const bool better = best < 0 || fraction < bestFraction - 1e-12 ||
            (std::abs(fraction - bestFraction) <= 1e-12 && (m.Health < bestHealth - 1e-9 ||
             (std::abs(m.Health - bestHealth) <= 1e-9 && m.Id < party[best].Id)));
        if (better) { best = i; bestFraction = fraction; bestHealth = m.Health; }
    }
    return best;
}
} // namespace Traits

RoleMask RoleBit(SkillDraftRole role)
{
    switch (role)
    {
    case SkillDraftRole::Tank: return RoleTank;
    case SkillDraftRole::Damage: return RoleDamage;
    case SkillDraftRole::Support: return RoleSupport;
    case SkillDraftRole::Any: return RoleAll;
    default: return RoleNone;
    }
}

RoleMask EffectiveRoleMask(const Progression& progression)
{
    if (progression.DraftRole == SkillDraftRole::Any) return RoleAll;
    return static_cast<RoleMask>((RoleBit(progression.DraftRole) | progression.SecondaryRoles) & RoleAll);
}

bool IsSkillAllowedForRoles(const std::string& id, RoleMask roles)
{
    return (SkillRoleTags(id) & roles & RoleAll) != 0;
}

bool IsSkillAllowedForRole(const std::string& id, SkillDraftRole role)
{
    // Any keeps legacy "unrestricted" semantics, including for unknown IDs.
    if (role == SkillDraftRole::Any) return true;
    return IsSkillAllowedForRoles(id, RoleBit(role));
}

std::vector<SkillDefinition> StarterSkillPoolForRoles(RoleMask roles)
{
    auto pool = StarterSkillPool();
    pool.erase(std::remove_if(pool.begin(), pool.end(), [roles](const SkillDefinition& skill)
        { return !IsSkillAllowedForRoles(skill.Id, roles); }), pool.end());
    return pool;
}

std::vector<SkillDefinition> StarterSkillPool(SkillDraftRole role)
{
    auto pool = StarterSkillPool();
    pool.erase(std::remove_if(pool.begin(), pool.end(), [role](const SkillDefinition& skill)
        { return !IsSkillAllowedForRole(skill.Id, role); }), pool.end());
    return pool;
}
} // namespace Cires
