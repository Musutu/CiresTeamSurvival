#include "CiresRules.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string>

using namespace Cires;

namespace
{
int Assertions = 0;
int Failures = 0;

void Check(bool passed, const char* expression, int line)
{
    ++Assertions;
    if (!passed)
    {
        ++Failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)

bool Near(double left, double right) { return std::abs(left - right) < 1.0e-8; }

std::string Signature(const AugmentOffer& offer)
{
    std::string result;
    for (const auto& skill : offer.Choices) result += skill.Id + ";";
    return result;
}

int PassiveCount(const AugmentOffer& offer)
{
    return static_cast<int>(std::count_if(offer.Choices.begin(), offer.Choices.end(),
        [](const SkillDefinition& skill) { return skill.Kind == SkillKind::Passive; }));
}

// Learn the opening (level 1) skill so later offers follow the normal rules.
Progression Opened(Progression p, const std::vector<SkillDefinition>& pool, std::uint64_t seed)
{
    const auto offer = GenerateAugmentOffer(p, pool, seed);
    if (offer.IsValid()) LearnSkill(p, offer, offer.Choices[static_cast<std::size_t>(seed % offer.Choices.size())].Id);
    return p;
}

void StatRules()
{
    CombatTuning tuning;
    tuning.WeaponDamage = 7;
    tuning.PureCooldownReduction = 0.25;
    const auto stats = CalculateStats({12, 15, 20}, PrimaryStat::Intelligence, tuning);
    // str-scaling (Eric, 2026-09-25): 10 health, 0.1 armor and 0.1 ward per STR point.
    CHECK(Near(stats.MaxHealth, 120));
    CHECK(Near(stats.Armor, 1.2));
    CHECK(Near(stats.Ward, 1.2));
    CHECK(Near(StartingBaseHealth(20), 300));
    CHECK(Near(StartingBaseHealth(10), 150));
    CHECK(Near(StartingBaseHealth(-5), 0));
    {
        // Level-1 health is unchanged from the old 25-per-STR formula; only growth changed.
        for (const int startStrength : {10, 20})
        {
            CombatTuning champion;
            champion.BaseHealth = StartingBaseHealth(startStrength);
            CHECK(Near(CalculateStats({startStrength, 10, 10}, PrimaryStat::Strength, champion).MaxHealth, startStrength * 25.0));
        }
        Progression tank;
        tank.Primary = PrimaryStat::Strength;
        tank.Stats = {20, 10, 10};
        tank.BaseHealth = StartingBaseHealth(20);
        CHECK(GainLevels(tank, 24));
        CombatTuning tankTuning;
        tankTuning.BaseHealth = tank.BaseHealth;
        const auto level25 = CalculateStats(tank.Stats, tank.Primary, tankTuning);
        CHECK(tank.Stats.Strength == 68);
        CHECK(Near(level25.MaxHealth, 980));   // was 1700 at 25 per point
        CHECK(Near(level25.Armor, 6.8));
        CHECK(Near(level25.Ward, 6.8));
        CHECK(Near(tank.BaseHealth, 300));     // level growth never changes the flat base
    }
    CHECK(Near(stats.MaxMana, 600));
    CHECK(Near(stats.MaxEnergy, 100));
    CHECK(Near(stats.BasicAttackDamage, 27));
    CHECK(Near(stats.AttackSpeedMultiplier, 1.15));
    CHECK(Near(stats.CooldownMultiplier, 0.75));
    CHECK(Near(CooldownSeconds(20, 0.25), 15));
    CHECK(Near(CooldownSeconds(20, 10), 8));
    CHECK(Near(CooldownSeconds(20, -1), 20));
    CHECK(Near(CooldownSeconds(-20, 0.25), 0));
    CHECK(Near(CooldownSeconds(20, std::numeric_limits<double>::quiet_NaN()), 20));
    tuning.PureCooldownReduction = 0.6;
    CHECK(Near(CalculateStats({12, 15, 20}, PrimaryStat::Intelligence, tuning).BasicAttackDamage, 27));
    CHECK(Near(CalculateStats({12, 15, 20}, PrimaryStat::Strength, tuning).BasicAttackDamage, 19));
    CHECK(Near(CalculateStats({12, 15, 20}, PrimaryStat::Agility, tuning).BasicAttackDamage, 22));

    for (const auto primary : {PrimaryStat::Strength, PrimaryStat::Agility, PrimaryStat::Intelligence})
    {
        Progression progression;
        progression.Primary = primary;
        CHECK(GainLevels(progression, 2));
        CHECK(progression.Level == 3);
        CHECK(progression.Stats.Strength == (primary == PrimaryStat::Strength ? 14 : 12));
        CHECK(progression.Stats.Agility == (primary == PrimaryStat::Agility ? 14 : 12));
        CHECK(progression.Stats.Intelligence == (primary == PrimaryStat::Intelligence ? 14 : 12));
        CHECK(!GainLevels(progression, -1));
        CHECK(!GainLevels(progression, 0));
        CHECK(progression.Level == 3);
        CHECK(!GainLevels(progression, std::numeric_limits<int>::max()));
        CHECK(progression.Level == 3);
    }
}

void DraftRules()
{
    const auto pool = StarterSkillPool();
    // One starting skill point: the opening offer is due at level 1 (actives only).
    Progression start;
    CHECK(start.Level == 1 && HasPendingAugment(start));
    CHECK(BreakpointForSkill(0) == 1 && BreakpointForSkill(1) == 3 && BreakpointForSkill(2) == 6 && BreakpointForSkill(7) == 21);
    {
        auto reversedPool = pool;
        std::reverse(reversedPool.begin(), reversedPool.end());
        for (std::uint64_t seed = 0; seed < 300; ++seed)
        {
            const auto opening = GenerateAugmentOffer(start, pool, seed);
            CHECK(opening.IsValid() && opening.BreakpointLevel == 1 && opening.Choices.size() == 4);
            CHECK(Signature(opening) == Signature(GenerateAugmentOffer(start, reversedPool, seed)));
            for (const auto& option : opening.Choices) CHECK(option.Kind == SkillKind::Active);
        }
    }
    Progression initial = Opened(start, pool, 3);
    CHECK(initial.LearnedSkills.size() == 1 && initial.NextAugmentLevel == 3);
    CHECK(!HasPendingAugment(initial));
    CHECK(!GenerateAugmentOffer(initial, pool, 0).IsValid());
    CHECK(GainLevels(initial, 2));
    CHECK(HasPendingAugment(initial));
    int onePassiveOffers = 0;
    int twoPassiveOffers = 0;
    std::set<std::string> differentOffers;
    auto reversed = pool;
    std::reverse(reversed.begin(), reversed.end());
    for (std::uint64_t seed = 0; seed < 1000; ++seed)
    {
        const auto offer = GenerateAugmentOffer(initial, pool, seed);
        CHECK(offer.IsValid());
        CHECK(offer.BreakpointLevel == 3);
        CHECK(Signature(offer) == Signature(GenerateAugmentOffer(initial, pool, seed)));
        CHECK(Signature(offer) == Signature(GenerateAugmentOffer(initial, reversed, seed)));
        const auto passives = PassiveCount(offer);
        CHECK(passives == 1 || passives == 2);
        if (passives == 1) ++onePassiveOffers;
        else ++twoPassiveOffers;
        std::set<std::string> uniqueIds;
        for (const auto& skill : offer.Choices) uniqueIds.insert(skill.Id);
        CHECK(uniqueIds.size() == 4);
        differentOffers.insert(Signature(offer));
    }
    CHECK(onePassiveOffers > 350 && onePassiveOffers < 650);
    CHECK(twoPassiveOffers > 350 && twoPassiveOffers < 650);
    CHECK(differentOffers.size() > 500);

    // Every path must finish six regular skills, one passive and one ultimate.
    // Exercise random picks, postponing passive, and postponing ultimate. Also
    // validate every offered alternative, not just the path we happen to take.
    for (std::uint64_t seed = 0; seed < 512; ++seed)
    for (int strategy = 0; strategy < 3; ++strategy)
    {
        Progression progression;
        CHECK(GainLevels(progression, 23));
        for (int slot = 0; slot < MaxSkills; ++slot)
        {
            const auto offer = GenerateAugmentOffer(progression, pool, seed * 31 + slot);
            CHECK(offer.IsValid());
            if (!offer.IsValid()) break;
            CHECK(offer.BreakpointLevel == BreakpointForSkill(slot));
            CHECK(Signature(offer) == Signature(GenerateAugmentOffer(progression, reversed, seed * 31 + slot)));
            const auto passiveCount = PassiveCount(offer);
            const bool onlyPassive = CountSkills(progression, SkillKind::Active) == MaxActiveSkills && HasUltimate(progression);
            CHECK(slot == 0 ? passiveCount == 0 : onlyPassive ? passiveCount == 4 : (HasPassive(progression) ? passiveCount == 0 : (passiveCount >= 1 && passiveCount <= 2)));
            CHECK(offer.Choices.size() == 4);
            for (const auto& option : offer.Choices)
            {
                Progression alternate = progression;
                CHECK(LearnSkill(alternate, offer, option.Id));
            }
            auto selected = offer.Choices.begin() + static_cast<std::ptrdiff_t>((seed + slot) % offer.Choices.size());
            if (strategy != 0)
            {
                const auto postponed = strategy == 1 ? SkillKind::Passive : SkillKind::Ultimate;
                const auto preferable = std::find_if(offer.Choices.begin(), offer.Choices.end(),
                    [&](const SkillDefinition& skill) { return skill.Kind != postponed; });
                if (preferable != offer.Choices.end()) selected = preferable;
            }
            CHECK(!LearnSkill(progression, offer, "not_in_this_offer"));
            CHECK(LearnSkill(progression, offer, selected->Id));
            CHECK(!LearnSkill(progression, offer, selected->Id)); // replay must fail
        }
        CHECK(progression.LearnedSkills.size() == 8);
        CHECK(CountSkills(progression, SkillKind::Active) == 6);
        CHECK(CountSkills(progression, SkillKind::Passive) == 1);
        CHECK(CountSkills(progression, SkillKind::Ultimate) == 1);
        CHECK(!HasPendingAugment(progression));
        CHECK(!GenerateAugmentOffer(progression, pool, 100).IsValid());
        CHECK(GainLevels(progression, 30));
        CHECK(progression.Level == 54);
        CHECK(!HasPendingAugment(progression));
    }

    // Skip-level XP grants queue every missed breakpoint rather than dropping choices.
    Progression skipped;
    CHECK(GainLevels(skipped, 9));
    for (int breakpoint : {1, 3, 6, 9})
    {
        const auto offer = GenerateAugmentOffer(skipped, pool, static_cast<std::uint64_t>(breakpoint));
        CHECK(offer.BreakpointLevel == breakpoint);
        CHECK(LearnSkill(skipped, offer, offer.Choices.front().Id));
    }
    CHECK(!HasPendingAugment(skipped));
    CHECK(GainLevels(skipped, 2));
    CHECK(GenerateAugmentOffer(skipped, pool, 42).BreakpointLevel == 12);

    std::vector<SkillDefinition> minimumPool = {
        {"a", "A", SkillKind::Active}, {"b", "B", SkillKind::Active},
        {"p", "P", SkillKind::Passive}, {"q", "Q", SkillKind::Passive}};
    CHECK(GenerateAugmentOffer(initial, minimumPool, 1).IsValid());
    CHECK(PassiveCount(GenerateAugmentOffer(initial, minimumPool, 1)) == 2);
    minimumPool.pop_back();
    CHECK(!GenerateAugmentOffer(initial, minimumPool, 1).IsValid());
    auto badPool = pool;
    badPool.push_back(pool.front());
    CHECK(!GenerateAugmentOffer(initial, badPool, 1).IsValid());
    badPool = pool;
    badPool.front().Id.clear();
    CHECK(!GenerateAugmentOffer(initial, badPool, 1).IsValid());
    auto badOffer = GenerateAugmentOffer(initial, pool, 1);
    badOffer.Choices[1] = badOffer.Choices[0];
    CHECK(!LearnSkill(initial, badOffer, badOffer.Choices.front().Id));
    CHECK(initial.LearnedSkills.size() == 1);

    // A shortened offer never satisfies the four-choice rule.
    const auto passive = std::find_if(pool.begin(), pool.end(), [](const SkillDefinition& skill) { return skill.Kind == SkillKind::Passive; });
    AugmentOffer shortOffer;
    shortOffer.BreakpointLevel = 3;
    shortOffer.Choices = {*passive};
    CHECK(!shortOffer.IsValid());
    CHECK(!LearnSkill(initial, shortOffer, passive->Id));
    CHECK(initial.LearnedSkills.size() == 1);

    // Full-category entries invalidate the whole offer, even if the selected
    // entry would fit. Offers are server-owned; malformed cached data is atomic.
    Progression oneUltimate;
    CHECK(GainLevels(oneUltimate, 5));
    const auto ultimate = std::find_if(pool.begin(), pool.end(), [](const SkillDefinition& skill) { return skill.Kind == SkillKind::Ultimate; });
    oneUltimate.LearnedSkills.push_back(*ultimate);
    oneUltimate.NextAugmentLevel = 3;
    auto forged = GenerateAugmentOffer(oneUltimate, pool, 88);
    const auto otherUltimate = std::find_if(pool.begin(), pool.end(), [&](const SkillDefinition& skill) { return skill.Kind == SkillKind::Ultimate && skill.Id != ultimate->Id; });
    const auto replaced = std::find_if(forged.Choices.begin(), forged.Choices.end(), [](const SkillDefinition& skill) { return skill.Kind == SkillKind::Active; });
    CHECK(replaced != forged.Choices.end());
    if (replaced != forged.Choices.end()) *replaced = *otherUltimate;
    const auto selectedPassive = std::find_if(forged.Choices.begin(), forged.Choices.end(), [](const SkillDefinition& skill) { return skill.Kind == SkillKind::Passive; });
    CHECK(selectedPassive != forged.Choices.end());
    if (selectedPassive != forged.Choices.end()) CHECK(!LearnSkill(oneUltimate, forged, selectedPassive->Id));
    CHECK(oneUltimate.LearnedSkills.size() == 1 && oneUltimate.NextAugmentLevel == 3);

    Progression finalPassive;
    CHECK(GainLevels(finalPassive, 23));
    for (const auto& skill : pool)
        if (skill.Kind == SkillKind::Active && CountSkills(finalPassive, skill.Kind) < MaxActiveSkills)
            finalPassive.LearnedSkills.push_back(skill);
    finalPassive.LearnedSkills.push_back(*ultimate);
    finalPassive.NextAugmentLevel = BreakpointForSkill(7);
    const auto finalOffer = GenerateAugmentOffer(finalPassive, pool, 12);
    CHECK(finalOffer.IsValid() && finalOffer.Choices.size() == 4);
    CHECK(PassiveCount(finalOffer) == static_cast<int>(finalOffer.Choices.size()));
    auto noPassives = pool;
    noPassives.erase(std::remove_if(noPassives.begin(), noPassives.end(), [](const SkillDefinition& skill) { return skill.Kind == SkillKind::Passive; }), noPassives.end());
    CHECK(!GenerateAugmentOffer(finalPassive, noPassives, 12).IsValid());
    auto badFinal = finalOffer;
    badFinal.Choices[0] = *otherUltimate;
    CHECK(badFinal.IsValid()); // structurally valid, but filled-category choice
    CHECK(!LearnSkill(finalPassive, badFinal, otherUltimate->Id));
    CHECK(LearnSkill(finalPassive, finalOffer, finalOffer.Choices.front().Id));
    CHECK(!HasPendingAugment(finalPassive));
}

void SkillCapacityRules()
{
    const auto pool = StarterSkillPool();
    Progression full;
    full.Level = 24;
    full.NextAugmentLevel = BreakpointForSkill(8);
    for (const auto& skill : pool)
    {
        const int capacity = skill.Kind == SkillKind::Active ? MaxActiveSkills : 1;
        if (CountSkills(full, skill.Kind) < capacity) full.LearnedSkills.push_back(skill);
    }
    CHECK(CountSkills(full, SkillKind::Active) == 6);
    CHECK(CountSkills(full, SkillKind::Passive) == 1);
    CHECK(CountSkills(full, SkillKind::Ultimate) == 1);
    CHECK(HasPassive(full) && HasUltimate(full));
    CHECK(full.LearnedSkills.size() == 8);
    CHECK(!HasPendingAugment(full));
    CHECK(GainLevels(full, 10));
    CHECK(full.Level == 34);

    // No category can consume another category's reserved slot.
    for (const auto kind : {SkillKind::Active, SkillKind::Passive, SkillKind::Ultimate})
    {
        Progression invalid = full;
        const auto replacement = std::find_if(pool.begin(), pool.end(), [&](const SkillDefinition& skill)
        {
            return skill.Kind == kind && std::none_of(invalid.LearnedSkills.begin(), invalid.LearnedSkills.end(),
                [&](const SkillDefinition& learned) { return skill.Id == learned.Id; });
        });
        const auto displaced = std::find_if(invalid.LearnedSkills.begin(), invalid.LearnedSkills.end(),
            [&](const SkillDefinition& skill) { return skill.Kind != kind; });
        CHECK(replacement != pool.end() && displaced != invalid.LearnedSkills.end());
        if (replacement == pool.end() || displaced == invalid.LearnedSkills.end()) continue;
        *displaced = *replacement;
        const int levelBefore = invalid.Level;
        CHECK(!GainLevels(invalid, 1));
        CHECK(invalid.Level == levelBefore);
        CHECK(!HasPendingAugment(invalid));
    }
}

void RoleDraftRules()
{
    const auto catalog=StarterSkillPool();
    for(const auto role:{SkillDraftRole::Tank,SkillDraftRole::Damage,SkillDraftRole::Support})
    {
        const auto pool=StarterSkillPool(role);
        auto reversed=pool;std::reverse(reversed.begin(),reversed.end());
        const auto count=[&](SkillKind kind){return std::count_if(pool.begin(),pool.end(),[kind](const SkillDefinition& s){return s.Kind==kind;});};
        // Even after five active picks, at least four active alternatives remain.
        // The only-ultimate and only-passive final slots also retain four choices.
        CHECK(count(SkillKind::Active)>=9);
        CHECK(count(SkillKind::Passive)>=4);
        CHECK(count(SkillKind::Ultimate)>=4);
        for(const auto& skill:pool)CHECK(IsSkillAllowedForRole(skill.Id,role));
        CHECK(IsSkillAllowedForRole("second_wind",role));
        for(const auto* id:{"restoring_light","sanctuary","purify","renewal","wellspring","mass_aegis"})
            CHECK(IsSkillAllowedForRole(id,role)==(role==SkillDraftRole::Support));
        for(std::uint64_t seed=0;seed<256;++seed)
        for(int strategy=0;strategy<4;++strategy)
        {
            Progression p;p.DraftRole=role;CHECK(GainLevels(p,23));
            for(int slot=0;slot<MaxSkills;++slot)
            {
                const auto offer=GenerateAugmentOffer(p,catalog,seed*29+slot);
                CHECK(offer.IsValid());if(!offer.IsValid())break;
                CHECK(Signature(offer)==Signature(GenerateAugmentOffer(p,reversed,seed*29+slot)));
                const bool finalPassive=CountSkills(p,SkillKind::Active)==6&&HasUltimate(p);
                CHECK(slot==0?PassiveCount(offer)==0:finalPassive?PassiveCount(offer)==4:HasPassive(p)?PassiveCount(offer)==0:PassiveCount(offer)>=1&&PassiveCount(offer)<=2);
                for(const auto& option:offer.Choices)
                {
                    CHECK(IsSkillAllowedForRole(option.Id,role));
                    Progression alternate=p;CHECK(LearnSkill(alternate,offer,option.Id));
                    CHECK(!HasPendingAugment(alternate)||GenerateAugmentOffer(alternate,catalog,seed+9).IsValid());
                }
                auto pick=offer.Choices.begin()+static_cast<std::ptrdiff_t>((seed+slot)%4);
                const SkillKind special=strategy<2?SkillKind::Passive:SkillKind::Ultimate;
                const bool preferSpecial=strategy%2==0;
                const auto preferred=std::find_if(offer.Choices.begin(),offer.Choices.end(),[&](const SkillDefinition& s){return (s.Kind==special)==preferSpecial;});
                if(preferred!=offer.Choices.end())pick=preferred;
                CHECK(LearnSkill(p,offer,pick->Id));
            }
            CHECK(CountSkills(p,SkillKind::Active)==6&&HasPassive(p)&&HasUltimate(p)&&p.LearnedSkills.size()==8);
        }
    }
    Progression tank;tank.DraftRole=SkillDraftRole::Tank;CHECK(GainLevels(tank,2));
    auto forged=GenerateAugmentOffer(tank,catalog,1);
    auto active=std::find_if(forged.Choices.begin(),forged.Choices.end(),[](const SkillDefinition& s){return s.Kind==SkillKind::Active;});
    CHECK(active!=forged.Choices.end());
    if(active!=forged.Choices.end())
    {
        *active={"restoring_light","Restoring Light",SkillKind::Active};
        CHECK(!LearnSkill(tank,forged,"restoring_light"));
        CHECK(tank.LearnedSkills.empty()&&tank.NextAugmentLevel==1);
    }
    CHECK(!IsSkillAllowedForRole("unknown_recipe",SkillDraftRole::Tank));
    CHECK(StarterSkillPool(static_cast<SkillDraftRole>(99)).empty());
    tank.DraftRole=static_cast<SkillDraftRole>(99);CHECK(!GainLevels(tank,1));
}

// Role tags: every skill is tagged Tank/DPS/Support (bit mask; hybrids carry
// several). Offers only ever contain skills sharing a role with the champion's
// primary + secondary roles, and every role set can finish a full build.
void RoleTagRules()
{
    const auto catalog=StarterSkillPool();
    const RoleMask roleBits[]={RoleTank,RoleDamage,RoleSupport};
    const SkillDraftRole roles[]={SkillDraftRole::Tank,SkillDraftRole::Damage,SkillDraftRole::Support};
    CHECK(SkillRoleTags("unknown_recipe")==RoleNone);
    CHECK(SkillRoleTags("")==RoleNone);
    CHECK(!IsSkillAllowedForRoles("unknown_recipe",RoleAll));
    CHECK(StarterSkillPoolForRoles(RoleNone).empty());
    CHECK(StarterSkillPoolForRoles(RoleAll).size()==catalog.size());
    for(const auto& skill:catalog)
    {
        const RoleMask tags=SkillRoleTags(skill.Id);
        CHECK(tags!=RoleNone);
        CHECK((tags&~RoleAll)==0);
        if(skill.Kind==SkillKind::Passive&&skill.Id!="executioner")CHECK(tags==RoleAll);
        for(int r=0;r<3;++r)CHECK(IsSkillAllowedForRole(skill.Id,roles[r])==((tags&roleBits[r])!=0));
    }
    // Documented exclusive tags (Docs/RoleDrafts.md).
    for(const auto* id:{"restoring_light","sanctuary","purify","renewal","mass_aegis","wellspring"})CHECK(SkillRoleTags(id)==RoleSupport);
    for(const auto* id:{"shield_slam","war_cry","last_stand","challenge_of_iron","seismic_reprisal"})CHECK(SkillRoleTags(id)==RoleTank);
    for(const auto* id:{"venom_ground","cinder_cone","grave_line","ashen_square","blight_sigil","piercing_shot","spectral_pack","cataclysm","executioners_verdict","starfall","spectral_hunt"})CHECK(SkillRoleTags(id)==RoleDamage);
    // Cross-class skills carry several tags.
    CHECK(SkillRoleTags("chain_spark")==(RoleDamage|RoleSupport));
    CHECK(SkillRoleTags("ember_lance")==(RoleDamage|RoleSupport));
    CHECK(SkillRoleTags("cleaving_strike")==(RoleTank|RoleDamage));
    CHECK(SkillRoleTags("shadow_step")==(RoleTank|RoleDamage));
    CHECK(SkillRoleTags("bastion_of_dawn")==(RoleTank|RoleSupport));
    for(const auto* id:{"iron_guard","frost_bind","summoned_wall","protection_dome","oathbound_guardian","second_wind"})CHECK(SkillRoleTags(id)==RoleAll);
    int hybridSkills=0;
    for(const auto& skill:catalog){const RoleMask t=SkillRoleTags(skill.Id);if(t!=RoleTank&&t!=RoleDamage&&t!=RoleSupport)++hybridSkills;}
    CHECK(hybridSkills>=10);

    // Validation of the role set itself.
    Progression bad;bad.DraftRole=SkillDraftRole::Tank;bad.SecondaryRoles=8;CHECK(!GainLevels(bad,2));
    bad.SecondaryRoles=RoleSupport;bad.DraftRole=SkillDraftRole::Any;CHECK(!GainLevels(bad,2));
    bad.DraftRole=SkillDraftRole::Tank;CHECK(GainLevels(bad,2));CHECK(EffectiveRoleMask(bad)==(RoleTank|RoleSupport));
    Progression any;CHECK(EffectiveRoleMask(any)==RoleAll);

    // Exhaustive primary x secondary role sets: offers stay inside the role set,
    // every allowed skill (including hybrids) is actually reachable, no path ever
    // runs dry, and the confirmed offer shape (4 choices; 1-2 passives until one
    // is learned; four passives for a final passive-only slot) is preserved.
    std::uint64_t pathCount=0;
    for(int r=0;r<3;++r)
    for(int secondaryValue=0;secondaryValue<=RoleAll;++secondaryValue)
    {
        const RoleMask secondary=static_cast<RoleMask>(secondaryValue);
        const RoleMask mask=static_cast<RoleMask>(roleBits[r]|secondary);
        std::set<std::string> seen;
        for(std::uint64_t seed=0;seed<96;++seed)
        for(int strategy=0;strategy<4;++strategy)
        {
            Progression p;p.DraftRole=roles[r];p.SecondaryRoles=secondary;CHECK(GainLevels(p,23));
            for(int slot=0;slot<MaxSkills;++slot)
            {
                const auto offer=GenerateAugmentOffer(p,catalog,seed*131+static_cast<std::uint64_t>(slot*7+r));
                CHECK(offer.IsValid());if(!offer.IsValid())break;
                const bool finalPassive=CountSkills(p,SkillKind::Active)==MaxActiveSkills&&HasUltimate(p);
                CHECK(slot==0?PassiveCount(offer)==0:finalPassive?PassiveCount(offer)==4:HasPassive(p)?PassiveCount(offer)==0:PassiveCount(offer)>=1&&PassiveCount(offer)<=2);
                for(const auto& option:offer.Choices)
                {
                    seen.insert(option.Id);
                    const RoleMask tags=SkillRoleTags(option.Id);
                    CHECK((tags&mask)!=0);
                    // A pure Tank (no Support secondary) is never offered a Support-only
                    // skill, and likewise for every exclusive tag outside the role set.
                    for(const RoleMask exclusive:{RoleTank,RoleDamage,RoleSupport})
                        if(tags==exclusive)CHECK((mask&exclusive)!=0);
                    Progression alternate=p;CHECK(LearnSkill(alternate,offer,option.Id));
                    CHECK(!HasPendingAugment(alternate)||GenerateAugmentOffer(alternate,catalog,seed+static_cast<std::uint64_t>(slot)).IsValid());
                }
                auto pick=offer.Choices.begin()+static_cast<std::ptrdiff_t>((seed+static_cast<std::uint64_t>(slot))%4);
                const SkillKind special=strategy<2?SkillKind::Passive:SkillKind::Ultimate;
                const bool preferSpecial=strategy%2==0;
                const auto preferred=std::find_if(offer.Choices.begin(),offer.Choices.end(),[&](const SkillDefinition& s){return (s.Kind==special)==preferSpecial;});
                if(preferred!=offer.Choices.end())pick=preferred;
                CHECK(LearnSkill(p,offer,pick->Id));
            }
            CHECK(CountSkills(p,SkillKind::Active)==MaxActiveSkills&&HasPassive(p)&&HasUltimate(p)&&p.LearnedSkills.size()==8);
            for(const auto& learned:p.LearnedSkills)CHECK((SkillRoleTags(learned.Id)&mask)!=0);
            ++pathCount;
        }
        // Every skill allowed for this role set was offered at least once; nothing else was.
        const auto allowed=StarterSkillPoolForRoles(mask);
        CHECK(seen.size()==allowed.size());
        for(const auto& skill:allowed)CHECK(seen.count(skill.Id)==1);
        // Pools are deep enough for any order of special-slot picks.
        const auto count=[&](SkillKind kind){return std::count_if(allowed.begin(),allowed.end(),[kind](const SkillDefinition& s){return s.Kind==kind;});};
        CHECK(count(SkillKind::Active)>=9&&count(SkillKind::Passive)>=4&&count(SkillKind::Ultimate)>=4);
    }
    CHECK(pathCount==3u*8u*96u*4u);

    // Hybrids receive both roles' exclusive skills; single roles never do.
    const auto offeredSomewhere=[&](SkillDraftRole role,RoleMask secondary,const char* id)
    {
        for(std::uint64_t seed=0;seed<400;++seed)
        {
            Progression p;p.DraftRole=role;p.SecondaryRoles=secondary;p=Opened(p,catalog,seed);GainLevels(p,2);
            const auto offer=GenerateAugmentOffer(p,catalog,seed);
            for(const auto& option:offer.Choices)if(option.Id==id)return true;
        }
        return false;
    };
    CHECK(offeredSomewhere(SkillDraftRole::Tank,RoleSupport,"restoring_light"));
    CHECK(offeredSomewhere(SkillDraftRole::Tank,RoleSupport,"shield_slam"));
    CHECK(!offeredSomewhere(SkillDraftRole::Tank,RoleNone,"restoring_light"));
    CHECK(!offeredSomewhere(SkillDraftRole::Tank,RoleDamage,"sanctuary"));
    CHECK(offeredSomewhere(SkillDraftRole::Damage,RoleSupport,"purify"));
    CHECK(!offeredSomewhere(SkillDraftRole::Damage,RoleNone,"purify"));
    CHECK(!offeredSomewhere(SkillDraftRole::Damage,RoleNone,"war_cry"));
    CHECK(offeredSomewhere(SkillDraftRole::Damage,RoleTank,"war_cry"));
    CHECK(!offeredSomewhere(SkillDraftRole::Support,RoleNone,"piercing_shot"));
    CHECK(offeredSomewhere(SkillDraftRole::Support,RoleNone,"chain_spark"));
    CHECK(offeredSomewhere(SkillDraftRole::Damage,RoleNone,"chain_spark"));
    CHECK(offeredSomewhere(SkillDraftRole::Tank,RoleNone,"cleaving_strike"));
    CHECK(offeredSomewhere(SkillDraftRole::Damage,RoleNone,"cleaving_strike"));

    // Server-side LearnSkill applies the same mask to a forged offer.
    for(const RoleMask secondary:{RoleNone,RoleDamage,RoleSupport})
    {
        Progression p;p.DraftRole=SkillDraftRole::Tank;p.SecondaryRoles=secondary;p=Opened(p,catalog,1);CHECK(GainLevels(p,2));
        auto forged=GenerateAugmentOffer(p,catalog,5);
        auto active=std::find_if(forged.Choices.begin(),forged.Choices.end(),[](const SkillDefinition& s){return s.Kind==SkillKind::Active;});
        CHECK(active!=forged.Choices.end());if(active==forged.Choices.end())continue;
        const bool present=std::any_of(forged.Choices.begin(),forged.Choices.end(),[](const SkillDefinition& s){return s.Id=="purify";});
        if(!present)*active={"purify","Purify",SkillKind::Active};
        CHECK(LearnSkill(p,forged,"purify")==(secondary==RoleSupport));
    }
}

// Eric's rulings: one starting point with a role-specific, non-passive opening
// offer (hybrids open in their primary role), then normal offers; class traits.
void OpeningAndTraitRules()
{
    const auto catalog=StarterSkillPool();
    const SkillDraftRole roles[]={SkillDraftRole::Tank,SkillDraftRole::Damage,SkillDraftRole::Support};
    const RoleMask bits[]={RoleTank,RoleDamage,RoleSupport};
    for(int r=0;r<3;++r)
    {
        const auto pool=OpeningSkillPool(roles[r]);
        CHECK(pool.size()>=4);
        for(const auto& s:pool){CHECK(s.Kind==SkillKind::Active);CHECK((SkillRoleTags(s.Id)&bits[r])!=0);}
        for(int secondaryValue=0;secondaryValue<=RoleAll;++secondaryValue)
        for(std::uint64_t seed=0;seed<200;++seed)
        {
            Progression p;p.DraftRole=roles[r];p.SecondaryRoles=static_cast<RoleMask>(secondaryValue&~bits[r]);
            CHECK(HasPendingAugment(p)&&IsOpeningOffer(p));
            const auto offer=GenerateAugmentOffer(p,catalog,seed);
            CHECK(offer.IsValid()&&offer.BreakpointLevel==1&&offer.Choices.size()==4);
            CHECK(PassiveCount(offer)==0);
            for(const auto& o:offer.Choices)
            {
                CHECK(o.Kind==SkillKind::Active);
                CHECK(IsOpeningSkill(o.Id,roles[r]));
                // Hybrids draw their opening from the primary role only.
                CHECK((SkillRoleTags(o.Id)&bits[r])!=0);
                Progression alt=p;CHECK(LearnSkill(alt,offer,o.Id));
                CHECK(alt.NextAugmentLevel==3&&!HasPendingAugment(alt));
            }
            // Then the normal flow resumes at level 3: four choices with one or two passives.
            Progression next=Opened(p,catalog,seed);CHECK(GainLevels(next,2));
            const auto second=GenerateAugmentOffer(next,catalog,seed+17);
            CHECK(second.IsValid()&&second.BreakpointLevel==3&&PassiveCount(second)>=1&&PassiveCount(second)<=2);
        }
    }
    // Role-defining opening pools.
    for(const auto* id:{"shield_slam","war_cry","iron_guard"})CHECK(IsOpeningSkill(id,SkillDraftRole::Tank));
    for(const auto* id:{"restoring_light","sanctuary","purify"})CHECK(IsOpeningSkill(id,SkillDraftRole::Support));
    for(const auto* id:{"restoring_light","sanctuary","purify","chain_spark","frost_bind"})CHECK(!IsOpeningSkill(id,SkillDraftRole::Tank));
    for(const auto* id:{"chain_spark","ember_lance","piercing_shot","war_cry"})CHECK(!IsOpeningSkill(id,SkillDraftRole::Support));
    for(const auto* id:{"restoring_light","sanctuary","purify","war_cry","iron_guard","second_wind"})CHECK(!IsOpeningSkill(id,SkillDraftRole::Damage));
    for(const auto* id:{"stone_skin","cataclysm","bastion_of_dawn"}){for(const auto role:roles){const auto op=OpeningSkillPool(role);CHECK(std::none_of(op.begin(),op.end(),[&](const SkillDefinition& s){return s.Id==id;}));}}
    // Server validation rejects forged opening offers (passive, ultimate, off-pool active).
    {
        Progression tank;tank.DraftRole=SkillDraftRole::Tank;
        const auto good=GenerateAugmentOffer(tank,catalog,4);
        const SkillDefinition forgedChoices[]={{"stone_skin","Stone Skin",SkillKind::Passive},{"last_stand","Last Stand",SkillKind::Ultimate},{"frost_bind","Frost Bind",SkillKind::Active}};
        for(const auto& f:forgedChoices)
        {
            auto forged=good;forged.Choices[0]=f;
            Progression t=tank;CHECK(!LearnSkill(t,forged,f.Id));CHECK(t.LearnedSkills.empty());
            CHECK(!LearnSkill(t,forged,forged.Choices[1].Id));
        }
    }
    // Bots take the same path: every role completes a full build from level 1
    // with an automatic first-choice picker (the in-engine bot uses Learn(Pick)).
    for(int r=0;r<3;++r)for(std::uint64_t seed=0;seed<64;++seed)
    {
        Progression bot;bot.DraftRole=roles[r];
        const auto first=GenerateAugmentOffer(bot,catalog,seed);
        CHECK(first.IsValid()&&LearnSkill(bot,first,first.Choices.front().Id));
        CHECK(bot.LearnedSkills.size()==1&&bot.Level==1&&IsOpeningSkill(bot.LearnedSkills.front().Id,roles[r]));
        CHECK(GainLevels(bot,20));
        while(HasPendingAugment(bot)){const auto o=GenerateAugmentOffer(bot,catalog,seed*7+bot.LearnedSkills.size());CHECK(o.IsValid());if(!o.IsValid()||!LearnSkill(bot,o,o.Choices.front().Id))break;}
        CHECK(bot.LearnedSkills.size()==8&&bot.Level==21&&HasPassive(bot)&&HasUltimate(bot));
    }

    // Class traits.
    using namespace Traits;
    CHECK(Near(OutgoingDamageMultiplier(SkillDraftRole::Support),.8));
    CHECK(Near(OutgoingDamageMultiplier(SkillDraftRole::Tank),1)&&Near(OutgoingDamageMultiplier(SkillDraftRole::Damage),1));
    CHECK(Near(AttackSpeedBonus(SkillDraftRole::Support),.1));
    CHECK(Near(AttackSpeedBonus(SkillDraftRole::Tank),0)&&Near(AttackSpeedBonus(SkillDraftRole::Damage),0));
    CHECK(Near(ApplyIncomingFlatReduction(25,SkillDraftRole::Tank),15));
    CHECK(Near(ApplyIncomingFlatReduction(10,SkillDraftRole::Tank),0));
    CHECK(Near(ApplyIncomingFlatReduction(4,SkillDraftRole::Tank),0));
    CHECK(Near(ApplyIncomingFlatReduction(0,SkillDraftRole::Tank),0));
    CHECK(Near(ApplyIncomingFlatReduction(-5,SkillDraftRole::Tank),0));
    CHECK(Near(ApplyIncomingFlatReduction(std::numeric_limits<double>::quiet_NaN(),SkillDraftRole::Tank),0));
    CHECK(Near(ApplyIncomingFlatReduction(25,SkillDraftRole::Damage),25)&&Near(ApplyIncomingFlatReduction(25,SkillDraftRole::Support),25));
    for(int k=0;k<500;++k){const double a=k*.37;CHECK(ApplyIncomingFlatReduction(a,SkillDraftRole::Tank)>=0&&ApplyIncomingFlatReduction(a,SkillDraftRole::Tank)<=a);}
    CHECK(Near(BaseCriticalChance(SkillDraftRole::Damage,.05),.10));
    CHECK(Near(BaseCriticalChance(SkillDraftRole::Damage,.25),.25));
    CHECK(Near(BaseCriticalChance(SkillDraftRole::Tank,.05),.05)&&Near(BaseCriticalChance(SkillDraftRole::Support,.05),.05));
    CHECK(Near(BaseCriticalChance(SkillDraftRole::Damage,std::numeric_limits<double>::quiet_NaN()),.10));
    CHECK(Near(MendingHealAmount(40,SkillDraftRole::Support),20));
    CHECK(Near(MendingHealAmount(40,SkillDraftRole::Damage),0)&&Near(MendingHealAmount(40,SkillDraftRole::Tank),0));
    CHECK(Near(MendingHealAmount(-5,SkillDraftRole::Support),0));
    // Support damage after the -20% still feeds 50% of the dealt amount back as healing.
    CHECK(Near(MendingHealAmount(100*OutgoingDamageMultiplier(SkillDraftRole::Support),SkillDraftRole::Support),40));
    // Mending target: lowest % health among living members, the healer included.
    std::vector<PartyMember> party={{1,50,100,true},{2,30,100,true},{3,1,100,false},{4,60,200,true},{5,80,80,true}};
    CHECK(SelectMendingTarget(party)==1);               // 30% ties with 60/200; lower absolute health wins
    party[1].Health=60;                                 // now 60% vs 30% (60/200)
    CHECK(SelectMendingTarget(party)==3);
    party={{7,40,100,true},{3,40,100,true}};             // exact tie: lowest Id
    CHECK(SelectMendingTarget(party)==1);
    party={{1,20,100,true},{2,90,100,true}};             // the healer itself is lowest
    CHECK(SelectMendingTarget(party)==0);
    party={{1,0,100,true},{2,5,100,false},{3,10,0,true}};// zero-health, dead, invalid max: nobody
    CHECK(SelectMendingTarget(party)==-1);
    CHECK(SelectMendingTarget({})==-1);
    party={{1,150,100,true},{2,100,100,true}};           // overhealth counts as full
    CHECK(SelectMendingTarget(party)==1);
}

// Ability database scaling curve and crowd-control / execute maths.
void AbilityAndControlRules()
{
    using namespace Abilities;
    const Curve standard;
    CHECK(ValidCurve(standard));
    Curve bad=standard;bad.CooldownFloorFraction=0;CHECK(!ValidCurve(bad));
    bad=standard;bad.CostCapMultiplier=.5;CHECK(!ValidCurve(bad));
    bad=standard;bad.EffectHalfLevels=std::numeric_limits<double>::quiet_NaN();CHECK(!ValidCurve(bad));
    Base b;b.Effect=100;b.ManaCost=40;b.EnergyCost=10;b.Cooldown=12;b.CastTime=1.5;
    CHECK(ValidBase(b));
    Base negative=b;negative.ManaCost=-1;CHECK(!ValidBase(negative));
    const auto l1=Scale(b,standard,1);
    CHECK(Near(l1.Effect,100)&&Near(l1.ManaCost,40)&&Near(l1.Cooldown,12)&&Near(l1.CastTime,1.5));
    CHECK(Scale(b,standard,0).Level==1&&Near(Scale(b,standard,-5).Effect,100));
    // Monotone, uncapped effect with diminishing gains; cost capped; cooldown floored.
    LevelStats prev=l1;double prevGain=1e9;
    for(int level=2;level<=100000;level+=(level<200?1:997))
    {
        const auto cur=Scale(b,standard,level);
        CHECK(cur.Effect>prev.Effect);
        CHECK(cur.ManaCost>=prev.ManaCost&&cur.ManaCost<=40*standard.CostCapMultiplier+1e-9);
        CHECK(cur.EnergyCost<=10*standard.CostCapMultiplier+1e-9);
        CHECK(cur.Cooldown<=prev.Cooldown+1e-12&&cur.Cooldown>=12*standard.CooldownFloorFraction-1e-9&&cur.Cooldown>=standard.MinCooldownSeconds-1e-9);
        CHECK(Near(cur.CastTime,1.5));
        if(level<200){const double gain=cur.Effect-prev.Effect;CHECK(gain<=prevGain+1e-9);prevGain=gain;}
        prev=cur;
    }
    CHECK(Scale(b,standard,100000).Effect>300); // no level cap on effect growth
    Curve capped=standard;capped.EffectCap=25;Base pct;pct.Effect=10;
    CHECK(Near(Scale(pct,capped,100000).Effect,25)&&Scale(pct,capped,5).Effect<=25);
    Curve floorTest=standard;floorTest.MinCooldownSeconds=5;Base quick;quick.Cooldown=6;quick.Effect=1;
    CHECK(Scale(quick,floorTest,100000).Cooldown>=5-1e-9);
    Base tiny;tiny.Cooldown=.5;tiny.Effect=1;CHECK(Near(Scale(tiny,standard,1000).Cooldown,.5)); // never raised above its base
    Base none;none.Effect=5;CHECK(Near(Scale(none,standard,50).Cooldown,0));

    using namespace CC;
    CHECK(Near(DiminishedDuration(2,0),2)&&Near(DiminishedDuration(2,1),1)&&Near(DiminishedDuration(2,2),.5)&&Near(DiminishedDuration(2,3),0)&&Near(DiminishedDuration(2,9),0));
    CHECK(Near(DiminishedDuration(-1,0),0)&&Near(DiminishedDuration(std::numeric_limits<double>::infinity(),0),0));
    CHECK(ClassifyVoidZone(0,180,420)==VoidZone::Inner&&ClassifyVoidZone(180,180,420)==VoidZone::Inner);
    CHECK(ClassifyVoidZone(181,180,420)==VoidZone::Outer&&ClassifyVoidZone(420,180,420)==VoidZone::Outer);
    CHECK(ClassifyVoidZone(421,180,420)==VoidZone::None&&ClassifyVoidZone(10,300,200)==VoidZone::None&&ClassifyVoidZone(-1,180,420)==VoidZone::None);
    CHECK(Near(ApplyHealingCut(100,.5,0),50)&&Near(ApplyHealingCut(100,.5,.5),25)&&Near(ApplyHealingCut(100,2,0),0)&&Near(ApplyHealingCut(100,-1,0),100));
    CHECK(Near(ApplyHealingCut(-10,.5,0),0)&&Near(ApplyHealingCut(100,std::numeric_limits<double>::quiet_NaN(),0),100));
    CHECK(Near(ArmorAfterBreak(.4,.5),.2)&&Near(ArmorAfterBreak(.4,0),.4)&&Near(ArmorAfterBreak(.4,3),0));
    CHECK(Near(ExecuteDamage(ExecuteTarget::Monster,900,1000,50),900));   // lethal
    CHECK(Near(ExecuteDamage(ExecuteTarget::Monster,20,1000,50),50));
    CHECK(Near(ExecuteDamage(ExecuteTarget::Boss,9000,10000,50),50));    // bosses take a normal hit
    CHECK(Near(ExecuteDamage(ExecuteTarget::Hero,900,1000,50),300));     // 30% max HP vs champions
    CHECK(Near(ExecuteDamage(ExecuteTarget::Hero,900,1000,400),400));
    CHECK(Near(ExecutionerIntervalSeconds,300)&&Near(ExecuteHeroMaxHealthFraction,.3));
    // New skills are in the pool with their roles and kinds.
    const auto catalog=StarterSkillPool();
    const auto kindOf=[&](const char* id){for(const auto& s:catalog)if(s.Id==id)return static_cast<int>(s.Kind);return -1;};
    CHECK(kindOf("executioner")==static_cast<int>(SkillKind::Passive)&&SkillRoleTags("executioner")==RoleDamage);
    CHECK(kindOf("decimating_strike")==static_cast<int>(SkillKind::Active)&&SkillRoleTags("decimating_strike")==(RoleTank|RoleDamage));
}

void RollRules()
{
    using namespace Roll;
    CHECK(Near(ReducedCooldown(10,15),8.5)&&Near(ReducedCooldown(10,0),10)&&Near(ReducedCooldown(10,100),0)&&Near(ReducedCooldown(10,250),0)&&Near(ReducedCooldown(0,15),0)&&Near(ReducedCooldown(-3,15),0));
    // Per-charge rolls compound: two rolls with 15% leave 72.25%.
    CHECK(Near(ReducedCooldown(ReducedCooldown(10,15),15),7.225));
    CHECK(Near(PointSegmentDistance2D(0,100,-100,0,100,0),100)&&Near(PointSegmentDistance2D(200,0,-100,0,100,0),100)&&Near(PointSegmentDistance2D(5,0,5,0,5,0),0));
    CHECK(Near(MomentumMultiplier(0,4),1)&&Near(MomentumMultiplier(3,4),1.12)&&Near(MomentumMultiplier(9,4),1.2)&&Near(MomentumMultiplier(-2,4),1));
    CHECK(Near(ShortenedReadyAt(10,13.5,70),11.05)&&Near(ShortenedReadyAt(10,13.5,0),13.5)&&Near(ShortenedReadyAt(10,9,70),9));
    CHECK(BlurDodges(.2,25)&&!BlurDodges(.25,25)&&!BlurDodges(.9,25)&&!BlurDodges(.1,0)&&BlurDodges(.99,100));
    int dodged=0;for(int i=0;i<1000;++i)dodged+=BlurDodges(i/1000.0,25);CHECK(dodged==250);
}

void ClockRules()
{
    MatchClock clock;
    CHECK(clock.Phase() == MatchPhase::Survival);
    CHECK(clock.Round() == 1);
    CHECK(Near(clock.RemainingSeconds(), -1));
    CHECK(!clock.ResolveArena());
    CHECK(clock.Advance(86400).empty());
    CHECK(clock.Phase() == MatchPhase::Survival);
    CHECK(clock.BeginIntermission());
    CHECK(!clock.BeginIntermission());
    CHECK(Near(clock.RemainingSeconds(), 60));
    CHECK(clock.Advance(59).empty());
    const auto arena = clock.Advance(1);
    CHECK(arena.size() == 1);
    CHECK(arena.front().From == MatchPhase::Intermission);
    CHECK(arena.front().To == MatchPhase::Arena);
    CHECK(!arena.front().ArenaTimedOut);
    CHECK(clock.Phase() == MatchPhase::Arena);
    CHECK(Near(clock.RemainingSeconds(), 90));
    CHECK(!clock.BeginIntermission());
    CHECK(clock.Advance(89).empty());
    const auto expired = clock.Advance(1);
    CHECK(expired.size() == 1);
    CHECK(expired.front().ArenaTimedOut);
    CHECK(expired.front().Round == 1);
    CHECK(expired.front().To == MatchPhase::Recovery);
    CHECK(clock.Phase() == MatchPhase::Recovery && clock.Round() == 1);
    CHECK(Near(clock.RemainingSeconds(), 15));
    CHECK(!clock.ResolveArena());
    CHECK(!clock.BeginIntermission());
    CHECK(clock.Advance(14).empty());
    const auto restored = clock.Advance(1);
    CHECK(restored.size() == 1);
    CHECK(restored.front().From == MatchPhase::Recovery);
    CHECK(restored.front().To == MatchPhase::Survival);
    CHECK(!restored.front().ArenaTimedOut);
    CHECK(clock.Round() == 2);
    CHECK(Near(clock.RemainingSeconds(), -1));
    CHECK(clock.Advance(600).empty());

    // A large hitch must never skip arena teleport or the recovery break.
    MatchClock hugeTick;
    CHECK(hugeTick.BeginIntermission());
    CHECK(hugeTick.Advance(910).size() == 1);
    CHECK(hugeTick.Phase() == MatchPhase::Arena && hugeTick.Round() == 1);
    CHECK(Near(hugeTick.RemainingSeconds(), 90));
    CHECK(hugeTick.Advance(-1).empty());
    CHECK(hugeTick.Advance(std::numeric_limits<double>::infinity()).empty());
    CHECK(hugeTick.Advance(std::numeric_limits<double>::quiet_NaN()).empty());
    CHECK(hugeTick.Advance(86401).empty());
    CHECK(Near(hugeTick.RemainingSeconds(), 90));
    CHECK(hugeTick.Advance(910).size() == 1);
    CHECK(hugeTick.Phase() == MatchPhase::Recovery && hugeTick.Round() == 1);
    CHECK(Near(hugeTick.RemainingSeconds(), 15));
    CHECK(hugeTick.Advance(910).size() == 1);
    CHECK(hugeTick.Phase() == MatchPhase::Survival && hugeTick.Round() == 2);

    MatchClock early;
    CHECK(early.BeginIntermission());
    early.Advance(60);
    early.Advance(15);
    CHECK(early.ResolveArena());
    CHECK(!early.ResolveArena());
    CHECK(early.Round() == 1 && early.Phase() == MatchPhase::Recovery);
    CHECK(Near(early.RemainingSeconds(), 15));
    early.Advance(15);
    CHECK(early.Round() == 2 && early.Phase() == MatchPhase::Survival);
    early.Finish();
    CHECK(early.Advance(900).empty());
    CHECK(!early.BeginIntermission());
    CHECK(!early.ResolveArena());
    CHECK(early.Phase() == MatchPhase::Finished);
    CHECK(Near(early.RemainingSeconds(), 0));

    MatchClock fractional;
    CHECK(fractional.BeginIntermission());
    for (int tick = 0; tick < 600; ++tick) fractional.Advance(0.1);
    CHECK(fractional.Phase() == MatchPhase::Arena);
    CHECK(Near(fractional.RemainingSeconds(), 90));
    MatchClock invalid({0, -1, std::numeric_limits<double>::quiet_NaN()});
    CHECK(Near(invalid.RemainingSeconds(), -1));
    CHECK(invalid.BeginIntermission());
    CHECK(Near(invalid.RemainingSeconds(), 60));
    CHECK(invalid.Advance(60).size() == 1);
    CHECK(Near(invalid.RemainingSeconds(), 90));
    CHECK(invalid.ResolveArena());
    CHECK(Near(invalid.RemainingSeconds(), 15));
    MatchClock adjustable;
    CHECK(adjustable.BeginIntermission());
    CHECK(adjustable.Advance(20).empty());
    CHECK(adjustable.SetDurations({80,120,30}));
    CHECK(adjustable.Phase()==MatchPhase::Intermission&&adjustable.Round()==1);
    CHECK(Near(adjustable.RemainingSeconds(),60));
    CHECK(!adjustable.SetDurations({0,20,20}));
    CHECK(!adjustable.SetDurations({10,std::numeric_limits<double>::quiet_NaN(),20}));
    CHECK(Near(adjustable.RemainingSeconds(),60));
    CHECK(adjustable.SetDurations({10,45,7}));
    CHECK(Near(adjustable.RemainingSeconds(),0));
    CHECK(adjustable.Phase()==MatchPhase::Intermission);
    CHECK(adjustable.Advance(0).size()==1);
    CHECK(adjustable.Phase()==MatchPhase::Arena&&Near(adjustable.RemainingSeconds(),45));
}
void RewardRules()
{
    CHECK(RemainingLivesAfterLeak(100, false) == 99);
    CHECK(RemainingLivesAfterLeak(100, true) == 90);
    CHECK(RemainingLivesAfterLeak(10, true) == 0);
    CHECK(RemainingLivesAfterLeak(9, true) == 0);
    CHECK(RemainingLivesAfterLeak(1, false) == 0);
    CHECK(RemainingLivesAfterLeak(0, true) == 0);
    CHECK(RemainingLivesAfterLeak(-100, false) == 0);
    CHECK(RemainingLivesAfterLeak(std::numeric_limits<int>::max(), true) == std::numeric_limits<int>::max() - 10);
    int lives = 100;
    for (int boss = 0; boss < 9; ++boss) lives = RemainingLivesAfterLeak(lives, true);
    CHECK(lives == 10);
    for (int creep = 0; creep < 9; ++creep) lives = RemainingLivesAfterLeak(lives, false);
    CHECK(lives == 1);
    CHECK(RemainingLivesAfterLeak(lives, false) == 0);
    TeamRewards team;
    CHECK(Near(team.PowerMultiplier, 1));
    AwardArenaWin(team);
    CHECK(team.ArenaWins == 1);
    CHECK(Near(team.PowerMultiplier, 1.03));
    CHECK(Near(team.LootMultiplier, 1.08));
    for (int win = 0; win < 10000; ++win) AwardArenaWin(team);
    CHECK(Near(team.PowerMultiplier, 1.12));
    CHECK(Near(team.LootMultiplier, 1.40));
    int rareCount = 0;
    int greaterCount = 0;
    for (std::uint64_t seed = 0; seed < 10000; ++seed)
    {
        const auto reward = RollChallengeReward(10, 1.4, seed);
        const auto same = RollChallengeReward(10, 1.4, seed);
        CHECK(reward.Gold >= 359 && reward.Gold <= 439);
        CHECK(reward.Experience == 850);
        CHECK(reward.Gold == same.Gold && reward.RareDrop == same.RareDrop &&
              reward.GreaterStatTome == same.GreaterStatTome);
        CHECK(reward.StatTomePoints == (reward.GreaterStatTome ? 5 : 1));
        if (reward.RareDrop) ++rareCount;
        if (reward.GreaterStatTome) ++greaterCount;
    }
    CHECK(rareCount > 3200 && rareCount < 3800);
    CHECK(greaterCount > 6600 && greaterCount < 7400);
    CHECK(RollChallengeReward(-10, -10, 5).Gold == RollChallengeReward(1, 1, 5).Gold);
    CHECK(RollChallengeReward(1000, 1000, 5).Gold == RollChallengeReward(10, 1.4, 5).Gold);
    CHECK(RollChallengeReward(1, std::numeric_limits<double>::quiet_NaN(), 5).Gold ==
          RollChallengeReward(1, 1, 5).Gold);
    CHECK(ChallengeHealthMultiplier(2, 1) > ChallengeHealthMultiplier(1, 1));
    CHECK(ChallengeHealthMultiplier(1, 2) > ChallengeHealthMultiplier(1, 1));
    CHECK(ChallengeDamageMultiplier(2, 1) > ChallengeDamageMultiplier(1, 1));
    CHECK(ChallengeDamageMultiplier(1, 2) > ChallengeDamageMultiplier(1, 1));
    CHECK(Near(ChallengeHealthMultiplier(-1, -1), 1));
}
} // namespace

// Skill Shop mode (Eric's ruling: it still levels up, and levels give the minor stat increases).
// Bought skills are independent of the level breakpoints; Classic Draft keeps its validation.
void SkillScheduleRules()
{
    const auto pool = StarterSkillPool();
    auto byKind = [&](SkillKind kind, std::size_t skip) {
        std::vector<SkillDefinition> out;
        for (const auto& s : pool) if (s.Kind == kind) out.push_back(s);
        return out.at(skip % out.size());
    };
    std::vector<SkillDefinition> actives;
    for (const auto& s : pool) if (s.Kind == SkillKind::Active) actives.push_back(s);

    // --- Skill Shop: free opening pick, then purchases, then levels 1 -> 25 with correct stats.
    for (const auto primary : {PrimaryStat::Strength, PrimaryStat::Agility, PrimaryStat::Intelligence})
    {
        Progression shop;
        shop.Schedule = SkillSchedule::Shop;
        shop.Primary = primary;
        shop.Stats = {primary == PrimaryStat::Strength ? 20 : 10, primary == PrimaryStat::Agility ? 20 : 10,
                      primary == PrimaryStat::Intelligence ? 20 : 10};
        shop.BaseHealth = StartingBaseHealth(shop.Stats.Strength);
        const StatBlock start = shop.Stats;
        CHECK(HasPendingAugment(shop));                       // the free opening pick
        shop = Opened(shop, pool, 1);
        CHECK(shop.LearnedSkills.size() == 1 && shop.NextAugmentLevel == 3);
        CHECK(!HasPendingAugment(shop));                      // no level offers in Skill Shop mode
        // Buy five more actives, a passive and an ultimate at level 1: the learned count far
        // outruns the level-1 breakpoint, which a Classic Draft progression would reject.
        int bought = 0;
        for (const auto& skill : actives)
        {
            if (bought == 5) break;
            if (AddPurchasedSkill(shop, skill)) ++bought;
        }
        CHECK(bought == 5);
        CHECK(AddPurchasedSkill(shop, byKind(SkillKind::Passive, 0)));
        CHECK(AddPurchasedSkill(shop, byKind(SkillKind::Ultimate, 0)));
        CHECK(static_cast<int>(shop.LearnedSkills.size()) == MaxSkills && shop.Level == 1);
        CHECK(!AddPurchasedSkill(shop, byKind(SkillKind::Passive, 1)));   // capacity still enforced
        CHECK(!AddPurchasedSkill(shop, shop.LearnedSkills.front()));     // no duplicates
        CHECK(!HasPendingAugment(shop) && !GenerateAugmentOffer(shop, pool, 7).IsValid());
        for (int level = 2; level <= 25; ++level)
        {
            CHECK(GainLevels(shop));
            CHECK(shop.Level == level);
        }
        const int grown = 24;
        CHECK(shop.Stats.Strength == start.Strength + grown * (primary == PrimaryStat::Strength ? 2 : 1));
        CHECK(shop.Stats.Agility == start.Agility + grown * (primary == PrimaryStat::Agility ? 2 : 1));
        CHECK(shop.Stats.Intelligence == start.Intelligence + grown * (primary == PrimaryStat::Intelligence ? 2 : 1));
        CombatTuning tuning;
        tuning.BaseHealth = shop.BaseHealth;
        const auto derived = CalculateStats(shop.Stats, shop.Primary, tuning);
        CHECK(Near(derived.MaxHealth, primary == PrimaryStat::Strength ? 980 : 490));
        CHECK(Near(derived.MaxMana, shop.Stats.Intelligence * 30.0));
        CHECK(Near(derived.Armor, shop.Stats.Strength * 0.1) && Near(derived.Ward, shop.Stats.Strength * 0.1));
        // The same purchases under the Classic Draft schedule would have stalled every level.
        Progression stalled = shop;
        stalled.Schedule = SkillSchedule::Draft;
        stalled.Level = 1;
        CHECK(!GainLevels(stalled));
    }
    {
        // Skills bought mid-game (level 4, three skills) keep levelling; no offer ever appears.
        Progression shop;
        shop.Schedule = SkillSchedule::Shop;
        shop = Opened(shop, pool, 2);
        CHECK(GainLevels(shop, 3) && shop.Level == 4);
        CHECK(AddPurchasedSkill(shop, actives.at(10)) || AddPurchasedSkill(shop, actives.at(11)));
        CHECK(AddPurchasedSkill(shop, actives.at(12)) || AddPurchasedSkill(shop, actives.at(13)));
        CHECK(shop.LearnedSkills.size() == 3 && !HasPendingAugment(shop));
        CHECK(GainLevels(shop, 20) && shop.Level == 24);
        // Buying before the opening pick is also fine; the free opening offer then no longer applies.
        Progression early;
        early.Schedule = SkillSchedule::Shop;
        CHECK(AddPurchasedSkill(early, actives.at(0)) && !HasPendingAugment(early) && GainLevels(early, 5));
    }

    // --- Classic Draft validation is unchanged.
    {
        Progression draft;   // default schedule
        CHECK(draft.Schedule == SkillSchedule::Draft);
        CHECK(!AddPurchasedSkill(draft, actives.at(0)));      // purchases are Skill Shop only
        draft = Opened(draft, pool, 3);
        CHECK(draft.NextAugmentLevel == 3 && GainLevels(draft, 2) && HasPendingAugment(draft));
        Progression pushed = draft;
        pushed.LearnedSkills.push_back(actives.at(15));       // skill outside the breakpoint schedule
        CHECK(!GainLevels(pushed) && !HasPendingAugment(pushed));
        Progression skipped = draft;
        skipped.NextAugmentLevel = 6;                           // skipped breakpoint
        CHECK(!GainLevels(skipped));
        Progression bad = draft;
        bad.Schedule = static_cast<SkillSchedule>(9);
        CHECK(!GainLevels(bad) && !HasPendingAugment(bad));
    }
}

int main()
{
    StatRules();
    DraftRules();
    SkillCapacityRules();
    RoleDraftRules();
    RoleTagRules();
    OpeningAndTraitRules();
    AbilityAndControlRules();
    RollRules();
    ClockRules();
    RewardRules();
    SkillScheduleRules();
    std::cout << Assertions << " assertions; " << Failures << " failures\n";
    return Failures == 0 ? 0 : 1;
}
