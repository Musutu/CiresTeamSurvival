# Purchased Fab packs: Add to Project

Checked in Eric's Fab library on 2026-09-25 (68 items; the 14 below are the new paid ones, the rest are the free pass in `FAB-ADD-TO-PROJECT.md`). Every pack is **Unreal Engine format only**, so it has to come in with **Add to Project**, a native UI that only Eric can click. Nothing was bought and no licence was accepted by an agent. Machine-readable list: `Art/Fab/PurchasedPacks.json`.

## Where the packs go (read first)

**Target project, always:**

```
C:\Users\Eric\Documents\GitHub\CiresTeamSurvival\CiresTeamSurvival.uproject
```

That is the main checkout. Do **not** add packs into a `cts-*` worktree.

- Every agent worktree is also named *CiresTeamSurvival*, so the Launcher's project picker shows about 28 identical names. The unambiguous route is: open the main project in the 5.8 editor (double-click the .uproject above), then **Window → Fab → My Library → pack → Add to Project**. Add to Project from inside the editor always targets the open project.
- With the Launcher route (**Unreal Engine → Library → Fab Library → pack → Add to Project**), pick the entry whose path is `Documents\GitHub\CiresTeamSurvival`. Hovering a project shows its path.
- **Pack doesn't declare 5.8?** (Crossbow Animation Set, Undead Pack) Tick **Show all projects** in the Add to Project dialog, choose CiresTeamSurvival, then pick the newest offered version (5.7). Content-only packs from 5.x load in 5.8.
- If a pack offers to change project settings, add plugins or overwrite config, choose **No / Skip** where possible, and tell the Fab integration agent which pack asked.

Each pack lands in its own top-level `Content/<PackFolder>`. Licensed content is **never committed** (the repo is public): see *Storage* below.

## Click list (in this order)

| # | Done | Exact Fab name | Seller | UE versions | 5.8 | What it is | Used for |
|---|---|---|---|---|---|---|---|
| 1 | [ ] | **GDH All Animation Bundle** | GameDevHero | 5.5–5.8 | yes | ~6,872 combat anims on UE5 Manny. If it lists sub-packs, add all of them | attacks, casts, hits, deaths, dodges per weapon archetype |
| 2 | [ ] | **Male Locomotion Set** | VanillaLoop | 5.0–5.8 | yes | strafe + non-strafe locomotion | idle / run / strafe / backpedal / jump |
| 3 | [ ] | **Gun & Sword Animation Pack** | 9CG | 5.0–5.8 | yes | pistol + sword set, UE5 Manny | pistol / gunblade champions |
| 4 | [ ] | **Crossbow Animation Set** | 2DragoH | 4.26–5.7 | **no → Show all projects** | 174 crossbow anims, UE5 Manny/Quinn | ranged champions |
| 5 | [ ] | **Big Pack Magic Effects Niagara** | Lord Enot Store | 5.0–5.8 | yes | 337 Niagara spell FX. **Complete project: Fab offers Create Project only** (see below) | projectiles / impacts / casts per school |
| 6 | [ ] | **Shadow Magic Niagara** | Lord Enot Store | 5.0–5.8 | yes | 60 shadow FX. **Complete project: Create Project only** | shadow school |
| 7 | [ ] | **State VFX Niagara** | Lord Enot Store | 5.0–5.8 | yes | 15 looping state FX | buff / debuff / aura signatures |
| 8 | [ ] | **[49] Earth Spells Niagara** | UrtanoVFX | 5.0–5.8 | yes | 49 earth FX | earth school |
| 9 | [ ] | **Nature VFX with Vine Growth** | SoftTofuVFX | 5.2–5.8 | yes | vines, heal beam, area buff | nature school, heals, roots |
| 10 | [ ] | **Realistic Blood VFX** | Hivemind | 5.0–5.8 | yes | Niagara blood hits + decals | physical hit impacts |
| 11 | [ ] | **Ultimate Weapons Bundle** | Hivemind | 5.0–5.8 | yes | medieval weapon meshes | champion weapon props |
| 12 | [ ] | **Medieval Kingdom** | Hivemind | 5.3–5.8 | yes | 580 meshes, castle + town (large, 4K) | town + arena slots |
| 13 | [ ] | **Undead Pack** | Lilpupinduy | 5.0–5.7 | **no → Show all projects** | Lich, Ghoul, Skeleton, Zombie, Goblin | monster races (for the monster agents) |
| 14 | [ ] | **ROG Creatures Pack** | Atlant Games | 4.22–5.8 | yes | Wolf, Boar, Bear, Deer, Mammoth with AnimBPs + blendspaces | **high priority**: dire_wolf, grave_hound, bristleback, Bear champion, feral_ursoth, feral_mammoth |
| 15 | [ ] | **Quadruped Fantasy Creatures** (owned earlier, free pass) | PROTOFACTOR INC | 4.15–5.5 | **no → Show all projects** | Centaur (85 anims), Barghest, Mountain Dragon, Griffon | **high priority**: Evergrove Centaur, grave_hound, drakkari |

**Complete-project packs** (Big Pack Magic Effects, Shadow Magic): Fab only offers **Create Project**. Create them anywhere outside the repo (e.g. `Documents\Unreal Projects\`). The integration agent waits for the download to finish, then copies the pack's `Content/<PackFolder>` into the main checkout at the same `/Game/` path, so Eric doesn't need to migrate anything.

Animation packs come first because they're the biggest visible upgrade; the order is otherwise free. Large packs (GDH, Medieval Kingdom) take a while to download: let them finish before closing the editor.

## Storage: one local copy, junctioned into every worktree

- Packs live only in the main checkout, `CiresTeamSurvival/Content/<PackFolder>`.
- `Tools/LinkFabContent.py` (run with any Python 3; the UE one is `F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe`):
  - finds every untracked top-level `Content` folder in the main checkout, plus the `folders` in `Art/Fab/PurchasedPacks.json`;
  - adds them to the shared `.git/info/exclude`, which covers every worktree immediately;
  - creates NTFS junctions (`mklink /J`) at `<worktree>/Content/<PackFolder>` in each `cts-*` worktree.
  - It never copies files and never replaces a real folder. Safe to rerun after each new pack.
- A fresh worktree: `python Tools/LinkFabContent.py --worktree <path>` (add `--include-fab` to also junction `Content/Fab`, the importer output). `--check` only reports, and exits 1 if anything is unlinked or unignored.
- The committed `.gitignore` has a commented *Purchased Fab packs* block that lists each pack folder by name.
- Code and data only reference Fab assets by soft path and fall back to the Tripo/procedural art when they are missing, so a clean clone builds and passes every test with no Fab content.
