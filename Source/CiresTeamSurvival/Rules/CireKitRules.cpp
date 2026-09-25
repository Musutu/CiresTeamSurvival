#include "CireKitRules.h"

#include <algorithm>
#include <cmath>

namespace Cires
{
namespace Kits
{
namespace
{
double Finite(double v, double fallback = 0.0) { return std::isfinite(v) ? v : fallback; }
constexpr double MaxCooldownReductionCap = 0.60; // mirrors Cires::MaxCooldownReduction
}

int PrimaryValue(const Attributes& s, Primary p)
{
    return p == Primary::Strength ? s.Strength : p == Primary::Agility ? s.Agility : s.Intelligence;
}

double ScaledAmount(double base, double coefficient, int primaryValue)
{
    const double b = std::max(0.0, Finite(base)), c = std::max(0.0, Finite(coefficient));
    return b + c * std::max(0, primaryValue);
}

const char* PrimaryShortName(Primary p)
{
    return p == Primary::Strength ? "STR" : p == Primary::Agility ? "AGI" : "INT";
}

double InheritedAttackInterval(double baseInterval, double ownerAttackSpeedMultiplier)
{
    const double base = std::max(0.05, Finite(baseInterval, 1.0));
    const double mult = std::clamp(Finite(ownerAttackSpeedMultiplier, 1.0), 0.1, 10.0);
    return std::max(0.1, base / mult);
}

double InheritedCooldown(double baseSeconds, double ownerCdr)
{
    const double base = std::max(0.0, Finite(baseSeconds));
    return base * (1.0 - std::clamp(Finite(ownerCdr), 0.0, MaxCooldownReductionCap));
}

double UnitHitDamage(double base, double coefficient, int ownerPrimary)
{
    return std::min(10000.0, ScaledAmount(base, coefficient, ownerPrimary));
}

double ShieldDefenseMultiplier(bool shieldTank) { return shieldTank ? 1.0 - ShieldDefensePenalty : 1.0; }

BlockResult ResolveShieldBlock(double damage, bool physical, bool shieldTank, double roll)
{
    BlockResult r;
    r.Damage = std::max(0.0, Finite(damage));
    if (!physical || !shieldTank || r.Damage <= 0 || !(Finite(roll, 1.0) < ShieldBlockChance)) return r;
    r.Blocked = true;
    r.Prevented = r.Damage * ShieldBlockFraction;
    r.Damage -= r.Prevented;
    return r;
}

Level15Bonus ParseLevel15(const std::string& id)
{
    if (id == "dot") return Level15Bonus::Dot;
    if (id == "healCut") return Level15Bonus::HealCut;
    if (id == "stun") return Level15Bonus::Stun;
    if (id == "slow") return Level15Bonus::Slow;
    if (id == "damageAmp") return Level15Bonus::DamageAmp;
    if (id == "vulnerability") return Level15Bonus::Vulnerability;
    if (id == "purge") return Level15Bonus::Purge;
    return Level15Bonus::None;
}

const char* Level15Id(Level15Bonus b)
{
    switch (b)
    {
    case Level15Bonus::Dot: return "dot";
    case Level15Bonus::HealCut: return "healCut";
    case Level15Bonus::Stun: return "stun";
    case Level15Bonus::Slow: return "slow";
    case Level15Bonus::DamageAmp: return "damageAmp";
    case Level15Bonus::Vulnerability: return "vulnerability";
    case Level15Bonus::Purge: return "purge";
    default: return "";
    }
}

bool Level15Unlocked(int level) { return level >= BonusLevel; }

const Level15Numbers& L15() { static const Level15Numbers N; return N; }

double VulnerableDefense(double defense, bool vulnerable)
{
    const double d = Finite(defense);
    return vulnerable ? d * (1.0 - L15().VulnerabilityIgnore) : d;
}

double AmplifiedDamage(double damage, bool amplified)
{
    const double d = std::max(0.0, Finite(damage));
    return amplified ? d * (1.0 + L15().DamageAmp) : d;
}

double DotTotal(double hit) { return std::max(0.0, Finite(hit)) * L15().DotFractionOfHit; }

Aura ParseAura(const std::string& id)
{
    if (id == "attackSpeed") return Aura::AttackSpeed;
    if (id == "doubleAttack") return Aura::DoubleAttack;
    if (id == "crit") return Aura::Crit;
    if (id == "magicLifesteal") return Aura::MagicLifesteal;
    if (id == "physicalLifesteal") return Aura::PhysicalLifesteal;
    if (id == "armor") return Aura::Armor;
    if (id == "magicResist") return Aura::MagicResist;
    if (id == "stunIgnore") return Aura::StunIgnore;
    if (id == "aoeResist") return Aura::AoeResist;
    if (id == "stunOnHit") return Aura::StunOnHit;
    if (id == "rangedDamage") return Aura::RangedDamage;
    return Aura::None;
}

const char* AuraId(Aura a)
{
    switch (a)
    {
    case Aura::AttackSpeed: return "attackSpeed";
    case Aura::DoubleAttack: return "doubleAttack";
    case Aura::Crit: return "crit";
    case Aura::MagicLifesteal: return "magicLifesteal";
    case Aura::PhysicalLifesteal: return "physicalLifesteal";
    case Aura::Armor: return "armor";
    case Aura::MagicResist: return "magicResist";
    case Aura::StunIgnore: return "stunIgnore";
    case Aura::AoeResist: return "aoeResist";
    case Aura::StunOnHit: return "stunOnHit";
    case Aura::RangedDamage: return "rangedDamage";
    default: return "";
    }
}

double AuraValue(Aura a)
{
    switch (a)
    {
    case Aura::AttackSpeed: return 0.30;
    case Aura::DoubleAttack: return 0.10;
    case Aura::Crit: return 0.05;
    case Aura::MagicLifesteal: return 0.05;
    case Aura::PhysicalLifesteal: return 0.05;
    case Aura::Armor: return 15.0;
    case Aura::MagicResist: return 15.0;
    case Aura::StunIgnore: return 0.05;
    case Aura::AoeResist: return 0.15;
    case Aura::StunOnHit: return 0.005;
    case Aura::RangedDamage: return 0.10;
    default: return 0.0;
    }
}

AuraTotals SumAuras(const std::vector<Aura>& auras)
{
    AuraTotals t;
    std::vector<Aura> seen;
    for (Aura a : auras)
    {
        if (a == Aura::None || std::find(seen.begin(), seen.end(), a) != seen.end()) continue;
        seen.push_back(a);
        const double v = AuraValue(a);
        switch (a)
        {
        case Aura::AttackSpeed: t.AttackSpeed += v; break;
        case Aura::DoubleAttack: t.DoubleAttackChance += v; break;
        case Aura::Crit: t.Crit += v; break;
        case Aura::MagicLifesteal: t.MagicLifesteal += v; break;
        case Aura::PhysicalLifesteal: t.PhysicalLifesteal += v; break;
        case Aura::Armor: t.Armor += v; break;
        case Aura::MagicResist: t.MagicResist += v; break;
        case Aura::StunIgnore: t.StunIgnoreChance += v; break;
        case Aura::AoeResist: t.AoeResistChance += v; break;
        case Aura::StunOnHit: t.StunOnHitChance += v; break;
        case Aura::RangedDamage: t.RangedDamage += v; break;
        default: break;
        }
    }
    return t;
}

double HeadshotExtra(double hit, int level, double roll)
{
    const double h = std::max(0.0, Finite(hit));
    if (h <= 0 || !(Finite(roll, 1.0) < HeadshotChance)) return 0.0;
    return h * (Level15Unlocked(level) ? 3.0 : 2.0);
}

void StartArtillery(ArtilleryState& s) { s.Remaining = ArtilleryDurationSeconds; s.Accumulated = 0; }

void RecordArtilleryDamage(ArtilleryState& s, double applied)
{
    if (s.Active() && std::isfinite(applied) && applied > 0) s.Accumulated += applied;
}

double TickArtillery(ArtilleryState& s, double dt, int level)
{
    if (!s.Active() || !std::isfinite(dt) || dt <= 0) return 0.0;
    s.Remaining -= dt;
    if (s.Remaining > 0) return 0.0;
    s.Remaining = 0;
    const double bomb = Level15Unlocked(level) ? s.Accumulated : 0.0;
    s.Accumulated = 0;
    return bomb;
}

double EffectiveBasicRange(double baseRange, double bonusRange, bool artilleryActive)
{
    if (artilleryActive) return 1.0e7; // "infinite": any hostile in sight
    return std::max(0.0, Finite(baseRange)) + std::max(0.0, Finite(bonusRange));
}

namespace
{
int SelectMech(const std::vector<MechCandidate>& c, double range, bool taunt)
{
    int best = -1;
    auto rank = [&](const MechCandidate& m)
    {
        // lower is better: attacking-an-ally first, then idle, taunt-only: already taunted last.
        int r = m.AttackingAlly ? 0 : 1;
        if (taunt && m.AlreadyTaunted) r += 2;
        return r;
    };
    for (int i = 0; i < static_cast<int>(c.size()); ++i)
    {
        const auto& m = c[i];
        if (!m.Alive || !std::isfinite(m.Distance) || m.Distance > range) continue;
        if (taunt && m.AttackingSummoner) continue; // taunt only pulls enemies that are NOT on the summoner
        if (best < 0) { best = i; continue; }
        const auto& b = c[best];
        const int ra = rank(m), rb = rank(b);
        if (ra < rb || (ra == rb && (m.Distance < b.Distance || (m.Distance == b.Distance && m.Id < b.Id)))) best = i;
    }
    return best;
}
}

int SelectMechTauntTarget(const std::vector<MechCandidate>& c, double range) { return SelectMech(c, range, true); }

int SelectMechAttackTarget(const std::vector<MechCandidate>& c, double range)
{
    // Protect the team first: never prefer an enemy that is only hitting the summoner.
    std::vector<MechCandidate> filtered = c;
    bool anyAlly = false;
    for (const auto& m : c) anyAlly = anyAlly || (m.Alive && m.AttackingAlly && m.Distance <= range);
    if (anyAlly)
        for (auto& m : filtered) if (!m.AttackingAlly) m.Alive = false;
    return SelectMech(filtered, range, false);
}
}
}
