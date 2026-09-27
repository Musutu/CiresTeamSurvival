# RTS concepts (ChatGPT): resume point (2026-09-27)

## Done
- drowned_deep: all 12 L1 concepts (11 buildings + builder), approved. sheet.png made.
- obelisks: duskborn, darkborn approved. obelisks/sheet.png made.
- blightwood: 8 of 12 saved (main_hall, income, builder_shop, barracks, knight_school, arcane_well, beast_trainer, siege_workshop). Manifest status "saved-unreviewed"; build a contact sheet and review them.
- Missing for blightwood: archer_range, tower, temple, builder.
- Race claims in the manifest: drowned_deep, obelisks, blightwood and ironhide are conceptBy "chatgpt"; feral_kin is "tripo". Re-check the claims before each race.
- Style guides for all 9 races are in RTSBuildings.json (races.<race>.style). Per-slot names and prompts are in slots_<race>.json.
- Final forms (final_<slot>.png): none yet. They come after all the L1 concepts (final.js builds each queue with the L1 attached as reference).

## Exact next queue
1. q_bw4.json: blightwood archer_range, tower, temple, builder
2. q_ih.json: ironhide, all 12
3. Then voidborn, drakkari, fallen_order, aetheri and stoneborn, skipping any race the manifest marks conceptBy "tripo". Build each queue with `node batch.js <race> main_hall,income,builder_shop,barracks,archer_range,knight_school,arcane_well,beast_trainer,siege_workshop,tower,temple,builder > q_<x>.json`, then add its name to pending.json.
4. Final forms: `node final.js <race> > q_final_<race>.json` for each race.

## Tools: F:\CiresTeamSurvival-agents\rts-concepts\
- saver.js: local receiver on 127.0.0.1:8765. It writes POSTed images to Art/RTSBuildings/<race>/concepts/ and serves /file?n=<json> (queues from its own folder) and /img?p=<race>/concepts/<slot>.png (reference images).
- inject2.js: the CURRENT in-page runner; paste it into the ChatGPT tab. It opens a NEW chat per image, attaches the race's main_hall as a style reference, and reads the result through the conversation API rather than the DOM, because the DOM stalls on "Thinking" in background tabs. It uses a Web Worker timer, and its pump reads pending.json from the receiver. inject.js is the older DOM-based runner; don't use it.
- prompt.js / batch.js / final.js / obelisks.js: prompt and queue builders. They record name, role and prompt into the manifest via man.js, which re-reads the file before every write.
- common.json (framing), slots_<race>.json, race_<race>.json and styles.js (style guides), sheet.ps1 (contact sheets: `sheet.ps1 -Race <race>`).

## Restart
1. `node F:\CiresTeamSurvival-agents\rts-concepts\saver.js` (run it in the background).
2. In Chrome, open a new tab at https://chatgpt.com/ in Chat mode (not Work), and paste inject2.js with javascript_tool.
3. Click the red RX button. It needs a real user-gesture click, which opens the receiver popup; type into its textarea once to keep it alive. Check that `window.__rx && !window.__rx.closed` is true.
4. Put the queue names into rts-concepts/pending.json, run `window.__startPump()` and watch `window.__st` and `window.__log`.
5. Use only tabs you created: the Tripo agent shares the Chrome window.
