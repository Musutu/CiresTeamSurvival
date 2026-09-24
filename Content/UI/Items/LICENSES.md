# Item icon licenses

All item icons in `Content/UI/Items` (`T_Item_<id>.uasset`, sources in `src/T_Item_<id>.png`) are
original to this project. They are generated procedurally by `Tools/BuildItemIcons.py` from
signed-distance shapes, gradients and lighting written for Cire's Team Survival; no third-party
image, icon pack, font or AI-generated artwork is used, and nothing is traced from League of Legends,
World of Warcraft or any other game. No license obligations apply.

Regenerate: `python Tools/BuildItemIcons.py` then `Tools/BuildShopContent.py` (imports as UI textures,
no mips, uncompressed icon format).
