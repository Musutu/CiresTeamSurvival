# Native spell presentation

The current abilities now have collision-free native modeled effects: forged
crescents, fire tongues and embers, faceted frost crystals, forked lightning,
shadow ribbons, healing helices, ward lattices and poison vapor. The palette uses
muted metal, amber, glacial blue, violet, and aged green, with brighter thin cores.
The polish pass adds fine rotating sigils, faceted meteor showers, interlocking
aegis shields, rising nature blooms, ochre stone bursts and spectral hunter
wisps. Camera-facing soft glows sit behind the thin modeled cores. These are
original prototype effects built from procedural Unreal meshes, not finished
production art or playback of every cosmetic timeline from Astra.

`CireSpellPresentation::Play` is local-only. Authoritative combat sends a cue to
eligible players; the local actor cannot damage, obstruct or select units. Cast
and launch cues announce attempts. Impact audio is played only for a confirmed
damage/healing event, so an avoided attack has no false hit sound. The critical
cue adds a gold faceted flash; combat text carries the readable critical marker.

Ground overlays follow the replicated `ACireAreaEffect`. They read the real
polygon/shape, warning state, visibility and lifetime. Vapor, embers, crystals
and runes are sampled inside the same containment test used for gameplay. The
amber warning boundary precedes active colors. They are removed when their area
is destroyed, and hidden whenever the area is not observable in the local realm.

Moving spell visuals follow the real projectile transform and retain up to 12
actual positions for curved trails, including a reflected projectile. They do not home,
change speed, decide hits or change collision. Construct rune trims likewise
follow the physical wall, while the protection cage decorates its real actor.

The renderer caps each effect at 8192 vertices (6144 modeled core plus 2048 soft
vertices), the world at 64 concurrent presentation actors and transient spell
lights at eight. Lights have no shadows; the meshes have no collision cooking or
navigation. Depth-tested materials fade into nearby surfaces. Ground warning
boundaries still use the actual collision geometry and are not expanded by the
decorative effects.
Actual gameplay keeps operating if a cosmetic cannot be allocated. Nine original
synthesized 48 kHz mono sounds use a shared 16-voice concurrency limit, a 160 cm
full-volume radius and an 1800 cm falloff. Their source waveforms, provenance and
amplitude validation are in `Art/Generated/CombatAudio01`. No external samples or
paid asset calls are involved. These gameplay cues still need final creative
mixing alongside the environment and music.

Rebuild content using `Tools/BuildSpellContent.py` with engine Python. This runs
the isolated ContentBuilder commandlet and copies only the two named new effect
materials, nine SoundWaves and one concurrency asset.
The newer core and radial glow materials use `Tools/BuildSpellPolish.py`. Run
`-CireSpellPolishBuild` in the coordinated isolated CreatureBuilder project, then
a separate process with `-CireSpellPolishVerify`. Only the two new packages in
`Art/Effects/CireSpellPolish01` are published after reload validation; all previous
Content file hashes must remain unchanged. An interrupted build can resume only
missing assets with `-CireSpellPolishResume`, validating any already saved asset.

After compiling the game, run `Tools/RunSpellGallery.py` for seven 1920×1080 offscreen views of the native
effects, warning boundaries, active ground areas, projectile heads and impacts.
The seventh page covers Second Wind, Last Stand, Challenge of Iron, Seismic
Reprisal, Starfall, Spectral Hunt, Mass Aegis and Wellspring. The fixture checks
explicit pass markers, finite vertices and UVs, mesh budgets, material assignment,
collision state, viewport inclusion and screenshot dimensions. Visual review of
the rendered images remains necessary; a technical pass is not an art-quality
rating.

Buff, aura and empowered-attack signatures (Blood Frenzy, guards, taunts, slows,
poison, item-ready buffs) are a separate data-driven system; see
[BuffVisuals.md](BuffVisuals.md).

## Shape-true telegraphs and ability VFX (ability-vfx)

Every ability's true hit shape lives in one descriptor, `CireAbilityShapes::Describe` (champion
skills, basic attacks and all monster race abilities, read from the same tuning/area/NPC data the
gameplay uses). Aim previews, enemy telegraphs, cast cues and the native tests read it, so a
telegraph cannot drift from what actually hits. See [AbilityVFXAudit.md](AbilityVFXAudit.md).

- **Ground telegraphs** (`CireAbilityVFX::PaintTelegraph`, drawn by the spell visual that follows each
  `ACireAreaEffect`): feathered fill, pulsing border on the true boundary, a fill that grows until the
  hit resolves, an arrowhead and travelling chevrons for lines, chevrons for cones and the
  designated-spot marker for circles. Decorations always stay inside the boundary. Enemy/monster
  warnings are amber; your team's are school-coloured; a monster's zero-damage buff radius (rally) is
  a calm ring. Actives: soft pool with ripples, detonation shock front and school spikes, 0.3 s dissolve.
  The area's own flat mesh stays as the fallback when the presentation cap is reached.
- **Line skillshots**: during the authored warning the projectile's true corridor (width = collision
  diameter, length = min(range, speed x lifetime)) is drawn from the caster with an arrowhead for
  every observer. Monster projectile casts draw the amber lane for the whole cast bar and drop it on
  interrupt. No point marker is drawn at the aim for line abilities.
- **Projectiles**: per-school heads (flame tongues, crystal spear, arrow shaft, void/shadow ribbons,
  arcane orb) with a 20 cm readability floor, recorded-path wake, ground glow under the head and a
  burst where the flight ends. Basic ranged attacks get the same wake.
- **Impacts**: flash, ballistic sparks away from the attacker, school variants (frost spikes, embers,
  holy rays, shadow/void implosion, poison droplets, tide water crown, storm forks, debris) and a
  ground splash. Optional small camera kick near your champion (Options > Graphics > Impact camera shake).
- **Casts**: self circles show a shockwave that reaches the true radius (War Cry 850 cm, Cleave 320 cm,
  Sanctuary 600 cm...), self buffs stay on the caster, unit spells draw a ground streak to the unit,
  Chain Spark arcs between victims, monsters wind up at the caster.
- **Release sync**: champion cast clips reach their contact frame when the effect releases
  (`CireAbilityVFX::ReleaseLead`: the skillshot warning, otherwise a 0.12 s snap-in) and cast cues /
  instant impacts from that champion wait for the same frame.
- **Budgets**: 64 presentation actors, 8 transient lights, 8192 core+soft and 3072 ground vertices
  per effect, no collision/shadows/navigation, realm privacy unchanged (hidden when the area or
  projectile is not observable).
- `cire.AbilityVFX 0` (or `-CireLegacyVFX`) restores the previous presentation for A/B captures.

Audit harness: `Tools/RunAbilityVFXGallery.py` (see the audit doc). Native suite:
`CireAbilityVFX::RunTests`, part of `Tools/RunExpansionChecks.py --only native`.
