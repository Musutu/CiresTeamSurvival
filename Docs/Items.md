# Items and the shop (Armory)

Status: `feat/items-v2`, 25 September 2026 (items v2 on top of the progression-shop prototype), brought
in line with Eric's stat ruling on `feat/rules-conformance` (see the next section). **Every number here is
a proposal for Eric to review.** The catalog is data (`Content/Data/Items.json`); the rules
are the engine-independent `Source/CiresTeamSurvival/Rules/CireItemRules.*` (tested natively); the Unreal
layer is `CireItems.*` + `CireItemsV2.cpp` (replicated inventory, effects, combat hooks),
`CireUltimateUpgrades.*`, and `CireShopUI.cpp` / `CireStatsPanel.cpp` (interface). Loot, challenge gating
and the NPC pause are in [Progression.md](Progression.md).

## Rules conformance (25 September 2026): primary stat and flat stats only

Eric's ruling (universal scaling): *items give only +primary stat, plus flat HP / mana / armour / MR;
completed items can add damage-reduction effects.* The audit found 29 of 54 items still carrying attack
speed, cooldown reduction, move speed, regeneration, crit or lifesteal. They now follow the ruling:

- **Allowed on every item:** `primaryStat`, flat `health`, `mana`, `armor`, `ward`.
- **Completed (legendary) items only:** damage reduction: `damageReduction` (% of every hit),
  `damageBlock` (flat per hit) and the new **`hitGuard`** passive, "reduce instances of incoming damage":
  you hold `count` guard charges (one returns every `cooldown` seconds) and each hit you take spends one
  and is reduced by `amount` % and then by `threshold` flat (never below 0). Sanguine Sabre's **Blood
  Parry** (2 charges, -35%, 8 s) and Ravenfeather Mantle's **Scatter** (3 charges, -40 each, 10 s) use it.
- **Eric's own exceptions stay:** boots (group `boots`) keep move speed, and the unique boots keep their
  roll burst / +1 dodge charge; the six path-defining uniques keep their mechanics; the four group on-use
  actives keep their actives.
- **Rejected everywhere** (`Cires::Items::StatAllowed` / `ValidateStatPolicy`; Items.json fails to load
  otherwise): attack speed, cooldown reduction, health/mana/energy regeneration, crit chance, lifesteal
  (including the ability-lifesteal and regen-aura passives), STR/AGI/INT and attack damage / spell power.
  The engine keys still exist for skills, buffs and class traits (the DPS trait's 10% crit, for example).
- **Lost power became primary stat, flat pools or damage reduction**, priced at the catalogue's rate
  (about 34 gold per primary point): attack speed became primary stat, cooldown reduction and mana regen
  became flat mana/health, lifesteal became Blood Parry. Recipes are unchanged (same components), so the
  LoL-style component -> epic -> legendary paths still hold; Bone Dagger and Sandglass Charm got cheaper.
- The Armory filters follow: "Crit & Lifesteal" became **Damage Reduction** (tag `block`), "Cooldowns"
  became **Healing**, and "Attack & Speed" / "Mana & Regen" became **Attack** / **Mana**. Tooltips are
  generated from the data, so they show the new stats and passive text.

| Item | Before | After |
|---|---|---|
| Elixir of Wrath (`elixir_of_wrath`) | +6 Primary, +12 AS % | +10 Primary |
| Elixir of Black Sorcery (`elixir_of_black_sorcery`) | +6 Primary, +3 Mana/s | +6 Primary, +300 Mana |
| Bone Dagger (`bone_dagger`) | +12 AS % | +4 Primary (cost 155 -> 140g) |
| Sandglass Charm (`sandglass_charm`) | +6 CDR % | +120 HP, +120 Mana (cost 190 -> 150g) |
| Pilgrim's Idol (`pilgrims_idol`) | +3 Mana/s | +160 Mana, +8 Ward |
| Serrated Cleaver (`serrated_cleaver`) | +8 Primary, +15 AS % | +12 Primary |
| Aether Conduit (`aether_conduit`) | +6 Primary, +4 Mana/s | +6 Primary, +250 Mana, +10 Ward |
| Oathkeeper's Charm (`oathkeeper_charm`) | +8 CDR %, +250 HP | +350 HP, +200 Mana |
| Stalker's Treads (`stalkers_treads`) | +12 MS %, +18 AS % | +12 MS %, +6 Primary |
| Veilwalker Slippers (`veilwalker_slippers`) | +12 MS %, +10 CDR % | +12 MS %, +250 Mana, +100 HP |
| Galeborn Twinstep (`twinstep_treads`) | +12 MS %, +10 AS %; Double Dash | +12 MS %, +150 HP; Double Dash |
| Nightfall Reaver (`nightfall_reaver`) | +16 Primary, +20 AS %, +20 Crit %; Nightfall | +26 Primary; Nightfall |
| Sanguine Sabre (`sanguine_sabre`) | +16 Primary, +250 HP, +12 Lifesteal %; Blood Tithe | +20 Primary, +350 HP; Blood Parry |
| Headsman's Greataxe (`headsmans_greataxe`) | +18 Primary, +12 AS %, +200 HP; Execution | +22 Primary, +200 HP; Execution |
| Wraithstring (`wraithstring`) | +10 Primary, +32 AS %; Wraith Echo | +22 Primary; Wraith Echo |
| Moonwell Codex (`moonwell_codex`) | +10 Primary, +500 Mana, +5 Mana/s; Moonwell | +12 Primary, +700 Mana; Moonwell |
| Hourglass of Ages (`hourglass_of_ages`) | +8 Primary, +12 CDR %, +200 HP; Active Borrowed Time | +14 Primary, +250 HP; Active Borrowed Time |
| Stoneheart (`stoneheart`) | +600 HP, +6 Primary, +4 HP/s, +6 DR %; Unbroken | +700 HP, +8 Primary, +6 DR %; Unbroken |
| Censer of Dawn (`censer_of_dawn`) | +10 Primary, +5 Mana/s; Consecrated Mending | +12 Primary, +300 Mana; Consecrated Mending |
| Ravenfeather Mantle (`ravenfeather_mantle`) | +20 Ward, +10 AS %, +5 MS %; Active Scatter | +20 Ward, +8 Primary, +150 HP; Scatter |
| Banner of the Vigil (`banner_of_the_vigil`) | +300 HP, +20 Armor, +10 CDR %; Active Raise the Vigil | +400 HP, +25 Armor; Active Raise the Vigil |
| Chalice of Mercy (`chalice_of_mercy`) | +6 Primary, +300 Mana, +4 Mana/s; Active Outpouring | +8 Primary, +500 Mana; Active Outpouring |
| Lifebinder's Reliquary (`lifebinders_reliquary`) | +8 Primary, +300 HP, +3 Mana/s; Active Bind the Living | +10 Primary, +300 HP, +150 Mana; Active Bind the Living |
| Artificer's Heartforge (`artificers_heartforge`) | +16 Primary, +10 CDR %, +200 Mana; Second Forge, Aegis Plating | +18 Primary, +300 Mana; Second Forge, Aegis Plating |
| Soulbinder's Crook (`soulbinders_crook`) | +18 Primary, +300 Mana, +10 AS %; Shepherd of Souls | +20 Primary, +300 Mana; Shepherd of Souls |
| Shackles of the Pale King (`shackles_of_the_pale_king`) | +8 Primary, +350 HP, +25 Ward, +10 CDR %; Pale Dominion | +10 Primary, +350 HP, +25 Ward; Pale Dominion |
| Sigil of Apotheosis (`sigil_of_apotheosis`) | +10 Primary, +350 HP, +15 CDR %; Apotheosis | +16 Primary, +350 HP; Apotheosis |
| Stormhowl Ravager (`stormhowl_ravager`) | +18 Primary, +20 AS %, +200 HP; Howling Cleave | +24 Primary, +200 HP; Howling Cleave |

Tag-only changes: Gravewarden Bulwark and Gravebell gained the `block` tag (Damage Reduction filter);
`speed`, `cooldown`, `regen` and `attackspeed` tags were removed from items that no longer grant them.

## Items v2 (25 September 2026): what changed and why

Eric's feedback and rulings, all applied on `feat/items-v2`:

1. **Universal primary scaling.** Every ability, summon and construct scales off the champion's primary
   stat (the ability side is another agent's work). Items therefore grant **only the owner's primary
   stat** through an adaptive `primaryStat` ("+18 Primary Stat (INT for you)" in the tooltip), plus
   **flat** health, mana, armor and ward (boots add move speed). No item grants attack damage, spell
   power or an off-stat (STR/AGI/INT directly). Only completed (legendary) items may add **damage
   reduction** (`damageReduction` %, `damageBlock` per hit, the `hitGuard` passive). *(Rules conformance:
   the regeneration, attack speed, cooldown reduction, crit and lifesteal that items v2 still allowed were
   removed; see above.)* `Cires::Items::ValidateStatPolicy` enforces this: Items.json fails to load
   otherwise.
2. **Path-defining uniques.** Six legendaries share the unique group `path`: a champion carries **one**.
   Each defines a build path (constructs, summons, area, control, ultimate, basic attacks).
3. **Group on-use actives:** party shield, +250 armor aura, 1,000 target heal, 200 area heal.
4. **Unique boots:** Windrunner Boots (speed) and Galeborn Twinstep (+1 dodge-roll charge; the roll
   gained a charges mechanic).
5. **Ultimate upgrades:** the Sigil of Apotheosis adds an extra effect to every ultimate (data per
   ultimate in the Ability DB, `ultimateUpgrade`).
6. **Mana is a budget.** Regen and costs were rebalanced so spamming runs casters and healers dry and
   mana items, regen and passives matter; energy is unchanged.
7. **Catalogue trimmed to 54 items** (from 48 + the new ones): off-stat components merged into the
   adaptive primary stat, weak items removed, every item has a one-line `effect`.


## How items work (League of Legends construction)

- **Six bag slots plus a three-slot consumable belt.** Bag items give stats, passives and actives.
  Consumables (potions, elixirs, lantern wards) stack on the belt. Tomes are read on purchase and
  never take a slot.
- **Build paths.** Basic components build into epic components and legendary items. Buying an item
  whose parts you own **consumes them** and charges only the difference ("your price", green in the
  shop). A missing epic part is replaced by its own owned parts (owning a Rusted Longsword and a
  Bone Dagger discounts Nightfall Reaver even without the Serrated Cleaver). A full bag still accepts a
  recipe that frees a slot.
- **Uniques.** Most legendaries are unique (carry one). Boots share the unique group `boots` (one pair;
  upgrading consumes the Road-Worn Boots). The six **path-defining uniques** share the group `path`
  (one per champion). Loot that would break a unique rule converts to gold. Unique passives with the
  same name never stack.
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

Item attributes are added in `ACireHero::Recalculate` **before** the existing formulas, so the primary
stat from items gives the same health, mana, attack speed and primary damage as any other point.

| Stat key | Effect | Where applied |
|---|---|---|
| primaryStat | adaptive: added to STR, AGI or INT, whichever is the owner's primary (STR: 10 HP + 0.1 armor + 0.1 ward / AGI: 1% AS / INT: 30 mana per point; +1 primary damage) | `CireItems::AddAttributes` |
| health, mana | flat maximum | `CireItems::ApplyDerived` |
| armor / ward | flat, plus 0.1 of each per STR point; mitigation `x / (x + 100)`, max 75%; armor vs basic attacks, ward vs abilities | `CireItems::ModifyIncomingDamage` |
| moveSpeed *(boots only)* | % move speed (client-predicted from the replicated bag and buffs) | `ACireHero::Tick` |
| damageReduction *(legendary only)* | % of every hit after armor/ward, capped at 40% | `Cires::Items::ApplyItemMitigation` |
| damageBlock *(legendary only)* | flat reduction of every hit after the %, never below 25% of the hit | same |
| `hitGuard` passive *(legendary only)* | the next `count` hits (one charge back every `cooldown` s) are reduced by `amount` % then `threshold` flat | `CireItems::ModifyIncomingDamage` (`Cires::Items::ApplyHitGuard`, charges on `UCireInventory::HitGuardState`) |
| attackSpeed, cooldownReduction, health/mana/energy regen, critChance, lifesteal, strength, agility, intelligence, attackDamage, spellPower | **rejected** on items (the engine keys still exist for skills, buffs and class traits) | `ValidateStatPolicy` |

"Basic attack" means a hero's weapon strike/shot or a monster's authored basic ability; everything
else counts as an ability for armor/ward. Item actives that used to scale with INT (Void Rupture,
Oathshield) now scale with the primary stat.


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

## Path-defining uniques (unique group `path`: one per champion)

Buying a second is refused ("Only one path-defining unique may be carried (Heart of the Cataclysm)"),
loot never grants one (it converts to gold), and selling yours frees the choice. They are ~935-1005g,
the most expensive items (a team affords one around round 3 on the current EconomyCurve).

| Path | Item | Effect | Hook |
|---|---|---|---|
| Constructs | Artificer's Heartforge | +1 live construct of each recipe (turrets, pylons, traps, skitters); constructs +20% health and spawn with a shield worth 30% of max health | `ACireConstruct::SpawnFor` (limit, health, `ItemShield`), `TakeDamage` (shield soaks first) |
| Summons | Soulbinder's Crook | summons and pets +35% health and damage | `ACireSummon::SpawnGroup` (health), `CireItems::ModifyOutgoingDamage` (damage). Pets: call `CireItems::SummonMultiplier(owner)` |
| Area | Heart of the Cataclysm | area abilities +20% damage; ground areas 20% wider | `ModifyOutgoingDamage` (`IsAreaAbility`: DB radius or void zone), `ACireAreaEffect::Spawn` (radius/length/width) |
| Control | Shackles of the Pale King | stuns, silences and slows you apply last 30% longer; +12% damage to stunned/silenced/slowed enemies | `CireCrowdControl::Stun/Silence/Slow` (before diminishing returns), `ModifyOutgoingDamage` |
| Ultimate | Sigil of Apotheosis | your ultimate also fires its upgrade (below) | `CireSkillShop::ApplyCastLevel` -> `CireItems::OnAbilityCast` -> `CireUltimateUpgrades::Trigger` |
| Basic attacks | Stormhowl Ravager | basic attacks +15% damage and cleave 40% to enemies within 2.5 m of the target | `ModifyOutgoingDamage`, `OnDamageDealt` ("Howling Cleave" is not a basic attack, so it never chains) |

## Group on-use actives (each unique; keys 7-0 / bag slot)

| Item | Active | Numbers |
|---|---|---|
| Aegis of the Last Oath | **Oathshield** (`partyBarrier`): you and allies within 8 m gain an absorb shield | 180 + 2x primary for 6 s, 75 s cooldown. Shields never stack (the stronger one wins). Shown as a pale-gold overlay and "+N" on the health bar, visual `party_barrier` |
| Banner of the Vigil | **Raise the Vigil** (`partyBuff`): you and allies within 9 m gain +250 armor | 10 s, 90 s cooldown; visual `vigil_banner` |
| Lifebinder's Reliquary | **Bind the Living** (`healTarget`): heal your targeted ally (or yourself) within 12 m | 1,000 (x healing modifiers), 90 s cooldown |
| Chalice of Mercy | **Outpouring** (`healAllies`): heal yourself and allies within 4.5 m | 200 (x healing modifiers), 30 s cooldown |

The old Vigil aura (3 HP/s) merged into the banner's active; the old 40% Oathshield guard became the
party shield.

## Boots and dodge-roll charges

One pair of boots (group `boots`; upgrades consume the Road-Worn Boots). New unique boots:

- **Windrunner Boots** (500g): +20% move speed; **Tailwind**: a dodge roll grants +30% move speed for 2 s.
- **Galeborn Twinstep** (560g): +12% move speed, +150 health; **Double Dash**: +1 dodge-roll charge.

`UCireMobility` now has replicated `RollCharges`/`MaxRollCharges`; charges refill one at a time on the
roll cooldown (3.5 s), maths in `Cires::Items::ChargeState` (native tests). With one charge the roll
behaves exactly as before (`CooldownRemaining`, the `ReadyAt = 0` reset). Each roll still costs 25
energy. The HUD hint reads "DODGE 2/2".

## Ultimate upgrades (Sigil of Apotheosis)

Every ultimate (16 implemented, 11 planned) has an `ultimateUpgrade` in `Content/Data/Abilities.json`,
authored in `Tools/UltimateUpgrades.py` and merged by `Tools/BuildAbilityDB.py`. The upgrade *adds*
an effect; the ultimate's own numbers are unchanged. Primitives: `partyBuff` (item stat keys, e.g. a
party armor/ward aura), `barrier`, `heal`, `restore` (mana/energy), `cleanse`, `stun`, `silence`,
`slow`, `armorBreak`, `damage`, `cooldownRefund`; centered on you or on the aimed point/target, with an
optional delay matching the ultimate's impact. Examples: Renewal **Second Dawn** (allies +30 armor/+30
ward for 8 s), Bastion of Dawn **Dawnward** (party shield 150 + 10% of your max health), Challenge of
Iron **Iron Echo** (1.5 s stun), Starfall **Falling Sky** (2 s silence, 20% mana back), Executioner's
Verdict **Verdict Rendered** (other cooldowns -50%, +25% attack speed). The full table is in
[Abilities.md](Abilities.md#ultimate-upgrades-sigil-of-apotheosis); the action-bar tooltip shows the
upgrade ("APOTHEOSIS (Dawnward, active): ..."), and the caster shows the `apotheosis` halo.

## Mana economy

**Audit (before):** mana regenerated 1.5% of max mana per second while costs were flat. Max mana grows
with INT (30 per point, +2 INT per level for casters), so regen outgrew every rotation: a level-12
caster regenerated 18.9 mana/s against a pool of 1,260, and the bounded simulation below never ran dry.
The Skill Shop's per-level cost growth (up to x1.4-1.5) did not keep up either. Energy (100 points,
9/s, flat 20-40 costs) was already a real constraint.

**Now (`Items.json -> manaEconomy`, `Cires::Items::ManaRules`):**

- Mana regen per second = **2 + 0.8% of max mana** (+ item regen; Deep Reserves still x1.5 on the base).
- Mana costs **x (1 + 0.05 x (level - 1))** (x1.45 at level 10, x1.95 at 20, capped x3), on top of the
  Skill Shop level. The action bar, its tooltip and "Not enough mana" all use the real cost.
- Town regen (15%/s in prep and recovery), potions (Aether Phial now 180 mana) and energy are unchanged:
  energy stays the snappy resource (flat costs, fast refill).
- Mana items that matter (flat pools since rules conformance; a bigger pool also raises the 0.8%/s
  regen): Pilgrim's Idol (+160), Aether Conduit (+250), Grimoire of Whispers (+350), Heart of the
  Cataclysm (+500), Moonwell Codex (+700 and a 20% refund), Chalice (+500), Elixir of Black Sorcery (+300).

**Feedback:** the mana bar shows "+x/s", pulses blue under 25%, and on a failed cast flashes red and
shakes with a large "NOT ENOUGH MANA 32 / 58" above the action bar (a replicated owner-only counter
on the inventory drives the flash), plus the precise notice "Not enough mana (32 / 58)." (energy
gets its own amber message). Unaffordable buttons are dimmed using the scaled cost.

**Bounded simulation** (`python Tools/ManaEconomySim.py --markdown`: three kits, four levels, 120 s,
casting on cooldown = "spam", every other window = "measured", "+ mana item" = +250 mana and +5/s):

| kit | lvl | pool | old regen | old spam dry | new regen | cost | new spam dry | casts ok/refused | measured dry | spam + mana item |
|---|---|---|---|---|---|---|---|---|---|---|
| caster (Wizard-like) | 1 | 600 | 9.0 | never | 6.8 | x1.00 | never | 32/0 | never | never |
| caster (Wizard-like) | 6 | 900 | 13.5 | never | 9.2 | x1.25 | 54s | 42/116 | never | never |
| caster (Wizard-like) | 12 | 1260 | 18.9 | never | 12.1 | x1.55 | 42s | 47/261 | never | 72s |
| caster (Wizard-like) | 18 | 1620 | 24.3 | never | 15.0 | x1.85 | 45s | 49/243 | never | 72s |
| healer (Scholar-like) | 1 | 600 | 9.0 | 110s | 6.8 | x1.00 | 73s | 33/35 | never | never |
| healer (Scholar-like) | 6 | 900 | 13.5 | 113s | 9.2 | x1.25 | 49s | 41/138 | never | 108s |
| healer (Scholar-like) | 12 | 1260 | 18.9 | never | 12.1 | x1.55 | 39s | 45/275 | 119s | 57s |
| healer (Scholar-like) | 18 | 1620 | 24.3 | never | 15.0 | x1.85 | 40s | 47/260 | never | 57s |
| constructor (Artificer) | 1 | 600 | 9.0 | never | 6.8 | x1.00 | never | 22/0 | never | never |
| constructor (Artificer) | 6 | 900 | 13.5 | never | 9.2 | x1.25 | never | 37/0 | never | never |
| constructor (Artificer) | 12 | 1260 | 18.9 | never | 12.1 | x1.55 | 57s | 42/203 | never | 101s |
| constructor (Artificer) | 18 | 1620 | 24.3 | never | 15.0 | x1.85 | 63s | 44/178 | never | 101s |

Reading: the old model never ran dry for casters; now full-rotation spam empties a mid-game caster in
~40-55 s (about one wave), a measured rotation is sustainable, and one mana item buys ~30 more seconds.
Constructors (long cooldowns) are only taxed late.

## Catalogue (54 items)

Totals include the recipe parts (recipe gold in brackets), priced for the current kill-gold economy
(`Tools/EconomyCurve.py`: consumables 60-300g, basics 120-190g, epics 390-465g, legendaries 490-1005g).
Generated by `python Tools/ItemCatalogDoc.py`.

<!-- generated by Tools/ItemCatalogDoc.py: 54 items -->

### Consumables and tomes (9)

| Item (`id`) | Total (recipe) | Build path | Stats | One-line effect | Passive / active |
|---|---|---|---|---|---|
| Vial of Crimson Draught (`vial_of_crimson`) | 60g (60g) | - | - | Drink: 150 health over 12 s. | **Use:** Restore 150 health over 12 seconds. |
| Aether Phial (`aether_phial`) | 60g (60g) | - | - | Drink: 180 mana and 60 energy over 10 s. | **Use:** Restore 180 mana and 60 energy over 10 seconds. |
| Elixir of the Iron Vigil (`elixir_of_iron`) | 300g (300g) | - | - | 3 min: +250 health, +15 armor. | **Use:** For 3 minutes: +250 health and +15 armor. Persists through death. |
| Elixir of Wrath (`elixir_of_wrath`) | 300g (300g) | - | - | 3 min: +10 primary stat. | **Use:** For 3 minutes: +10 primary stat. Persists through death. |
| Elixir of Black Sorcery (`elixir_of_black_sorcery`) | 300g (300g) | - | - | 3 min: +6 primary stat, +300 mana. | **Use:** For 3 minutes: +6 primary stat and +300 mana. Persists through death. |
| Watcher's Lantern (`watchers_lantern`) | 90g (90g) | - | - | Plant a ward: slows and marks monsters for your team. | **Use:** Plant a lantern ward at your feet for 2 minutes. Monsters within 7 m are slowed 20% and Marked: they take 10% more damage from your team. |
| Tome of Insight (`tome_of_insight`) | 120g (120g) | - | - | Read: +300 experience. | **Use:** Read on purchase: gain 300 experience. |
| Tome of Ascendance (`tome_of_ascendance`) | 145g (145g) | - | - | Read: +3 primary attribute. | **Use:** Read on purchase: permanently gain +3 to your primary attribute. |
| Greater Tome of Ascendance (`greater_tome_of_ascendance`) | 0g (0g) | - | - | Loot: +2 to +5 primary attribute. | **Use:** Challenge loot only: permanently gain 2-5 primary attribute (by pack tier). |

### Basic components (11)

| Item (`id`) | Total (recipe) | Build path | Stats | One-line effect | Passive / active |
|---|---|---|---|---|---|
| Rusted Longsword (`rusted_longsword`) | 170g (170g) | - | +5 Primary | +5 primary stat. | - |
| Ashwood Wand (`ashwood_wand`) | 160g (160g) | - | +3 Primary, +150 Mana | Primary stat and mana. | - |
| Gauntlet of the Ox (`gauntlet_of_the_ox`) | 160g (160g) | - | +3 Primary, +150 HP | Primary stat and health. | - |
| Bone Dagger (`bone_dagger`) | 140g (140g) | - | +4 Primary | +4 primary stat (the cheap blade). | - |
| Bloodstone Shard (`bloodstone_shard`) | 140g (140g) | - | +180 HP | Flat health. | - |
| Nightglass Shard (`nightglass_shard`) | 120g (120g) | - | +220 Mana | Flat mana. | - |
| Boiled Leather Jerkin (`boiled_jerkin`) | 130g (130g) | - | +15 Armor | Flat armor (vs basic attacks). | - |
| Hexweave Cloak (`hexweave_cloak`) | 130g (130g) | - | +15 Ward | Flat ward (vs abilities). | - |
| Road-Worn Boots (`road_worn_boots`) *(boots)* | 180g (180g) | - | +8 MS % | Move speed; upgrades into every boots. | - |
| Sandglass Charm (`sandglass_charm`) | 150g (150g) | - | +120 HP, +120 Mana | Health and mana. | - |
| Pilgrim's Idol (`pilgrims_idol`) | 130g (130g) | - | +160 Mana, +8 Ward | Mana and a little ward. | - |

### Epic components (7)

| Item (`id`) | Total (recipe) | Build path | Stats | One-line effect | Passive / active |
|---|---|---|---|---|---|
| Serrated Cleaver (`serrated_cleaver`) | 450g (140g) | Rusted Longsword + Bone Dagger | +12 Primary | A large primary stat bonus. | - |
| Grimoire of Whispers (`grimoire_of_whispers`) | 420g (140g) | Ashwood Wand + Nightglass Shard | +7 Primary, +350 Mana | Primary stat and a deep mana pool. | - |
| Chainmail of the Fallen (`chainmail_of_the_fallen`) | 390g (120g) | Boiled Leather Jerkin + Bloodstone Shard | +25 Armor, +250 HP | Armor and health. | - |
| Mantle of Ash (`mantle_of_ash`) | 390g (120g) | Hexweave Cloak + Bloodstone Shard | +25 Ward, +250 HP | Ward and health. | - |
| Bloodletter (`bloodletter`) | 460g (130g) | Rusted Longsword + Gauntlet of the Ox | +9 Primary, +200 HP | Primary stat and health (bruisers). | - |
| Aether Conduit (`aether_conduit`) | 420g (130g) | Ashwood Wand + Pilgrim's Idol | +6 Primary, +250 Mana, +10 Ward | Primary stat, mana and ward. | - |
| Oathkeeper's Charm (`oathkeeper_charm`) | 410g (120g) | Sandglass Charm + Bloodstone Shard | +350 HP, +200 Mana | Health and mana. | - |

### Path-defining uniques (one per champion) (6)

| Item (`id`) | Total (recipe) | Build path | Stats | One-line effect | Passive / active |
|---|---|---|---|---|---|
| Artificer's Heartforge (`artificers_heartforge`) *(path unique)* | 960g (380g) | Aether Conduit + Ashwood Wand | +18 Primary, +300 Mana | Construct path: +1 construct per recipe; constructs are shielded. | **Second Forge:** +1 live construct of each recipe (turrets, pylons, traps, skitters). **Aegis Plating:** Your constructs have +20% health and spawn with a shield worth 30% of their maximum health. |
| Soulbinder's Crook (`soulbinders_crook`) *(path unique)* | 920g (360g) | Grimoire of Whispers + Bone Dagger | +20 Primary, +300 Mana | Summon path: summons and pets +35% health and damage. | **Shepherd of Souls:** Your summons and pets have +35% health and deal +35% damage. |
| Heart of the Cataclysm (`heart_of_cataclysm`) *(path unique)* | 940g (400g) | Grimoire of Whispers + Nightglass Shard | +18 Primary, +500 Mana | Area path: area abilities +20% damage and 20% wider. | **Widening Ruin:** Area abilities deal +20% damage, and your ground areas are 20% wider. |
| Shackles of the Pale King (`shackles_of_the_pale_king`) *(path unique)* | 910g (370g) | Mantle of Ash + Sandglass Charm | +10 Primary, +350 HP, +25 Ward | Control path: your crowd control lasts 30% longer. | **Pale Dominion:** Your stuns, slows and silences last 30% longer; you deal 12% more damage to enemies under your control. |
| Sigil of Apotheosis (`sigil_of_apotheosis`) *(path unique)* | 960g (390g) | Oathkeeper's Charm + Gauntlet of the Ox | +16 Primary, +350 HP | Ultimate path: your ultimate gains an extra effect. | **Apotheosis:** Your ultimate gains an extra effect (its upgrade is listed on the ultimate's tooltip). |
| Stormhowl Ravager (`stormhowl_ravager`) *(path unique)* | 990g (380g) | Serrated Cleaver + Gauntlet of the Ox | +24 Primary, +200 HP | Attack path: basic attacks hit harder and cleave 40%. | **Howling Cleave:** Basic attacks deal +15% damage and cleave 40% of it to enemies within 2.5 m of the target. |

### Boots (one pair) (5)

| Item (`id`) | Total (recipe) | Build path | Stats | One-line effect | Passive / active |
|---|---|---|---|---|---|
| Greaves of the Undying (`greaves_of_the_undying`) *(boots)* | 490g (180g) | Road-Worn Boots + Boiled Leather Jerkin | +12 MS %, +20 Armor | Boots: speed and armor. | - |
| Stalker's Treads (`stalkers_treads`) *(boots)* | 500g (180g) | Road-Worn Boots + Bone Dagger | +12 MS %, +6 Primary | Boots: speed and primary stat. | - |
| Veilwalker Slippers (`veilwalker_slippers`) *(boots)* | 475g (145g) | Road-Worn Boots + Sandglass Charm | +12 MS %, +250 Mana, +100 HP | Boots: speed, mana and health. | - |
| Windrunner Boots (`windrunner_boots`) *(boots)* | 500g (320g) | Road-Worn Boots | +20 MS % | Unique boots: +20% move speed; rolls grant a burst of speed. | **Tailwind:** Dodge rolling grants +30% move speed for 2 seconds. |
| Galeborn Twinstep (`twinstep_treads`) *(boots)* | 560g (380g) | Road-Worn Boots | +12 MS %, +150 HP | Unique boots: +1 dodge-roll charge (double dash). | **Double Dash:** +1 dodge-roll charge: roll twice back to back; charges refill one at a time. |

### Group on-use actives (4)

| Item (`id`) | Total (recipe) | Build path | Stats | One-line effect | Passive / active |
|---|---|---|---|---|---|
| Aegis of the Last Oath (`aegis_of_the_last_oath`) *(unique)* | 800g (280g) | Chainmail of the Fallen + Hexweave Cloak | +25 Armor, +25 Ward, +250 HP | Active: shield the party around you (180 + 2x primary). | **Active - Oathshield:** You and allies within 8 m gain a shield absorbing 180 + 2x primary stat damage for 6 seconds. 75 s cooldown. |
| Banner of the Vigil (`banner_of_the_vigil`) *(unique)* | 760g (220g) | Oathkeeper's Charm + Boiled Leather Jerkin | +400 HP, +25 Armor | Active: allies within 9 m gain +250 armor for 10 s. | **Active - Raise the Vigil:** You and allies within 9 m gain +250 armor for 10 seconds. 90 s cooldown. |
| Chalice of Mercy (`chalice_of_mercy`) *(unique)* | 770g (230g) | Aether Conduit + Nightglass Shard | +8 Primary, +500 Mana | Active: heal a small area for 200. | **Active - Outpouring:** Heal yourself and allies within 4.5 m for 200. 30 s cooldown. |
| Lifebinder's Reliquary (`lifebinders_reliquary`) *(unique)* | 820g (260g) | Aether Conduit + Bloodstone Shard | +10 Primary, +300 HP, +150 Mana | Active: heal one ally for 1,000. | **Active - Bind the Living:** Heal your targeted ally (or yourself) within 12 m for 1,000. 90 s cooldown. |

### Other legendaries (12)

| Item (`id`) | Total (recipe) | Build path | Stats | One-line effect | Passive / active |
|---|---|---|---|---|---|
| Nightfall Reaver (`nightfall_reaver`) *(unique)* | 950g (330g) | Serrated Cleaver + Rusted Longsword | +26 Primary | Your critical strikes deal 175% (crits come from the DPS trait and skills). | **Nightfall:** Critical strikes deal 175% damage instead of 150%. |
| Sanguine Sabre (`sanguine_sabre`) *(unique)* | 950g (320g) | Bloodletter + Rusted Longsword | +20 Primary, +350 HP | The next 2 hits you take are reduced by 35% (a charge returns every 8 s). | **Blood Parry:** You hold 2 parry charges (one returns every 8 seconds). Each hit you take spends one and is reduced by 35%. |
| Headsman's Greataxe (`headsmans_greataxe`) *(unique)* | 930g (330g) | Bloodletter + Bone Dagger | +22 Primary, +200 HP | +20% damage to targets under 30% health. | **Execution:** Deal 20% more damage to targets below 30% health. |
| Wraithstring (`wraithstring`) *(unique)* | 890g (300g) | Serrated Cleaver + Bone Dagger | +22 Primary | Every third attack deals 35 bonus damage. | **Wraith Echo:** Every third basic attack deals 35 bonus damage. |
| Voidglass Orb (`voidglass_orb`) *(unique)* | 880g (300g) | Grimoire of Whispers + Ashwood Wand | +14 Primary, +300 Mana | Active: burst your target and everything within 4 m. | **Active - Void Rupture:** Deal 80 + 2x primary stat damage to your target and every enemy within 4 m of it. 45 s cooldown. |
| Moonwell Codex (`moonwell_codex`) *(unique)* | 850g (300g) | Grimoire of Whispers + Pilgrim's Idol | +12 Primary, +700 Mana | Abilities refund 20% of their mana cost. | **Moonwell:** Your abilities refund 20% of their mana cost. |
| Hourglass of Ages (`hourglass_of_ages`) *(unique)* | 850g (280g) | Oathkeeper's Charm + Ashwood Wand | +14 Primary, +250 HP | Active: 60% damage reduction and a 10% heal. | **Active - Borrowed Time:** For 4 seconds take 60% less damage, and heal 10% of your maximum health. 60 s cooldown. |
| Gravewarden Bulwark (`gravewarden_bulwark`) *(unique)* | 810g (280g) | Chainmail of the Fallen + Bloodstone Shard | +400 HP, +35 Armor, +12 Block | Blocks 12 damage from every hit; attackers take 25% back. | **Iron Retribution:** Attackers take 25% of the basic-attack damage they deal to you. |
| Stoneheart (`stoneheart`) *(unique)* | 740g (300g) | Bloodstone Shard + Bloodstone Shard + Gauntlet of the Ox | +700 HP, +8 Primary, +6 DR % | 6% less damage from everything; a last-stand shield below 30%. | **Unbroken:** Dropping below 30% health grants 40% damage reduction for 4 seconds. 45 s cooldown. |
| Gravebell (`gravebell`) *(unique)* | 650g (260g) | Mantle of Ash | +30 Ward, +350 HP, +8 Block | Active: taunt every monster within 8 m. | **Active - Toll of the Grave:** Every monster within 8 m must attack you for 3 seconds. 30 s cooldown. |
| Censer of Dawn (`censer_of_dawn`) *(unique)* | 650g (230g) | Aether Conduit | +12 Primary, +300 Mana | Your healing is 15% stronger. | **Consecrated Mending:** Your healing is 15% stronger. |
| Ravenfeather Mantle (`ravenfeather_mantle`) *(unique)* | 490g (220g) | Hexweave Cloak + Bone Dagger | +20 Ward, +8 Primary, +150 HP | The next 3 hits you take lose 40 damage each (a charge returns every 10 s). | **Scatter:** You hold 3 feather charges (one returns every 10 seconds). Each hit you take spends one and loses 40 damage. |

### Recommended builds (Items.json -> recommended; core = the six-slot finished build)

| Role | Starting | Core (in order) | Situational |
|---|---|---|---|
| tank | Vial of Crimson Draught, Boiled Leather Jerkin, Bloodstone Shard | Greaves of the Undying, Shackles of the Pale King, Gravewarden Bulwark, Stoneheart, Banner of the Vigil, Gravebell | Aegis of the Last Oath, Sigil of Apotheosis, Galeborn Twinstep, Elixir of the Iron Vigil |
| physical | Vial of Crimson Draught, Rusted Longsword, Bone Dagger | Stalker's Treads, Stormhowl Ravager, Nightfall Reaver, Sanguine Sabre, Wraithstring, Headsman's Greataxe | Galeborn Twinstep, Ravenfeather Mantle, Sigil of Apotheosis, Elixir of Wrath |
| caster | Aether Phial, Ashwood Wand, Nightglass Shard | Veilwalker Slippers, Heart of the Cataclysm, Moonwell Codex, Voidglass Orb, Hourglass of Ages, Ravenfeather Mantle | Shackles of the Pale King, Sigil of Apotheosis, Windrunner Boots, Elixir of Black Sorcery |
| support | Aether Phial, Pilgrim's Idol, Watcher's Lantern | Veilwalker Slippers, Sigil of Apotheosis, Censer of Dawn, Lifebinder's Reliquary, Chalice of Mercy, Aegis of the Last Oath | Banner of the Vigil, Moonwell Codex, Shackles of the Pale King, Elixir of the Iron Vigil |
| summoner | Aether Phial, Ashwood Wand, Bone Dagger | Veilwalker Slippers, Soulbinder's Crook, Moonwell Codex, Voidglass Orb, Hourglass of Ages, Ravenfeather Mantle | Sigil of Apotheosis, Windrunner Boots, Elixir of Black Sorcery |
| constructor | Aether Phial, Pilgrim's Idol, Ashwood Wand | Veilwalker Slippers, Artificer's Heartforge, Moonwell Codex, Hourglass of Ages, Aegis of the Last Oath, Voidglass Orb | Heart of the Cataclysm, Galeborn Twinstep, Elixir of Black Sorcery |

**Removed or merged:** Raven's Eye and Vampire Fang (off-stat components; crit and lifesteal live on
Nightfall Reaver and Sanguine Sabre), Band of the Fox and Circlet of the Owl (merged into the adaptive
primary-stat components), Gravemoss Pendant (weak), Crown of Cinders (a spell-power multiplier, which the
primary-stat ruling removes; its role went to Heart of the Cataclysm and Moonwell Codex). Loot tables
now drop replacements. The Vigil regen aura merged into the Banner's active.

**New item ids (procedural placeholder icons, awaiting the 2D art pass):** `aether_conduit`,
`oathkeeper_charm`, `windrunner_boots`, `twinstep_treads`, `moonwell_codex`, `lifebinders_reliquary`,
`artificers_heartforge`, `soulbinders_crook`, `heart_of_cataclysm`, `shackles_of_the_pale_king`,
`sigil_of_apotheosis`, `stormhowl_ravager`. Existing painted icons are kept for reused ids (their
stats changed but not their look).

## Legacy compatibility

`ServerAction(4, 0..3)` still works and buys Tome of Insight, Tome of Ascendance, Rusted Longsword and
Sandglass Charm through the same inventory path. `GearRank` counts owned legendaries. Bots follow their
role's recommended core build one component at a time and keep a health potion; the Aetheri Artificer
follows the `constructor` build and the Summoner the `summoner` build.

## Decisions for Eric to review

1. **One path-defining unique per champion** (shared group `path`, League-mythic style). Alternative:
   give each path its own group so a champion could stack several paths; one data field per item.
2. **Mana numbers:** 2 + 0.8% max mana per second, costs +5% per level (Items.json `manaEconomy`).
3. **Mitigation specials:** % reduction capped at 40%; flat block never removes more than 75% of a hit.
4. **Crit and lifesteal are gone from items** (rules conformance). Nightfall Reaver keeps its crit-damage
   passive (crits come from the DPS trait and skills); Sanguine Sabre's lifesteal became Blood Parry.
   Other legendary passives that are neither stats nor damage reduction (Execution, Wraith Echo,
   Moonwell refund, Iron Retribution thorns, Censer heal amp, Unbroken) and the self actives (Void
   Rupture, Borrowed Time, Toll of the Grave) are kept pending Eric's call.
5. **Heals** (Reliquary 1,000, Chalice 200) are boosted by healing modifiers (team power, Censer), so
   the numbers are floors. Party shields do not stack; the stronger one wins.
6. **Ultimate upgrades** fire when the ultimate is cast (delayed to the impact for Starfall, Seismic
   Reprisal and Hexbane); planned ultimates carry data-ready upgrades.
7. Earlier decisions still open: shopping during waves (closed), sell ratio 60%, undo scope, the
   armor/ward split and 75% cap, teleport channel, lantern ward.

## Verification

- `Tests/Run-MSVC.cmd` builds `Tests/ItemRulesTests.cpp` (catalog, recipes, uniques, slots, belt,
  sell, undo, use and cooldowns, totals, shop access, loot, fairness, pack gating, teleport, pause).
- `python Tools/RunProgressionChecks.py` (data, native, network, gallery). The in-engine progression
  suite also runs inside `Tools/RunExpansionChecks.py --only native`.
- Icons: original procedural icons from `Tools/BuildItemIcons.py` (painted overrides where they exist) (licenses in
  `Content/UI/Items/LICENSES.md`); contact sheet via `Tools/ItemIconSheet.py`.

### Evidence (24 September 2026, after merging main with the town world)

- Native rules: 1,930,816 + 45,777 item assertions, 0 failures (`Tests/Run-MSVC.cmd`).
- In-engine: `CIRE_ITEMS_PASS checks=45`, `CIRE_PROGRESSION_PASS checks=26`; `RunExpansionChecks --only native` and `--only network` PASS; `RunNetworkSmoke.py` PASS; `RunNPCChecks.py` PASS; `-CireSmoke` full cycle PASS (cycle 2 unlocks the tier-2 bay).
- Shop network probe (dedicated server + client): prep purchase 30 m from town, recipe, sell, undo, invalid item, potion, survival rejection, teleport channel and cooldown all replicated: `Saved/ProgressionChecks/20260924T105835834752Z/report.json`.
- Captures (1920x1080, reviewed): `Saved/ShopGallery/20260924-110101/` (shop browsing + hover, recommended, buy flight, error shake, sell, stats hover, loot chest drop/opened, teleport channel/cooldown).

### Personal loot update (25 September 2026)

Loot is now personal (see Progression.md): per-player rolls, owner-only chests, loot window, toasts,
bag flights, loot log (L), minimap chest markers, auto-collect summary at prep. Evidence: item rules
145,787 assertions; `CIRE_PROGRESSION_PASS checks=39` (independent rolls, eligibility, owner-only
relevancy/opening, bots auto-loot, auto-collect summary, exact tome stat, team drop totals); network
probe: the remote client never receives the teammate's chest, cannot open it by standing on it, and
receives its own loot report (`CIRE_SHOP_NET_CLIENT_PASS personal_report_lines=2`). Captures:
`Saved/ShopGallery/20260925-023312/11-13`.

### Evidence: items v2 (25 September 2026, after merging main with the dodge-roll skills)

- Native rules: `Tests/Run-MSVC.cmd` 2,064,683 + 146,196 item assertions, 0 failures (new: path/boots
  unique groups, new passives in totals, `ValidateBuild`, stat policy, mitigation specials, mana
  economy, dodge charges).
- In-engine `RunProgressionChecks --only native`: `CIRE_ITEMS_PASS checks=47`, **`CIRE_ITEMS_V2_PASS
  checks=247`** (catalog shape and one-line effects, stat policy, every recommended core carryable,
  adaptive primary stat on STR and INT champions, mitigation specials, path/boots/active uniques incl.
  loot, party shield/armor banner/1,000 heal/200 area heal on allies in range only, double dash and
  Tailwind, all 27 ultimate upgrades apply their added effect plus the real Bastion of Dawn cast path,
  mana regen formula, level-scaled costs, spam runs dry, regen items extend it, "Not enough mana (x / y)"
  + HUD flash, Moonwell refund), `CIRE_SKILLSHOP_PASS checks=48`, `CIRE_PROGRESSION_PASS checks=39`.
- `RunExpansionChecks --only native`: `CIRE_COMBAT_EXPANSION_PASS` (aura smoke 596 checks with the three
  new buff visuals, tech constructs, new champions, crowd control, arenas, waves, navigation, VFX).
- `RunProgressionChecks --only network`: shop network probe PASS.
- Captures (reviewed): Armory `Saved/ShopGallery/20260925-141458/11_shop_path_uniques.png` (path filter,
  PATH ribbons, "+10 Primary Stat (STR for you)", tooltip) and `12_shop_path_unique_refused.png`
  (second path unique refused, BUY UNAVAILABLE, toast); combat `Saved/AuraGallery/20260925-141556/
  13_items_v2_apotheosis_combat.png` (Dawnward shields on allies, apotheosis halo, stunned monsters)
  and `12_items_v2_actives.png`. The aura gallery stages the buff visuals; the gameplay effects are
  proven by the in-engine suite above.
