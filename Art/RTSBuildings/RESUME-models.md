# RTS models: resume point (Tripo modeler, stopped 2026-09-27 at Eric's usage limit)

## Credits
- Tripo balance 12,741 (start 14,486): **1,745 spent** of the 13,500 cap, 11,755 left before the cap.
- Every generation is logged per slot in RTSBuildings.json (`model.attempts`) and in `tripo.balanceLog`.

## Done: exported GLB + Blender turnaround + QA pass
- drowned_deep: all 11 buildings (`drowned_deep/models/*.glb`, `qa/*_turnaround.png`).
- feral_kin: main_hall, income, builder_shop, barracks, archer_range, knight_school.

## Generated in Tripo, paid for, NOT exported yet (export is free)
| race | slot | Tripo task id |
|---|---|---|
| drowned_deep | builder (8K) | 7f43c6da-d483-4f88-8b85-4a3c432a7ba2 |
| feral_kin | builder (8K) | bc95543b-b5a9-403d-b59e-3ffc6b878d98 |
| obelisks | duskborn (8K, 150k tris) | 0577c95b-cdf1-4783-8f13-cc745e943954 |
| obelisks | darkborn (8K, 150k tris) | 2f99eac6-c137-4b60-87de-26ceb468c2f7 |
| feral_kin | arcane_well | 8dd1ed0c-2b51-43a4-8358-acdfbf8ba9e0 |
| feral_kin | beast_trainer | 117cf76b-1627-4a3b-ab9b-45da080bdf42 |
| feral_kin | siege_workshop | 99c275da-dc85-42c9-ab1b-9156e39ec5b2 |
| feral_kin | tower | d49530e8-7452-4ee8-a042-bcfe38647b7c |
| feral_kin | temple | 43e3a993-bcc8-4126-9495-6df62fd275c0 |
| blightwood | main_hall | 938c65ba-c548-4f39-bbbe-616c632e0cb3 |
| blightwood | income | 9a27647a-33da-49db-99ae-4bcfddc08eb2 |
| blightwood | builder_shop | 95df3815-9d63-4047-a059-9f6e6aa2e2e2 |
| blightwood | barracks | 037f6a72-5049-4acb-9cde-a4ea4cff143e |
| blightwood | knight_school | 3de5734b-6b25-425d-b358-ab27e3c12a41 |

URL: https://studio.tripo3d.ai/workspace/generate/<task id>

## Next queue (in order)
1. Export and QA the 14 above (for each: open the URL, click Export twice (the first click is swallowed), name the file `rtsm_<race>_<slot>`, click the yellow Export, then run `qa.sh <race> <slot>`, Read the turnaround, then `qalog.sh`).
2. Builders (drowned_deep, feral_kin): turnaround + hand/face check, then Rig > Humanoid, UE5 Mannequin preset (20 cr), re-check, export the rigged GLB as `<slot>_rigged.glb`.
3. Generate blightwood arcane_well, beast_trainer, siege_workshop (concepts exist), then the rest of blightwood as the concept agent lands them.
4. Tripo concepts continue backwards: stoneborn, then aetheri, then onward (feral_kin is done). Stop at any race the concept agent has claimed.
5. Commit per race; contact sheets at the end.

## Settings
- Buildings: HD Model H3.1 Best, Ultra Mesh, 4K PBR, remove lighting, triangle 50k = 55 cr.
- Builders: the same with 8K = 65 cr.
- Obelisks: 8K + 150k tris = 65 cr.
- The Geometry & Texture panel remembers the last settings; it is currently 4K / 50k.

## Concept claims
- feral_kin: `conceptBy: "tripo"`, all 12 concepts done (Tripo Image, GPT Image 2.5, free quota; main_hall used as style reference).
- No other race is claimed by Tripo. The concept agent owns drowned_deep, blightwood, ironhide and obelisks.

## Tools (F:\CiresTeamSurvival-agents\rts-modeler\)
- `manifest.js`: safe JSON patch.
- `logm.sh`: log a generation (obelisks go under `entries`).
- `qalog.sh`: log QA.
- `take.sh`: move an exported `rtsm_` GLB from Downloads to F:.
- `qa.sh`: take + Blender turnaround via `turn_glb.py`.
- `takeimg.sh`: move a Tripo concept image.
- `tprompt.js` / `short.js`: concept prompts.
- `todo.sh`: concepts without a model.
- `pending.sh`: generated but not exported.
- Concept upload staging: scratchpad `up/` folder (C: temp, per Eric's rule).
