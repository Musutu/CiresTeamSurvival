// progression-shop: native tests for items, shop, loot, pack gating, teleport and NPC pause rules.
#include "CireItemRules.h"

#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string>

using namespace Cires::Items;

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
bool Near(double a, double b, double epsilon = 1e-9) { return std::abs(a - b) < epsilon; }

ItemDef Make(const std::string& id, ItemTier tier, int recipe, std::vector<std::string> components = {})
{
    ItemDef item;
    item.Id = id;
    item.Name = id;
    item.Tier = tier;
    item.RecipeCost = recipe;
    item.Components = std::move(components);
    return item;
}

Catalog TestCatalog()
{
    Catalog catalog;
    auto sword = Make("sword", ItemTier::Basic, 140); sword.Stats[ItemStat::AttackDamage] = 6;
    auto dagger = Make("dagger", ItemTier::Basic, 130); dagger.Stats[ItemStat::AttackSpeed] = 10;
    auto eye = Make("eye", ItemTier::Basic, 160); eye.Stats[ItemStat::CritChance] = 10;
    auto wand = Make("wand", ItemTier::Basic, 140); wand.Stats[ItemStat::SpellPower] = 10;
    auto stone = Make("stone", ItemTier::Basic, 120); stone.Stats[ItemStat::Health] = 160;
    auto boots = Make("boots", ItemTier::Basic, 150); boots.Stats[ItemStat::MoveSpeed] = 8; boots.UniqueGroup = "boots";
    auto cleaver = Make("cleaver", ItemTier::Epic, 120, {"sword", "dagger"});
    cleaver.Stats[ItemStat::AttackDamage] = 9; cleaver.Stats[ItemStat::AttackSpeed] = 14;
    auto reaver = Make("reaver", ItemTier::Legendary, 300, {"cleaver", "eye"});
    reaver.Unique = true; reaver.Stats[ItemStat::AttackDamage] = 16;
    reaver.Passives.push_back({PassiveKind::CritMultiplier, "Nightfall", 0.25, 0, 0, 0, 0, 0});
    auto reaver2 = Make("reaver_twin", ItemTier::Legendary, 500, {"sword"});
    reaver2.Passives.push_back({PassiveKind::CritMultiplier, "Nightfall", 0.25, 0, 0, 0, 0, 0});
    auto crown = Make("crown", ItemTier::Legendary, 300, {"wand", "wand"});
    crown.Stats[ItemStat::SpellPower] = 28;
    crown.Passives.push_back({PassiveKind::SpellPowerAmp, "Kindled", 25, 0, 0, 0, 0, 0});
    auto treads = Make("treads", ItemTier::Legendary, 150, {"boots", "dagger"}); treads.UniqueGroup = "boots";
    treads.Stats[ItemStat::MoveSpeed] = 12;
    auto orb = Make("orb", ItemTier::Legendary, 250, {"wand"});
    orb.Use.Kind = EffectKind::DamageArea; orb.Use.Amount = 80; orb.Use.Cooldown = 45;
    auto potion = Make("potion", ItemTier::Consumable, 50); potion.Belt = true; potion.MaxStack = 5;
    potion.Use.Kind = EffectKind::HealOverTime; potion.Use.Amount = 150; potion.Use.Duration = 12; potion.Use.Cooldown = 1;
    auto lantern = Make("lantern", ItemTier::Consumable, 75); lantern.Belt = true; lantern.MaxStack = 3;
    lantern.Use.Kind = EffectKind::Ward; lantern.Use.Duration = 120;
    auto elixir = Make("elixir", ItemTier::Consumable, 250); elixir.Belt = true; elixir.MaxStack = 1;
    elixir.Use.Kind = EffectKind::Elixir; elixir.Use.Duration = 180;
    auto tome = Make("tome", ItemTier::Consumable, 100); tome.Instant = true;
    tome.Use.Kind = EffectKind::Experience; tome.Use.Amount = 300;
    auto relic = Make("relic", ItemTier::Legendary, 0); relic.Purchasable = false; relic.Stats[ItemStat::Armor] = 10;
    catalog.Items = {sword, dagger, eye, wand, stone, boots, cleaver, reaver, reaver2, crown, treads, orb,
                     potion, lantern, elixir, tome, relic};
    const std::string error = catalog.Finalize();
    if (!error.empty()) std::cerr << "catalog: " << error << '\n';
    return catalog;
}

void CatalogRules()
{
    Catalog catalog = TestCatalog();
    CHECK(catalog.IsFinalized());
    CHECK(catalog.Find("cleaver")->TotalCost == 390);
    CHECK(catalog.Find("reaver")->TotalCost == 850);
    CHECK(catalog.Find("crown")->TotalCost == 580);
    CHECK(catalog.Find("missing") == nullptr);
    const auto into = catalog.BuildsInto("sword");
    CHECK(into.size() == 2);
    CHECK(catalog.BuildsInto("reaver").empty());

    Catalog cycle;
    cycle.Items = {Make("a", ItemTier::Epic, 10, {"b"}), Make("b", ItemTier::Epic, 10, {"a"})};
    CHECK(cycle.Finalize().find("cycle") != std::string::npos);
    CHECK(!cycle.IsFinalized());
    Catalog unknown;
    unknown.Items = {Make("a", ItemTier::Epic, 10, {"ghost"})};
    CHECK(unknown.Finalize().find("unknown") != std::string::npos);
    Catalog duplicate;
    duplicate.Items = {Make("a", ItemTier::Basic, 10), Make("a", ItemTier::Basic, 10)};
    CHECK(duplicate.Finalize().find("Duplicate") != std::string::npos);
    Catalog freeCatalog;
    freeCatalog.Items = {Make("a", ItemTier::Basic, 0)};
    CHECK(!freeCatalog.Finalize().empty());
    Catalog consumableRecipe;
    auto potion = Make("p", ItemTier::Consumable, 50); potion.Belt = true; potion.Use.Kind = EffectKind::HealOverTime;
    consumableRecipe.Items = {potion, Make("x", ItemTier::Epic, 10, {"p"})};
    CHECK(!consumableRecipe.Finalize().empty());
    Catalog badStack;
    auto stacked = Make("s", ItemTier::Basic, 10); stacked.MaxStack = 3;
    badStack.Items = {stacked};
    CHECK(!badStack.Finalize().empty());
    Catalog nanStat;
    auto nan = Make("n", ItemTier::Basic, 10); nan.Stats[ItemStat::Armor] = std::nan("");
    nanStat.Items = {nan};
    CHECK(!nanStat.Finalize().empty());
    CHECK(std::string(StatKey(ItemStat::AttackDamage)) == "attackDamage");
    ItemStat parsed = ItemStat::Count;
    CHECK(ParseStatKey("ward", parsed) && parsed == ItemStat::Ward);
    CHECK(!ParseStatKey("bogus", parsed));
    for (int index = 0; index < StatCount; ++index)
    {
        ItemStat roundTrip = ItemStat::Count;
        CHECK(ParseStatKey(StatKey(static_cast<ItemStat>(index)), roundTrip) && static_cast<int>(roundTrip) == index);
    }
    EffectKind effect = EffectKind::None;
    CHECK(ParseEffectKind("tauntArea", effect) && effect == EffectKind::TauntArea);
    CHECK(std::string(EffectKey(EffectKind::Haste)) == "haste");
    PassiveKind passive = PassiveKind::None;
    CHECK(ParsePassiveKind("thorns", passive) && passive == PassiveKind::Thorns);
    CHECK(!ParsePassiveKind("nope", passive));
}

void RecipeRules()
{
    const Catalog catalog = TestCatalog();
    Inventory inventory;
    int gold = 2000;
    // Components first, then the recipe consumes them and charges only the remainder.
    auto plan = PlanPurchase(catalog, inventory, "sword", gold);
    CHECK(plan.Ok && plan.Cost == 140 && plan.TargetSlot == 0);
    CHECK(ApplyPurchase(catalog, inventory, gold, "sword", plan));
    CHECK(gold == 1860 && inventory.Equipment[0].Id == "sword");
    plan = PlanPurchase(catalog, inventory, "stone", gold);
    CHECK(ApplyPurchase(catalog, inventory, gold, "stone", plan) && inventory.Equipment[1].Id == "stone");
    plan = PlanPurchase(catalog, inventory, "dagger", gold);
    CHECK(ApplyPurchase(catalog, inventory, gold, "dagger", plan) && inventory.Equipment[2].Id == "dagger");
    plan = PlanPurchase(catalog, inventory, "cleaver", gold);
    CHECK(plan.Ok && plan.Cost == 120 && plan.ConsumedEquipment.size() == 2 && plan.TargetSlot == 0);
    CHECK(ApplyPurchase(catalog, inventory, gold, "cleaver", plan));
    CHECK(inventory.Equipment[0].Id == "cleaver" && inventory.Equipment[1].Id == "stone" && inventory.Equipment[2].Empty());
    CHECK(gold == 2000 - 140 - 120 - 130 - 120);
    // Nested: reaver uses the owned cleaver whole and buys the missing eye.
    plan = PlanPurchase(catalog, inventory, "reaver", gold);
    CHECK(plan.Ok && plan.Cost == 850 - 390 && plan.ConsumedEquipment.size() == 1);
    // Nested discount through a missing epic: own only a sword and an eye.
    Inventory parts;
    parts.Equipment[3].Id = "sword";
    parts.Equipment[5].Id = "eye";
    const auto nested = PlanPurchase(catalog, parts, "reaver", 5000);
    CHECK(nested.Ok && nested.Cost == 850 - 140 - 160 && nested.ConsumedEquipment.size() == 2 && nested.TargetSlot == 3);
    // Two copies of the same component are both consumed by a double recipe.
    Inventory wands;
    wands.Equipment[1].Id = "wand";
    wands.Equipment[4].Id = "wand";
    const auto crown = PlanPurchase(catalog, wands, "crown", 5000);
    CHECK(crown.Ok && crown.Cost == 300 && crown.ConsumedEquipment.size() == 2 && crown.TargetSlot == 1);
    // Stale plans are rejected without mutation.
    Inventory stale = parts;
    int staleGold = 5000;
    stale.Equipment[3] = Slot{};
    CHECK(!ApplyPurchase(catalog, stale, staleGold, "reaver", nested) && staleGold == 5000);
    CHECK(!ApplyPurchase(catalog, stale, staleGold, "sword", PurchasePlan{}) && staleGold == 5000);
    // Gold shortfall reports the exact amount.
    const auto poor = PlanPurchase(catalog, Inventory{}, "reaver", 100);
    CHECK(!poor.Ok && poor.Error.find("750") != std::string::npos);
    CHECK(!PlanPurchase(catalog, Inventory{}, "relic", 99999).Ok);
    CHECK(PlanPurchase(catalog, Inventory{}, "relic", 99999).Error.find("loot") != std::string::npos);
    CHECK(!PlanPurchase(catalog, Inventory{}, "ghost", 99999).Ok);
    // Instant tomes never take a slot.
    Inventory full;
    for (auto& slot : full.Equipment) slot.Id = "stone";
    const auto tome = PlanPurchase(catalog, full, "tome", 100);
    CHECK(tome.Ok && tome.Instant && tome.TargetSlot == -1 && tome.Cost == 100);
    int tomeGold = 100;
    CHECK(ApplyPurchase(catalog, full, tomeGold, "tome", tome) && tomeGold == 0);
}

void UniqueAndSlotRules()
{
    const Catalog catalog = TestCatalog();
    Inventory inventory;
    inventory.Equipment[0].Id = "reaver";
    auto plan = PlanPurchase(catalog, inventory, "reaver", 9999);
    CHECK(!plan.Ok && plan.Error.find("unique") != std::string::npos);
    // Non-unique items may be duplicated.
    inventory.Equipment[1].Id = "stone";
    CHECK(PlanPurchase(catalog, inventory, "stone", 9999).Ok);
    // Unique group: one pair of boots, but upgrading consumes them.
    Inventory feet;
    feet.Equipment[2].Id = "boots";
    plan = PlanPurchase(catalog, feet, "boots", 9999);
    CHECK(!plan.Ok && plan.Error.find("boots") != std::string::npos);
    plan = PlanPurchase(catalog, feet, "treads", 9999);
    CHECK(plan.Ok && plan.Cost == 280 && plan.TargetSlot == 2);
    Inventory upgraded;
    upgraded.Equipment[0].Id = "treads";
    CHECK(!PlanPurchase(catalog, upgraded, "boots", 9999).Ok);
    CHECK(!PlanPurchase(catalog, upgraded, "treads", 9999).Ok);
    // Six slots: a full bag rejects new items but accepts a recipe that frees a slot.
    Inventory full;
    full.Equipment = {Slot{"sword"}, Slot{"dagger"}, Slot{"stone"}, Slot{"stone"}, Slot{"stone"}, Slot{"stone"}};
    CHECK(full.FreeEquipment() == 0);
    plan = PlanPurchase(catalog, full, "eye", 9999);
    CHECK(!plan.Ok && plan.Error.find("full") != std::string::npos);
    plan = PlanPurchase(catalog, full, "cleaver", 9999);
    CHECK(plan.Ok && plan.TargetSlot == 0);
    int gold = 9999;
    CHECK(ApplyPurchase(catalog, full, gold, "cleaver", plan) && full.FreeEquipment() == 1);
    // Belt: stacks up to MaxStack, three distinct stacks.
    Inventory belt;
    int beltGold = 10000;
    for (int count = 0; count < 5; ++count)
    {
        plan = PlanPurchase(catalog, belt, "potion", beltGold);
        CHECK(plan.Ok && plan.ToBelt && plan.TargetSlot == 0 && plan.Stacks == (count > 0));
        CHECK(ApplyPurchase(catalog, belt, beltGold, "potion", plan));
    }
    CHECK(belt.Belt[0].Charges == 5);
    plan = PlanPurchase(catalog, belt, "potion", beltGold);
    CHECK(plan.Ok && plan.TargetSlot == 1 && !plan.Stacks);
    CHECK(ApplyPurchase(catalog, belt, beltGold, "potion", plan));
    CHECK(ApplyPurchase(catalog, belt, beltGold, "lantern", PlanPurchase(catalog, belt, "lantern", beltGold)));
    plan = PlanPurchase(catalog, belt, "elixir", beltGold);
    CHECK(!plan.Ok && plan.Error.find("belt") != std::string::npos);
    Inventory elixirs;
    elixirs.Belt[0] = Slot{"elixir", 1, 0};
    elixirs.Belt[1] = Slot{"potion", 1, 0};
    elixirs.Belt[2] = Slot{"lantern", 1, 0};
    plan = PlanPurchase(catalog, elixirs, "elixir", 9999);
    CHECK(!plan.Ok && plan.Error.find("stack") != std::string::npos);
}

void SellAndUndoRules()
{
    const Catalog catalog = TestCatalog();
    ShopRules rules;
    Inventory inventory;
    inventory.Equipment[0].Id = "reaver";
    inventory.Belt[0] = Slot{"potion", 3, 0};
    CHECK(SellValue(catalog, inventory.Equipment[0], rules) == 510);
    CHECK(SellValue(catalog, inventory.Belt[0], rules) == 90);
    rules.SellRatio = 0.5;
    CHECK(SellValue(catalog, inventory.Equipment[0], rules) == 425);
    rules.SellRatio = 7;
    CHECK(SellValue(catalog, inventory.Equipment[0], rules) == 850);
    rules.SellRatio = 0.6;
    int gold = 0;
    CHECK(Sell(catalog, inventory, gold, 0, false, rules) == 510 && gold == 510 && inventory.Equipment[0].Empty());
    CHECK(Sell(catalog, inventory, gold, 0, false, rules) == -1 && gold == 510);
    CHECK(Sell(catalog, inventory, gold, 0, true, rules) == 90 && gold == 600 && inventory.Belt[0].Empty());
    CHECK(Sell(catalog, inventory, gold, 9, false, rules) == -1 && Sell(catalog, inventory, gold, -1, true, rules) == -1);

    // Undo: every purchase/sale in the visit restores gold and slots exactly.
    ShopSession session;
    Inventory bag;
    int purse = 1000;
    session.Record(bag, purse, "sword");
    CHECK(ApplyPurchase(catalog, bag, purse, "sword", PlanPurchase(catalog, bag, "sword", purse)));
    session.Record(bag, purse, "dagger");
    CHECK(ApplyPurchase(catalog, bag, purse, "dagger", PlanPurchase(catalog, bag, "dagger", purse)));
    session.Record(bag, purse, "cleaver");
    CHECK(ApplyPurchase(catalog, bag, purse, "cleaver", PlanPurchase(catalog, bag, "cleaver", purse)));
    CHECK(purse == 1000 - 390 && bag.Equipment[0].Id == "cleaver");
    session.Record(bag, purse, "sell");
    CHECK(Sell(catalog, bag, purse, 0, false, rules) == 234);
    std::string label;
    CHECK(session.Undo(bag, purse, label) && label == "sell" && bag.Equipment[0].Id == "cleaver" && purse == 610);
    CHECK(session.Undo(bag, purse, label) && label == "cleaver" && bag.Equipment[0].Id == "sword" && bag.Equipment[1].Id == "dagger" && purse == 730);
    CHECK(session.Undo(bag, purse, label) && session.Undo(bag, purse, label));
    CHECK(purse == 1000 && bag.FreeEquipment() == 6);
    CHECK(!session.Undo(bag, purse, label));
    for (int index = 0; index < 40; ++index) session.Record(bag, index, "x");
    CHECK(session.History.size() == 32);
    session.Clear();
    CHECK(session.History.empty());
}

void UseAndTotalsRules()
{
    const Catalog catalog = TestCatalog();
    Inventory inventory;
    inventory.Belt[0] = Slot{"potion", 2, 0};
    inventory.Equipment[0] = Slot{"orb", 1, 0};
    inventory.Equipment[1] = Slot{"sword", 0, 0};
    Effect effect;
    CHECK(UseSlot(catalog, inventory, 0, true, 10, effect) == UseResult::Used && effect.Kind == EffectKind::HealOverTime);
    CHECK(inventory.Belt[0].Charges == 1 && Near(inventory.Belt[0].ReadyAt, 11));
    CHECK(UseSlot(catalog, inventory, 0, true, 10.5, effect) == UseResult::OnCooldown);
    CHECK(UseSlot(catalog, inventory, 0, true, 11, effect) == UseResult::Used && inventory.Belt[0].Empty());
    CHECK(UseSlot(catalog, inventory, 0, true, 12, effect) == UseResult::Empty);
    CHECK(UseSlot(catalog, inventory, 0, false, 20, effect) == UseResult::Used && effect.Kind == EffectKind::DamageArea && Near(effect.Amount, 80));
    CHECK(Near(inventory.Equipment[0].ReadyAt, 65) && inventory.Equipment[0].Id == "orb");
    CHECK(UseSlot(catalog, inventory, 0, false, 64, effect) == UseResult::OnCooldown);
    CHECK(UseSlot(catalog, inventory, 0, false, 65, effect) == UseResult::Used);
    CHECK(UseSlot(catalog, inventory, 1, false, 65, effect) == UseResult::NoEffect);
    CHECK(UseSlot(catalog, inventory, 7, false, 65, effect) == UseResult::Empty);

    Inventory build;
    build.Equipment[0].Id = "reaver";
    build.Equipment[1].Id = "reaver_twin";
    build.Equipment[2].Id = "crown";
    build.Equipment[3].Id = "stone";
    build.Equipment[4].Id = "stone";
    StatBlock buff;
    buff[ItemStat::SpellPower] = 12;
    buff[ItemStat::AttackDamage] = 4;
    const Totals totals = ComputeTotals(catalog, build, {buff});
    CHECK(Near(totals.Stats.Get(ItemStat::AttackDamage), 20));
    CHECK(Near(totals.Stats.Get(ItemStat::Health), 320));
    CHECK(Near(totals.CritMultiplierBonus, 0.25)); // unique passive does not stack
    CHECK(Near(totals.Stats.Get(ItemStat::SpellPower), (28 + 12) * 1.25));
    CHECK(ComputeTotals(catalog, Inventory{}).Stats.IsZero());
    CHECK(Near(Mitigation(100), 0.5) && Near(Mitigation(0), 0) && Near(Mitigation(-5), 0) && Near(Mitigation(1e9), 0.75));
    CHECK(Near(Mitigation(std::nan("")), 0));
    StatBlock sum;
    sum += buff;
    sum += buff;
    CHECK(Near(sum.Get(ItemStat::SpellPower), 24));
}

void ShopAccessRules()
{
    ShopRules rules;
    CHECK(CheckShopAccess(rules, 1, 99999, false) == ShopAccess::Allowed);     // prep: anywhere
    CHECK(CheckShopAccess(rules, 1, 0, true) == ShopAccess::Dead);
    CHECK(CheckShopAccess(rules, 4, 100, false) == ShopAccess::Allowed);       // recovery in town
    CHECK(CheckShopAccess(rules, 4, 5000, false) == ShopAccess::NotInTown);
    CHECK(CheckShopAccess(rules, 0, 100, false) == ShopAccess::WrongPhase);    // waves: closed by default
    CHECK(CheckShopAccess(rules, 2, 100, false) == ShopAccess::WrongPhase);    // arena
    CHECK(CheckShopAccess(rules, 3, 100, false) == ShopAccess::WrongPhase);    // finished
    CHECK(ShopAccessMessage(ShopAccess::WrongPhase).find("intermission") != std::string::npos);
    CHECK(ShopAccessMessage(ShopAccess::Allowed).empty());
    rules.TownShoppingDuringWaves = true;
    CHECK(CheckShopAccess(rules, 0, 100, false) == ShopAccess::Allowed);
    CHECK(CheckShopAccess(rules, 0, 1000, false) == ShopAccess::NotInTown);
    CHECK(CheckShopAccess(rules, 0, std::nan(""), false) == ShopAccess::NotInTown);
    rules.BuyAnywhereInPrep = false;
    CHECK(CheckShopAccess(rules, 1, 5000, false) == ShopAccess::NotInTown);
}

void LootRules()
{
    LootTable table;
    table.Id = "t";
    LootEntry gold; gold.Kind = LootKind::Gold; gold.Min = 40; gold.Max = 60;
    LootEntry xp; xp.Kind = LootKind::Experience; xp.Chance = 0.5; xp.Min = xp.Max = 150;
    LootEntry tome; tome.Kind = LootKind::PrimaryTome; tome.Chance = 0.25; tome.Min = 1; tome.Max = 2;
    LootEntry item; item.Kind = LootKind::Item; item.Chance = 0.3; item.Pool = {"a", "b", "c"};
    table.Entries = {gold, xp, tome, item};
    const auto first = RollLoot(table, 1, 1.0, 42);
    const auto again = RollLoot(table, 1, 1.0, 42);
    CHECK(first.Gold == again.Gold && first.Experience == again.Experience && first.Items == again.Items && first.PrimaryTomes == again.PrimaryTomes);
    int xpHits = 0, tomeHits = 0, itemHits = 0, highTierItems = 0;
    std::map<std::string, int> picks;
    long long goldTotal = 0, highGold = 0;
    for (std::uint64_t seed = 1; seed <= 20000; ++seed)
    {
        const auto bundle = RollLoot(table, 1, 1.0, seed);
        CHECK(bundle.Gold >= 40 && bundle.Gold <= 60);
        goldTotal += bundle.Gold;
        xpHits += bundle.Experience > 0;
        tomeHits += !bundle.PrimaryTomes.empty();
        itemHits += !bundle.Items.empty();
        for (const auto& id : bundle.Items) ++picks[id];
        for (int points : bundle.PrimaryTomes) CHECK(points >= 1 && points <= 2);
        const auto high = RollLoot(table, 5, 1.4, seed);
        highGold += high.Gold;
        highTierItems += !high.Items.empty();
        CHECK(high.Experience == 0 || high.Experience == 300); // 150 * (1 + 0.25 * 4)
    }
    CHECK(xpHits > 9500 && xpHits < 10500);
    CHECK(tomeHits > 4600 && tomeHits < 5400);
    CHECK(itemHits > 5600 && itemHits < 6400);
    CHECK(picks.size() == 3 && picks["a"] > 1700 && picks["b"] > 1700 && picks["c"] > 1700);
    CHECK(highGold > goldTotal * 2.6 && highGold < goldTotal * 2.9);        // 2x tier scale x 1.4 loot
    CHECK(highTierItems > 10500 && highTierItems < 11700); // 0.3 x 1.32 x 1.4 ~ 0.554
    const auto clamped = RollLoot(table, 99, 50.0, 7);
    const auto capped = RollLoot(table, 10, 1.4, 7);
    CHECK(clamped.Gold == capped.Gold && clamped.Items == capped.Items);
    LootTable empty;
    CHECK(RollLoot(empty, 1, 1, 1).Empty());
    // Personal loot: each player rolls independently; tomes/items scale by 1/eligible so the
    // team's expected drops match one shared roll, while every player keeps full gold/XP.
    CHECK(Near(PersonalItemShare(5), .2) && Near(PersonalItemShare(1), 1) && Near(PersonalItemShare(0), 1) && Near(PersonalItemShare(4, 2), .5) && Near(PersonalItemShare(1, 3), 1));
    const double shared = ExpectedPersonalDrops(table, 3, 1.0, LootScaling{}, 1.0);
    for (int players = 1; players <= 5; ++players)
        CHECK(Near(ExpectedPersonalDrops(table, 3, 1.0, LootScaling{}, PersonalItemShare(players)) * players, shared, 1e-9));
    long long teamItems = 0, teamGold = 0, sharedItems = 0, sharedGold = 0;
    int differing = 0;
    for (std::uint64_t source = 1; source <= 20000; ++source)
    {
        const auto one = RollLoot(table, 1, 1.0, source * 977);
        sharedItems += one.Items.size() + one.PrimaryTomes.size();
        sharedGold += one.Gold;
        std::vector<LootBundle> players;
        for (int player = 0; player < 5; ++player)
        {
            players.push_back(RollLoot(table, 1, 1.0, source * 977 + 0x9E37ull * (player + 1), LootScaling{}, PersonalItemShare(5)));
            teamItems += players.back().Items.size() + players.back().PrimaryTomes.size();
            teamGold += players.back().Gold;
            CHECK(players.back().Gold >= 40 && players.back().Gold <= 60);
        }
        differing += players[0].Gold != players[1].Gold || players[0].Items != players[1].Items;
    }
    CHECK(differing > 14000);                                              // rolls are independent per player
    const double expectedTeam = ExpectedPersonalDrops(table, 1, 1.0, LootScaling{}, 1.0) * 20000;
    CHECK(std::abs(teamItems / expectedTeam - 1.0) < .04);   // five personal rolls ~= one shared roll
    CHECK(std::abs(sharedItems / expectedTeam - 1.0) < .04);
    CHECK(std::abs(static_cast<double>(teamGold) / (sharedGold * 5.0) - 1.0) < .02); // everyone keeps their gold
    LootKind kind = LootKind::Gold;
    CHECK(ParseLootKind("primaryTome", kind) && kind == LootKind::PrimaryTome && !ParseLootKind("x", kind));

    // Fair recipients: lowest score wins, full bags skipped when room is needed.
    std::vector<Recipient> team(5);
    team[0].LootScore = 300; team[1].LootScore = 100; team[2].LootScore = 100; team[3].LootScore = 50; team[4].LootScore = 400;
    CHECK(PickFairRecipient(team, false, 1) == 3);
    team[3].HasRoom = false;
    CHECK(PickFairRecipient(team, false, 1) == 3);
    std::set<int> tieWinners;
    for (std::uint64_t seed = 0; seed < 200; ++seed)
    {
        const int winner = PickFairRecipient(team, true, seed);
        CHECK(winner == 1 || winner == 2);
        tieWinners.insert(winner);
    }
    CHECK(tieWinners.size() == 2);
    team[1].Eligible = false; team[2].Eligible = false;
    CHECK(PickFairRecipient(team, true, 3) == 0);
    for (auto& member : team) member.Eligible = false;
    CHECK(PickFairRecipient(team, false, 3) == -1);
    // Rotation: repeatedly awarding equal value spreads rewards over the whole team.
    std::vector<Recipient> rotation(5);
    std::vector<int> received(5, 0);
    for (int drop = 0; drop < 50; ++drop)
    {
        const int winner = PickFairRecipient(rotation, true, static_cast<std::uint64_t>(drop));
        rotation[winner].LootScore += 100;
        ++received[winner];
    }
    for (int count : received) CHECK(count == 10);
}

void PackScheduleRules()
{
    PackSchedule schedule;
    schedule.Bays = {{1, 1, 1}, {2, 2, 1}, {3, 3, 2}};
    schedule.PromotionStartRound = 4;
    schedule.PromotionEveryRounds = 2;
    schedule.MaxTier = 8;
    CHECK(ValidateSchedule(schedule).empty());
    CHECK(BayTier(schedule, 1, 1, 1) == 1);
    CHECK(BayTier(schedule, 2, 1, 3) == 0);   // tier 2 locked in cycle 1
    CHECK(BayTier(schedule, 3, 1, 3) == 0);
    CHECK(BayTier(schedule, 2, 2, 1) == 2);
    CHECK(BayTier(schedule, 3, 2, 3) == 0);
    CHECK(BayTier(schedule, 3, 3, 1) == 0);   // mid-cycle unlock at wave 2
    CHECK(BayTier(schedule, 3, 3, 2) == 3);
    CHECK(BayTier(schedule, 3, 4, 1) == 4);   // promotions start
    CHECK(BayTier(schedule, 1, 4, 1) == 2);
    CHECK(BayTier(schedule, 1, 5, 1) == 2);
    CHECK(BayTier(schedule, 1, 6, 1) == 3);
    CHECK(BayTier(schedule, 3, 40, 1) == 8);  // capped
    CHECK(BayTier(schedule, 4, 5, 1) == 0);   // unknown bay
    CHECK(BayUnlocksAt(schedule, 2, 2, 1) && !BayUnlocksAt(schedule, 2, 3, 1) && BayUnlocksAt(schedule, 3, 3, 2));
    CHECK(!BayUnlocksAt(schedule, 1, 2, 1));
    // Deeper bays always host an equal or higher tier than nearer ones.
    for (int round = 1; round <= 30; ++round)
        for (int wave = 1; wave <= 3; ++wave)
        {
            const int one = BayTier(schedule, 1, round, wave), two = BayTier(schedule, 2, round, wave), three = BayTier(schedule, 3, round, wave);
            CHECK(one >= 1);
            CHECK(two == 0 || two >= one);
            CHECK(three == 0 || three >= two);
        }
    PackSchedule bad = schedule;
    bad.Bays.push_back({1, 1, 1});
    CHECK(!ValidateSchedule(bad).empty());
    bad = schedule;
    bad.Bays[0].UnlockRound = 0;
    CHECK(!ValidateSchedule(bad).empty());
    bad = schedule;
    bad.MaxTier = 11;
    CHECK(!ValidateSchedule(bad).empty());
    CHECK(!ValidateSchedule(PackSchedule{}).empty());
}

void TeleportRulesTests()
{
    TeleportRules rules;
    TeleportState state;
    CHECK(BeginTeleport(state, rules, 10, false, false) == TeleportStart::NotAllowed);
    CHECK(BeginTeleport(state, rules, 10, true, false) == TeleportStart::Channeling);
    CHECK(state.Channeling() && Near(state.ChannelEnd, 16));
    CHECK(BeginTeleport(state, rules, 11, true, false) == TeleportStart::AlreadyChanneling);
    CHECK(!CompleteTeleport(state, rules, 15.9));
    // Damage interrupts: no cooldown is spent.
    CHECK(InterruptTeleport(state) && !state.Channeling() && Near(TeleportCooldownRemaining(state, 12), 0));
    CHECK(!InterruptTeleport(state));
    CHECK(BeginTeleport(state, rules, 20, true, false) == TeleportStart::Channeling);
    CHECK(CompleteTeleport(state, rules, 26));
    CHECK(!CompleteTeleport(state, rules, 27));
    CHECK(Near(TeleportCooldownRemaining(state, 26), 120) && Near(TeleportCooldownRemaining(state, 100), 46));
    CHECK(BeginTeleport(state, rules, 100, true, false) == TeleportStart::OnCooldown);
    CHECK(BeginTeleport(state, rules, 100, true, true) == TeleportStart::Instant);   // free prep recall
    CHECK(!state.Channeling());
    CHECK(BeginTeleport(state, rules, 146, true, false) == TeleportStart::Channeling);
    CHECK(BeginTeleport(state, rules, std::nan(""), true, false) == TeleportStart::NotAllowed);
}

void PauseRules()
{
    // Pause from t=100 for 60s: future timestamps shift, expired ones stay.
    CHECK(Near(ShiftForPause(105, 100, 60), 165));
    CHECK(Near(ShiftForPause(95, 100, 60), 95));
    CHECK(Near(ShiftForPause(100, 100, 60), 100));
    CHECK(Near(ShiftForPause(105, 100, 0), 105));
    CHECK(Near(ShiftForPause(105, 100, -3), 105));
    // Remaining time is preserved exactly across the pause.
    for (double remaining = 0.25; remaining < 30; remaining += 0.25)
        CHECK(Near(ShiftForPause(100 + remaining, 100, 60) - 160, remaining));
    CHECK(std::isnan(ShiftForPause(std::nan(""), 100, 60)));
}
} // namespace

void EconomyAndSkillShopRules()
{
    Economy economy;
    // Eric's ruling: 1 gold at the start, +1 every 3 waves (wave 6 = 3), armored x2, boss x10, pack unit x10, leader x100.
    CHECK(MobValue(economy, 1) == 1 && MobValue(economy, 2) == 1 && MobValue(economy, 3) == 2 && MobValue(economy, 6) == 3 && MobValue(economy, 9) == 4);
    CHECK(MobValue(economy, 0) == 1 && MobValue(economy, -5) == 1);
    CHECK(KillGold(economy, BountyKind::Mob, 6) == 3 && KillGold(economy, BountyKind::Armored, 6) == 6);
    CHECK(KillGold(economy, BountyKind::Boss, 6) == 30 && KillGold(economy, BountyKind::PackUnit, 6) == 30 && KillGold(economy, BountyKind::PackLeader, 6) == 300);
    CHECK(KillGold(economy, BountyKind::Mob, 1) == 1 && KillGold(economy, BountyKind::Boss, 1) == 10 && KillGold(economy, BountyKind::PackLeader, 2) == 100);
    CHECK(KillGold(economy, BountyKind::Mob, 6, 2.0) == 6 && KillGold(economy, BountyKind::Mob, 6, std::nan("")) == 3);
    for (int wave = 1; wave <= 60; ++wave)
    {
        CHECK(MobValue(economy, wave) == 1 + wave / 3);
        CHECK(KillGold(economy, BountyKind::PackLeader, wave) == 10 * KillGold(economy, BountyKind::PackUnit, wave));
        CHECK(KillGold(economy, BountyKind::Boss, wave) == 10 * KillGold(economy, BountyKind::Mob, wave));
    }
    Economy tuned; tuned.MobBase = 2; tuned.MobStep = 3; tuned.StepEveryWaves = 5; tuned.ArmoredMultiplier = 3;
    CHECK(MobValue(tuned, 10) == 8 && KillGold(tuned, BountyKind::Armored, 10) == 24);

    SkillShopRules rules;
    // Prices follow the gold available: in mob values.
    CHECK(SkillBuyPrice(rules, economy, ShopSkillKind::Active, 0, 1) == 15);
    CHECK(SkillBuyPrice(rules, economy, ShopSkillKind::Active, 2, 1) == 23);           // 15 x 1.5
    CHECK(SkillBuyPrice(rules, economy, ShopSkillKind::Active, 0, 6) == 45);           // mob value 3
    CHECK(SkillBuyPrice(rules, economy, ShopSkillKind::Passive, 5, 6) == 90 && SkillBuyPrice(rules, economy, ShopSkillKind::Ultimate, 5, 12) == 300);
    CHECK(SkillLevelPrice(rules, economy, 1, 1) == 8 && SkillLevelPrice(rules, economy, 2, 1) == 11 && SkillLevelPrice(rules, economy, 3, 6) == 44);
    int last = 0;
    for (int level = 1; level <= 40; ++level) { const int p = SkillLevelPrice(rules, economy, level, 3); CHECK(p >= last); last = p; }
    // Slot gate: 2 actives at the start, +1 every 3 waves up to 6; passive from wave 5, ultimate from wave 10.
    CHECK(SlotsAvailable(rules, ShopSkillKind::Active, 0) == 2 && SlotsAvailable(rules, ShopSkillKind::Active, 3) == 3 && SlotsAvailable(rules, ShopSkillKind::Active, 12) == 6 && SlotsAvailable(rules, ShopSkillKind::Active, 99) == 6);
    CHECK(SlotsAvailable(rules, ShopSkillKind::Passive, 4) == 0 && SlotsAvailable(rules, ShopSkillKind::Passive, 5) == 1);
    CHECK(SlotsAvailable(rules, ShopSkillKind::Ultimate, 9) == 0 && SlotsAvailable(rules, ShopSkillKind::Ultimate, 10) == 1);
    CHECK(NextSlotWave(rules, ShopSkillKind::Active, 1) == 3 && NextSlotWave(rules, ShopSkillKind::Active, 12) == 0 && NextSlotWave(rules, ShopSkillKind::Passive, 1) == 5);
    int price = 0;
    CHECK(CheckSkillBuy(rules, economy, ShopSkillKind::Active, 1, 1, false, true, 1, 100, price) == SkillShopResult::Ok && price == 19);
    CHECK(CheckSkillBuy(rules, economy, ShopSkillKind::Active, 2, 2, false, true, 1, 100, price) == SkillShopResult::SlotLocked);
    CHECK(CheckSkillBuy(rules, economy, ShopSkillKind::Active, 2, 2, false, true, 3, 100, price) == SkillShopResult::Ok);
    CHECK(CheckSkillBuy(rules, economy, ShopSkillKind::Active, 0, 0, false, true, 1, 10, price) == SkillShopResult::NotEnoughGold);
    CHECK(CheckSkillBuy(rules, economy, ShopSkillKind::Active, 0, 0, true, true, 1, 100, price) == SkillShopResult::AlreadyOwned);
    CHECK(CheckSkillBuy(rules, economy, ShopSkillKind::Active, 0, 0, false, false, 1, 100, price) == SkillShopResult::NotAllowed);
    CHECK(CheckSkillBuy(rules, economy, ShopSkillKind::Ultimate, 0, 3, false, true, 9, 9999, price) == SkillShopResult::SlotLocked);
    // No level cap: effect and cost grow, cooldown shrinks toward its floor.
    CHECK(Near(SkillEffectScale(rules, 1), 1) && Near(SkillEffectScale(rules, 6), 1.4) && Near(SkillCostScale(rules, 3), 1.1));
    CHECK(Near(SkillCooldownScale(rules, 1), 1) && Near(SkillCooldownScale(rules, 2), .96) && Near(SkillCooldownScale(rules, 500), .4));
    for (int level = 2; level <= 100; ++level)
        CHECK(SkillEffectScale(rules, level) > SkillEffectScale(rules, level - 1) && SkillCooldownScale(rules, level) <= SkillCooldownScale(rules, level - 1));
}

// items-v2: path uniques (shared group), new passives in totals, build validation, mana economy, dodge charges.
void ItemsV2Rules()
{
    Catalog catalog = TestCatalog();
    auto forge = Make("forge", ItemTier::Legendary, 400, {"stone"}); forge.Unique = true; forge.UniqueGroup = "path";
    forge.Passives.push_back({PassiveKind::ConstructLimit, "Heartforge", 0, 0, 0, 0, 0, 1});
    forge.Passives.push_back({PassiveKind::ConstructShield, "Aegis Plating", 30, 20, 0, 0, 0, 0});
    auto apex = Make("apex", ItemTier::Legendary, 400, {"wand"}); apex.Unique = true; apex.UniqueGroup = "path";
    apex.Passives.push_back({PassiveKind::UltimateUpgrade, "Apotheosis", 0, 0, 0, 0, 0, 0});
    auto storm = Make("storm", ItemTier::Legendary, 400, {"sword"}); storm.Unique = true; storm.UniqueGroup = "path";
    storm.Passives.push_back({PassiveKind::AttackSplash, "Cleave", 40, 15, 250, 0, 0, 0});
    auto twin = Make("twinstep", ItemTier::Legendary, 200, {"boots"}); twin.UniqueGroup = "boots";
    twin.Passives.push_back({PassiveKind::DodgeCharges, "Twinstep", 0, 0, 0, 0, 0, 1});
    auto banner = Make("banner", ItemTier::Legendary, 300, {"stone"}); banner.Unique = true;
    banner.Use.Kind = EffectKind::PartyBuff; banner.Use.Radius = 900; banner.Use.Duration = 10; banner.Use.Cooldown = 60;
    banner.Use.Buff[ItemStat::Armor] = 250;
    auto codex = Make("codex", ItemTier::Legendary, 300, {"wand"});
    codex.Passives.push_back({PassiveKind::ManaRefund, "Moonwell", 20, 0, 0, 0, 0, 0});
    for (auto* item : {&forge, &apex, &storm, &twin, &banner, &codex}) catalog.Items.push_back(*item);
    CHECK(catalog.Finalize().empty());

    // Unique groups: one path-defining unique, one pair of boots, and a unique id only once.
    Inventory bag;
    int gold = 99999;
    CHECK(ApplyPurchase(catalog, bag, gold, "forge", PlanPurchase(catalog, bag, "forge", gold)));
    PurchasePlan plan = PlanPurchase(catalog, bag, "apex", gold);
    CHECK(!plan.Ok && plan.Error.find("path-defining unique") != std::string::npos && plan.Error.find("forge") != std::string::npos);
    CHECK(!PlanPurchase(catalog, bag, "storm", gold).Ok);
    CHECK(UniqueGroupConflict(catalog, bag, "storm") == catalog.Find("forge"));
    CHECK(UniqueGroupConflict(catalog, bag, "banner") == nullptr);
    CHECK(ApplyPurchase(catalog, bag, gold, "banner", PlanPurchase(catalog, bag, "banner", gold)));
    CHECK(!PlanPurchase(catalog, bag, "banner", gold).Ok);
    CHECK(ApplyPurchase(catalog, bag, gold, "twinstep", PlanPurchase(catalog, bag, "twinstep", gold)));
    CHECK(!PlanPurchase(catalog, bag, "boots", gold).Ok && !PlanPurchase(catalog, bag, "treads", gold).Ok);
    // Selling the path unique frees the group.
    int forgeSlot = -1;
    for (int i = 0; i < EquipmentSlots; ++i) if (bag.Equipment[i].Id == "forge") forgeSlot = i;
    CHECK(forgeSlot >= 0 && Sell(catalog, bag, gold, forgeSlot, false, ShopRules{}) > 0);
    CHECK(PlanPurchase(catalog, bag, "apex", gold).Ok);

    // New passives reach the totals (and same-name passives never stack).
    Inventory kit;
    kit.Equipment[0].Id = "forge"; kit.Equipment[1].Id = "twinstep"; kit.Equipment[2].Id = "codex";
    Totals totals = ComputeTotals(catalog, kit);
    CHECK(totals.ConstructLimitBonus == 1 && Near(totals.ConstructShield, 30) && Near(totals.ConstructHealth, 20));
    CHECK(totals.DodgeCharges == 1 && Near(totals.ManaRefund, 20) && !totals.UltimateUpgrade);
    kit.Equipment[3].Id = "apex"; kit.Equipment[4].Id = "storm";
    totals = ComputeTotals(catalog, kit);
    CHECK(totals.UltimateUpgrade && Near(totals.SplashPercent, 40) && Near(totals.SplashRadius, 250) && Near(totals.BasicDamageBonus, 15));
    kit.Equipment[5].Id = "codex";
    CHECK(Near(ComputeTotals(catalog, kit).ManaRefund, 20));
    // Party buff actives carry their stat block (applied to allies as a timed buff).
    CHECK(Near(catalog.Find("banner")->Use.Buff.Get(ItemStat::Armor), 250) && catalog.Find("banner")->HasActive());
    EffectKind kind{};
    for (const char* name : {"partyBarrier", "partyBuff", "healTarget"}) CHECK(ParseEffectKind(name, kind) && std::string(EffectKey(kind)) == name);
    PassiveKind passive{};
    for (const char* name : {"constructLimit", "constructShield", "summonPower", "areaAmp", "controlAmp", "ultimateUpgrade", "attackSplash", "manaRefund", "dodgeCharges", "rollHaste"})
        CHECK(ParsePassiveKind(name, passive));

    // Recommended builds.
    CHECK(ValidateBuild(catalog, {"forge", "banner", "twinstep", "codex", "potion"}).empty());
    CHECK(!ValidateBuild(catalog, {"forge", "apex"}).empty());
    CHECK(!ValidateBuild(catalog, {"boots", "twinstep"}).empty());
    CHECK(!ValidateBuild(catalog, {"banner", "banner"}).empty());
    CHECK(!ValidateBuild(catalog, {"relic"}).empty() && !ValidateBuild(catalog, {"nope"}).empty());
    CHECK(!ValidateBuild(catalog, {"sword", "dagger", "eye", "wand", "stone", "cleaver", "crown"}).empty());

    // Mana economy: flat + percent regen, level-scaled costs, bounded.
    ManaRules mana;
    CHECK(Near(ManaRegenPerSecond(mana, 600, 0), 2 + 4.8) && Near(ManaRegenPerSecond(mana, 600, 5), 11.8));
    CHECK(Near(ManaRegenPerSecond(mana, 600, 0, 1.5), (2 + 4.8) * 1.5));
    CHECK(Near(ManaCostScale(mana, 1), 1) && Near(ManaCostScale(mana, 11), 1.5) && Near(ManaCostScale(mana, 10000), 3));
    CHECK(Near(ManaCostScale(mana, -5), 1) && ManaRegenPerSecond(mana, -100, -3) >= 0);
    // Spam drains: a level-12 caster (1260 mana) casting 40-mana spells every 1.5 s runs dry; regen items extend it.
    const auto secondsToEmpty = [&](double itemRegen)
    {
        double pool = 1260, t = 0;
        const double cost = 40 * ManaCostScale(mana, 12);
        for (; t < 600; t += 1.5)
        {
            pool = std::min(1260.0, pool + 1.5 * ManaRegenPerSecond(mana, 1260, itemRegen));
            if (pool < cost) break;
            pool -= cost;
        }
        return t;
    };
    CHECK(secondsToEmpty(0) < 120 && secondsToEmpty(8) > secondsToEmpty(0) * 1.3);
    // A measured rotation (one spell every 6 s) is sustainable.
    CHECK(40 * ManaCostScale(mana, 12) / 6.0 < ManaRegenPerSecond(mana, 1260, 0));

    // Dodge-roll charges.
    ChargeState roll;
    CHECK(AvailableCharges(roll, 2, 3.5, 0) == 2);          // buying the boots fills the new charge
    CHECK(SpendCharge(roll, 2, 3.5, 10) && SpendCharge(roll, 2, 3.5, 10.5));
    CHECK(!SpendCharge(roll, 2, 3.5, 11) && AvailableCharges(roll, 2, 3.5, 13) == 0);
    CHECK(AvailableCharges(roll, 2, 3.5, 13.6) == 1 && Near(ChargeCooldown(roll, 2, 3.5, 13.6), 3.4, 1e-6));
    CHECK(AvailableCharges(roll, 2, 3.5, 17.1) == 2 && Near(ChargeCooldown(roll, 2, 3.5, 17.1), 0));
    ChargeState single;
    CHECK(SpendCharge(single, 1, 3.5, 5) && !SpendCharge(single, 1, 3.5, 8) && SpendCharge(single, 1, 3.5, 8.6));
    single.ReadyAt = 0; single.Charges = 0;                     // legacy "ReadyAt = 0" reset still refills
    CHECK(AvailableCharges(single, 1, 3.5, 1) == 1);
}

int main()
{
    ItemsV2Rules();
    EconomyAndSkillShopRules();
    CatalogRules();
    RecipeRules();
    UniqueAndSlotRules();
    SellAndUndoRules();
    UseAndTotalsRules();
    ShopAccessRules();
    LootRules();
    PackScheduleRules();
    TeleportRulesTests();
    PauseRules();
    std::cout << "Item rules: " << Assertions << " assertions, " << Failures << " failures\n";
    return Failures == 0 ? 0 : 1;
}
