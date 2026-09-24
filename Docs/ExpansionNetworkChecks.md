# Combat expansion network verification

`Tools/RunExpansionChecks.py` runs three independent development checks. Use `--only native`, `--only network`, or `--only replay` to scope a rerun; the default runs all three. It does not compile the game or generate assets. Reports, exit codes, marker evidence, and engine logs are saved under `Saved/ExpansionChecks/<UTC timestamp>`.

The network case creates one dedicated server and two separate remote client processes on UDP port 7784 by default. It refuses an already occupied port, waits for server readiness, bounds execution, and terminates only its own child processes on failure. Both clients and the server must exit 0 and emit their explicit success markers.

`CireExpansionNetProbe` is entered through `TickServer(GameMode)` and `TickClient(Controller)`. The functions return false unless their matching `-CireExpansionNetServer` or `-CireExpansionNetClient` flag is present. An owner-only replicated probe actor sends reliable, stage-specific acknowledgements. It validates its owner/team and cannot perform gameplay mutations. Probe behavior is excluded from shipping builds.

The six acknowledged stages verify:

1. Each client receives its own projectile, construct, and summon while none from the opposing survival realm is replicated. Projectile position advances on the remote client; speed, wall dimensions/health/collision, summon owner, commandability, and health replicate.
2. Guaranteed critical damage produces a private 40-damage event with critical metadata and stable identity, and the target's resulting health replicates.
3. Killing one caster removes that caster's transient actors on its client, while the other team's actors remain.
4. After an authoritative arena transition, both clients receive both teams' new actors.
5. A moving authoritative skillshot hits the opposing hero for 30; a separate guaranteed critical hits for 20. Both clients receive correct local-source/local-target metadata and final health. Friendly wall damage is rejected, an opposing wall can be destroyed, and an opposing summon takes damage.
6. Leaving the arena removes the transient actors on both clients.

The server marker is `CIRE_EXPANSION_NET_SERVER_PASS`; each client emits `CIRE_EXPANSION_NET_CLIENT_PASS`. This file describes test coverage, not a recorded passing run. Use the generated report for actual results.

Automatic match replay recording must be disabled for these fixture flags. The separate `-CireReplayProbe` case validates actual native replay files and is documented in `Replays.md`.
