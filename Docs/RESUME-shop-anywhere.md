# RESUME: feat/shop-anywhere (Playtest 6, section F)

Worktree `F:\CiresTeamSurvival-agents\cts-shop-anywhere`, ports 17550-17559 (first session), 17850-17899 (post-crash session).

## Done
- **Buy anytime, anywhere (server-authoritative).**
  - `CireItems::ShopAccessFor` allows item shopping in every phase except Finished, dead or alive, when `Vendors.json pricing.shopAnywhere` is true.
  - The Skill Shop is open at any time (`SkillShop.json access.anytime`). Bots still shop only in the old windows (`CireSkillShop::InShopWindow`), and the auto-open still happens only after a wave clear or in prep.
  - The client mirror of the access rule is in `CireShopUI` (`ClientAccess`).
- **Pricing by location** (`CireVendors`: `ApplyZone`, `ZoneFor`, `QuoteItem`, `QuoteSkill`, `QuoteLabel`, `QuoteTag`):
  - **Vendor:** in person (interactRange + `vendorReachSlack`) at the matching merchant, -10%.
    - For items, the matching merchant is the one who sells the item. Shared wares (potions, tomes) count at any merchant.
    - For skills, it is the merchant of the champion's primary stat: Arcane for INT, Armory for STR, Weaponsmith for AGI.
  - **Town:** within `Items.json townRadius` of the base, or within `pricing.townVendorRadius` of any of your realm's merchants. List price.
  - **Field:** everywhere else, +10%.
  - The server computes the price in `UCireInventory::Buy` and in `CireSkillShop::Buy` / `LevelUp` / `BuyBlocker`. The gold check uses the location price. Undo refunds exactly what was paid. The server message says why: "(-10% at the Weaponsmith)" or "(+10% out-of-town surcharge)".
- **Price delta in the UI:**
  - A "-10%" (green) or "+10%" (orange) pill on item cards and skill scrolls, with the price text tinted to match.
  - The detail panel shows "Your price: Xg (reason)", and the BUY button reads "BUY 495g (+10%)".
  - Item tooltips get a "reason: list > price" row. Skill tooltips show "(list Xg, reason)".
  - The status line in both tabs reads AT THE WEAPONSMITH · -10%, IN TOWN · LIST PRICES, or OUT OF TOWN · +10% SURCHARGE.
- **Out-of-town confirmation dialog** (`CireShopUI` `DrawConfirm`):
  - Styled like the shop's gilded panel. It shows the icon and name, then a breakdown of list price, surcharge and "You pay".
  - It adds a hint: "In town it costs Xg, and only Yg in person at the <merchant>".
  - It has a "Don't show this again" checkbox, BUY (Enter/Space) and CANCEL (Esc or a click outside) buttons.
  - While it is open the shop underneath is inert. Closing the shop cancels it.
  - It covers item buys, skill learns and skill level-ups.
- **Options > Interface > "Confirmation dialogs"** toggle (`bConfirmDialogs`), persisted. The per-dialog switch `bConfirmOutOfTownBuy` is also persisted. Turning the master switch back on re-enables every hidden dialog.
- **Skills follow the item-shop rules (Eric via coordinator, 2026-09-28):**
  - Skills and skill level-ups can be bought anytime, anywhere.
  - They get -10% at the merchant of the champion's primary stat (INT Arcane Emporium, STR/tank Armory, AGI/DPS Weaponsmith) and +10% out of town.
  - They use the same confirmation dialog, "Don't show this again" checkbox and Options toggle as items.
  - The Skill Shop is reachable anywhere with [K], or from a new clickable "[K] SKILLS" entry on the HUD bag bar next to "[B] SHOP".
  - The keybinding label now reads "Skill Shop (anytime, anywhere)".
- **Buy/sell feedback polish:**
  - A red "-Ng" coin floater rises from the gold counter on item and skill purchases.
  - Purchase toasts and the event strip carry the price reason.
  - The existing fly-to-slot, flash, vendor nod and sounds are unchanged.
- **Tests:**
  - `CireVendors::RunSmoke`:
    - Pure maths: 450 becomes 405 / 450 / 495, rounding is checked, and the percentages come from data.
    - The pricing JSON parses.
    - Zones: at the Weaponsmith, another merchant's ware, shared potions, and skills by stat.
    - The server charges -10% at the counter (and undo refunds it) and +10% out of town, and the surcharge counts in the gold check.
    - Classic mode means list price.
  - `CireSkillShopTests`: with anytime on, a mid-wave level-up charges the location price.
  - `CireOptionsTests`: both toggles round-trip.
  - Interface probe, on each remote client at the last step: `CIRE_INTERFACE_CLIENT_SHOP_CONFIRM_PASS`. It checks that the dialog appears out of town (item and skill) and that Cancel sends nothing. It stays silent in town and at a merchant. "Don't show this again" suppresses it next time, and Options off suppresses it. The run is dry, nothing is bought, and settings are restored.
  - Network probe (`CireController.cpp` client / `CireMatch.cpp` server): the rejected-action check now uses an unaffordable buy (rusted longsword, 170g, above the starting 120g even at -10%), since mid-wave buying is legal. The server then moves the champion out of town along the route and funds 500g. The client buys the Sandglass Charm mid-wave and must be charged exactly 165g (+10%) with the out-of-town notice (`CIRE_NET_CLIENT_FIELD_BUY_PASS`). The server checks after the bot takeover that the charm and gold stuck. The client probe timeout went from 30 to 45 s.
  - The classic-rules checks in `CireItems::RunSmoke` (CireProgressionTests) and in the Skill Shop tests pin `shopAnywhere = false` / `anytime = false` for their duration.

## Not done / Next
- F8 live editing of the pricing block isn't wired (F8 files aren't in my ownership). Vendors.json is data; `CireVendors::MutablePricing()` is the hook for an F8 row.
- The dialog was only probe-checked for logic (the probe clients run -nullrhi). A visual capture at the next playtest is advised.

## Assumptions / questions for Eric
- "Anytime" includes while dead and during the arena (only the Finished phase is closed). Tell me if dead or arena buying should be blocked.
- The skill discount comes from the merchant of your primary stat (skills scale off the primary stat).
- Potions and tomes (sold by all three merchants) get the discount at any merchant.
- Town = the old 900 cm base radius, or within 3200 cm of any of your realm's merchants (`Vendors.json pricing.townVendorRadius`). Tune it when the town layout settles.
- Rounding goes to the nearest gold, with halves rounding up.
- Selling has no location modifier (still 60% everywhere).
- "Don't show this again" hides only the out-of-town purchase dialog. The Options toggle is the master switch, and turning it back on restores hidden dialogs.
- Bots keep shopping only between waves (they don't pay surcharges mid-wave).
- The between-wave READY TO CONTINUE pause and the Skill Shop auto-open after a wave clear are kept as a strategy break. They don't block anytime buying.
- The skill-to-merchant match uses the champion's primary stat, not per-skill role tags, because every skill scales off the primary stat. INT maps to Arcane, STR (tanks) to Armory, AGI (DPS) to Weaponsmith.

## Shared-file edits (small, additive)
- `CireItems.cpp`: include; one line in `ShopAccessFor`; location price in `UCireInventory::Buy` (plan without a gold cap, quote, charge the quote, message reason).
- `CireUISettings.h/.cpp`: `bConfirmDialogs`, `bConfirmOutOfTownBuy` (defaults, load, save).
- `CireOptions.cpp`: the "Confirmation dialogs" toggle (Interface page 0, right column).
- `CireHUD.cpp`: `HandleEscape` cancels the confirmation first.
- `CireInterfaceProbe.cpp`: the client step-9 shop-confirm check (item plus skill learn/level-up out of town).
- `CireKeybindings.cpp`: the ToggleSkillShop label text only.
- `CireProgressionTests.cpp`: pins the classic rules for `CireItems::RunSmoke`.
- Data: `Content/Data/Vendors.json` (`pricing` block), `Content/Data/SkillShop.json` (`access.anytime`).

## Gate logs (branch = main 8b323888 merged in as 4f59bbbf + this work)
- Merge: conflicts in `CireController.cpp` (main's ability-tuner client check now runs at the rejection step, before the shop-anywhere field-buy steps) and `CireHUD.cpp` (Esc chain: tuner, then quick keybind, then shop confirm). Both resolved additively.
- Build: Succeeded (`Saved/shopany-build.log`).
- Native: **PASS**, `Saved/ExpansionChecks/20260928T074248254470Z/report.json` (VENDORS 49, SKILLSHOP 72, COMBAT_EXPANSION_PASS).
- Network: **PASS** on the first try, `Saved/NetworkSmoke/20260928T074248236957Z/report.json`. The field buy charged 165g against the 150g list price, and the server kept the charm and 335g after the bot takeover.
- Interface: **PASS**, `Saved/InterfaceSmoke/20260928T074401488566Z/report.json`. Both clients logged `CIRE_INTERFACE_CLIENT_SHOP_CONFIRM_PASS`.
- The earlier network failures (first session, 7 attempts) were environmental: the client stalled in the AssetRegistry gather under load.
