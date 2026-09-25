#pragma once

// progression-shop: engine-independent item, shop, loot, challenge-pack
// progression, teleport and NPC-pause rules. The Unreal layer loads
// Content/Data/Items.json and LootTables.json into these structures and calls
// the mutations on the authoritative server only. See Docs/Items.md.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Cires
{
namespace Items
{
// Every stat an item, elixir or passive can grant. Percent stats are stored in
// percentage points (12 = 12%).
enum class ItemStat : std::uint8_t
{
    Strength, Agility, Intelligence, Health, Mana, AttackDamage, SpellPower, Armor, Ward,
    AttackSpeed, CritChance, Lifesteal, CooldownReduction, MoveSpeed, HealthRegen, ManaRegen,
    EnergyRegen,
    // items-v2 (Eric's universal-scaling ruling): items grant the owner's primary stat, flat pools,
    // flat armor/ward, and completed-item mitigation.
    Primary,          // adaptive: added to STR, AGI or INT, whichever is the owner's primary
    DamageReduction,  // % of every incoming hit (after armor/ward), capped at 40%
    DamageBlock,      // flat reduction of every incoming hit (after armor/ward), never below 25% of the hit
    Count
};
constexpr int StatCount = static_cast<int>(ItemStat::Count);
const char* StatKey(ItemStat stat);            // JSON key, e.g. "attackDamage"
const char* StatLabel(ItemStat stat);          // UI label, e.g. "Attack Damage"
bool StatIsPercent(ItemStat stat);
bool ParseStatKey(const std::string& key, ItemStat& out);
// items-v2 stat policy: every item may grant Primary, flat Health/Mana, flat Armor/Ward, regen,
// attack speed, cooldown reduction and move speed; only legendary (completed) items may add
// DamageReduction, DamageBlock, CritChance or Lifesteal; nothing grants STR/AGI/INT directly,
// Attack Damage or Spell Power. Returns "" when the catalog follows the policy.
constexpr double MaxItemDamageReduction = 40.0;   // percent
constexpr double MinBlockedFraction = 0.25;       // a block never removes more than 75% of a hit
// Incoming hit after the item mitigation specials (percent reduction, then flat block).
double ApplyItemMitigation(double amount, double reductionPercent, double block);

struct StatBlock
{
    std::array<double, StatCount> Values{};
    double Get(ItemStat stat) const { return Values[static_cast<int>(stat)]; }
    double& operator[](ItemStat stat) { return Values[static_cast<int>(stat)]; }
    StatBlock& operator+=(const StatBlock& other);
    bool IsZero() const;
};

enum class ItemTier : std::uint8_t { Consumable, Basic, Epic, Legendary };
const char* TierName(ItemTier tier);

// What a consumable does when used, or what an active item does on "use".
enum class EffectKind : std::uint8_t
{
    None,
    HealOverTime,   // Amount HP over Duration seconds
    ManaOverTime,   // Amount mana (and Amount/3 energy) over Duration seconds
    Elixir,         // grants Buff stats for Duration seconds (survives death)
    Ward,           // places a lantern ward: Radius, Duration, Amount = slow %, Scaling = mark %
    Experience,     // instant: Amount XP (tomes are consumed on purchase)
    PrimaryStat,    // instant: Amount points of the champion's primary stat
    DamageArea,     // Amount + Scaling * INT spell damage to the target and enemies within Radius
    SelfBarrier,    // Amount % damage reduction for Duration seconds, heal Scaling % max HP
    ShieldAllies,   // allies within Radius gain the 40% guard for Duration seconds
    TauntArea,      // taunt monsters within Radius for Duration seconds
    HealAllies,     // heal allies within Radius for Amount + Scaling * INT
    Haste,          // +Amount % move speed for Duration seconds and cleanse slows
    // items-v2: group on-use actives
    PartyBarrier,   // allies within Radius gain an absorb shield of Amount + Scaling * INT for Duration
    PartyBuff,      // allies within Radius gain the Buff stats for Duration (e.g. +250 armor)
    HealTarget      // heal the targeted ally (yourself when none) for Amount + Scaling * INT, Radius = range
};
bool ParseEffectKind(const std::string& key, EffectKind& out);
const char* EffectKey(EffectKind kind);

struct Effect
{
    EffectKind Kind = EffectKind::None;
    std::string Name;
    double Amount = 0, Scaling = 0, Radius = 0, Duration = 0, Cooldown = 0;
    StatBlock Buff;   // Elixir only
    bool IsSet() const { return Kind != EffectKind::None; }
};

// Unique passives: two items with the same passive Name never stack.
enum class PassiveKind : std::uint8_t
{
    None,
    CritMultiplier,   // +Amount to the critical multiplier (0.25 = 150% -> 175%)
    AbilityLifesteal, // Amount % of ability damage heals you
    ExecuteBonus,     // +Amount % damage against targets below Threshold % health
    EveryNthHit,      // every Count-th basic attack deals +Amount damage
    SpellPowerAmp,    // total spell power x (1 + Amount/100)
    Thorns,           // reflect Amount % of basic-attack damage taken
    LowHealthShield,  // falling below Threshold % health grants the guard for Duration, Cooldown
    HealAmp,          // +Amount % healing done
    AuraRegen,        // allies within Radius regenerate Amount HP per second
    // items-v2: path-defining uniques and unique boots
    ConstructLimit,   // +Count live constructs of each recipe (turrets, pylons, traps, skitters)
    ConstructShield,  // constructs spawn with an absorb shield of Amount % of their max health; +Threshold % health
    SummonPower,      // summons and pets: +Amount % health and damage
    AreaAmp,          // area abilities: +Amount % damage and +Radius % radius (ground effects)
    ControlAmp,       // crowd control you apply lasts +Amount % longer; +Threshold % damage to controlled enemies
    UltimateUpgrade,  // your ultimate also triggers its Ability DB "ultimateUpgrade" effect
    AttackSplash,     // basic attacks deal +Threshold % damage and splash Amount % to enemies within Radius cm
    ManaRefund,       // abilities refund Amount % of their mana cost
    DodgeCharges,     // +Count dodge-roll charges
    RollHaste         // a dodge roll grants +Amount % move speed for Duration seconds
};
bool ParsePassiveKind(const std::string& key, PassiveKind& out);

struct Passive
{
    PassiveKind Kind = PassiveKind::None;
    std::string Name;
    double Amount = 0, Threshold = 0, Radius = 0, Duration = 0, Cooldown = 0;
    int Count = 0;
};

struct ItemDef
{
    std::string Id;
    std::string Name;
    std::string Lore;
    std::string Icon;
    ItemTier Tier = ItemTier::Basic;
    int RecipeCost = 0;              // gold paid on top of the components
    int TotalCost = 0;               // computed by Catalog::Finalize
    std::vector<std::string> Components;
    std::vector<std::string> Tags;   // filters: attack, spell, defense, health, mana, speed, support, active, ...
    StatBlock Stats;
    bool Unique = false;             // at most one copy
    std::string UniqueGroup;         // e.g. "boots": at most one item of the group
    bool Purchasable = true;         // false = loot-only
    bool Instant = false;            // consumed on purchase (tomes); never occupies a slot
    bool Belt = false;               // stacks in the consumable belt
    int MaxStack = 1;
    Effect Use;                      // consumable effect or active ability
    std::vector<Passive> Passives;
    bool HasActive() const { return Use.IsSet() && !Belt && !Instant; }
    bool HasTag(const std::string& tag) const;
};

constexpr int EquipmentSlots = 6;
constexpr int BeltSlots = 3;

struct Slot
{
    std::string Id;
    int Charges = 0;
    double ReadyAt = 0;              // server time when the active/consumable may be used again
    bool Empty() const { return Id.empty(); }
};

struct Inventory
{
    std::array<Slot, EquipmentSlots> Equipment;
    std::array<Slot, BeltSlots> Belt;
    int CountOf(const std::string& id) const;
    int FreeEquipment() const;
};

struct ShopRules
{
    double SellRatio = 0.6;
    bool BuyAnywhereInPrep = true;
    bool TownShoppingDuringWaves = false;
    bool TownShoppingDuringRecovery = true;
    double TownRadius = 900;
};

class Catalog
{
public:
    std::vector<ItemDef> Items;
    // Computes total costs and reverse recipes. Returns an empty string on success,
    // otherwise the first validation failure (unknown component, cycle, bad cost...).
    std::string Finalize();
    const ItemDef* Find(const std::string& id) const;
    std::vector<std::string> BuildsInto(const std::string& id) const;
    bool IsFinalized() const { return Finalized; }
private:
    bool Finalized = false;
    int ComputeTotal(std::size_t index, std::vector<int>& state, std::string& error);
};

bool StatAllowed(ItemStat stat, ItemTier tier);          // items-v2 stat policy (see above)
std::string ValidateStatPolicy(const Catalog& catalog);

// Where an item lands after purchase and which owned parts the recipe consumes.
struct PurchasePlan
{
    bool Ok = false;
    std::string Error;
    int Cost = 0;                        // gold actually charged (after owned components)
    std::vector<int> ConsumedEquipment;  // equipment slot indices removed by the recipe
    int TargetSlot = -1;                 // equipment or belt index; -1 for instant items
    bool ToBelt = false;
    bool Instant = false;
    bool Stacks = false;                 // adds a charge to an existing belt stack
};

// Phase-aware shop permission. Survival shopping is off unless the data enables it.
enum class ShopAccess : std::uint8_t { Allowed, WrongPhase, NotInTown, Dead };
ShopAccess CheckShopAccess(const ShopRules& rules, int matchPhase, double distanceToTown, bool dead);
std::string ShopAccessMessage(ShopAccess access);

PurchasePlan PlanPurchase(const Catalog& catalog, const Inventory& inventory,
                          const std::string& id, int gold);
// Applies a successful plan. Returns false (and changes nothing) if the plan is stale.
bool ApplyPurchase(const Catalog& catalog, Inventory& inventory, int& gold,
                   const std::string& id, const PurchasePlan& plan);

int SellValue(const Catalog& catalog, const Slot& slot, const ShopRules& rules);
// Sells an equipment (belt=false) or belt slot. Returns gold gained, -1 when empty/invalid.
int Sell(const Catalog& catalog, Inventory& inventory, int& gold, int index, bool belt,
         const ShopRules& rules);

// Undo history: one entry per purchase/sale in the current shop visit.
struct ShopSnapshot { Inventory Before; int GoldBefore = 0; std::string Label; };
struct ShopSession
{
    std::vector<ShopSnapshot> History;
    bool Open = false;
    void Record(const Inventory& inventory, int gold, const std::string& label);
    bool Undo(Inventory& inventory, int& gold, std::string& label);
    void Clear() { History.clear(); }
};

// Sum of item stats plus unique passives (SpellPowerAmp applied last).
struct Totals
{
    StatBlock Stats;
    double CritMultiplierBonus = 0;
    double AbilityLifesteal = 0;
    double ExecuteBonus = 0, ExecuteThreshold = 0;
    double EveryNthDamage = 0; int EveryNthCount = 0;
    double Thorns = 0;
    double LowHealthThreshold = 0, LowHealthDuration = 0, LowHealthCooldown = 0;
    double HealAmp = 0;
    double AuraRegen = 0, AuraRadius = 0;
    // items-v2
    int ConstructLimitBonus = 0;
    double ConstructShield = 0, ConstructHealth = 0;
    double SummonPower = 0;
    double AreaDamage = 0, AreaRadius = 0;
    double ControlDuration = 0, ControlDamage = 0;
    bool UltimateUpgrade = false;
    double SplashPercent = 0, SplashRadius = 0, BasicDamageBonus = 0;
    double ManaRefund = 0;
    int DodgeCharges = 0;
    double RollHaste = 0, RollHasteDuration = 0;
};
Totals ComputeTotals(const Catalog& catalog, const Inventory& inventory,
                     const std::vector<StatBlock>& activeBuffs = {});

// Consumes one charge of a belt consumable or triggers an equipment active.
enum class UseResult : std::uint8_t { Used, Empty, NoEffect, OnCooldown };
UseResult UseSlot(const Catalog& catalog, Inventory& inventory, int index, bool belt,
                  double now, Effect& effect);

// Label for a unique group in messages and the shop ("path" -> "path-defining unique").
std::string UniqueGroupLabel(const std::string& group);
// The owned item that blocks buying `id` because of its unique group (nullptr when none).
const ItemDef* UniqueGroupConflict(const Catalog& catalog, const Inventory& inventory, const std::string& id);

// A recommended build (the items a role ends with): every id exists and is purchasable, at most
// six bag items, no two items share a unique id or unique group. Returns "" when valid.
std::string ValidateBuild(const Catalog& catalog, const std::vector<std::string>& ids);

// ---------------------------------------------------------------- mana economy (items-v2)
// Mana regenerates Flat + Percent x max mana per second (+ item regen), times passives such as
// Deep Reserves; mana costs grow with champion level so the pool/cost ratio stays tight.
// Energy is untouched: 100 points, fast regen, flat costs (the snappy resource).
struct ManaRules
{
    double RegenFlat = 2.0;
    double RegenPercent = 0.008;   // of max mana per second (was 0.015 with flat costs)
    double CostPerLevel = 0.05;    // mana cost x (1 + CostPerLevel x (level - 1))
    double MaxCostScale = 3.0;
};
double ManaRegenPerSecond(const ManaRules& rules, double maxMana, double itemRegen, double regenMultiplier = 1.0);
double ManaCostScale(const ManaRules& rules, int heroLevel);

// ---------------------------------------------------------------- dodge-roll charges (items-v2)
// Charges refill one at a time; ReadyAt is when the next missing charge returns.
struct ChargeState { int Charges = 1; double ReadyAt = 0; };
int AvailableCharges(const ChargeState& state, int maxCharges, double rechargeSeconds, double now);
// Spends one charge (refilling lazily first). Returns false when none is available.
bool SpendCharge(ChargeState& state, int maxCharges, double rechargeSeconds, double now);
// Seconds until the next charge returns (0 when full).
double ChargeCooldown(const ChargeState& state, int maxCharges, double rechargeSeconds, double now);

// Armor/ward mitigation: value / (value + 100), never negative, capped at 75%.
double Mitigation(double value);
constexpr double MaxCooldownReductionTotal = 0.60;

// ---------------------------------------------------------------- loot
enum class LootKind : std::uint8_t { Gold, Experience, PrimaryTome, Item };
bool ParseLootKind(const std::string& key, LootKind& out);
struct LootEntry
{
    LootKind Kind = LootKind::Gold;
    double Chance = 1.0;            // independent roll chance (before tier/loot scaling)
    int Min = 0, Max = 0;           // gold / XP / tome points
    std::vector<std::string> Pool;  // item ids; one is chosen uniformly
};
struct LootTable
{
    std::string Id;
    std::string Label;              // e.g. "Gravemaw's hoard"
    std::vector<LootEntry> Entries;
};
struct LootBundle
{
    int Gold = 0;
    int Experience = 0;
    std::vector<int> PrimaryTomes;  // one entry per tome, value = points
    std::vector<std::string> Items;
    bool Empty() const { return Gold <= 0 && Experience <= 0 && PrimaryTomes.empty() && Items.empty(); }
};
struct LootScaling
{
    double GoldPerTier = 0.25;      // +25% gold/XP per tier above 1
    double ChancePerTier = 0.08;    // +8% relative chance per tier above 1
    double MaxChance = 0.95;
};
// Deterministic for a seed; tier >= 1, lootMultiplier clamped to [1, 1.4].
// itemShare scales the chance of tome and item entries only (gold/XP untouched):
// personal loot rolls once per eligible player with itemShare = personalFactor / eligible,
// so the team's expected tomes/items match one shared roll while gold/XP stay per player.
LootBundle RollLoot(const LootTable& table, int tier, double lootMultiplier,
                    std::uint64_t seed, const LootScaling& scaling = {}, double itemShare = 1.0);
// Per-player share of tome/item chances for personal loot (clamped to (0, 1]).
double PersonalItemShare(int eligiblePlayers, double personalFactor = 1.0);
// Expected tomes+items one roll of the table yields (for balance reports/tests).
double ExpectedPersonalDrops(const LootTable& table, int tier, double lootMultiplier, const LootScaling& scaling, double itemShare);

// Team-fair distribution: the eligible member with the lowest loot score (value
// received so far) gets the next personal reward; ties break by seed.
struct Recipient { bool Eligible = true; bool HasRoom = true; int LootScore = 0; };
int PickFairRecipient(const std::vector<Recipient>& members, bool needsRoom, std::uint64_t seed);

// ---------------------------------------------------------------- challenge packs
struct PackBay { int Bay = 1; int UnlockRound = 1; int UnlockWave = 1; };
struct PackSchedule
{
    std::vector<PackBay> Bays;          // bay 1 is nearest town, deeper bays sit toward the spawn
    int PromotionStartRound = 4;        // from this round every bay's tier rises
    int PromotionEveryRounds = 2;       // ... by one every N rounds
    int MaxTier = 8;
};
// Tier the bay hosts in the given round/wave (0 = locked). waveInCycle is 1-based.
int BayTier(const PackSchedule& schedule, int bay, int round, int waveInCycle);
// True when the bay first becomes available exactly at this round/wave.
bool BayUnlocksAt(const PackSchedule& schedule, int bay, int round, int waveInCycle);
std::string ValidateSchedule(const PackSchedule& schedule);

// ---------------------------------------------------------------- teleport to base
struct TeleportRules { double ChannelSeconds = 6.0; double CooldownSeconds = 120.0; double MoveTolerance = 40.0; };
struct TeleportState
{
    double ChannelStart = -1, ChannelEnd = -1;
    double ReadyAt = 0;
    bool Channeling() const { return ChannelEnd >= 0; }
};
enum class TeleportStart : std::uint8_t { Channeling, Instant, OnCooldown, AlreadyChanneling, NotAllowed };
// instant = free town recall during prep/recovery (no cooldown); allowed = phase/life check.
TeleportStart BeginTeleport(TeleportState& state, const TeleportRules& rules, double now,
                            bool allowed, bool instant);
// Damage or movement: cancels without starting the cooldown (hearthstone rule).
bool InterruptTeleport(TeleportState& state);
// Returns true once when the channel completes; starts the cooldown.
bool CompleteTeleport(TeleportState& state, const TeleportRules& rules, double now);
double TeleportCooldownRemaining(const TeleportState& state, double now);

// ---------------------------------------------------------------- gold economy (Eric's ruling)
// A normal mob is worth Base gold, +Step every StepEveryWaves waves (wave 6 = 3 with 1/1/3).
// Armored x2, bosses x10, challenge-pack units x10, Pack Leaders x100 (another x10).
enum class BountyKind : std::uint8_t { Mob, Armored, Boss, PackUnit, PackLeader };
struct Economy
{
    int MobBase = 1;
    int MobStep = 1;
    int StepEveryWaves = 3;
    double ArmoredMultiplier = 2;
    double BossMultiplier = 10;
    double PackUnitMultiplier = 10;
    double PackLeaderMultiplier = 100;
};
int MobValue(const Economy& economy, int wave);                       // wave is the global wave number (>= 1)
int KillGold(const Economy& economy, BountyKind kind, int wave, double rewardMultiplier = 1.0);

// ---------------------------------------------------------------- skill shop
enum class ShopSkillKind : std::uint8_t { Active, Passive, Ultimate };
struct SkillShopRules
{
    // Prices are in mob values, so they follow the gold players actually have (MobValue(wave)).
    double ActivePrice = 15, PassivePrice = 30, UltimatePrice = 60;
    double ActiveOwnedGrowth = 0.25;   // each owned active makes the next one 25% dearer
    double LevelUpBase = 8, LevelUpGrowth = 1.35;
    // Per-level effect scaling (no level cap): effect +x, resource cost +y, cooldown x (1-z) per level.
    double EffectPerLevel = 0.08, CostPerLevel = 0.05, CooldownPerLevel = 0.04, MinCooldownFactor = 0.4;
    // Slot gate: active slots open over the match, passive/ultimate from a wave.
    int ActiveSlotsStart = 2, ActiveSlotEveryWaves = 3, MaxActive = 6;
    int PassiveFromWave = 5, UltimateFromWave = 10;
    int MaxPassive = 1, MaxUltimate = 1;
};
int SkillBuyPrice(const SkillShopRules& rules, const Economy& economy, ShopSkillKind kind, int ownedActives, int wave);
int SkillLevelPrice(const SkillShopRules& rules, const Economy& economy, int currentLevel, int wave);
// Slots of each kind available at this wave (the champion's cap still applies).
int SlotsAvailable(const SkillShopRules& rules, ShopSkillKind kind, int wave);
// Next wave that opens another slot of this kind (0 = none left).
int NextSlotWave(const SkillShopRules& rules, ShopSkillKind kind, int wave);
enum class SkillShopResult : std::uint8_t { Ok, NotOwned, AlreadyOwned, SlotLocked, NotEnoughGold, NotAllowed };
SkillShopResult CheckSkillBuy(const SkillShopRules& rules, const Economy& economy, ShopSkillKind kind, int ownedOfKind,
                              int ownedActives, bool alreadyOwned, bool allowedForChampion, int wave, int gold, int& price);
double SkillEffectScale(const SkillShopRules& rules, int level);    // 1.0 at level 1
double SkillCostScale(const SkillShopRules& rules, int level);
double SkillCooldownScale(const SkillShopRules& rules, int level);

// ---------------------------------------------------------------- NPC pause
// Timestamps that were still in the future when the pause began are pushed back
// by the pause length, so cooldowns/buffs/casts resume exactly where they froze.
double ShiftForPause(double timestamp, double pausedAt, double pauseSeconds);
} // namespace Items
} // namespace Cires
