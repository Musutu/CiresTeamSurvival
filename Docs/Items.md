# Items and the shop (design proposal)

Status: engineering prototype on `feat/progression-shop`, 24 September 2026. **Every number here is a
proposal for Eric to review.** The catalog is data (`Content/Data/Items.json`); the rules are the
engine-independent `Source/CiresTeamSurvival/Rules/CireItemRules.*` (tested natively); the Unreal
layer is `CireItems.*` (replicated inventory, effects, combat hooks) and `CireShopUI.cpp` /
`CireStatsPanel.cpp` (interface, built on the shared `CireUIStyle` kit). Loot, challenge gating and the
NPC pause are in [Progression.md](Progression.md).

## How items work (League of Legends construction)

- **Six bag slots plus a three-slot consumable belt.** Bag items give stats, passives and actives.
  Consumables (potions, elixirs, lantern wards) stack on the belt. Tomes are read on purchase and
  never take a slot.
- **Build paths.** Basic components build into epic components and legendary items. Buying an item
  whose parts you own **consumes them** and charges only the difference ("your price", green in the
  shop). A missing epic part is replaced by its own owned parts (owning a Rusted Longsword and a
  Raven's Eye discounts Nightfall Reaver even without the Serrated Cleaver). A full bag still accepts a
  recipe that frees a slot.
- **Uniques.** Most legendaries are unique (carry one). Boots share the unique group `boots` (one pair;
  upgrading consumes the Road-Worn Boots). Unique passives with the same name never stack.
- **Sell** returns **60%** of an item's total cost (`shopRules.sellRatio`); belt stacks sell per charge.
- **Undo** reverts the last purchase or sale **inside the same shop visit** with a full refund,
  including recipes (the parts come back). A visit ends when the shop window closes, you leave town
  (outside prep), use an item, receive loot, or the phase changes. Tomes cannot be undone.
- **Server authority.** Clients send only `ServerBuy(itemId)`, `ServerSell(slot)`, `ServerUndo()`,
  `ServerUse(slot)`. The server re-plans every request against its own inventory and gold, checks phase,
  distance to town, death, uniques and slots, then answers with a reliable `ClientFeedback` (ok/reason).
  Bag, belt, elixir buffs, teleport state, tome points and loot score replicate on the hero's
  `UCireInventory` component; the undo depth and shop-visit flag replicate to the owner only.
- **Where you can shop.** *Prep intermission:* anywhere on the map (Eric's requirement). *Recovery:* in
  town (within 9 m of your base, the existing rule). *Survival waves and arena:* closed, which keeps the
  pre-existing rule from Design.md ("validate phase, proximity"). Open question for Eric below.

## Stats (one pipeline)

Item attributes are added in `ACireHero::Recalculate` **before** the existing formulas, so STR/AGI/INT
from items give the same health, mana, attack speed and primary-attribute damage as any other point.

| Stat key | Effect | Where applied |
|---|---|---|
| strength / agility / intelligence | attributes (25 HP / 1% AS / 30 mana per point; primary +1 AD) | `Recalculate` |
| health, mana | flat maximum | `Recalculate` |
| attackDamage | flat basic-attack damage (before team power) | `AttackDamage()` |
| spellPower | % more damage **and healing** from abilities and item actives | `CireCombat::ApplyDamage` / `ApplyHealing` |
| armor / ward | mitigation `x / (x + 100)`, max 75%; armor vs basic attacks, ward vs abilities | `ACireHero::TakeDamage` |
| attackSpeed | added to the attack-speed multiplier (like AGI) | `BasicAttack()` |
| critChance | added to the 5% base | `Recalculate` |
| lifesteal | % of basic-attack damage healed | `CireCombat::ApplyDamage` |
| cooldownReduction | pure CDR, shares the 60% cap with other CDR | `Recalculate` |
| moveSpeed | % move speed (client-predicted from the replicated bag) | `ACireHero::Tick` |
| healthRegen / manaRegen / energyRegen | per second | `ACireHero::Tick` |

"Basic attack" means a hero's weapon strike/shot or a monster's authored basic ability; everything
else counts as an ability (spell) for armor/ward and spell power.

## Keys (rebindable, `CireKeybindings`)

| Action | Default |
|---|---|
| Shop | **B** |
| Teleport to Base (merged town recall, action 8) | **G** |
| Character stats window | **C** |
| Consumable belt 1 / 2 / 3 | **Z / X / V** |
| Use item in bag slot 1-6 | unbound; active items also appear automatically on **action bar 1 slots 9-12 (keys 7, 8, 9, 0)** in bag order |
| Shop: select / buy / sell / undo | left click / double-click or right-click / right-click a bag slot / Ctrl+Z |

Action-bar slots accept an item: the slot store keeps `"item:<id>"` (for example
`AssignSlot(profile, "ActionBar2_Slot4", "item:voidglass_orb")`), which resolves to the bag slot
holding that item.

## Teleport to Base

Replaces the old "town recall" (ServerAction 8, key G). **During prep and recovery it is the old instant,
free recall**; during survival waves it is a WoW-hearthstone **6 s channel** that **damage or moving
more than 40 cm cancels** (no cooldown spent), then a **120 s cooldown**. Sealed during the arena. The
bag bar shows the button with a radial cooldown sweep and key label; a teal cast bar shows the channel.
Values: `Items.json -> teleport`.

## Feedback (what the player sees)

Buy: the icon flashes, the item **flies along an arc into its bag slot** with a gold trail and a landing
ring, a **coin chime** plays, the **gold counter ticks down** (red while falling), and a toast confirms
"Purchased Nightfall Reaver -460g". Sell: the item flies into the gold counter, "+234g" floats up and
coins pour. Failure: the icon or button **shakes**, an error buzz plays and a red toast states the reason
("Not enough gold: 540 more needed", "Inventory full", "... is unique", "The market is shut: buy
anywhere during the prep intermission..."). Obvious failures are predicted locally for instant
feedback; the server still validates everything. Sounds are original synthesized WAVs under
`Content/UI/Shop` (see `Tools/BuildShopContent.py`).

## Catalog (48 entries: 9 consumables/tomes, 16 basic, 5 epic, 18 legendary)

Costs are totals (recipe gold in brackets). Economy reference: a team earns roughly 300-500 gold per
cycle from kills plus chests and arena wins, so a first legendary lands around cycle 3-4 and a full
build late in the match. Recommended builds per role (tank, physical, caster, support) are in
`Items.json -> recommended` and shown on the shop's RECOMMENDED tab.

### Consumables and tomes

| Item | Total (recipe) | Build path | Stats | Passive / active / use |
|---|---|---|---|---|
| Vial of Crimson Draught *(belt, stacks 5)* | 50g (50g) | - | - | **Use:** Restore 150 health over 12 seconds. |
| Aether Phial *(belt, stacks 5)* | 50g (50g) | - | - | **Use:** Restore 120 mana and 40 energy over 10 seconds. |
| Elixir of the Iron Vigil *(belt, stacks 1)* | 250g (250g) | - | - | **Use:** For 3 minutes: +250 health and +12 armor. Persists through death. |
| Elixir of Wrath *(belt, stacks 1)* | 250g (250g) | - | - | **Use:** For 3 minutes: +8 attack damage and +12% attack speed. Persists through death. |
| Elixir of Black Sorcery *(belt, stacks 1)* | 250g (250g) | - | - | **Use:** For 3 minutes: +14% spell power and +3 mana per second. Persists through death. |
| Watcher's Lantern *(belt, stacks 3)* | 75g (75g) | - | - | **Use:** Plant a lantern ward at your feet for 2 minutes. Monsters within 7 m are slowed 20% and Marked: they take 10% more damage from your team. |
| Tome of Insight *(read on purchase)* | 100g (100g) | - | - | **Use:** Read on purchase: gain 300 experience. |
| Tome of Ascendance *(read on purchase)* | 120g (120g) | - | - | **Use:** Read on purchase: permanently gain +3 to your primary attribute. |
| Greater Tome of Ascendance *(read on purchase, loot only)* | 0g (0g) | - | - | **Use:** Challenge loot only: permanently gain 2-5 primary attribute (by pack tier). |

### Basic components

| Item | Total (recipe) | Build path | Stats | Passive / active / use |
|---|---|---|---|---|
| Rusted Longsword | 140g (140g) | - | +6 AD | - |
| Bone Dagger | 130g (130g) | - | +10 AS % | - |
| Ashwood Wand | 140g (140g) | - | +10 Spell Power % | - |
| Bloodstone Shard | 120g (120g) | - | +160 HP | - |
| Nightglass Shard | 100g (100g) | - | +200 Mana | - |
| Boiled Leather Jerkin | 110g (110g) | - | +12 Armor | - |
| Hexweave Cloak | 110g (110g) | - | +12 Ward | - |
| Gauntlet of the Ox | 130g (130g) | - | +5 STR | - |
| Band of the Fox | 130g (130g) | - | +5 AGI | - |
| Circlet of the Owl | 130g (130g) | - | +5 INT | - |
| Road-Worn Boots *(one boots)* | 150g (150g) | - | +8 MS % | - |
| Sandglass Charm | 160g (160g) | - | +5 CDR % | - |
| Vampire Fang | 150g (150g) | - | +7 Lifesteal % | - |
| Raven's Eye | 160g (160g) | - | +10 Crit % | - |
| Gravemoss Pendant | 100g (100g) | - | +2.5 HP/s | - |
| Pilgrim's Idol | 110g (110g) | - | +4 Mana/s | - |

### Epic components

| Item | Total (recipe) | Build path | Stats | Passive / active / use |
|---|---|---|---|---|
| Serrated Cleaver | 390g (120g) | Rusted Longsword + Bone Dagger | +9 AD, +14 AS % | - |
| Grimoire of Whispers | 360g (120g) | Ashwood Wand + Nightglass Shard | +14 Spell Power %, +250 Mana | - |
| Chainmail of the Fallen | 330g (100g) | Boiled Leather Jerkin + Bloodstone Shard | +18 Armor, +200 HP | - |
| Bloodletter | 400g (110g) | Rusted Longsword + Vampire Fang | +9 AD, +10 Lifesteal % | - |
| Mantle of Ash | 330g (100g) | Hexweave Cloak + Bloodstone Shard | +18 Ward, +200 HP | - |

### Legendary items

| Item | Total (recipe) | Build path | Stats | Passive / active / use |
|---|---|---|---|---|
| Greaves of the Undying *(one boots)* | 410g (150g) | Road-Worn Boots + Boiled Leather Jerkin | +12 MS %, +15 Armor | - |
| Stalker's Treads *(one boots)* | 430g (150g) | Road-Worn Boots + Bone Dagger | +12 MS %, +15 AS % | - |
| Veilwalker Slippers *(one boots)* | 430g (120g) | Road-Worn Boots + Sandglass Charm | +12 MS %, +8 CDR % | - |
| Nightfall Reaver *(unique)* | 850g (300g) | Serrated Cleaver + Raven's Eye | +16 AD, +18 AS %, +18 Crit % | **Nightfall** (unique passive): Critical strikes deal 175% damage instead of 150%. |
| Sanguine Sabre *(unique)* | 820g (280g) | Bloodletter + Rusted Longsword | +18 AD, +14 Lifesteal % | **Blood Tithe** (unique passive): Your abilities also heal you for 6% of the damage they deal. |
| Headsman's Greataxe *(unique)* | 820g (300g) | Serrated Cleaver + Gauntlet of the Ox | +15 AD, +8 STR, +10 AS % | **Execution** (unique passive): Deal 20% more damage to targets below 30% health. |
| Wraithstring *(unique)* | 670g (280g) | Bone Dagger + Band of the Fox + Bone Dagger | +30 AS %, +8 AGI | **Wraith Echo** (unique passive): Every third basic attack deals 35 bonus damage. |
| Crown of Cinders *(unique)* | 800g (300g) | Grimoire of Whispers + Ashwood Wand | +28 Spell Power %, +250 Mana | **Kindled Mind** (unique passive): Increases your total spell power by 25%. |
| Voidglass Orb *(unique)* | 620g (250g) | Ashwood Wand + Circlet of the Owl + Nightglass Shard | +16 Spell Power %, +8 INT, +200 Mana | **Active - Void Rupture:** Deal 80 + 2x INT spell damage to your target and every enemy within 4 m of it. 45 s cooldown. |
| Hourglass of Ages *(unique)* | 550g (260g) | Sandglass Charm + Circlet of the Owl | +12 CDR %, +8 INT, +10 Spell Power % | **Active - Borrowed Time:** For 4 seconds take 60% less damage, and heal 10% of your maximum health. 60 s cooldown. |
| Gravewarden Bulwark *(unique)* | 700g (250g) | Chainmail of the Fallen + Bloodstone Shard | +350 HP, +25 Armor | **Iron Retribution** (unique passive): Attackers take 25% of the basic-attack damage they deal to you. |
| Aegis of the Last Oath *(unique)* | 720g (280g) | Chainmail of the Fallen + Hexweave Cloak | +18 Armor, +18 Ward, +200 HP | **Active - Oathshield:** You and allies within 7 m take 40% less damage for 3 seconds. 75 s cooldown. |
| Stoneheart *(unique)* | 630g (280g) | Bloodstone Shard + Gravemoss Pendant + Gauntlet of the Ox | +450 HP, +8 STR, +4 HP/s | **Unbroken** (unique passive): Dropping below 30% health grants 40% damage reduction for 4 seconds. 45 s cooldown. |
| Gravebell *(unique)* | 560g (230g) | Mantle of Ash | +20 Ward, +250 HP | **Active - Toll of the Grave:** Every monster within 8 m must attack you for 3 seconds. 30 s cooldown. |
| Censer of Dawn *(unique)* | 500g (250g) | Pilgrim's Idol + Ashwood Wand | +12 Spell Power %, +5 Mana/s | **Consecrated Mending** (unique passive): Your healing is 15% stronger. |
| Chalice of Mercy *(unique)* | 570g (230g) | Pilgrim's Idol + Nightglass Shard + Circlet of the Owl | +8 INT, +250 Mana, +4 Mana/s | **Active - Outpouring:** Heal every ally within 8 m for 60 + 2.5x INT. 60 s cooldown. |
| Banner of the Vigil *(unique)* | 530g (250g) | Sandglass Charm + Bloodstone Shard | +200 HP, +10 CDR % | **Vigil** (unique passive): Allies within 10 m regenerate 3 health per second. |
| Ravenfeather Mantle *(unique)* | 460g (220g) | Hexweave Cloak + Band of the Fox | +15 Ward, +6 AGI, +5 MS % | **Active - Scatter:** Gain 40% move speed for 3 seconds and shake off slows. 40 s cooldown. |

## Legacy compatibility

`ServerAction(4, 0..3)` still works and buys Tome of Insight, Tome of Ascendance, Rusted Longsword and
Sandglass Charm through the same inventory path (the old "Field Equipment" and "Focus Relic" became
those two components). `GearRank` now counts owned legendaries. Bots follow their role's recommended
core build one component at a time and keep a health potion.

## Decisions for Eric to review

1. **Shopping during waves.** Kept closed (existing rule), so Teleport to Base during waves is for
   escaping/defending, not shopping. Set `shopRules.townShoppingDuringWaves: true` to allow buying in
   town during waves, League-style.
2. **Sell ratio 60%**, **undo scope** (ends when the shop closes) and **tomes not undoable**.
3. **Prices** relative to current gold income (first legendary around cycle 3-4).
4. **Armor/ward split** (weapon hits vs abilities) and the 75% mitigation cap.
5. **Spell power as a percentage** rather than LoL's flat AP (skills already scale with INT/primary).
6. **Teleport:** 6 s channel, moving also cancels (not only damage), instant and free during prep and recovery.
7. **Six active items** (Void Rupture, Borrowed Time, Oathshield, Toll of the Grave, Outpouring,
   Scatter) and the lantern ward (slow + "Marked" +10% team damage) as the ward mechanic, since the
   game has no fog of war.
8. **Consumables:** potions are not interrupted by damage; elixirs persist through death; belt keys Z/X/V.

## Verification

- `Tests/Run-MSVC.cmd` builds `Tests/ItemRulesTests.cpp` (catalog, recipes, uniques, slots, belt,
  sell, undo, use and cooldowns, totals, shop access, loot, fairness, pack gating, teleport, pause).
- `python Tools/RunProgressionChecks.py` (data, native, network, gallery). The in-engine progression
  suite also runs inside `Tools/RunExpansionChecks.py --only native`.
- Icons: 48 original procedural icons from `Tools/BuildItemIcons.py` (licenses in
  `Content/UI/Items/LICENSES.md`); contact sheet via `Tools/ItemIconSheet.py`.
