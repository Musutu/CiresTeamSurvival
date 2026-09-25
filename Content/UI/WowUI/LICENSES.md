# WowUI content licenses

All interface artwork under `Content/UI/WowUI` is original to this project or used under the
licenses below. Nothing here is taken from World of Warcraft or any Blizzard asset; the frames
are drawn procedurally by the HUD (`CireHUD*.cpp`) and only follow WoW's layout conventions.

## Fonts (SIL Open Font License 1.1)

| Asset | Source file (`Fonts/src`) | Family / author | License text |
| --- | --- | --- | --- |
| `Fonts/UIHeading` | `marcellus_Marcellus-Regular.ttf` | Marcellus, Copyright (c) 2012 Brian J. Bonislawsky DBA Astigmatic (AOETI) | `Fonts/src/marcellus_OFL.txt` |
| `Fonts/UIBody` | `alegreyasans_AlegreyaSans-Medium.ttf` | Alegreya Sans, Copyright 2013 The Alegreya Sans Project Authors (Huerta Tipografica) | `Fonts/src/alegreyasans_OFL.txt` |
| `Fonts/UIBodyRegular` | `alegreyasans_AlegreyaSans-Regular.ttf` | Alegreya Sans | `Fonts/src/alegreyasans_OFL.txt` |
| `Fonts/UIBold` | `alegreyasans_AlegreyaSans-Bold.ttf` | Alegreya Sans | `Fonts/src/alegreyasans_OFL.txt` |
| `Fonts/UINumbers` | `alegreyasans_AlegreyaSans-ExtraBold.ttf` | Alegreya Sans | `Fonts/src/alegreyasans_OFL.txt` |
| `Fonts/UIDisplay` | `cinzel_Cinzel-wght.ttf` | Cinzel, Copyright 2020 The Cinzel Project Authors (github.com/NDISCOVER/Cinzel) | `Fonts/src/cinzel_OFL.txt` |

Downloaded 2026-09-24 from the Google Fonts repository (`github.com/google/fonts`, `ofl/`).
The OFL permits bundling and embedding in software, including commercial games; the fonts may
not be sold by themselves, and the Reserved Font Names must not be used for modified versions.
The `.uasset` font faces embed the unmodified TTF data. Marcellus is used as an OFL stand-in for
the "Friz Quadrata" look of WoW headings; Alegreya Sans is the body/number face. Cinzel (downloaded
2026-09-25, imported by `Tools/BuildSkillShopArt.py`) is the wide-spaced display face of the Skill Shop
and Armory titles (`ECireFont::Display`).

## Textures (original, procedural)

`Textures/T_Panel, T_Border, T_Button, T_ButtonUlt, T_ButtonPassive, T_Glow, T_Gloss, T_IconBg,
T_Gem, T_Header` are generated from value noise and geometry by `Tools/BuildWowUITextures.py`
(sources in `Textures/src/*.png`). Original to this project; no third-party license.

## Sounds (original)

`Sounds/S_LevelUp`, `S_AggroGained`, `S_AggroLost`, `S_ThreatWarning`, `S_TargetSelect`, `S_BannerHorn`, `S_BannerChime` are
synthesized from sine partials by `Tools/BuildWowUIContent.py` (sources in `Sounds/src/*.wav`).
They are original to this project and carry no third-party license.

## Rebuilding

`F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/BuildWowUIContent.py`
regenerates the WAVs, imports fonts and sounds in the isolated `Tools/ContentBuilder` project
(full editor, offscreen: font-face import requires a Slate application) and copies the assets here.
