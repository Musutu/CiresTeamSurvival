#include "CireItemRules.h"

#include <algorithm>
#include <cmath>

// progression-shop: see CireItemRules.h and Docs/Items.md.
namespace Cires
{
namespace Items
{
namespace
{
struct StatInfo { const char* Key; const char* Label; bool Percent; };
constexpr StatInfo StatTable[StatCount] = {
    {"strength", "Strength", false},
    {"agility", "Agility", false},
    {"intelligence", "Intelligence", false},
    {"health", "Health", false},
    {"mana", "Mana", false},
    {"attackDamage", "Attack Damage", false},
    {"spellPower", "Spell Power", true},
    {"armor", "Armor", false},
    {"ward", "Spell Ward", false},
    {"attackSpeed", "Attack Speed", true},
    {"critChance", "Critical Chance", true},
    {"lifesteal", "Lifesteal", true},
    {"cooldownReduction", "Cooldown Reduction", true},
    {"moveSpeed", "Move Speed", true},
    {"healthRegen", "Health Regen /s", false},
    {"manaRegen", "Mana Regen /s", false},
    {"energyRegen", "Energy Regen /s", false},
    {"primaryStat", "Primary Stat", false},
    {"damageReduction", "Damage Reduction", true},
    {"damageBlock", "Damage Block per hit", false},
};

struct KeyedEffect { const char* Key; EffectKind Kind; };
constexpr KeyedEffect EffectTable[] = {
    {"none", EffectKind::None}, {"healOverTime", EffectKind::HealOverTime},
    {"manaOverTime", EffectKind::ManaOverTime}, {"elixir", EffectKind::Elixir},
    {"ward", EffectKind::Ward}, {"experience", EffectKind::Experience},
    {"primaryStat", EffectKind::PrimaryStat}, {"damageArea", EffectKind::DamageArea},
    {"selfBarrier", EffectKind::SelfBarrier}, {"shieldAllies", EffectKind::ShieldAllies},
    {"tauntArea", EffectKind::TauntArea}, {"healAllies", EffectKind::HealAllies},
    {"haste", EffectKind::Haste},
    {"partyBarrier", EffectKind::PartyBarrier}, {"partyBuff", EffectKind::PartyBuff},
    {"healTarget", EffectKind::HealTarget},
};

struct KeyedPassive { const char* Key; PassiveKind Kind; };
constexpr KeyedPassive PassiveTable[] = {
    {"critMultiplier", PassiveKind::CritMultiplier}, {"abilityLifesteal", PassiveKind::AbilityLifesteal},
    {"executeBonus", PassiveKind::ExecuteBonus}, {"everyNthHit", PassiveKind::EveryNthHit},
    {"spellPowerAmp", PassiveKind::SpellPowerAmp}, {"thorns", PassiveKind::Thorns},
    {"lowHealthShield", PassiveKind::LowHealthShield}, {"healAmp", PassiveKind::HealAmp},
    {"auraRegen", PassiveKind::AuraRegen},
    {"constructLimit", PassiveKind::ConstructLimit}, {"constructShield", PassiveKind::ConstructShield},
    {"summonPower", PassiveKind::SummonPower}, {"areaAmp", PassiveKind::AreaAmp},
    {"controlAmp", PassiveKind::ControlAmp}, {"ultimateUpgrade", PassiveKind::UltimateUpgrade},
    {"attackSplash", PassiveKind::AttackSplash}, {"manaRefund", PassiveKind::ManaRefund},
    {"dodgeCharges", PassiveKind::DodgeCharges}, {"rollHaste", PassiveKind::RollHaste},
};

// SplitMix64: portable, identical across standard libraries and the Unreal build.
class Random
{
public:
    explicit Random(std::uint64_t seed) : State(seed ^ 0x9E3779B97F4A7C15ull) {}
    std::uint64_t Next()
    {
        std::uint64_t z = (State += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double Unit() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
    int Bounded(int count) { return count <= 1 ? 0 : static_cast<int>(Next() % static_cast<std::uint64_t>(count)); }
private:
    std::uint64_t State;
};

bool Finite(double value) { return std::isfinite(value); }
} // namespace

const char* StatKey(ItemStat stat) { return StatTable[static_cast<int>(stat)].Key; }
const char* StatLabel(ItemStat stat) { return StatTable[static_cast<int>(stat)].Label; }
bool StatIsPercent(ItemStat stat) { return StatTable[static_cast<int>(stat)].Percent; }
bool ParseStatKey(const std::string& key, ItemStat& out)
{
    for (int index = 0; index < StatCount; ++index)
        if (key == StatTable[index].Key) { out = static_cast<ItemStat>(index); return true; }
    return false;
}

StatBlock& StatBlock::operator+=(const StatBlock& other)
{
    for (int index = 0; index < StatCount; ++index) Values[index] += other.Values[index];
    return *this;
}
bool StatBlock::IsZero() const
{
    return std::all_of(Values.begin(), Values.end(), [](double value) { return value == 0.0; });
}

const char* TierName(ItemTier tier)
{
    switch (tier)
    {
    case ItemTier::Consumable: return "Consumable";
    case ItemTier::Basic: return "Basic";
    case ItemTier::Epic: return "Epic";
    default: return "Legendary";
    }
}

bool ParseEffectKind(const std::string& key, EffectKind& out)
{
    for (const auto& entry : EffectTable) if (key == entry.Key) { out = entry.Kind; return true; }
    return false;
}
const char* EffectKey(EffectKind kind)
{
    for (const auto& entry : EffectTable) if (entry.Kind == kind) return entry.Key;
    return "none";
}
bool ParsePassiveKind(const std::string& key, PassiveKind& out)
{
    for (const auto& entry : PassiveTable) if (key == entry.Key) { out = entry.Kind; return true; }
    return false;
}

bool ItemDef::HasTag(const std::string& tag) const
{
    return std::find(Tags.begin(), Tags.end(), tag) != Tags.end();
}

int Inventory::CountOf(const std::string& id) const
{
    int count = 0;
    for (const auto& slot : Equipment) if (slot.Id == id) ++count;
    for (const auto& slot : Belt) if (slot.Id == id) ++count;
    return count;
}
int Inventory::FreeEquipment() const
{
    return static_cast<int>(std::count_if(Equipment.begin(), Equipment.end(), [](const Slot& slot) { return slot.Empty(); }));
}

// ------------------------------------------------------------------ catalog
const ItemDef* Catalog::Find(const std::string& id) const
{
    for (const auto& item : Items) if (item.Id == id) return &item;
    return nullptr;
}

std::vector<std::string> Catalog::BuildsInto(const std::string& id) const
{
    std::vector<std::string> result;
    for (const auto& item : Items)
        if (std::find(item.Components.begin(), item.Components.end(), id) != item.Components.end() &&
            std::find(result.begin(), result.end(), item.Id) == result.end())
            result.push_back(item.Id);
    return result;
}

int Catalog::ComputeTotal(std::size_t index, std::vector<int>& state, std::string& error)
{
    // state: 0 = unvisited, 1 = visiting (cycle guard), 2 = done
    if (state[index] == 2) return Items[index].TotalCost;
    if (state[index] == 1) { error = "Recipe cycle through " + Items[index].Id; return 0; }
    state[index] = 1;
    long long total = Items[index].RecipeCost;
    for (const auto& component : Items[index].Components)
    {
        std::size_t found = Items.size();
        for (std::size_t other = 0; other < Items.size(); ++other) if (Items[other].Id == component) { found = other; break; }
        if (found == Items.size()) { error = Items[index].Id + " uses unknown component " + component; return 0; }
        if (Items[found].Belt || Items[found].Instant) { error = Items[index].Id + " cannot use consumable component " + component; return 0; }
        total += ComputeTotal(found, state, error);
        if (!error.empty()) return 0;
    }
    if (total < 0 || total > 100000) { error = Items[index].Id + " has an invalid total cost"; return 0; }
    Items[index].TotalCost = static_cast<int>(total);
    state[index] = 2;
    return Items[index].TotalCost;
}

std::string Catalog::Finalize()
{
    Finalized = false;
    if (Items.empty()) return "Item catalog is empty";
    for (std::size_t index = 0; index < Items.size(); ++index)
    {
        const auto& item = Items[index];
        if (item.Id.empty() || item.Id.size() > 48) return "Item id is empty or too long";
        for (std::size_t other = index + 1; other < Items.size(); ++other)
            if (Items[other].Id == item.Id) return "Duplicate item id " + item.Id;
        if (item.Name.empty()) return item.Id + " has no name";
        if (item.RecipeCost < 0) return item.Id + " has a negative cost";
        if (item.MaxStack < 1 || item.MaxStack > 20) return item.Id + " has an invalid stack size";
        if (item.Belt && item.Instant) return item.Id + " cannot be both belt and instant";
        if ((item.Belt || item.Instant) && !item.Use.IsSet()) return item.Id + " consumable has no effect";
        if ((item.Belt || item.Instant) && !item.Components.empty()) return item.Id + " consumables cannot have recipes";
        if (!item.Belt && item.MaxStack != 1) return item.Id + " only belt items can stack";
        for (const double value : item.Stats.Values) if (!Finite(value) || std::abs(value) > 5000) return item.Id + " has an invalid stat";
        const Effect& use = item.Use;
        if (!Finite(use.Amount) || !Finite(use.Scaling) || !Finite(use.Radius) || !Finite(use.Duration) || !Finite(use.Cooldown) ||
            use.Amount < 0 || use.Radius < 0 || use.Duration < 0 || use.Cooldown < 0 || use.Duration > 900 || use.Cooldown > 900 || use.Radius > 5000)
            return item.Id + " has invalid effect numbers";
        for (const auto& passive : item.Passives)
            if (passive.Kind == PassiveKind::None || passive.Name.empty() || !Finite(passive.Amount) || passive.Amount < 0 || passive.Amount > 500)
                return item.Id + " has an invalid passive";
    }
    std::vector<int> state(Items.size(), 0);
    std::string error;
    for (std::size_t index = 0; index < Items.size(); ++index)
    {
        ComputeTotal(index, state, error);
        if (!error.empty()) return error;
    }
    for (const auto& item : Items)
        if (item.Purchasable && item.TotalCost <= 0) return item.Id + " is purchasable for free";
    Finalized = true;
    return {};
}

// ------------------------------------------------------------------ shop
ShopAccess CheckShopAccess(const ShopRules& rules, int matchPhase, double distanceToTown, bool dead)
{
    if (dead) return ShopAccess::Dead;
    const bool inTown = Finite(distanceToTown) && distanceToTown <= rules.TownRadius;
    switch (matchPhase)
    {
    case 1: // intermission / prep
        return rules.BuyAnywhereInPrep || inTown ? ShopAccess::Allowed : ShopAccess::NotInTown;
    case 4: // recovery
        if (!rules.TownShoppingDuringRecovery) return ShopAccess::WrongPhase;
        return inTown ? ShopAccess::Allowed : ShopAccess::NotInTown;
    case 0: // survival waves
        if (!rules.TownShoppingDuringWaves) return ShopAccess::WrongPhase;
        return inTown ? ShopAccess::Allowed : ShopAccess::NotInTown;
    default:
        return ShopAccess::WrongPhase;
    }
}

std::string ShopAccessMessage(ShopAccess access)
{
    switch (access)
    {
    case ShopAccess::Allowed: return {};
    case ShopAccess::Dead: return "The fallen cannot trade.";
    case ShopAccess::NotInTown: return "Return to town to trade (prep intermission allows buying anywhere).";
    default: return "The market is shut: buy anywhere during the prep intermission, or in town during recovery.";
    }
}

namespace
{
// Recursively marks owned parts of a recipe as consumed, LoL style: an owned
// component is used whole; a missing one is replaced by its own owned parts.
void ConsumeOwned(const Catalog& catalog, const Inventory& inventory, const ItemDef& item,
                  std::vector<int>& consumed, int& discount)
{
    for (const auto& componentId : item.Components)
    {
        int owned = -1;
        for (int index = 0; index < EquipmentSlots; ++index)
            if (inventory.Equipment[index].Id == componentId &&
                std::find(consumed.begin(), consumed.end(), index) == consumed.end()) { owned = index; break; }
        const ItemDef* component = catalog.Find(componentId);
        if (!component) continue;
        if (owned >= 0) { consumed.push_back(owned); discount += component->TotalCost; }
        else ConsumeOwned(catalog, inventory, *component, consumed, discount);
    }
}
} // namespace

PurchasePlan PlanPurchase(const Catalog& catalog, const Inventory& inventory, const std::string& id, int gold)
{
    PurchasePlan plan;
    const ItemDef* item = catalog.Find(id);
    if (!item) { plan.Error = "Unknown item."; return plan; }
    if (!item->Purchasable) { plan.Error = item->Name + " is only found in challenge loot."; return plan; }
    if (item->Instant)
    {
        plan.Instant = true;
        plan.Cost = item->TotalCost;
    }
    else if (item->Belt)
    {
        plan.ToBelt = true;
        plan.Cost = item->TotalCost;
        for (int index = 0; index < BeltSlots; ++index)
            if (inventory.Belt[index].Id == id && inventory.Belt[index].Charges < item->MaxStack)
            { plan.TargetSlot = index; plan.Stacks = true; break; }
        if (plan.TargetSlot < 0)
        {
            for (int index = 0; index < BeltSlots; ++index) if (inventory.Belt[index].Empty()) { plan.TargetSlot = index; break; }
        }
        if (plan.TargetSlot < 0)
        {
            plan.Error = inventory.CountOf(id) > 0 ? item->Name + " stack is full (" + std::to_string(item->MaxStack) + ")."
                                                    : "Consumable belt is full (3 slots).";
            return plan;
        }
    }
    else
    {
        int discount = 0;
        ConsumeOwned(catalog, inventory, *item, plan.ConsumedEquipment, discount);
        plan.Cost = std::max(0, item->TotalCost - discount);
        const auto consumed = [&](int index) { return std::find(plan.ConsumedEquipment.begin(), plan.ConsumedEquipment.end(), index) != plan.ConsumedEquipment.end(); };
        for (int index = 0; index < EquipmentSlots; ++index)
        {
            const Slot& slot = inventory.Equipment[index];
            if (slot.Empty() || consumed(index)) continue;
            if (item->Unique && slot.Id == id) { plan.Error = item->Name + " is unique: you already own one."; return plan; }
            const ItemDef* owned = catalog.Find(slot.Id);
            if (owned && !item->UniqueGroup.empty() && owned->UniqueGroup == item->UniqueGroup)
            { plan.Error = "Only one " + UniqueGroupLabel(item->UniqueGroup) + " may be carried (" + owned->Name + ")."; return plan; }
        }
        if (!plan.ConsumedEquipment.empty())
            plan.TargetSlot = *std::min_element(plan.ConsumedEquipment.begin(), plan.ConsumedEquipment.end());
        else
            for (int index = 0; index < EquipmentSlots; ++index) if (inventory.Equipment[index].Empty()) { plan.TargetSlot = index; break; }
        if (plan.TargetSlot < 0) { plan.Error = "Inventory full: sell an item first (6 slots)."; return plan; }
    }
    if (gold < plan.Cost)
    {
        plan.Error = "Not enough gold: " + std::to_string(plan.Cost - gold) + " more needed.";
        return plan;
    }
    plan.Ok = true;
    return plan;
}

bool ApplyPurchase(const Catalog& catalog, Inventory& inventory, int& gold, const std::string& id, const PurchasePlan& plan)
{
    const PurchasePlan fresh = PlanPurchase(catalog, inventory, id, gold);
    if (!plan.Ok || !fresh.Ok || fresh.Cost != plan.Cost || fresh.TargetSlot != plan.TargetSlot ||
        fresh.ConsumedEquipment != plan.ConsumedEquipment) return false;
    const ItemDef* item = catalog.Find(id);
    gold -= plan.Cost;
    if (plan.Instant) return true;
    if (plan.ToBelt)
    {
        Slot& slot = inventory.Belt[plan.TargetSlot];
        if (plan.Stacks) ++slot.Charges;
        else { slot = Slot{}; slot.Id = id; slot.Charges = 1; }
        return true;
    }
    for (const int index : plan.ConsumedEquipment) inventory.Equipment[index] = Slot{};
    Slot& slot = inventory.Equipment[plan.TargetSlot];
    slot = Slot{};
    slot.Id = id;
    slot.Charges = item && item->HasActive() ? 1 : 0;
    return true;
}

int SellValue(const Catalog& catalog, const Slot& slot, const ShopRules& rules)
{
    const ItemDef* item = catalog.Find(slot.Id);
    if (!item) return 0;
    const double ratio = std::clamp(Finite(rules.SellRatio) ? rules.SellRatio : 0.6, 0.0, 1.0);
    const int each = static_cast<int>(std::floor(item->TotalCost * ratio));
    return item->Belt ? each * std::max(1, slot.Charges) : each;
}

int Sell(const Catalog& catalog, Inventory& inventory, int& gold, int index, bool belt, const ShopRules& rules)
{
    if (index < 0 || index >= (belt ? BeltSlots : EquipmentSlots)) return -1;
    Slot& slot = belt ? inventory.Belt[index] : inventory.Equipment[index];
    if (slot.Empty() || !catalog.Find(slot.Id)) return -1;
    const int value = SellValue(catalog, slot, rules);
    gold += value;
    slot = Slot{};
    return value;
}

void ShopSession::Record(const Inventory& inventory, int gold, const std::string& label)
{
    History.push_back({inventory, gold, label});
    if (History.size() > 32) History.erase(History.begin());
}

bool ShopSession::Undo(Inventory& inventory, int& gold, std::string& label)
{
    if (History.empty()) return false;
    const ShopSnapshot snapshot = History.back();
    History.pop_back();
    // Keep cooldowns that moved on during the visit; restore contents and gold exactly.
    inventory = snapshot.Before;
    gold = snapshot.GoldBefore;
    label = snapshot.Label;
    return true;
}

// ------------------------------------------------------------------ totals
Totals ComputeTotals(const Catalog& catalog, const Inventory& inventory, const std::vector<StatBlock>& activeBuffs)
{
    Totals totals;
    std::vector<std::string> seenPassives;
    double spellAmp = 0;
    for (const auto& slot : inventory.Equipment)
    {
        const ItemDef* item = catalog.Find(slot.Id);
        if (!item) continue;
        totals.Stats += item->Stats;
        for (const auto& passive : item->Passives)
        {
            if (std::find(seenPassives.begin(), seenPassives.end(), passive.Name) != seenPassives.end()) continue;
            seenPassives.push_back(passive.Name);
            switch (passive.Kind)
            {
            case PassiveKind::CritMultiplier: totals.CritMultiplierBonus += passive.Amount; break;
            case PassiveKind::AbilityLifesteal: totals.AbilityLifesteal += passive.Amount; break;
            case PassiveKind::ExecuteBonus:
                totals.ExecuteBonus += passive.Amount;
                totals.ExecuteThreshold = std::max(totals.ExecuteThreshold, passive.Threshold);
                break;
            case PassiveKind::EveryNthHit:
                totals.EveryNthDamage += passive.Amount;
                totals.EveryNthCount = totals.EveryNthCount > 0 ? std::min(totals.EveryNthCount, std::max(1, passive.Count)) : std::max(1, passive.Count);
                break;
            case PassiveKind::SpellPowerAmp: spellAmp += passive.Amount; break;
            case PassiveKind::Thorns: totals.Thorns += passive.Amount; break;
            case PassiveKind::LowHealthShield:
                totals.LowHealthThreshold = std::max(totals.LowHealthThreshold, passive.Threshold);
                totals.LowHealthDuration = std::max(totals.LowHealthDuration, passive.Duration);
                totals.LowHealthCooldown = totals.LowHealthCooldown > 0 ? std::min(totals.LowHealthCooldown, passive.Cooldown) : passive.Cooldown;
                break;
            case PassiveKind::HealAmp: totals.HealAmp += passive.Amount; break;
            case PassiveKind::AuraRegen:
                totals.AuraRegen += passive.Amount;
                totals.AuraRadius = std::max(totals.AuraRadius, passive.Radius);
                break;
            // items-v2
            case PassiveKind::ConstructLimit: totals.ConstructLimitBonus += std::max(0, passive.Count); break;
            case PassiveKind::ConstructShield:
                totals.ConstructShield += passive.Amount;
                totals.ConstructHealth += passive.Threshold;
                break;
            case PassiveKind::SummonPower: totals.SummonPower += passive.Amount; break;
            case PassiveKind::AreaAmp:
                totals.AreaDamage += passive.Amount;
                totals.AreaRadius += passive.Radius;
                break;
            case PassiveKind::ControlAmp:
                totals.ControlDuration += passive.Amount;
                totals.ControlDamage += passive.Threshold;
                break;
            case PassiveKind::UltimateUpgrade: totals.UltimateUpgrade = true; break;
            case PassiveKind::AttackSplash:
                totals.SplashPercent += passive.Amount;
                totals.SplashRadius = std::max(totals.SplashRadius, passive.Radius);
                totals.BasicDamageBonus += passive.Threshold;
                break;
            case PassiveKind::ManaRefund: totals.ManaRefund += passive.Amount; break;
            case PassiveKind::DodgeCharges: totals.DodgeCharges += std::max(0, passive.Count); break;
            case PassiveKind::RollHaste:
                totals.RollHaste = std::max(totals.RollHaste, passive.Amount);
                totals.RollHasteDuration = std::max(totals.RollHasteDuration, passive.Duration);
                break;
            default: break;
            }
        }
    }
    for (const auto& buff : activeBuffs) totals.Stats += buff;
    totals.Stats[ItemStat::SpellPower] *= 1.0 + spellAmp / 100.0;
    return totals;
}

UseResult UseSlot(const Catalog& catalog, Inventory& inventory, int index, bool belt, double now, Effect& effect)
{
    if (index < 0 || index >= (belt ? BeltSlots : EquipmentSlots)) return UseResult::Empty;
    Slot& slot = belt ? inventory.Belt[index] : inventory.Equipment[index];
    if (slot.Empty()) return UseResult::Empty;
    const ItemDef* item = catalog.Find(slot.Id);
    if (!item || !item->Use.IsSet() || item->Instant || (!belt && item->Belt)) return UseResult::NoEffect;
    if (Finite(now) && slot.ReadyAt > now) return UseResult::OnCooldown;
    effect = item->Use;
    if (belt)
    {
        if (--slot.Charges <= 0) slot = Slot{};
        else slot.ReadyAt = now + item->Use.Cooldown;
    }
    else slot.ReadyAt = now + item->Use.Cooldown;
    return UseResult::Used;
}

double Mitigation(double value)
{
    if (!Finite(value) || value <= 0) return 0;
    return std::min(0.75, value / (value + 100.0));
}

// ------------------------------------------------------------------ loot
bool ParseLootKind(const std::string& key, LootKind& out)
{
    if (key == "gold") out = LootKind::Gold;
    else if (key == "experience") out = LootKind::Experience;
    else if (key == "primaryTome") out = LootKind::PrimaryTome;
    else if (key == "item") out = LootKind::Item;
    else return false;
    return true;
}

double PersonalItemShare(int eligiblePlayers, double personalFactor)
{
    const double factor = Finite(personalFactor) ? std::clamp(personalFactor, 0.0, 10.0) : 1.0;
    return std::clamp(factor / std::max(1, eligiblePlayers), 0.0, 1.0);
}

namespace
{
double EntryChance(const LootEntry& entry, int tier, double lootMultiplier, const LootScaling& scaling, double itemShare)
{
    const bool personalShare = entry.Kind == LootKind::Item || entry.Kind == LootKind::PrimaryTome;
    const double share = personalShare ? std::clamp(Finite(itemShare) ? itemShare : 1.0, 0.0, 1.0) : 1.0;
    if (entry.Chance >= 1.0 && share >= 1.0) return 1.0;
    const double chanceScale = (1.0 + std::max(0.0, scaling.ChancePerTier) * (tier - 1)) * lootMultiplier;
    const double base = entry.Chance >= 1.0 ? 1.0 : std::min(scaling.MaxChance, std::max(0.0, entry.Chance) * chanceScale);
    return base * share;
}
} // namespace

double ExpectedPersonalDrops(const LootTable& table, int tier, double lootMultiplier, const LootScaling& scaling, double itemShare)
{
    tier = std::clamp(tier, 1, 10);
    lootMultiplier = std::clamp(Finite(lootMultiplier) ? lootMultiplier : 1.0, 1.0, 1.4);
    double expected = 0;
    for (const auto& entry : table.Entries)
        if (entry.Kind == LootKind::Item || entry.Kind == LootKind::PrimaryTome) expected += EntryChance(entry, tier, lootMultiplier, scaling, itemShare);
    return expected;
}

LootBundle RollLoot(const LootTable& table, int tier, double lootMultiplier, std::uint64_t seed, const LootScaling& scaling, double itemShare)
{
    tier = std::clamp(tier, 1, 10);
    lootMultiplier = std::clamp(Finite(lootMultiplier) ? lootMultiplier : 1.0, 1.0, 1.4);
    Random random(seed ^ (static_cast<std::uint64_t>(tier) << 48));
    LootBundle bundle;
    const double amountScale = 1.0 + std::max(0.0, scaling.GoldPerTier) * (tier - 1);
    for (const auto& entry : table.Entries)
    {
        const double chance = EntryChance(entry, tier, lootMultiplier, scaling, itemShare);
        const double roll = random.Unit();
        if (chance < 1.0 && roll >= chance) continue;
        const int low = std::min(entry.Min, entry.Max), high = std::max(entry.Min, entry.Max);
        const int amount = low + random.Bounded(high - low + 1);
        switch (entry.Kind)
        {
        case LootKind::Gold: bundle.Gold += static_cast<int>(std::lround(amount * amountScale * lootMultiplier)); break;
        case LootKind::Experience: bundle.Experience += static_cast<int>(std::lround(amount * amountScale)); break;
        case LootKind::PrimaryTome: if (amount > 0) bundle.PrimaryTomes.push_back(amount); break;
        case LootKind::Item:
            if (!entry.Pool.empty()) bundle.Items.push_back(entry.Pool[random.Bounded(static_cast<int>(entry.Pool.size()))]);
            break;
        }
    }
    return bundle;
}

int PickFairRecipient(const std::vector<Recipient>& members, bool needsRoom, std::uint64_t seed)
{
    int best = -1;
    std::vector<int> tied;
    for (int index = 0; index < static_cast<int>(members.size()); ++index)
    {
        const auto& member = members[index];
        if (!member.Eligible || (needsRoom && !member.HasRoom)) continue;
        if (best < 0 || member.LootScore < members[best].LootScore) { best = index; tied.assign(1, index); }
        else if (member.LootScore == members[best].LootScore) tied.push_back(index);
    }
    if (tied.empty()) return -1;
    Random random(seed);
    return tied[random.Bounded(static_cast<int>(tied.size()))];
}

// ------------------------------------------------------------------ packs
int BayTier(const PackSchedule& schedule, int bay, int round, int waveInCycle)
{
    for (const auto& entry : schedule.Bays)
    {
        if (entry.Bay != bay) continue;
        if (round < entry.UnlockRound || (round == entry.UnlockRound && waveInCycle < entry.UnlockWave)) return 0;
        int tier = std::max(1, bay);
        if (schedule.PromotionEveryRounds > 0 && round >= schedule.PromotionStartRound)
            tier += 1 + (round - schedule.PromotionStartRound) / schedule.PromotionEveryRounds;
        return std::clamp(tier, 1, std::clamp(schedule.MaxTier, 1, 10));
    }
    return 0;
}

bool BayUnlocksAt(const PackSchedule& schedule, int bay, int round, int waveInCycle)
{
    for (const auto& entry : schedule.Bays)
        if (entry.Bay == bay) return entry.UnlockRound == round && entry.UnlockWave == waveInCycle;
    return false;
}

std::string ValidateSchedule(const PackSchedule& schedule)
{
    if (schedule.Bays.empty() || schedule.Bays.size() > 3) return "packSchedule needs 1-3 bays";
    for (std::size_t index = 0; index < schedule.Bays.size(); ++index)
    {
        const auto& bay = schedule.Bays[index];
        if (bay.Bay < 1 || bay.Bay > 3) return "pack bay must be 1-3";
        if (bay.UnlockRound < 1 || bay.UnlockRound > 100 || bay.UnlockWave < 1 || bay.UnlockWave > 10) return "pack bay unlock out of range";
        for (std::size_t other = index + 1; other < schedule.Bays.size(); ++other)
            if (schedule.Bays[other].Bay == bay.Bay) return "duplicate pack bay";
    }
    if (schedule.MaxTier < 1 || schedule.MaxTier > 10 || schedule.PromotionEveryRounds < 0 || schedule.PromotionStartRound < 1)
        return "pack promotion settings out of range";
    return {};
}

// ------------------------------------------------------------------ teleport
TeleportStart BeginTeleport(TeleportState& state, const TeleportRules& rules, double now, bool allowed, bool instant)
{
    if (!allowed || !Finite(now)) return TeleportStart::NotAllowed;
    if (state.Channeling()) return TeleportStart::AlreadyChanneling;
    if (instant) return TeleportStart::Instant;
    if (now < state.ReadyAt) return TeleportStart::OnCooldown;
    state.ChannelStart = now;
    state.ChannelEnd = now + std::max(0.0, rules.ChannelSeconds);
    return TeleportStart::Channeling;
}

bool InterruptTeleport(TeleportState& state)
{
    if (!state.Channeling()) return false;
    state.ChannelStart = state.ChannelEnd = -1;
    return true;
}

bool CompleteTeleport(TeleportState& state, const TeleportRules& rules, double now)
{
    if (!state.Channeling() || !Finite(now) || now < state.ChannelEnd) return false;
    state.ChannelStart = state.ChannelEnd = -1;
    state.ReadyAt = now + std::max(0.0, rules.CooldownSeconds);
    return true;
}

double TeleportCooldownRemaining(const TeleportState& state, double now)
{
    return Finite(now) ? std::max(0.0, state.ReadyAt - now) : 0.0;
}

int MobValue(const Economy& economy, int wave)
{
    wave = std::max(1, wave);
    const int every = std::max(1, economy.StepEveryWaves);
    return std::max(0, economy.MobBase + economy.MobStep * (wave / every));
}

int KillGold(const Economy& economy, BountyKind kind, int wave, double rewardMultiplier)
{
    double multiplier = 1;
    switch (kind)
    {
    case BountyKind::Armored: multiplier = economy.ArmoredMultiplier; break;
    case BountyKind::Boss: multiplier = economy.BossMultiplier; break;
    case BountyKind::PackUnit: multiplier = economy.PackUnitMultiplier; break;
    case BountyKind::PackLeader: multiplier = economy.PackLeaderMultiplier; break;
    default: break;
    }
    const double reward = Finite(rewardMultiplier) ? std::clamp(rewardMultiplier, 0.0, 100.0) : 1.0;
    return static_cast<int>(std::lround(MobValue(economy, wave) * std::max(0.0, multiplier) * reward));
}

int SkillBuyPrice(const SkillShopRules& rules, const Economy& economy, ShopSkillKind kind, int ownedActives, int wave)
{
    const double value = MobValue(economy, wave);
    double units = kind == ShopSkillKind::Ultimate ? rules.UltimatePrice : kind == ShopSkillKind::Passive ? rules.PassivePrice
        : rules.ActivePrice * (1.0 + std::max(0.0, rules.ActiveOwnedGrowth) * std::max(0, ownedActives));
    return std::max(1, static_cast<int>(std::lround(units * value)));
}

int SkillLevelPrice(const SkillShopRules& rules, const Economy& economy, int currentLevel, int wave)
{
    const double growth = std::pow(std::max(1.0, rules.LevelUpGrowth), std::max(0, currentLevel - 1));
    return std::max(1, static_cast<int>(std::lround(rules.LevelUpBase * growth * MobValue(economy, wave))));
}

int SlotsAvailable(const SkillShopRules& rules, ShopSkillKind kind, int wave)
{
    wave = std::max(0, wave);
    switch (kind)
    {
    case ShopSkillKind::Passive: return wave >= rules.PassiveFromWave ? rules.MaxPassive : 0;
    case ShopSkillKind::Ultimate: return wave >= rules.UltimateFromWave ? rules.MaxUltimate : 0;
    default:
    {
        const int every = std::max(1, rules.ActiveSlotEveryWaves);
        return std::clamp(rules.ActiveSlotsStart + wave / every, 0, rules.MaxActive);
    }
    }
}

int NextSlotWave(const SkillShopRules& rules, ShopSkillKind kind, int wave)
{
    const int now = SlotsAvailable(rules, kind, wave);
    for (int later = std::max(0, wave) + 1; later <= wave + 200; ++later)
        if (SlotsAvailable(rules, kind, later) > now) return later;
    return 0;
}

SkillShopResult CheckSkillBuy(const SkillShopRules& rules, const Economy& economy, ShopSkillKind kind, int ownedOfKind,
                              int ownedActives, bool alreadyOwned, bool allowedForChampion, int wave, int gold, int& price)
{
    price = SkillBuyPrice(rules, economy, kind, ownedActives, wave);
    if (!allowedForChampion) return SkillShopResult::NotAllowed;
    if (alreadyOwned) return SkillShopResult::AlreadyOwned;
    if (ownedOfKind >= SlotsAvailable(rules, kind, wave)) return SkillShopResult::SlotLocked;
    if (gold < price) return SkillShopResult::NotEnoughGold;
    return SkillShopResult::Ok;
}

double SkillEffectScale(const SkillShopRules& rules, int level) { return 1.0 + std::max(0.0, rules.EffectPerLevel) * std::max(0, level - 1); }
double SkillCostScale(const SkillShopRules& rules, int level) { return 1.0 + std::max(0.0, rules.CostPerLevel) * std::max(0, level - 1); }
double SkillCooldownScale(const SkillShopRules& rules, int level)
{
    const double trim = std::clamp(rules.CooldownPerLevel, 0.0, 0.5);
    return std::max(std::clamp(rules.MinCooldownFactor, 0.05, 1.0), std::pow(1.0 - trim, std::max(0, level - 1)));
}

double ShiftForPause(double timestamp, double pausedAt, double pauseSeconds)
{
    if (!Finite(timestamp) || !Finite(pausedAt) || !Finite(pauseSeconds) || pauseSeconds <= 0) return timestamp;
    return timestamp > pausedAt ? timestamp + pauseSeconds : timestamp;
}
} // namespace Items
} // namespace Cires

// ---------------------------------------------------------------- items-v2
namespace Cires
{
namespace Items
{
std::string UniqueGroupLabel(const std::string& group)
{
    if (group == "path") return "path-defining unique";
    if (group == "boots") return "boots item";
    return group + " item";
}

const ItemDef* UniqueGroupConflict(const Catalog& catalog, const Inventory& inventory, const std::string& id)
{
    const ItemDef* item = catalog.Find(id);
    if (!item) return nullptr;
    for (const auto& slot : inventory.Equipment)
    {
        if (slot.Empty()) continue;
        const ItemDef* owned = catalog.Find(slot.Id);
        if (!owned) continue;
        if (item->Unique && owned->Id == item->Id) return owned;
        if (!item->UniqueGroup.empty() && owned->UniqueGroup == item->UniqueGroup) return owned;
    }
    return nullptr;
}

std::string ValidateBuild(const Catalog& catalog, const std::vector<std::string>& ids)
{
    std::vector<std::string> groups, uniques;
    int bag = 0;
    for (const auto& id : ids)
    {
        const ItemDef* item = catalog.Find(id);
        if (!item) return "unknown item " + id;
        if (!item->Purchasable) return id + " is not sold in the shop";
        if (item->Instant || item->Belt) continue;
        if (++bag > EquipmentSlots) return "more than six bag items";
        if (item->Unique)
        {
            if (std::find(uniques.begin(), uniques.end(), id) != uniques.end()) return id + " is unique";
            uniques.push_back(id);
        }
        if (!item->UniqueGroup.empty())
        {
            if (std::find(groups.begin(), groups.end(), item->UniqueGroup) != groups.end()) return "two " + UniqueGroupLabel(item->UniqueGroup) + "s (" + id + ")";
            groups.push_back(item->UniqueGroup);
        }
    }
    return {};
}

double ManaRegenPerSecond(const ManaRules& rules, double maxMana, double itemRegen, double regenMultiplier)
{
    const double base = std::max(0.0, rules.RegenFlat) + std::max(0.0, maxMana) * std::max(0.0, rules.RegenPercent);
    return base * std::max(0.0, regenMultiplier) + std::max(0.0, itemRegen);
}

double ManaCostScale(const ManaRules& rules, int heroLevel)
{
    const double scale = 1.0 + std::max(0.0, rules.CostPerLevel) * std::max(0, heroLevel - 1);
    return std::clamp(scale, 1.0, std::max(1.0, rules.MaxCostScale));
}

namespace
{
void Refill(ChargeState& state, int maxCharges, double rechargeSeconds, double now)
{
    maxCharges = std::max(1, maxCharges);
    const double step = std::max(0.01, rechargeSeconds);
    // Bounded: at most maxCharges refills per call.
    for (int guard = 0; guard < maxCharges && state.Charges < maxCharges && now >= state.ReadyAt; ++guard)
    {
        ++state.Charges;
        if (state.Charges < maxCharges) state.ReadyAt += step;
    }
    if (state.Charges >= maxCharges) state.Charges = maxCharges;
    if (state.Charges < maxCharges && state.ReadyAt < now - step) state.ReadyAt = now; // long gaps never bank time
}
}

int AvailableCharges(const ChargeState& state, int maxCharges, double rechargeSeconds, double now)
{
    ChargeState copy = state;
    Refill(copy, maxCharges, rechargeSeconds, now);
    return copy.Charges;
}

bool SpendCharge(ChargeState& state, int maxCharges, double rechargeSeconds, double now)
{
    Refill(state, maxCharges, rechargeSeconds, now);
    if (state.Charges <= 0) return false;
    if (state.Charges >= std::max(1, maxCharges)) state.ReadyAt = now + std::max(0.01, rechargeSeconds);
    --state.Charges;
    return true;
}

double ChargeCooldown(const ChargeState& state, int maxCharges, double rechargeSeconds, double now)
{
    ChargeState copy = state;
    Refill(copy, maxCharges, rechargeSeconds, now);
    return copy.Charges >= std::max(1, maxCharges) ? 0.0 : std::max(0.0, copy.ReadyAt - now);
}
} // namespace Items
} // namespace Cires

namespace Cires
{
namespace Items
{
bool StatAllowed(ItemStat stat, ItemTier tier)
{
    switch (stat)
    {
    case ItemStat::Primary: case ItemStat::Health: case ItemStat::Mana: case ItemStat::Armor: case ItemStat::Ward:
    case ItemStat::AttackSpeed: case ItemStat::CooldownReduction: case ItemStat::MoveSpeed:
    case ItemStat::HealthRegen: case ItemStat::ManaRegen: case ItemStat::EnergyRegen:
        return true;
    case ItemStat::DamageReduction: case ItemStat::DamageBlock: case ItemStat::CritChance: case ItemStat::Lifesteal:
        return tier == ItemTier::Legendary;
    default:
        return false;   // Strength/Agility/Intelligence, Attack Damage, Spell Power
    }
}

std::string ValidateStatPolicy(const Catalog& catalog)
{
    for (const auto& item : catalog.Items)
        for (int index = 0; index < StatCount; ++index)
        {
            const auto stat = static_cast<ItemStat>(index);
            if (item.Stats.Get(stat) != 0 && !StatAllowed(stat, item.Tier))
                return item.Id + " grants " + StatKey(stat) + " (items grant the primary stat and flat stats only)";
            if (item.Use.Buff.Get(stat) != 0 && !StatAllowed(stat, ItemTier::Legendary))
                return item.Id + " buff grants " + StatKey(stat);
        }
    return {};
}

double ApplyItemMitigation(double amount, double reductionPercent, double block)
{
    if (!(amount > 0)) return 0;
    const double reduced = amount * (1.0 - std::clamp(reductionPercent, 0.0, MaxItemDamageReduction) / 100.0);
    return std::max(reduced * MinBlockedFraction, reduced - std::max(0.0, block));
}
} // namespace Items
} // namespace Cires
