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

void StatRules()
{
    CombatTuning tuning;
    tuning.WeaponDamage = 7;
    tuning.PureCooldownReduction = 0.25;
    const auto stats = CalculateStats({12, 15, 20}, PrimaryStat::Intelligence, tuning);
    CHECK(Near(stats.MaxHealth, 300));
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
    Progression initial;
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
            CHECK(offer.BreakpointLevel == 3 * (slot + 1));
            CHECK(Signature(offer) == Signature(GenerateAugmentOffer(progression, reversed, seed * 31 + slot)));
            const auto passiveCount = PassiveCount(offer);
            const bool onlyPassive = CountSkills(progression, SkillKind::Active) == MaxActiveSkills && HasUltimate(progression);
            CHECK(onlyPassive ? passiveCount == 4 : (HasPassive(progression) ? passiveCount == 0 : (passiveCount >= 1 && passiveCount <= 2)));
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
    for (int breakpoint : {3, 6, 9})
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
    CHECK(initial.LearnedSkills.empty());

    // A shortened offer never satisfies the four-choice rule.
    const auto passive = std::find_if(pool.begin(), pool.end(), [](const SkillDefinition& skill) { return skill.Kind == SkillKind::Passive; });
    AugmentOffer shortOffer;
    shortOffer.BreakpointLevel = 3;
    shortOffer.Choices = {*passive};
    CHECK(!shortOffer.IsValid());
    CHECK(!LearnSkill(initial, shortOffer, passive->Id));
    CHECK(initial.LearnedSkills.empty());

    // Full-category entries invalidate the whole offer, even if the selected
    // entry would fit. Offers are server-owned; malformed cached data is atomic.
    Progression oneUltimate;
    CHECK(GainLevels(oneUltimate, 5));
    const auto ultimate = std::find_if(pool.begin(), pool.end(), [](const SkillDefinition& skill) { return skill.Kind == SkillKind::Ultimate; });
    oneUltimate.LearnedSkills.push_back(*ultimate);
    oneUltimate.NextAugmentLevel = 6;
    auto forged = GenerateAugmentOffer(oneUltimate, pool, 88);
    const auto otherUltimate = std::find_if(pool.begin(), pool.end(), [&](const SkillDefinition& skill) { return skill.Kind == SkillKind::Ultimate && skill.Id != ultimate->Id; });
    const auto replaced = std::find_if(forged.Choices.begin(), forged.Choices.end(), [](const SkillDefinition& skill) { return skill.Kind == SkillKind::Active; });
    CHECK(replaced != forged.Choices.end());
    if (replaced != forged.Choices.end()) *replaced = *otherUltimate;
    const auto selectedPassive = std::find_if(forged.Choices.begin(), forged.Choices.end(), [](const SkillDefinition& skill) { return skill.Kind == SkillKind::Passive; });
    CHECK(selectedPassive != forged.Choices.end());
    if (selectedPassive != forged.Choices.end()) CHECK(!LearnSkill(oneUltimate, forged, selectedPassive->Id));
    CHECK(oneUltimate.LearnedSkills.size() == 1 && oneUltimate.NextAugmentLevel == 6);

    Progression finalPassive;
    CHECK(GainLevels(finalPassive, 23));
    for (const auto& skill : pool)
        if (skill.Kind == SkillKind::Active && CountSkills(finalPassive, skill.Kind) < MaxActiveSkills)
            finalPassive.LearnedSkills.push_back(skill);
    finalPassive.LearnedSkills.push_back(*ultimate);
    finalPassive.NextAugmentLevel = 24;
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
    full.NextAugmentLevel = 27;
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
                CHECK(finalPassive?PassiveCount(offer)==4:HasPassive(p)?PassiveCount(offer)==0:PassiveCount(offer)>=1&&PassiveCount(offer)<=2);
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
        CHECK(tank.LearnedSkills.empty()&&tank.NextAugmentLevel==3);
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
        if(skill.Kind==SkillKind::Passive)CHECK(tags==RoleAll);
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
                CHECK(finalPassive?PassiveCount(offer)==4:HasPassive(p)?PassiveCount(offer)==0:PassiveCount(offer)>=1&&PassiveCount(offer)<=2);
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
            Progression p;p.DraftRole=role;p.SecondaryRoles=secondary;GainLevels(p,2);
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
        Progression p;p.DraftRole=SkillDraftRole::Tank;p.SecondaryRoles=secondary;CHECK(GainLevels(p,2));
        auto forged=GenerateAugmentOffer(p,catalog,5);
        auto active=std::find_if(forged.Choices.begin(),forged.Choices.end(),[](const SkillDefinition& s){return s.Kind==SkillKind::Active;});
        CHECK(active!=forged.Choices.end());if(active==forged.Choices.end())continue;
        const bool present=std::any_of(forged.Choices.begin(),forged.Choices.end(),[](const SkillDefinition& s){return s.Id=="purify";});
        if(!present)*active={"purify","Purify",SkillKind::Active};
        CHECK(LearnSkill(p,forged,"purify")==(secondary==RoleSupport));
    }
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

int main()
{
    StatRules();
    DraftRules();
    SkillCapacityRules();
    RoleDraftRules();
    RoleTagRules();
    ClockRules();
    RewardRules();
    std::cout << Assertions << " assertions; " << Failures << " failures\n";
    return Failures == 0 ? 0 : 1;
}
