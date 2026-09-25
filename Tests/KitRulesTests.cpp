// scaling-kits: universal primary-stat scaling, owner inheritance, shield block, level-15
// bonuses/auras, Headshot, Artillery and the Mechanical Tank's target choice.
#include "CireKitRules.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace Cires::Kits;

namespace
{
int Assertions = 0;
int Failures = 0;
void Check(bool passed, const char* expression, int line)
{
    ++Assertions;
    if (!passed) { ++Failures; std::cerr << "FAIL line " << line << ": " << expression << '\n'; }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)
bool Near(double a, double b) { return std::abs(a - b) < 1.0e-8; }

void PrimaryScaling()
{
    // A tank's damaging stun scales with STR, a ranger's tether with AGI, mages/healers with INT.
    const Attributes tank{30, 12, 10}, ranger{10, 34, 10}, mage{10, 10, 40}, healer{12, 10, 36};
    CHECK(PrimaryValue(tank, Primary::Strength) == 30);
    CHECK(PrimaryValue(ranger, Primary::Agility) == 34);
    CHECK(PrimaryValue(mage, Primary::Intelligence) == 40);
    CHECK(Near(ScaledAmount(40, 1.2, PrimaryValue(tank, Primary::Strength)), 76));   // Shield Bash-style stun
    CHECK(Near(ScaledAmount(30, 1.0, PrimaryValue(ranger, Primary::Agility)), 64));  // ranger tether
    CHECK(Near(ScaledAmount(65, 2.0, PrimaryValue(mage, Primary::Intelligence)), 145));
    CHECK(Near(ScaledAmount(90, 2.4, PrimaryValue(healer, Primary::Intelligence)), 176.4)); // heal
    // The same ability scales with whichever stat is primary: a STR tank and an INT caster with
    // equal primary values deal the same damage.
    CHECK(Near(ScaledAmount(50, 1.5, PrimaryValue(tank, Primary::Strength)), ScaledAmount(50, 1.5, 30)));
    CHECK(Near(ScaledAmount(-5, -1, 20), 0));
    CHECK(Near(ScaledAmount(10, NAN, 20), 10));
    CHECK(std::string(PrimaryShortName(Primary::Agility)) == "AGI");
}

void Inheritance()
{
    // Owner attack speed and CDR carry to summons and constructs.
    CHECK(Near(InheritedAttackInterval(1.2, 1.5), 0.8));
    CHECK(Near(InheritedAttackInterval(1.2, 1.0), 1.2));
    CHECK(Near(InheritedCooldown(10, 0.25), 7.5));
    CHECK(Near(InheritedCooldown(10, 0.95), 4.0)); // capped at 60%
    CHECK(Near(UnitHitDamage(18, 0.35, 40), 32));   // turret bolt off the owner's primary
    CHECK(Near(UnitHitDamage(1.0e6, 1, 1), 10000));
}

void ShieldBlock()
{
    CHECK(Near(ShieldDefenseMultiplier(true), 0.9));
    CHECK(Near(ShieldDefenseMultiplier(false), 1.0));
    auto b = ResolveShieldBlock(100, true, true, 0.29);
    CHECK(b.Blocked && Near(b.Damage, 50) && Near(b.Prevented, 50));
    CHECK(!ResolveShieldBlock(100, true, true, 0.30).Blocked);
    CHECK(!ResolveShieldBlock(100, false, true, 0.0).Blocked);   // magic cannot be blocked
    CHECK(!ResolveShieldBlock(100, true, false, 0.0).Blocked);   // no shield, no block
    // Roughly 30% of rolls block.
    int blocked = 0;
    for (int i = 0; i < 1000; ++i) blocked += ResolveShieldBlock(10, true, true, (i + 0.5) / 1000.0).Blocked ? 1 : 0;
    CHECK(blocked == 300);
}

void Level15()
{
    CHECK(!Level15Unlocked(14) && Level15Unlocked(15) && Level15Unlocked(40));
    for (const char* id : {"dot", "healCut", "stun", "slow", "damageAmp", "vulnerability", "purge"})
        CHECK(std::string(Level15Id(ParseLevel15(id))) == id);
    CHECK(ParseLevel15("nope") == Level15Bonus::None);
    CHECK(Near(VulnerableDefense(50, true), 40));
    CHECK(Near(VulnerableDefense(50, false), 50));
    CHECK(Near(AmplifiedDamage(100, true), 112));
    CHECK(Near(DotTotal(100), 40));
    // Auras: one of each kind per party, duplicates do not stack.
    for (const char* id : {"attackSpeed", "doubleAttack", "crit", "magicLifesteal", "physicalLifesteal", "armor", "magicResist",
                           "stunIgnore", "aoeResist", "stunOnHit", "rangedDamage"})
        CHECK(std::string(AuraId(ParseAura(id))) == id && AuraValue(ParseAura(id)) > 0);
    const auto t = SumAuras({Aura::AttackSpeed, Aura::AttackSpeed, Aura::Crit, Aura::Armor, Aura::StunOnHit, Aura::None});
    CHECK(Near(t.AttackSpeed, 0.30) && Near(t.Crit, 0.05) && Near(t.Armor, 15) && Near(t.StunOnHitChance, 0.005) && Near(t.MagicResist, 0));
}

void Headshot()
{
    CHECK(Near(HeadshotExtra(100, 1, 0.05), 200));  // extra hit is 2x the original, on top of it
    CHECK(Near(HeadshotExtra(100, 14, 0.0999), 200));
    CHECK(Near(HeadshotExtra(100, 15, 0.05), 300)); // level 15: 3x
    CHECK(Near(HeadshotExtra(100, 15, 0.10), 0));
    int procs = 0;
    for (int i = 0; i < 1000; ++i) procs += HeadshotExtra(10, 1, (i + 0.5) / 1000.0) > 0 ? 1 : 0;
    CHECK(procs == 100);
}

void Artillery()
{
    ArtilleryState s;
    CHECK(!s.Active());
    StartArtillery(s);
    CHECK(s.Active() && Near(s.Remaining, 8));
    CHECK(EffectiveBasicRange(650, 150, true) > 1.0e6);   // infinite range
    CHECK(Near(EffectiveBasicRange(650, 150, false), 800));
    RecordArtilleryDamage(s, 120); RecordArtilleryDamage(s, 80.5); RecordArtilleryDamage(s, -4);
    CHECK(Near(TickArtillery(s, 7.9, 15), 0) && s.Active());
    CHECK(Near(TickArtillery(s, 0.2, 15), 200.5)); // bomb = everything Artillery dealt in the 8s
    CHECK(!s.Active() && Near(s.Accumulated, 0));
    StartArtillery(s); RecordArtilleryDamage(s, 500);
    CHECK(Near(TickArtillery(s, 9, 14), 0));        // no bomb below level 15
    RecordArtilleryDamage(s, 50);
    CHECK(Near(s.Accumulated, 0));                   // inactive windows record nothing
}

void MechTank()
{
    std::vector<MechCandidate> c = {
        {1, 200, true, true, false, false},   // hitting the summoner
        {2, 500, true, false, true, false},   // hitting the DPS
        {3, 300, true, false, false, false},  // idle
        {4, 250, true, false, true, true},    // hitting an ally, already taunted
    };
    CHECK(SelectMechTauntTarget(c, 900) == 1);       // id 2: attacking an ally, not taunted
    CHECK(SelectMechAttackTarget(c, 900) == 3);      // id 4: nearest enemy hitting an ally
    c[1].Alive = false; c[3].Alive = false;
    CHECK(SelectMechTauntTarget(c, 900) == 2);       // idle enemy; never the one on the summoner
    CHECK(SelectMechAttackTarget(c, 900) == 0);      // falls back to the nearest enemy
    c[2].Alive = false;
    CHECK(SelectMechTauntTarget(c, 900) == -1);      // only the summoner's attacker left: no taunt
    CHECK(SelectMechTauntTarget({{9, 1000, true, false, true, false}}, 900) == -1); // out of range
}
}

int main()
{
    PrimaryScaling();
    Inheritance();
    ShieldBlock();
    Level15();
    Headshot();
    Artillery();
    MechTank();
    std::cout << Assertions << " kit assertions; " << Failures << " failures\n";
    return Failures == 0 ? 0 : 1;
}
