#pragma once
// scaling-kits: engine-independent maths for the combat-scaling overhaul (Eric's rulings).
//   1. Every ability's damage / heal / shield / DoT = base + coefficient x the caster's PRIMARY stat.
//   2. Summons and constructs inherit the owner's attack speed and cooldown reduction, and
//      deal damage off the owner's primary stat.
//   3. Shield-bearing tanks: -10% armour and magic resist, 30% chance to block 50% of physical damage.
//   4. Skill level 15: every active gains one bonus mechanic; every passive grants a team aura.
//   5. Headshot, Artillery and the Mechanical Tank's target choice.
// Engine code (CireScalingKits) calls these; Tests/KitRulesTests.cpp covers them.
#include <cstdint>
#include <string>
#include <vector>

namespace Cires
{
namespace Kits
{
    // ---- 1. universal primary-stat scaling ----
    enum class Primary : std::uint8_t { Strength, Agility, Intelligence };
    struct Attributes { int Strength = 10, Agility = 10, Intelligence = 10; };
    int PrimaryValue(const Attributes& stats, Primary primary);
    // base + coefficient x primary; negative/nonfinite inputs clamp to 0.
    double ScaledAmount(double base, double primaryCoefficient, int primaryValue);
    const char* PrimaryShortName(Primary primary); // "STR" / "AGI" / "INT"

    // ---- 2. owner inheritance (summons, constructs) ----
    // The owner's attack-speed multiplier (1.0 = none) shortens the unit's attack interval.
    double InheritedAttackInterval(double baseIntervalSeconds, double ownerAttackSpeedMultiplier);
    // The owner's cooldown reduction (0..MaxCooldownReduction) shortens the unit's ability cooldowns.
    double InheritedCooldown(double baseSeconds, double ownerCooldownReduction);
    // Per-hit damage of a summon / construct: base + coefficient x owner primary.
    double UnitHitDamage(double base, double primaryCoefficient, int ownerPrimaryValue);

    // ---- 3. shield-bearing tanks ----
    constexpr double ShieldDefensePenalty = 0.10;   // -10% armour and magic resist
    constexpr double ShieldBlockChance = 0.30;      // 30% chance...
    constexpr double ShieldBlockFraction = 0.50;    // ...to block 50% of a physical hit
    double ShieldDefenseMultiplier(bool shieldTank); // 0.9 or 1
    struct BlockResult { bool Blocked = false; double Damage = 0; double Prevented = 0; };
    // roll is uniform [0,1). Only physical damage can be blocked.
    BlockResult ResolveShieldBlock(double damage, bool physical, bool shieldTank, double roll);

    // ---- 4a. level-15 active bonuses ----
    constexpr int BonusLevel = 15;
    enum class Level15Bonus : std::uint8_t { None, Dot, HealCut, Stun, Slow, DamageAmp, Vulnerability, Purge };
    Level15Bonus ParseLevel15(const std::string& id); // "dot", "healCut", "stun", "slow", "damageAmp", "vulnerability", "purge"
    const char* Level15Id(Level15Bonus bonus);
    bool Level15Unlocked(int skillLevel);
    struct Level15Numbers
    {
        double DotFractionOfHit = 0.40;  // DoT total = 40% of the triggering hit, over 4s
        double DotSeconds = 4.0;
        double HealCutFraction = 0.40, HealCutSeconds = 4.0;
        double StunSeconds = 0.75, StunInternalCooldown = 6.0;
        double SlowSeconds = 2.5;
        double DamageAmp = 0.12, DamageAmpSeconds = 5.0; // target takes +12% damage
        double VulnerabilityIgnore = 0.20, VulnerabilitySeconds = 5.0; // ignore 20% of the target's defences
    };
    const Level15Numbers& L15();
    double VulnerableDefense(double defense, bool vulnerable); // defense x 0.8 while vulnerable
    double AmplifiedDamage(double damage, bool amplified);     // damage x 1.12 while amplified
    double DotTotal(double triggeringHit);

    // ---- 4b. level-15 passive team auras ----
    enum class Aura : std::uint8_t { None, AttackSpeed, DoubleAttack, Crit, MagicLifesteal, PhysicalLifesteal, Armor, MagicResist,
                                     StunIgnore, AoeResist, StunOnHit, RangedDamage };
    Aura ParseAura(const std::string& id);
    const char* AuraId(Aura aura);
    double AuraValue(Aura aura);  // 0.30 AS, 0.10 double, 0.05 crit, 0.05 / 0.05 lifesteal, 20 armour, 20 MR, 0.05, 0.15, 0.005, 0.10
    struct AuraTotals
    {
        double AttackSpeed = 0, DoubleAttackChance = 0, Crit = 0, MagicLifesteal = 0, PhysicalLifesteal = 0;
        double Armor = 0, MagicResist = 0, StunIgnoreChance = 0, AoeResistChance = 0, StunOnHitChance = 0, RangedDamage = 0;
    };
    // Each distinct aura counts once per party (two Battle Rhythm owners do not stack the same aura).
    AuraTotals SumAuras(const std::vector<Aura>& partyAuras);

    // ---- 5a. Headshot ----
    constexpr double HeadshotChance = 0.10;
    // Extra hit (on top of the normal hit): 2x the original hit, 3x at skill level 15. 0 when the roll misses.
    double HeadshotExtra(double hitDamage, int skillLevel, double roll);

    // ---- 5b. Artillery (active) ----
    constexpr double ArtilleryDurationSeconds = 8.0;
    constexpr double ArtilleryAttackSpeedBonus = 1.0; // +100% attack speed
    constexpr double ArtilleryBombRadius = 700.0;     // large radius (cm)
    struct ArtilleryState
    {
        double Remaining = 0, Accumulated = 0;
        bool Active() const { return Remaining > 0; }
    };
    void StartArtillery(ArtilleryState& state);
    void RecordArtilleryDamage(ArtilleryState& state, double applied);
    // Advances the timer; returns the bomb damage (accumulated damage) on the tick the window
    // ends when the skill is level 15+, else 0. The accumulated total resets either way.
    double TickArtillery(ArtilleryState& state, double dt, int skillLevel);
    // Basic range while Artillery is active is unbounded; otherwise the normal range (+ passives).
    double EffectiveBasicRange(double baseRange, double bonusRange, bool artilleryActive);

    // ---- 5c. Mechanical Tank target choice ----
    struct MechCandidate
    {
        int Id = 0;
        double Distance = 0;
        bool Alive = true;
        bool AttackingSummoner = false;  // its current victim is the summoner
        bool AttackingAlly = false;      // its current victim is another allied unit (DPS / support)
        bool AlreadyTaunted = false;
    };
    // Taunt: an enemy that is NOT attacking the summoner, preferring those attacking another ally,
    // then the nearest; already-taunted enemies last. -1 when nobody qualifies (within range).
    int SelectMechTauntTarget(const std::vector<MechCandidate>& candidates, double range);
    // Attack: the nearest enemy attacking an ally other than the summoner; falls back to the nearest enemy.
    int SelectMechAttackTarget(const std::vector<MechCandidate>& candidates, double range);
    constexpr double MechSlowAttackSpeed = 0.10;  // level-15 slam: -10% enemy attack speed
}
}
