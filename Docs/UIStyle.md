# Cire UI style kit

`Source/CiresTeamSurvival/CireUIStyle.h/.cpp` is the single visual language of the HUD
(WoW-inspired dark fantasy: textured slate panels, iron-and-gold 9-slice borders, gem
accents, glossy bars, crisp OFL fonts). Use it for every new screen so menus stay
consistent. It is Canvas-based and **draw-only**: you do the hit testing, pass the
state in. `CireBanners.h/.cpp` adds the queued transition banners.

## Painter

```cpp
FCireUIPainter P = Painter();          // inside ACireHUD (panel transform applied)
// or standalone:
FCireUIPainter P; P.Canvas = Canvas; P.Scale = PixelsPerLogicalUnit;
P.Origin = {X, Y}; P.Stretch = {1, 1}; P.Alpha = 1;
```

Coordinates are logical units (1280x720 reference before the interface scale).
`ACireHUD::Scale` already includes the player's UI scale, so everything drawn through a
painter scales consistently. Primitives: `Rect, Line, Disc, Circle, Tri, Tex, NineSlice,
Text, TextWidth, Wrapped`. `Alpha` fades everything the painter draws (fades/slide-ins).

## Typography

`ECireFont`: `Body` (Alegreya Sans Medium), `Bold` (Alegreya Sans Bold), `Heading`
(Marcellus, the Friz-Quadrata stand-in, best in CAPS), `Numbers` (Alegreya Sans ExtraBold),
`Auto` (digits -> Numbers, ALL CAPS -> Heading, size >= 14 -> Bold, else Body).
Text is rasterized at its final pixel size (never a stretched bitmap), with an optional
1px outline (`bOutline`, use over the 3D world) and a drop shadow (default on).
Sizes are logical: 8-9 captions, 10-11 body, 12-15 names/titles, 24+ numbers, 36 banners.

## Palette (`CireUIColors`)

`Gold` trim/captions, `BrightGold` elite, `Parchment` body text, `Muted` secondary,
`Hostile/Neutral/Friendly` reaction colours, `Health/Mana/Energy` resources,
`Cast` (interruptible) / `CastLocked` (uninterruptible), `Silver` rare, `Orange` warnings.

## Components (`CireUIStyle::`)

| Call | Use |
| --- | --- |
| `Frame(P, X, Y, W, H, Accent, ECireFrame::Panel/Unit/Card/Inset/Tooltip)` | Ornate panel (slate texture, sheen, iron+gold border, accent trim + gem). Card = buttons/list rows, Inset = recessed wells (bars, lists). |
| `Header(P, X, Y, W, Caption)` | Title strip with a gold filigree underline. |
| `Button(P, X, Y, W, H, Label, ECireButtonState::Normal/Hover/Pressed/Disabled/Selected)` | Standard button; hover glows, pressed sinks 1px. |
| `Glow(P, X, Y, W, H, Color)` | Soft additive glow (hover/selection/proc). |
| `IconSlot(P, X, Y, Size, FCireIconSlot, Time)` | Action-bar button: icon (painted texture or sigil), keybind label, radial cooldown sweep + countdown, blue no-resource and red out-of-range tints, press flash, animated proc glow, charges, passive (octagon) and ultimate (gold) frames. |
| `Sigil(P, Id, X, Y, Size, Color)` | Procedural vector ability icon for an id. |
| `FindAbilityIcon(Id)` | Shared painted icon lookup: `/Game/UI/Abilities/T_<id>` (champion-draft set), cached; null -> use the sigil. |
| `CooldownSweep(P, X, Y, Size, Remaining01)` | Clockwise WoW sweep over a square. |
| `Bar(P, X, Y, W, H, Value01, Color, &Trail, Now, Text)` | Glossy bar with smooth fill and a trailing pale "damage chunk" (keep one `FCireBarTrail` per bar). |
| `TooltipFrame` / `Tooltip(P, X, Y, W, Title, Body, Scale, Opacity, bDraw)` | WoW tooltip backdrop / title+body tooltip (bDraw=false measures height). |
| `Toast(P, X, Y, W, IconId, Title, Body, Age, Life, Accent)` | Slide-in toast card (loot, purchases, pickups). |
| `Banner(P, ViewW, Y, FCireBannerSpec, Age)` | Big animated banner (use the queue below instead of calling directly). |

## Ornate shop framing (`CireShopArt`)

The Skill Shop and the Armory share the framing of Eric's target image: `Panel` (near-black, double
thin gold border, corner ornaments, a raised title plate), `Title` / `Spaced` (letter-spaced
`ECireFont::Display` = Cinzel caps), `Rule` / `Divider` (diamond-studded), `CompassStar`, `CrestRing` /
`Crest`, `Scroll` (3-sliced scroll card with tier glow, golden sparks, prismatic sheen and wisps), and
`WaxSeal` (purchase stamp). Ink colours for parchment are linear values (`CireShopArt::Ink`).

## Transition banners (`CireBanners`)

```cpp
#include "CireBanners.h"
CireBanners::Show(ECireBanner::BossSpawned, TEXT("Gravemaw"), TEXT("A pack leader has appeared."));
```

Types: `LevelUp, WaveIncoming, WaveCleared, PrepPhase, Arena, Recovery, ChallengeUnlocked,
BossSpawned, Victory, Defeat, Custom`. Banners are priority-queued (Victory/Defeat >
Boss > phase changes > challenge > wave > level > cleared), never overlap, drop
duplicates within 1.5s, carry a sound (horn / chime / level chime) and scale with the UI
scale. `ACireHUD` detects phase, wave, cleared-wave, boss, pack-leader/challenge-tier and
victory/defeat transitions itself from replicated state (so LAN clients see them too); game
code does not need to call anything for those. Other systems (shop, loot) can call `Show`.
Banners are local presentation: calling `Show` on the server shows it only on the host.

## Assets

`Content/UI/WowUI/` — fonts (OFL), textures `T_Panel, T_Border, T_Button, T_ButtonUlt,
T_ButtonPassive, T_Glow, T_Gloss, T_IconBg, T_Gem, T_Header` (procedural, generated by
`Tools/BuildWowUITextures.py`), sounds (synthesized). Rebuild everything with
`Tools/BuildWowUIContent.py`. Licenses: `Content/UI/WowUI/LICENSES.md`.
`CireUIStyle::AssetPaths()` lists them for hard references so they cook.

### Painted icons (ChatGPT art)

Item, ability, status-effect and role icons are painted art generated for Eric via ChatGPT
(OpenAI). Source sheets live in `Art/Icons/ChatGPT/*.png`; `Tools/SliceIconSheet.py` cuts a 3x3
sheet into 256px PNGs in `Art/Icons/ChatGPT/Items` (`T_Item_<id>`) and `Art/Icons/ChatGPT/Abilities`
(`<id>`). `Tools/BuildItemIcons.py` + `Tools/BuildShopContent.py` and `Tools/RunAbilityIcons.py`
use those PNGs in place of the procedural renders (`--procedural` ignores them), so asset paths stay
`/Game/UI/Items/T_Item_<id>` and `/Game/UI/Abilities/T_<id>`. Extra painted-only textures:
`T_status_<stun|silence|root|slow|heal_cut|taunt|disarm|fear|armor_break|poison|curse>` (buff/debuff
frames, `ACireHUD::StatusIconId`), `T_role_<tank|damage|support|hybrid>` (unit-frame portraits when
no champion portrait is drawn, >= 24px) and `T_Item_gold|teleport|challenge` (toasts).
`Tools/IconContactSheet.py DIR OUT.png` renders a 64/40px review sheet. Licences:
`Content/UI/Items/LICENSES.md`, `Content/UI/Abilities/LICENSES.md`.

## Verification

`Tools/RunWowUIGallery.py` renders 20 deterministic 1080p captures (`-CireWowUIGallery`, isolated
profile) to `Saved/WowUIGallery/<utc>/`: target/focus/boss frames, NPC and boss unit tooltips, tooltip
avoidance, SCT, aggro/threat/lost-aggro alerts, level-up, 0.7 and 1.15 interface scale, options pages,
layout editor, action-bar states, Quick Keybind mode, Keybindings page, a wave banner and an ability
tooltip. Native checks cover fonts loading, the interface scale, anchoring, tooltip placement and the
NPC read API; the images still need a human look.

## Themes

The kit is skinnable: `Docs/UIThemes.md`. The palette's themed block (`Gold`, `Parchment`, `Muted`,
`Ink`, `Card`, `Hover`, ...) is written by `CireUITheme::SetActive`; the painters draw the active
theme's atlas pieces (nine-slice frames, slots, rings, bars, banners). New themed calls: `CastBar`,
`PortraitRing`, `Medallion`, `MinimapFrame`, `Divider`, `Ornament`, `BarFrame`, `HasThemeArt`.
