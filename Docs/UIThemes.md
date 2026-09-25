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
