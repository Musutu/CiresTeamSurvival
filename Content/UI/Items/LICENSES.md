# Item icon licenses

## Painted icons (current art)

The item icons in `Content/UI/Items` (`T_Item_<id>.uasset`, sources in `src/T_Item_<id>.png`) are
painted icons **generated for Eric via ChatGPT (OpenAI), 2026-09-25**. They belong to Eric (the
project owner) under OpenAI's terms for generated output; no third-party icon pack, traced game art or
licensed asset is used. The full source sheets are kept in `Art/Icons/ChatGPT/items_*.png` and are
sliced to 256x256 by `Tools/SliceIconSheet.py` into `Art/Icons/ChatGPT/Items/T_Item_<id>.png`.
This covers every shop item plus the toast icons `T_Item_gold`, `T_Item_teleport` and
`T_Item_challenge`.

Any item without a painted PNG falls back to the procedural icon below.

## Items v2 (art-2d)

The 12 items-v2 icons (`aether_conduit`, `oathkeeper_charm`, `windrunner_boots`, `twinstep_treads`,
`moonwell_codex`, `lifebinders_reliquary` and the six path uniques `artificers_heartforge`, `soulbinders_crook`,
`heart_of_cataclysm`, `shackles_of_the_pale_king`, `sigil_of_apotheosis`, `stormhowl_ravager`) are painted icons
**generated for Eric via ChatGPT (OpenAI), 2026-09-25**, from `Art/Icons/ChatGPT/items_v2_paths.png` (3x3) and
`items_v2_uniques.png` (2x2; its 4th tile, a coin purse, is unused).

## Procedural fallback

`Tools/BuildItemIcons.py` draws original procedural icons (signed-distance shapes, gradients and
lighting written for Cire's Team Survival); no licence obligations apply. `--procedural` forces them.

Regenerate: `python Tools/BuildItemIcons.py` (copies painted overrides, renders the rest) then
`Tools/BuildShopContent.py` (imports as UI textures, no mips, uncompressed icon format).
