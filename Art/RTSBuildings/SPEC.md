# RTS buildings + builders: asset collection spec (Eric, 2026-09-26)

Goal: collect concept images (ChatGPT) and 3D models (Tripo) for a future Warcraft 3 / Command & Conquer style build mode. This is collection only; gameplay integration comes later.

## Scope: 9 races x (11 buildings + 1 builder) = 108 models
Races (the 9 monster races; identity, palette and silhouettes from Art/TripoRaces.json and the existing race art): drowned_deep, blightwood, ironhide, voidborn, drakkari, fallen_order, aetheri, stoneborn, feral_kin.

Per race, 11 buildings, each named in race flavour (e.g. drowned_deep "Tidecaller Spire"):
1. Main Hall / Town Hall (largest; the race's seat)
2. Income structure: increases income (mine, farm, tithe, trade post...)
3. Builder Item structure: sells builder items/skills (workshop / artificer)
4. Barracks: melee infantry
5. Archer Range: ranged units
6. Knight School: elite/heavy units
7. Arcane Well: magic, casters, mana
8. Beast Trainer: creatures/mounts
9. Siege Workshop: siege engines (the 9th "various" structure; Eric can swap it)
10. Tower: defensive tower
11. Temple: trains healers/support units (added by Eric)

Builder, one per race: a worker unit in the same style as that race's existing generated monsters. Builders are immune to damage and hold 6 skills bought at the Builder Item structure, so give them a readable tool/worker silhouette (hammer, satchel, construction gear).

## Game context (Eric): an offshoot RTS set in the Cire's Team Survival world
Team-based build strategy. Each building auto-produces units that are CTS heroes or monsters of that race, each with a skill. The units auto-battle through the middle, heading to destroy the enemy team's obelisk. So the buildings must clearly read as unit-producing structures of that CTS race (the units coming out of them are the race's existing monsters/heroes).

## Style
- Match the game's current generated assets: "vibrant and fun looking, and crisp"; stylised Warcraft 3 readability; chunky, readable silhouettes from a top-down RTS camera.
- Buildings are placeable props, not enterable full-scale architecture. A solid, walkable-around footprint on a base/plinth, one object, no ground plane or scenery, centred.
- Within a race, all 11 share one palette, materials and motifs; across races they are unmistakably different.
- Concept image for Tripo: a single object, 3/4 elevated view, plain neutral background, no text, no people (buildings); builders in a front-facing A-pose, full body, plain background.

## Files
- Concepts (ChatGPT): Art/RTSBuildings/<race>/concepts/<slot>.png (slot = main_hall, income, builder_shop, barracks, archer_range, knight_school, arcane_well, beast_trainer, siege_workshop, tower, temple, builder).
- Models (Tripo): Art/RTSBuildings/<race>/models/<slot>.glb (+ builder rigged .fbx/.glb), plus a 4-view turnaround PNG per model in Art/RTSBuildings/<race>/qa/.
- Manifest: Art/RTSBuildings/RTSBuildings.json, one entry per race/slot:
  - name, role, prompt, concept path, Tripo task/model id, credits spent, attempts, QA status, defects.
  - The concept agent writes concept fields; the Tripo agent writes model fields. Edit only your own fields, re-reading the file right before each write.
- Contact sheets: Art/RTSBuildings/<race>/sheet.png (all 12 for that race), made at the end.

## Reference: Castle Fight (WC3 custom map)
Builders place spawner buildings on a grid near their base. Each building auto-spawns its unit on a timer, units march down the middle uncontrolled, and the goal is the enemy castle (our obelisk). So: compact square footprints, readable from a top-down camera, a distinct silhouette per role, and a spawn gate facing front.

## Obelisks (Eric): 2 team objectives, not per race
- DUSKBORN Obelisk and DARKBORN Obelisk: the objective each team defends and the enemy destroys. Two distinct, imposing monuments (the team identities, independent of race), clearly rivals at a glance. For example, Duskborn is twilight amber/violet and Darkborn is deep black/crimson-void; the concept agent decides.
- Files: Art/RTSBuildings/obelisks/concepts/{duskborn,darkborn}.png, models/{duskborn,darkborn}.glb, qa/. Manifest key "obelisks". Total is now 110 models.

## Building upgrades (Eric): 4 levels per building, 2 models
- L1 = base model. L2 and L3 = the SAME base mesh with upgraded textures (for example rough wood/hide, then reinforced stone/iron banding, then gilded/rune-etched premium materials in race palette). The progression must read at a glance from the RTS camera.
- L4 = FINAL FORM: a new, grander model of the same building, clearly its evolved version (same silhouette DNA, taller, more ornate, more race identity).
- Applies to all 11 buildings x 9 races (99 final forms). Builders and obelisks do not upgrade.
- Concept agent: final_<slot>.png per race. Generate from the L1 concept as a reference for continuity. Optionally add small texture-tier reference swatches.
- Tripo agent: models/<slot>_final.glb. Texture tiers are models/<slot>_L2.glb and <slot>_L3.glb (same geometry): use Tripo texture-only regeneration if the budget allows, otherwise a headless Blender material/texture pass on the base mesh.
- PRIORITY for the credits: every L1 building, builder and obelisk first, then the final forms, then the Tripo texture tiers. Total: 110 base + 99 final = 209 meshes.
