# Fab: Add to My Library, Add to Project, and downloads

Checked on 2026-09-24. Every item below costs 0 on every license tier. Listings that were free for Personal use but charged for Professional use were left out on purpose (for example Perfect Fire VFX, Zap VFX, Firewood, Mossy Rock Cluster, Forest Boulder, Animal Skull and the Megascans "Cobblestone" surfaces).

Source of truth: `Art/Fab/FabCatalog.json`. Licenses and credits: `Art/Fab/PROVENANCE.md`. Slot map for the town world: `Content/Data/TownAssetSlots.fab.json`.

## 0. Status (2026-09-24)

- **My Library: done.** All 36 catalog items were added to Eric's Fab library from fab.com; each was checked as price 0 on every license tier before clicking. Nothing was bought, and the items Eric had put in his cart were left untouched.
- **Website downloads: 1 of 19 done.** Castle Wall (cf0a6982) downloaded, imported and in use as the `low_wall` slot. After that first file, Chrome silently stopped starting any further fab.com download: trusted clicks, Fab's own Download button and the signed Epic CDN link all fail with no file and no error. This looks like Chrome's **automatic downloads** site permission for fab.com (after one download, Chrome blocks the rest until the user allows it). Changing that browser permission is Eric's call. **To unblock:** in Chrome, open the site settings for https://www.fab.com and set *Automatic downloads* to *Allow*, or click *Allow* on the blocked-download icon in the address bar. Then an agent can rerun the download loop.
- Barbarian Banners (Snit, CC-BY) is in the library but turned out to be hand-painted/stylized, so it is **rejected** for the realistic town.
- **Surfaces, still to do once downloaded:** `ImportFabAssets.py` builds UV-mapped `MI_*` instances. That is right for `plaster_material` (it has `meshSlot: Plaster`, so it swaps onto the town kit meshes). But `cobblestone_material` and `castle_material` are applied to scaled world cubes and must be world-aligned (see Docs/EnvironmentProps.md). Before flipping those two to `imported`, parent them to the town's world-triplanar `M_TownWorld` instead of `M_FabPBR`.
- **In-editor route:** since f977dae the Fab plugin is enabled in the .uproject (Smart App Control no longer blocks it). **Window → Fab** inside the editor can also Add to Project every item below, and it downloads Megascans at chosen quality straight into the project.

## 1. Epic Games Launcher: Add to Project (UE-format packs)

Target project: `C:\Users\Eric\Documents\GitHub\cts-fab-assets\CiresTeamSurvival.uproject`

In the Launcher go to **Unreal Engine → Library → Fab Library**, search for the exact name, then choose **Add to Project → CiresTeamSurvival**. If the project isn't listed because the pack doesn't declare 5.8, tick **Show all projects** and pick the newest listed engine version. Content-only packs from 5.4 to 5.7 normally load in 5.8, but check them once they open. Alternatively use **Window → Fab** in the editor (plugin enabled since f977dae).

| Done | Exact Fab name | Author | Declared UE versions | Declares 5.8 | Use in town | Link |
|---|---|---|---|---|---|---|
| [ ] | **Medieval Banquet** | Quixel Megascans | UE 5.4-5.6 | no, pick newest | market_goods, table | [listing](https://www.fab.com/listings/d422ac6c-ce50-4273-8d4a-2ac594a3a2b2) |
| [ ] | **Medieval Modular Wall F01 - Plaster & Wood \| Nanite Ready** | Massive Realities | UE 5.7 | no, pick newest | house_a, house_b | [listing](https://www.fab.com/listings/bb63d427-0098-4bbb-a262-41d628b5c6a5) |
| [ ] | **Medieval Modular Wall F02 - Plaster & Wood \| Nanite Ready** | Massive Realities | UE 5.7 | no, pick newest | house_c | [listing](https://www.fab.com/listings/92e9b5fe-7893-44da-aec3-5e96c688f0b7) |
| [ ] | **Medieval Modular Roof Front 01 - Plaster & Wood \| Nanite Ready** | Massive Realities | UE 5.7 | no, pick newest | house_roof | [listing](https://www.fab.com/listings/1c718658-3c00-4dc5-809a-5da60b7313cf) |
| [ ] | **Advanced Village Pack** | Advanced Asset Packs | UE 4.8-5.4 | no, pick newest | house_a, house_b, house_c, market_stall_a | [listing](https://www.fab.com/listings/250a5622-d5fd-49fc-b1c4-0550a444e117) |
| [ ] | **Medieval Dungeon** | Infuse Studio | UE 4.21-5.4 | no, pick newest | lamp, castle_gate, torch | [listing](https://www.fab.com/listings/c13bd0dc-ac4d-4595-b284-f81386b2e6ef) |
| [ ] | **Megaplants: Dead Pine** | Quixel Megaplants | UE 5.8 | yes | tree_dead | [listing](https://www.fab.com/listings/bc9e4e89-fcf4-4be6-b0f0-8b37395a72de) |
| [ ] | **M5 VFX Vol2. Fire and Flames(Niagara)** | M5VFX | UE 4.15-5.7 | no, pick newest | vfx_fire, vfx_torch_flame | [listing](https://www.fab.com/listings/c5b0270a-a295-4644-a4be-42cb1e56a197) |
| [ ] | **FX Variety Pack** | Kakky | UE 4.20-5.8 | yes | vfx_spell_cast, vfx_spell_impact | [listing](https://www.fab.com/listings/53531e17-369f-4bba-b493-8588f0dec07b) |
| [ ] | **Monster for Survival Game PBR Polyart** | Dungeon Mason | UE 4.19-5.8 | yes | creature_ghoul | [listing](https://www.fab.com/listings/9fdd1645-600d-460f-be4b-0b6421b7cf38) |
| [ ] | **Dragon for Boss Monster : PBR** | Dungeon Mason | UE 4.17-5.8 | yes | creature_boss_dragon | [listing](https://www.fab.com/listings/6c4bd321-0263-44fe-a3d9-82599eeee12c) |
| [ ] | **Skeleton Necromancer** | Feyloom | UE 5.5-5.6 | no, pick newest | creature_necromancer | [listing](https://www.fab.com/listings/8e5c5f8f-993a-4c76-b53c-b48bac9766ee) |
| [ ] | **Monster** | Khornes | UE 4.26-5.8 | yes | creature_brute | [listing](https://www.fab.com/listings/6fc1668f-c5d9-4146-8da1-ee3d7f958026) |
| [ ] | **Quadruped Fantasy Creatures** | PROTOFACTOR INC | UE 4.15-5.5 | no, pick newest | creature_beast | [listing](https://www.fab.com/listings/52d686b6-1180-4f26-901f-ce3c69a14767) |

## 2. Complete sample projects: Create Project, then Migrate

These two are full projects, so the Launcher offers **Create Project** instead of Add to Project, and only for the engine versions they declare. Create each in a scratch folder **outside** this repo. Open it in that engine version, select the meshes/materials you want, then use **Asset Actions → Migrate…** into `C:\Users\Eric\Documents\GitHub\cts-fab-assets\Content`. Migrate only what the town needs (houses, market stalls, lamps, ruins), not the whole map.

| Done | Exact Fab name | Declared UE versions | Needs engine installed | Use in town | Link |
|---|---|---|---|---|---|
| [ ] | **Medieval Village Megascans Sample** | UE 4.26-4.27, 5.3 | UE 5.3 | house_a, house_b, house_c, market_stall_a, market_stall_b, lamp | [listing](https://www.fab.com/listings/2e11a225-a6ea-4781-a3e1-fe975b461894) |
| [ ] | **Dark Ruins Megascans Sample** | UE 5.5, 5.6 | UE 5.6 (or 5.5) | castle_keep, gatehouse, watchtower | [listing](https://www.fab.com/listings/836ed2f8-e2d6-49be-98d3-59d104bd351e) |

## 3. Website downloads (FBX / texture sets): the importer handles these

After signing in, open each listing, click **Add to My Library**, then **Download**, and choose **FBX** (3D) or the texture set (surfaces). **2K** is the budget in `Docs/AssetPipeline.md`. Save the ZIP into the folder shown; `Art/Downloads/` is gitignored. Then run:

```
F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/ImportFabAssets.py --plan   # shows which folders are present or missing
F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/ImportFabAssets.py          # imports every present download into Content/Fab/<Category>/<Asset>/
```

The importer extracts only mesh/texture file types (it never runs anything), imports inside the isolated `Tools/ContentBuilder` project, creates `MI_<Asset>` from the shared `M_FabPBR` master, enables Nanite above 20k triangles, adds box collision when a mesh has none, re-imports at 100× when a mesh looks authored in metres, copies the result to `Content/Fab`, and flips the matching slots in `TownAssetSlots.fab.json` to `imported`. `--selftest` runs the same pipeline on repo-owned sample sources in staging only.

| Done | Exact Fab name | Download into | Slots | Link |
|---|---|---|---|---|
| [ ] | **Castle Wall** | `Art/Downloads/Fab/Architecture/CastleWallA/` | low_wall | [listing](https://www.fab.com/listings/cf0a6982-f96f-4aef-97e6-7dc24b3a639c) |
| [ ] | **Castle Wall (variant)** | `Art/Downloads/Fab/Architecture/CastleWallB/` | low_wall | [listing](https://www.fab.com/listings/b8cfda6a-6514-4242-820d-fdeced558ce3) |
| [ ] | **Castle Stairs** | `Art/Downloads/Fab/Architecture/CastleStairs/` | castle_stairs | [listing](https://www.fab.com/listings/bb6f516f-48b5-4343-8884-1c300330ff95) |
| [ ] | **Ancient Temple Stones** | `Art/Downloads/Fab/Architecture/AncientTempleStones/` | ruin_stones | [listing](https://www.fab.com/listings/1a117f26-677e-4ad9-ab4b-f2177b6cda9c) |
| [ ] | **Roman Stone Floor** | `Art/Downloads/Fab/Architecture/RomanStoneFloor/` | square_paving | [listing](https://www.fab.com/listings/e195504c-3230-4505-ba2a-71d89d33c085) |
| [ ] | **Old Stone Well** | `Art/Downloads/Fab/Props/OldStoneWell/` | well | [listing](https://www.fab.com/listings/41c8f2a3-71c0-4a42-922c-db4ec1c429a1) |
| [ ] | **Wooden Barrel** | `Art/Downloads/Fab/Props/WoodenBarrel/` | barrel | [listing](https://www.fab.com/listings/a9d0d237-ef5b-47f3-b153-f6b4ab9733f1) |
| [ ] | **Barrel On Stand** | `Art/Downloads/Fab/Props/BarrelOnStand/` | barrel_wine | [listing](https://www.fab.com/listings/3655c9b4-706d-4189-a821-b35ee3a6c632) |
| [ ] | **Wooden Crate** | `Art/Downloads/Fab/Props/WoodenCrate/` | crate | [listing](https://www.fab.com/listings/ae4d047d-f585-413c-9440-69e1af3935a5) |
| [ ] | **Tarped Crate** | `Art/Downloads/Fab/Props/TarpedCrate/` | crate_long | [listing](https://www.fab.com/listings/8aa1f000-e6b2-48dc-b33d-e233fdf59956) |
| [ ] | **Wooden Wheelbarrow** | `Art/Downloads/Fab/Props/WoodenWheelbarrow/` | wheelbarrow | [listing](https://www.fab.com/listings/8f945fbe-00e4-4fc3-b363-81cdc4ad1653) |
| [ ] | **Wooden Wheel** | `Art/Downloads/Fab/Props/WoodenWheel/` | cart_wheel | [listing](https://www.fab.com/listings/9ce45851-50b2-49a0-8cf8-d527e41aa2b4) |
| [ ] | **Military Trenches Wall Wood Low 04** | `Art/Downloads/Fab/Props/WoodBarricadeLow/` | barricade | [listing](https://www.fab.com/listings/3bd57e7e-feab-46b3-a67f-18287199159c) |
| [ ] | **Wooden Beam** | `Art/Downloads/Fab/Props/WoodenBeam/` | beam | [listing](https://www.fab.com/listings/6b7559aa-79c4-4e8d-931a-6be7ab22c1a9) |
| [ ] | **Decorative Statuettes Pack** | `Art/Downloads/Fab/Props/DecorativeStatuettes/` | statuette | [listing](https://www.fab.com/listings/e581059c-6146-4d34-83b2-e963d41dc6f0) |
| [ ] | **Mossy Cobblestone Path** | `Art/Downloads/Fab/Surfaces/MossyCobblestonePath/` | cobblestone_material | [listing](https://www.fab.com/listings/cae0b8c6-aead-486f-9da2-896dd8eb8fd5) |
| [ ] | **Damaged Wall Plaster** | `Art/Downloads/Fab/Surfaces/DamagedWallPlaster/` | plaster_material | [listing](https://www.fab.com/listings/c16498bb-264a-4376-8b39-401a3a7b4bd1) |
| [ ] | **Fresh Wall Plaster** | `Art/Downloads/Fab/Surfaces/FreshWallPlaster/` | plaster_clean_material | [listing](https://www.fab.com/listings/b427a11b-5d79-41bc-af42-3cd3aadae25c) |
| [ ] | **Castle Wall (surface)** | `Art/Downloads/Fab/Surfaces/CastleWallSurface/` | castle_material | [listing](https://www.fab.com/listings/c7bacf7e-8073-4f4f-aaf9-ef9fa357c11c) |

## 4. Items Eric added himself

| Item | Seller | License owned | Formats | Declared UE | Verdict |
|---|---|---|---|---|---|
| **Paladin RPG Set** ([listing](https://www.fab.com/listings/59fb644c-2d7f-401f-ab48-4cf02dce7392)) | Polyphoria | Personal | MetaHuman + Unreal Engine (no FBX/GLB) | 5.6, 5.7, 5.8 | Good fit for the Paladin (Holy/Righteous) champions. UE/MetaHuman only, so add it through the in-editor Fab window or the Launcher. `C:/Users/Eric/Downloads/mhc_pala_5_8.mhpkg` (431 MB, 08:05 today) looks like this pack's MetaHuman Creator package. See notes below. |
| **Asian Canal Environment** ([listing](https://www.fab.com/listings/2238bb32-b440-48fe-85c1-9ee02f5ccdcd)) | Leartes Studios | Personal | Unreal Engine | 4.24 to 5.8 | East Asian canal-town kit and showcase map (category *Historical*). Its architecture clashes with a European medieval town, so it is **not** used in the town. It could suit a themed PvP arena (arenas agent). |

**Paladin integration notes.** The vendor says the armour is a stripped-down version of *Modular Heavy Armour*: skeletal mesh on the **UE5 (Manny) skeleton**, supplied as a parametric clothing component for **MetaHuman Creator**. To use it:
1. Enable the MetaHuman Creator plugin with *MetaHuman Creator Core Data* installed.
2. Add the pack through Window → Fab, or import the `.mhpkg`.
3. Build a MetaHuman wearing the Paladin outfit and export/assemble it.

Because it is on the UE5 skeleton, the existing Manny locomotion and attack clips should drive it directly, or through a trivial Manny→Manny retarget. That avoids the per-character IK-retarget pipeline `Tools/RetargetTripo.py` needs for the 61-bone Tripo skeletons. A MetaHuman is far heavier than the prototype bodies (grooms, face rig, several LODs), so budget and profile it before binding it in `ChampionArtBindings.json`. This needs Eric in the editor: the MetaHuman Creator steps are interactive.

## 5. After adding packs

Launcher packs land in their own top-level Content folder, named by the pack. Record the real mesh paths in `Content/Data/TownAssetSlots.fab.json` (set `mesh` and `status: "imported"`) so the town-world agent picks them up. Slots stay on prototype art until their status is `imported`.
