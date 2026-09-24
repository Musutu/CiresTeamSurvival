# Combat feedback

Successful damage and healing now feed two independent displays, both enabled by default:

- Floating numbers rise above the affected unit at the recorded impact position. A brief size pulse, dark outline, and screen-space separation make rapid and area hits easier to read.
- Personal scrolling combat text displays incoming events on the left and outgoing events on the right of the movable `CombatText` area. Entries include the amount, ability, and source or target name. Off-screen impacts still appear here.

Outgoing damage is gold, incoming damage is red with a minus sign, and healing is green with a plus sign. Both views show effective health removed or restored after mitigation and health limits. Overkill, overhealing, passive regeneration, rejected attacks, and zero damage do not inflate the numbers or the meters. A self-heal appears once in the incoming lane.

Floating numbers last 2.1 seconds; personal scrolling entries last 3.2 seconds. Both fade near the end. Simultaneous hits from the same ability (within 0.12 seconds) combine in personal SCT, showing their total effective amount and affected target count; each target keeps its individual floating number. Dense bursts have bounded visible rows; the optional combat log and cumulative damage/healing meters remain available. Numbers are cosmetic client feedback and do not drive combat results.

Open **F9** for separate floating/SCT toggles and font sizes, plus shared damage/healing and incoming/outgoing filters. **F10** moves, resizes, and locks the personal `CombatText` panel. Preferences save locally. Existing profiles migrate to both displays while preserving an explicitly disabled master switch.

## Delivery and identity

Basic attacks now also emit MISS and DODGE outcomes. These carry zero damage without incrementing meters. Floating text, personal scrolling text and the optional combat log display the outcome word instead of a numeric zero. Avoidance entries are not combined with damage bursts. Poison-area membership counts are displayed separately in unit frames.

The authoritative server records effective amounts, names, stable actor identities, and the target's capsule-top impact position before destruction. It sets personal source/target flags separately for each recipient. Actor pointers are removed from the network event, so a killing blow does not wait for a destroyed or never-replicated actor to resolve. A monotonically assigned receive sequence stays stable when old buffer entries are removed.

PvE events stay within the observing hero's team; opposing engagements remain private outside arena PvP. The bounded event buffer retains recent personal hits preferentially over teammate log traffic. The existing cosmetic RPC is unreliable: authoritative damage/healing totals replicate separately, but packet-loss delivery of every individual visual event is not guaranteed.

## Verification

The telemetry fixture checks actual applied amounts, per-recipient identity, killing blows, impact positions, buffer limits, and privacy. The two-client interface fixture includes a server-spawned monster killed before it can replicate; only its attacking team should receive the hit snapshot. The dev-only `-CireFeedbackPreview` fixture produces real single-target, area, incoming, healing, and lethal events and captures rendered screenshots. See `Validation.md` for the latest executed results; fixture existence alone is not evidence of a pass.

## WoW-style combat text (September 24)

Text is drawn with the kit's TTF fonts at pixel size (outlined numbers). Outgoing damage is coloured by
school when **Colour by spell school** is on (derived from the ability name: gold physical, orange fire,
pale blue frost, green poison/nature, purple shadow, yellow holy, blue storm, pink arcane); incoming
damage stays red, healing green, avoidance grey ("Miss"/"Dodge"). Criticals "pop" (start ~2.2x and
settle at 1.3x) when **Critical pop** is on, with a rotating starburst in the scrolling lanes. Options:
misses on/off, AoE merging on/off, scroll direction up / down / fountain, speed .5-2 and display time
1.5-5 s (floating numbers last two thirds of it). A small school-coloured pip precedes each lane's
ability line.

## Threat and aggro feedback

The threat meter (panel `Threat`), target-frame threat badge, boss frames, nameplate glows and alerts read
the replicated `UCireNPCState` threat table (`Docs/NPCs.md`), so they work on LAN clients.
Damage dealers/healers: a red "AGGRO!" alert with screen-edge pulse and alarm when an enemy turns on
them, an orange "THREAT n%" warning when their pull progress (110% melee / 130% ranged rule) passes the
threshold, red nameplate glow + diamond while an enemy targets them, orange glow when close to pulling.
Tanks: "LOST AGGRO" with the new victim and a knock sound, orange nameplate glow for engaged enemies not
on them, "TAUNTED"/"AGGRO GAINED" confirmations for elites/bosses. Player and party frames glow red with
an "AGGRO xN" count while enemies attack that member.
