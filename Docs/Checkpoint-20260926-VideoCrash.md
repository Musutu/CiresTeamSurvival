# Checkpoint 2026-09-26: video settings crash, save/relaunch, champion-select look

Branch `fix/video-crash`. Eric's report while playing `PlayTripoPreview.cmd`: changing video settings crashed the
game, "save and reload" never reloaded, and champion select was too bright and "glitchy" for the 3D model. His three
crash reports (`Saved/Crashes/UECC-...-3FDECC3D..._0000/_0001/_0002`) were reproduced in the real windowed game.

## Root causes and fixes

| Symptom | Root cause | Fix |
| --- | --- | --- |
| Fatal crash ~1 s after applying Cinematic + 2560x1440 (`_0002`: access violation in mimalloc realloc under `FCanvas::PushAbsoluteTransform` <- `UGameViewportClient::Draw`) | Options applied the resolution / window mode from a HUD button, i.e. inside `UGameViewportClient::Draw`. The resize makes `FSceneViewport` create a new debug canvas and free the old one, but `Draw` still holds the old pointer and pushes a transform onto it right after the HUD returns. | `CireVideo` (`CireVideoSettings.h/.cpp`): the UI only requests a change; it is applied on the core ticker at the start of the next engine frame. `ACireHUD::DrawHUD` holds a `CireVideo::FDrawScope`, so an apply attempted during a draw is refused and deferred (logged `CIRE_VIDEO_APPLY_DURING_DRAW`). |
| Second fatal crash found by the new cycle: D3D12 `CloseCommandList` `E_INVALIDARG` after the window grew | The champion preview's exposure meter reused `FRHIGPUTextureReadback` staging textures sized for the old preview target (the engine assumes a fixed size) and read back past the mapped buffer. | `CireDraftStage.cpp`: staging textures are recreated when the target size changes; the CPU read uses the copied size; a copy taken mid-resize is retried. |
| Distance-field ensures `_0000/_0001` ("precision loss converting matrix to GPU format", then "InverseFast non-invertible matrix" every frame) | Not a scale-0 instance: the Iron Warden / Relic Paladin heater shield `/Game/FabDerived/Props/Polyphoria/SM_wp_shield_tri_01_a` loaded **corrupt cached render data** (`FStaticMeshRenderData::Serialize found NaN in Bounds`). The GPU scene takes instance bounds from render data, so the DF scene got a NaN world position every frame the shield moved. `Tools/BuildFabPaladins.py derive_shield()` meant to force a rebuild by changing LOD build settings, but `EditorStaticMeshLibrary.set_lod_build_settings` is a silent no-op in commandlets, so the copy kept the pack's corrupt derived-data key. The corrupt geometry also drew the shield as a large blown-out smear. | `derive_shield()` now rebuilds from a freshly committed mesh description (new DDC key) and fails loudly if settings do not apply; the shield in the shared `Content/FabDerived` was re-derived (no NaN warning since). Runtime guard: `CireRenderSanity::RepairLoadedMeshes` (0.25 s core ticker in game processes) restores NaN render bounds from the asset bounds and re-creates affected components; `CireRenderSanity::Scan` reports NaN / zero-scale / out-of-float-range primitives and instances. |
| "It never reloaded after I said save and reload" | **Save & Close** called `RevertVideoPreview()`, silently undoing the previewed change. Separately, `Play.cmd` / `PlayTripoPreview.cmd` always passed `-windowed -ResX=1600 -ResY=900`, overriding any saved resolution / window mode at the next launch. | Nothing needs a restart: resolution, window mode, preset, render scale, VSync and frame cap apply live. Save & Close now keeps a preview; Keep saves `GameUserSettings.ini`. The launchers no longer force a resolution; first-launch default is 1600x900 windowed (`Config/DefaultGameUserSettings.ini`). New render-scale (screen percentage) slider. Tested for real by relaunching the game (`RunVideoCycle.py --persist`). |
| Champion select too bright in the real game | The preview metered its exposure while meshes were still building, shaders compiling, PSOs precaching and textures streaming (Eric's log: first pass median 26 -> +1.28 stops), and cached that for the session; the corrupt shield's white smear dominated the frame. | Metering waits for settled content (`ACireDraftStage::IsContentSettled`), is keyed per scalability level (re-meters on a preset change while the figure stays on screen), targets a slightly darker figure (median 110 / p97 232 / floor 84, was 122/238/92) and lets >1.2 % clipped pixels pull the mid-tones half a stop lower. |
| "Glitchy" rendering of the model | The world-scale merge added `r.Tonemapper.Sharpen=0.6` as a global cvar, which overrides per-view post-process, so it also sharpened the preview capture (rendered without TAA): sparkling, crunchy specular on armour and skin. The unbound town grade also leaked its 6900 K white balance into the capture. | Sharpen moved into the town and arena post-process grades (gameplay unchanged); the capture pins Sharpen 0 and neutral white balance / gains. |

## Regression checks

* `Tools/RunVideoCycle.py` (`-CireVideoCycle`, `CireVideoCycle.cpp`): real windowed game (no `-RenderOffscreen`),
  Options > Video open, applies Low / Medium / High / Epic / Cinematic at 1600x900 and 1920x1080, render scale 50 and 75,
  2560x1440 + Cinematic at once (Eric's crash), borderless, back to windowed: on champion select, then again in the match.
  Checks every apply landed and the viewport resized, saves a screenshot and the raw champion render per step, scans all
  primitives, proves an apply inside the draw is deferred, and fails on any ensure, crash, NaN-bounds warning or bad
  primitive. `--fullscreen` adds exclusive fullscreen.
* `Tools/RunVideoCycle.py --persist`: keep settings, quit, relaunch like `Play.cmd`, verify.

## Captures (windowed, this worktree)

* Before: `Saved/VideoCycle/20260926T030054Z_first/` (knight blown out, shield smear; the run then hit the D3D12 crash at
  High 1920x1080).
* After: `Saved/VideoCycle/20260926T041340Z_fourth/` (`draft_q<level>_<name>_<res>.png` + `.figure.png`, `match_*.png`).

## Not changed (noted)

* The knight's sword shows mostly its hilt in the preview; not investigated here (the mesh and its render data are
  clean, so it is most likely the grip pose in the champion-art weapon data).
* Low preset: plate reads darker than at Epic (no screen-space reflections, the capture has no sky light by design).

## Verification after the F: move (2026-09-26, rebuilt from this branch)

Build `CiresTeamSurvivalEditor Win64 Development`: Result: Succeeded (bUseUnity=false).

* `RunVideoCycle.py`: PASS, 108/108 checks, 23 shots, no engine failures (draft + match phases),
  `Saved/VideoCycle/20260926T064652992408Z_rebuilt2/`. A first attempt ended early with `ViewportClosed`
  (the window was closed externally mid-transition into the match; no ensure/crash, 71/71 checks up to then).
* `RunVideoCycle.py --persist`: PASS (1280x720 mode=2 quality=2 scale=80 fps=90 survived the relaunch).
* `RunExpansionChecks.py --timeout 240`: CIRE_COMBAT_EXPANSION_PASS, all 4 stages passed.
* `RunNetworkSmoke.py --startup-timeout 120 --probe-timeout 90`: CIRE_NET_SERVER_PASS / CIRE_NETWORK_SMOKE_PASS.
* `RunInterfaceSmoke.py --startup-timeout 120 --probe-timeout 120`: CIRE_INTERFACE_SMOKE_PASS on the second run.
  The first run hit the known arena-validation stall (~775 ms hitch after the chat stage) that
  `fix/interface-stall` fixes; nothing in this branch touches it.
