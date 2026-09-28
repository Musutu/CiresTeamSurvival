# RESUME: champ-select (feat/champ-select)

Champion select redesigned for the large roster (27 authored + 62 Paragon heroes, 93 reskins) and made smooth.

## Done

- **Roster browser** (`CireDraftBrowser.h/.cpp`, drawn by `CireRosterHUD.cpp`):
  - A paged grid of fixed-size portrait tiles: 24 per page at 1080p / 900p, 28 at 4K. Pages turn with the `<` / `>` buttons,
    the page diamonds, the mouse wheel over the roster, PageUp / PageDown, and Left / Right at the page edge.
  - Role tabs (ALL / TANK / DPS / SUPPORT) show live counts. In a role tab, primary-role heroes come first, then hybrids.
  - Search is case-insensitive and every word must match. It covers name, class and race, plus the keywords
    tank / dps / support / str / agi / int / paragon / authored.
  - Sort by Role, Name, Difficulty or Source.
  - Favourites: click the star on a tile. They are stored per user in `Saved/CireDraftFavourites.json`, always listed
    first, and can be shown on their own with the Favourites filter.
  - Chips filter by primary stat (STR / AGI / INT) and by source (AUTHORED / PARAGON), with a result count.
- **Skins**: a skin strip above LOCK IN shows Default plus each reskin as a chip. Click a chip or cycle with `<` `>` /
  `[` `]`. It uses the existing replicated `ServerSetChampionSkin`. The 24 skin-based "new champions" stay their own
  heroes, as classified in ParagonChampions.json.
- **Details column**:
  - name, class and race
  - role chips and a source chip
  - Difficulty / Primary / Attack / Weapon cells
  - lore
  - the **kit as 8 icons** (6 actives, passive, ultimate; tooltips)
  - STR / AGI / INT bars
  - the class trait

  Sections drop from the bottom when the height runs out. The old centre OVERVIEW / ABILITIES / LORE panel was removed so
  the figure has room.
- **Readability**: no text is below 12 logical units anywhere on the screen, which is 18 px at 1080p, 15 px at 1600x900
  and 30 px at 4K. The header, game-type list and LOCK IN were raised to 12 as well. The gallery audit enforces it, plus
  no overlapping text runs, cards or main regions.
- **Performance** (`CireDraftAssets.h/.cpp`, `CireDraftStage`):
  - No synchronous LoadObject on the hover path. Portraits, backgrounds, kit icons and bodies stream through one
    FStreamableManager.
  - Hover shows the details and a portrait card at once. The 3D body waits for a hover debounce (0.15 s, Paragon 0.9 s),
    then streams its binding packages asynchronously. The stage spawns it only once the packages are resident and the
    meshes have finished compiling. The card then crossfades into the live figure.
  - Stale body requests are cancelled. Completed bodies stay in an LRU of 6. The stage keeps a pool of 4 spawned bodies,
    so a revisit is instant. A neighbour body is preloaded (authored heroes only).
  - Backgrounds are kept in an LRU of 10, pinned at full resolution. A new scene only replaces the current one once it is
    resident, then crossfades, so shared paintings never reload.
  - The async-loading budget is raised to 12 ms per frame while the stage lives.
  - The preview binds only the idle, gait and attack clips (`GCireCreatureArtPreviewLite`), not the cast, hit and death
    clips and the FX their notifies drag in.
  - Tunables are in `Content/Data/DraftSelect.json` and are read at runtime.
- **Probe**: `CireDraftHoverProbe` / `Tools/RunDraftHoverProbe.py` (`-CireDraftHoverProbe`). It runs four phases:
  browse (30 heroes, 0.7 s), scrub (20 heroes, 60 ms), revisit (10 heroes) and linger (8 unseen heroes, 4 s / 10 s).
  It reports the stall, hitches, the ms spent on background / portrait / body loads, time to details and time to the
  live figure. Diagnostics log synchronous loads, GC and long frames.
- **Tests**: `CireDraftBrowser::RunTests` + `CireDraftAssets::RunTests` run in the native gate
  (CIRE_DRAFT_BROWSER_PASS / CIRE_DRAFT_ASSETS_PASS). They cover filters, search, sort, favourites pinning and
  persistence, grid fit, paging, skin cycling, LRU, debounce, the real roster (each hero once, no skin rows, Paragon
  flagged), the body path index, request / hit / cancel / LRU eviction, completion after a flush, background sharing /
  pinning, and the tunables.

## Measurements (same probe, same heroes, 1920x1080, -CireTripoChampions)

| Phase | Before avg / p95 / max stall | After | Hitches >50 ms | Body game-thread ms / switch |
|---|---|---|---|---|
| browse | 2165 / 7972 / 8435 | 71 / 216 / 220 | 45 -> 21 | 2155 -> 0 |
| scrub | 28 / 30 / 41 | 31 / 49 / 52 | 0 -> 1 | 0 -> 0 |
| revisit | 330 / 1111 / 1111 | 37 / 59 / 59 | 8 -> 1 | 318 -> 26 |
| linger | 730 / 994 / 994 | 294 / 731 / 731 | 13 -> 16 | 788 -> 14 |

- Details on hover: 187 ms -> 22 ms.
- Time to the live figure on unseen heroes: 3.9 s -> 4.0 s. The portrait card shows immediately in the meantime.
- Logs:
  - before: `Saved/DraftHoverChecks/20260928T140954Z_before2`
  - after: `Saved/DraftHoverChecks/20260928T140302Z_after6`

**Root cause of the 5-9 s Paragon freezes** (found with the probe diagnostics): every Paragon skeletal mesh logs "The
derived data key is different after the build. Save the asset". It then rebuilds its render data (LOD reduction, 5-7 s)
on its first load in **every editor session**, inside the loader's game-thread PostLoad. No async loading can hide it.

- Measured on a local copy of Terra: 6.5 s before a resave, 1.1 s plain load after it.
- `Tools/ResaveParagonMeshes.py` resaves all 155 Paragon meshes once. It was **not run**: it rewrites the shared
  licensed store while other editors are open. See Eric questions.

## Not done / next

- The Paragon heroes have no portraits (`Content/ParagonDerived` does not exist on this PC). Their tiles show the role
  sigil, and the hover card shows a sigil card. Fix by running `Tools/RunParagonPortraits.py` (local only).
- The F8 panel does not expose the DraftSelect.json tunables yet. They are JSON-only and read at startup.
- The 4K layout keeps tiles at about 86 logical units (28 per page). Larger tiles at 4K would be a small change in the
  `FitGrid` call.

## Assumptions / questions for Eric

1. **Resave the Paragon meshes?** Run `python Tools/ResaveParagonMeshes.py` once, with every editor closed. This removes
   the 5-7 s first-load freeze per Paragon hero, both in champion select and when a Paragon hero spawns in a match. It
   touches only the local licensed store. Afterwards `paragonHoverDebounceSeconds` can drop from 0.9 to about 0.35.
2. The kit shows the profile's 8 draft examples as icons. This follows the new brief; the 2026-09-25 ruling had said "no
   kit list". Descriptions stay in tooltips.
3. Paging was chosen over scrolling: the canvas HUD has no clipping.
4. Favourites are per user and local, never replicated.
5. A Paragon body loads only after 0.9 s of hover, or at once on select, so browsing never triggers the editor-only mesh
   rebuild freeze.

## Shared-file edits

- `Source/CiresTeamSurvival/CireCombatExpansionProbe.cpp`: 1 include + 1 line (`CireDraftBrowser::RunTests`).
- `Source/CiresTeamSurvival/CireCreatureArt.h/.cpp`: additive `GCireCreatureArtPreviewLite` flag (default false). While
  it is set, `Clip()` skips non-gait/attack roles. Only the champion-select stage sets it.
- `Tools/RunDraftGallery.py`: the new shot list and the readability floor text.

## Gate logs (after merging main daddb063)

- **Native: PASS.** `Saved/ExpansionChecks/20260928T153535070876Z` (CIRE_DRAFT_ASSETS_PASS 17, CIRE_DRAFT_BROWSER_PASS 33,
  CIRE_COMBAT_EXPANSION_PASS).
- **Network: PASS.** `Saved/NetworkSmoke/20260928T154106254972Z`. The first attempt hit an environmental probe timeout.
- **Interface: PASS.** `Saved/InterfaceSmoke/20260928T154249720948Z`.
- **Gallery: PASS at 1920x1080, 1600x900 and 3840x2160** (10 states, audit: text >= 12, no text / card / region overlaps).
  Report: `Saved/DraftGalleryChecks/20260928T142001369924Z`.
- **Review captures for Eric:** `Saved/ChampSelectReview/20260928T142655Z/index.html`.
- Bug found by the native gate and fixed: `DraftBodyLru()` read the tunables inside its own static initialiser
  (Tunables -> Reload -> DraftBodyLru), which re-entered the initialiser and deadlocked on the first call.
