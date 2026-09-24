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
