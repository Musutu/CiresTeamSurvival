# UI theme art: provenance and licensing

All theme art in this folder was **generated for Eric via ChatGPT (OpenAI), 2026-09-25**, in Eric's own
ChatGPT account, and belongs to Eric. It was made from the game's own HUD captures and Eric's reference
images (`skills-target.png`, `champion-select-target.png`, also his own). No third-party, Fab or Epic
assets are included.

| Theme | Source sheets (`Art/UI/Themes/ChatGPT/`) | Concept mockup |
| --- | --- | --- |
| Gilded Citadel | `gilded_frames.png`, `gilded_parts.png` | `concept_gilded_citadel.png` |
| Ironbound | `ironbound_frames.png`, `ironbound_parts.png` | `concept_ironbound.png` |
| Arcane Veil | `arcane_frames.png`, `arcane_parts.png` | `concept_arcane_veil.png` |
| Verdant Bloom | `verdant_frames.png`, `verdant_parts.png` | `concept_verdant_bloom.png` |

`Tools/BuildUIThemes.py` slices those sheets with Pillow (nine-slice corners + straight edge strips, bar
caps, slot/ring crops, a mirrored tileable panel fill) into `<Theme>/src/*.png`, packs each theme into
one atlas (`T_<Theme>_Atlas`) plus a fill (`T_<Theme>_Fill`) and imports them as UI textures. The
UI-only reference mockups (`ui_only_*.png`) are also ChatGPT generations for Eric (same date).
