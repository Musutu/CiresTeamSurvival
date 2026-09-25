# Ability icon licenses

## Painted icons

Ability (all 39 pool skills and all 63 champion-kit skills), passive, ultimate, trait, status-effect (`T_status_<name>`) and role (`T_role_<name>`)
icons listed in `Art/Icons/ChatGPT/Abilities/` are painted icons **generated for Eric via ChatGPT
(OpenAI), 2026-09-25**. They belong to Eric (the project owner) under OpenAI's terms for generated
output; no third-party icon pack, traced game art or licensed asset is used. The full source sheets
are in `Art/Icons/ChatGPT/abilities_*.png` and `ui_*.png`, sliced to 256x256 by
`Tools/SliceIconSheet.py`.

## Procedural icons

Any future `T_<id>` without a painted PNG falls back to original procedural art from `Tools/BuildAbilityIcons.py`
(no licensed sources).

Rebuild and import both: `python Tools/RunAbilityIcons.py --pylib <dir with Pillow>`
(`--procedural` ignores the painted overrides).
