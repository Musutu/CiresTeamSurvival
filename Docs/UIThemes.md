# UI themes

The interface has four selectable themes, one design with swappable skins. Pick one in
**Options (F9) → Interface → UI theme**. The change applies immediately to every screen and is saved
in the profile (`UITheme`, schema 6). **Gilded Citadel** is the default.

| Id | Name | Look |
| --- | --- | --- |
| `GildedCitadel` | Gilded Citadel | Navy-black panels, bevelled gold filigree frames, gold diamonds. Closest to Eric's Skills and Champion Select references. |
| `Ironbound` | Ironbound | Forged iron and blackened steel, riveted corner plates, glowing ember-orange seams and crystals. |
| `ArcaneVeil` | Arcane Veil | Violet obsidian, rune-engraved silver, amethyst gems, soft violet glow. |
| `VerdantBloom` | Verdant Bloom | Living wood and vines, blossoms, soft greens with rose and gold light. |

The concepts, UI-only references and art sheets were generated for Eric with ChatGPT and are listed
in `Content/UI/Themes/LICENSES.md`.

## Data

`Content/Data/UIThemes.json` holds one entry per theme:

* `palette`: linear RGBA colours. `CireUITheme::SetActive` writes them into the themed block of
  `CireUIColors` (`Gold` = trim, `BrightGold`, `Parchment` = text, `Muted`, `Ink`, `Card`, `Hover`,
  `ThemeAccent`, `ThemeGlow`, `TooltipBg`, `TooltipBorder`, `BarBack`, `TitleText`, `ThemeFiligree`,
  `PanelTint`). Semantic colours (health, mana, reactions, schools, rarities) never change.
* `atlas` / `fill`: the theme's texture atlas and tileable panel fill (`/Game/UI/Themes/<Id>/`).
* `pieces`: the atlas rectangle (`rect`, pixels), `slice` (nine-slice corner or strip cap fraction) and
  drawn `corner` (logical units) of each piece: `Panel, Card, Tooltip, Slot, SlotPassive, SlotUltimate,
  Ring, BarFrame, CastFrame, Minimap, Banner, Divider, Ornament, BarFill`. Written by the build tool.
* `glowStrength`, `fillScale`, `panelOpacity`, `gem`, `cornerOrnaments`.

Files that keep a local palette copy bind **references** to `CireUIColors` (e.g.
`const FLinearColor &Gold=CireUIColors::Gold;`), so they follow the theme without code changes.

## Painters

`CireUIStyle` draws from the active theme when its art is loaded (`CireUIStyle::HasThemeArt()`), and
falls back to the original procedural kit otherwise:

| Kit call | Themed rendering |
| --- | --- |
| `Frame` Panel / Unit | themed fill with a tone gradient and inner shadow, nine-sliced `Panel` frame drawn just outside the rectangle, state accent line (aggro, selection), `Ornament` crest on wide panels |
| `Frame` Card / Inset | fill + `Card` nine-slice / recessed fill |
| `Button` | fill brightened per state, `Card` frame, accent glow on hover / selection |
| `IconSlot` | `Slot`, `SlotPassive`, `SlotUltimate` frames; hover brightens, pressed darkens; themed glow |
| `Bar` | `BarFill` gloss tinted by the resource colour, bevelled trim when 8+ units tall |
| `CastBar` (new) | `CastFrame` with ornamental caps outside the bar, themed fill |
| `TooltipFrame` / `Tooltip` | dark backdrop at the player's opacity, `Tooltip` nine-slice, `TitleText` titles |
| `Header` | caption over a `Divider` |
| `Banner` | `Banner` ribbon behind the title band |
| `PortraitRing`, `Medallion`, `MinimapFrame`, `Divider`, `Ornament` (new) | the corresponding pieces |

`CireUITheme::Draw(P, Piece, X, Y, W, H, Tint, CornerScale)` draws any piece (nine-slice for frames,
horizontal three-slice for strips, stretched for slots/rings); `DrawFill` tiles the fill in logical units.
Each theme is one atlas, so a frame is at most nine quads from one texture; nothing is allocated per frame.

Covered: player, party, target, focus, boss frames, threat meter, action bars, minimap, chat, meters,
tooltips, cast bars, buff rows, nameplates, banners and callouts, loot window, bag bar, stats window,
Options, Skill Shop and Armory framing (`CireShopArt::Panel` gets the theme frame and filigree colour;
the scroll art and the near-black ground stay). Champion select keeps its own look.

## Building the art

```
python Tools/BuildUIThemes.py --pylib <dir with Pillow+numpy>   # slice, pack, update JSON, import
python Tools/BuildUIThemes.py --no-import --theme Ironbound      # slice one theme only
```

Sources: `Art/UI/Themes/ChatGPT/<key>_frames.png` (2x2: panel, tooltip, minimap, card) and
`<key>_parts.png` (4 rows: button / passive / ultimate / ring; bar + cast frames; banner + divider;
crest, bar fill, fill swatch), transparent PNGs. Pieces are found as alpha components.

## Checks

* `CireUITheme::RunSmoke` (native, `RunExpansionChecks.py --only native`): four themes validate, every
  piece resolves in every theme, palettes apply, the parser reports broken data, and the profile
  persists the theme, sanitises unknown ids and migrates schema 5 to the default.
* `Tools/RunWowUIGallery.py [--theme <Id>]`: 30 captures in the chosen theme (`-CireUITheme=<Id>`
  overrides the profile without saving); stage 29 is the theme picker, stage 30 the HUD at scale 0.8.

## Rounded bars and nameplate stacking (playtest pass)

* **Rounded bars.** `CireUIStyle::RoundBar` / `Capsule` draw WoW-style capsule bars from
  `/Game/UI/Themes/Common/T_BarCapsule` (generated by `Tools/BuildUIThemes.py`: a white capsule with a
  soft gradient + a gloss layer, 3-sliced so the ends stay round; very low fills become a small pill).
  A faint theme-coloured rim, a thin dark border, a colour-tinted dark back, the pale damage chunk,
  the fill and the sheen are all drawn **inside** the bar's rectangle, so stacked bars never overlap.
  Every themed `Bar` (player, target, focus, party, boss, threat and meter rows), cast bar and
  nameplate uses it; health, mana and reaction colours are unchanged.
* **Nameplate stacking.** `DrawNameplates` collects the plates, keeps the selected and nearest in
  place, pushes any plate whose box (status chips, name, bar, cast bar) would overlap an earlier one
  upward (max 160 units), then draws far to near. `ACireHUD::LastPlateBoxes` exposes the result; the
  WoW UI gallery checks for overlaps. Enemy cast bars sit 4 units below the plate, inset and thinner.
  Boss-frame rows use in-bounds card frames so stacked rows never overlap.
* **Real-fight captures.** `Tools/RunUIWaveCapture.py --theme <Id>` runs the wave soak rendered
  (`-CireWaveSoak -CireUIWaveCapture`), targets the nearest monster when 5+ fight near the player
  during an active wave, and saves `Saved/UIWaveCapture/<utc>/wave_fight_NN.png`.
* **Concept pass.** Themed action slots show the bevelled frame just inside the slot with the icon
  inset (a dark gap separates neighbours); themed buttons use brighter, larger engraved caps.

## Per-piece frame art (hud-art)

The frames were rebuilt from **individually generated pieces** (ChatGPT image generation for Eric, one
image per piece, front-on on a flat key colour: magenta for Gilded Citadel / Ironbound, green planned
for Arcane Veil, blue for Verdant Bloom). Raw captures: `Art/UI/Themes/ChatGPT/Pieces/<key>/<Piece>.png`.

```
python Tools/BuildHUDArt.py --pylib <dir with Pillow+numpy> [--theme GildedCitadel] [--no-import | --import-only]
```

`BuildHUDArt.py` keys the background to alpha (colour unmixing, no fringe), rebuilds nine-slice frames
from four corners plus straight edge strips whose cross-section is the mean of the whole straight run
(tileable, corners feathered into the profile), 3-slices the strips (cast frame, bar frame, title plate,
divider), squares the slots/rings (the portrait ring's hole is placed at 72% of the piece), splits the
state sheets (`SlotStates` → `Slot, SlotHover, SlotPressed, SlotCooldown, SlotDisabled`; `ButtonStates`
→ `Button, ButtonHover, ButtonPressed, ButtonDisabled`; `Card` stays the slimmer card frame because party rows and list rows draw it behind text), grades
every piece of a theme to one metal colour, deepens recesses and adds an S-curve so bevels read at
15-20 px, adds a faint theme-coloured halo, packs the atlas with the shared packer of
`BuildUIThemes.py` and writes rects/slices into `UIThemes.json`. The drawn corner size is solved from
the measured band thickness so every frame's band has a set logical width (`NINE[...]["band"]`),
capped by `CORNER_MAX` so corner ornaments never cover the panels' corner labels. Pieces without a new
file keep their previous slice. Recess darkening, S-curve and saturation are per theme (`DIMENSION`): Gilded Citadel and Ironbound
push recesses toward black, Arcane Veil keeps its violet rune channel and Verdant Bloom its mossy
living wood (Eric: "vibrant and fun looking, and crisp").

All four themes now have the same 14 per-piece files (Panel, Tooltip, Card, ButtonStates, Minimap,
SlotStates, SlotPassive, SlotUltimate, Ring, BarFrame, CastFrame, Banner, Divider, BuffBorder); the
panel fill, bar fill and crest ornament keep the sheet slices. Key colours: magenta (Gilded Citadel,
Ironbound), green (Arcane Veil), blue (Verdant Bloom). Prompts follow one recipe per theme (rails,
corner ornament, small cap, gem, crest, hover / disabled look, face colour) so every piece of a theme
shares one material language.

### Painted state pieces

`ECireThemePiece` has optional pieces after the 14 required ones (`RequiredCount`): `SlotHover`,
`SlotPressed`, `SlotCooldown`, `SlotDisabled`, `Button`, `ButtonHover`, `ButtonPressed`,
`ButtonDisabled`, `BuffBorder`. A theme without them validates and falls back to the base piece plus a
tint. When present:

* `CireUIStyle::IconSlot` draws the painted hover / pressed / cooldown frame of normal slots (passive
  and ultimate keep their own frames);
* `CireUIStyle::Button` draws the painted button state (opaque face + frame) instead of fill + card
  frame, so every menu button, tab and HUD button switches together;
* buff / debuff icons draw the theme's `BuffBorder`, tinted toward the dispel colour so the colour code
  still reads (plain dispel-coloured lines under 16 px or without theme art).

`CireUITheme::RunSmoke` checks that every shipped theme resolves the state pieces too.

### Portraits in the unit frames

`CireUIStyle::PortraitFace` draws the painted champion portrait (`/Game/UI/Draft/Portraits/
T_Portrait_<ChampionProfileId>`, the same art as champion select) clipped round (`FCireUIPainter::
TexDisc`, a textured triangle fan, so no square corners show) inside the theme's portrait ring, with a
soft top light. The player frame, party rows and hero target / focus frames use it; the role emblem
moves to a small `CireUIStyle::RoleBadge` (the theme's ring as a medallion) at the ring's lower right.
Monsters and constructs keep the role sigil in the ring. Missing portraits fall back to the sigil.

### Corner clearance

`CireUIStyle::FrameCornerClear(W, H)` returns how far the active theme's panel corner ornament reaches
into a Panel / Unit frame; the target / focus caption (e.g. `ELITE / CASTER`) starts past it. The
caption itself comes from `CireUnitFrameHeader` (monster, hero and construct are separate branches:
an unranked monster used to fall through to `...CONSTRUCT`), checked by the native smoke.
