# Monster tiers and zones

Eric (2026-09-26):

> "The monster packs can have their coloring removed that designated their strengths, I would rather they didn't glow.
> If anything put a T# next to the name, or a border around the unit that signifies that."
>
> "When entering a new area, add 'Monster Tier: X', whatever that district or area is most containing."

Code: `Source/CiresTeamSurvival/CireZones.{h,cpp}`. Data: `Content/Data/TownZones.json` and the layout's Zone markers.

## Pack tiers read in the UI, not on the body

- **No glow.** Monster bodies no longer wear their rank colour. That covers the armour tint, the body tint, the emissive
  glow, the fresnel rim and the elite / pack-leader rim overlay. The race palette and the reskins stay, so one body still
  reads as different races.
  - The switch is the `cire.RankBodyColours` cvar. `1` brings the old look back.
  - Kept: the enraged rim (a combat telegraph), the Rare Spawn and Bonus Loot colours, and the lane boss rim.
- **T# next to the name.** A jungle-pack monster shows `T1`..`T4` after its name, in its tier colour. The same tag and
  colour appear everywhere the unit shows:
  - the nameplate (with a tier-coloured border around the bar; the aggro glow goes outside it)
  - the target and focus frames (the border, the caption `T3 PACK / TANK` or `T3 PACK LEADER / TANK`, the portrait ring
    and badge)
  - the boss frame row of a pack leader
  - the unit tooltip (title, tag and accent)

| Tier | Colour | Extra |
| --- | --- | --- |
| T1 | silver | |
| T2 | green | |
| T3 | blue | elite dragon on the portrait, marker on the plate |
| T4 | gold | elite dragon on the portrait, marker on the plate |

- Pack leaders keep the boss frame (skull badge, boss-frame row).
- Wave monsters with a rank (Veteran, Elite, Champion...) keep their rank colour in the UI. Only their bodies stopped glowing.

## Zones

A zone is a **named polygon** in realm-local centimetres, shared by both realms unless it has a team owner.

When your champion enters a zone and stays in it for 0.8 s, you get the WoW zone text:

```
ENTERING
Market Plaza
Monster Tier: 1
```

- **Monster Tier** is the most common tier among the challenge packs whose centre lies inside the zone. Ties go to the
  higher tier. A zone without packs reads "No monster camps".
- The tier is read from the live packs of your realm, so it is right on LAN clients too.
- It is local to each player and works in both realms. There is no zone text in the arena.
- It never spams: it shows only when the zone really changes, at most one zone banner every 4 s.

### Where zones come from

1. **Zone markers in the layout.** The map layout editor has a **Zone / Area** setter (Shift+3, bar 2 slot 3). Each
   press chains a corner and Enter finishes, like Play Bounds. Rename it with N. When a layout has any Zone marker, the
   game uses only the layout's zones.
2. **Otherwise, `Content/Data/TownZones.json`** for the running map.
3. **Otherwise, on the procedural town**, its `TownLayout.json` districts.

When zones overlap, the first one in the list wins.

Clients read the zones from their own copy of the data. They only replicate with the packs, which carry the tiers.

### The default castle-town zones

These were derived from layout v2's pack clusters and the explore landmarks in `CastleTown.json`. They are placeholders:
rename and redraw them. They cover every pack of both realms.

| Zone | Packs inside (per realm) | Monster Tier |
| --- | --- | --- |
| Castle Keep | 2 x T1 | 1 |
| Smithy Row | 10 x T4, 1 x T2 | 4 |
| West Gate Road | 3 x T3 | 3 |
| The Breach | none | No monster camps |
| North Market | 12 x T2, 1 x T3, 1 x T1 | 2 |
| Market Plaza | 9 x T1 | 1 |
| The Western Fields | 7 x T1 | 1 |
| The Eastern Fields | 18 x T1 | 1 |

### Editing zones

1. Arm the Zone setter (Shift+3).
2. With nothing selected, click **USE DEFAULT ZONES**. It copies the defaults above into the layout as Zone markers, in
   one undo step.
3. Select a zone to change it:
   - **RENAME (N)** changes the name the banner shows.
   - Move its corners (R), remove corners (X), or **ADD CORNERS**.
   - The inspector shows the zone text and the packs inside it, by tier.
4. Each zone's label in the world also shows its monster tier.
5. **APPLY** writes the layout. Zones need at least 3 corners, and Validate flags the ones that do not have them.

## Tests

`CireZones::RunTests` runs inside `CireRouteEditor::RunTests` (`CIRE_ROUTE_TOOLS_PASS`, part of the expansion gate). It checks:

- the tier tags, colours, nameplate label and frame caption
- the dominant tier: most common, ties to the higher, none gives 0
- concave polygons, the zone tier and the tier counts
- zone lookup by realm
- `TownZones.json` parsing and the shipped defaults
- Zone markers: layout zones, the JSON round-trip, and Validate

The route-tools pack spawn checks that every spawned pack monster reads `T#` next to its name and carries no rim glow.
The race checks confirm that a Champion's skin has no rank tint, glow or rim, and keeps its race palette.
