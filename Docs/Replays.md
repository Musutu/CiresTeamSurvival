# Native match replays

Cire uses Unreal Engine's `DemoNetDriver` and `LocalFileNetworkReplayStreaming`. The recording is a native `.replay` network stream in `Saved/Demos`, containing replicated actors and state. It is not a JSON simulation, video capture, or a copy of the save game. The browser lists recordings compatible with the current replay/network version; engine or gameplay serialization changes can make older recordings incompatible.

The authoritative standalone game, listen server, or dedicated server can record. A remote client cannot start a full-match recording through the Cire API. A server recording includes both survival realms, independently of live client relevancy. Playback exposes both recorded realms to a local spectator; this exception applies only while the world is playing a replay.

The in-game replay browser can record, stop, refresh, and open a finished recording. Opening a replay replaces the current standalone match. An active multiplayer world must be left first; the local playback API rejects listen-server, dedicated-server, and remote-client sessions. Exiting playback returns to the default battlefield as a new standalone session. It does not restore the match that was replaced.

## Controls

During playback, use the replay panel or these keys:

- `WASD` moves the free camera; `Q/E` moves down/up; hold `Shift` to move faster.
- Hold the right mouse button to look around. `1` and `2` move the camera to the two survival lanes.
- `Space` pauses/resumes. Left/right arrows seek backward/forward 10 seconds.
- `-` / `+` changes speed within 0.25–4×.
- `F9` opens settings. `Esc` closes an open settings panel, then exits playback.

Development builds also expose the local console command:

```text
cire.Replay record Example match
cire.Replay stop
cire.Replay list
cire.Replay play Cire_20260923T210000_abcdef123456
cire.Replay pause
cire.Replay resume
cire.Replay seek 30
cire.Replay speed 2
cire.Replay exit
```

Use an ID returned by `list`; replay IDs reject path separators, URL options, extensions, and traversal. There is no delete command or network RPC for replay control.

## Integration

`CireReplay::Get(World)` returns `UCireReplaySubsystem`. This game-instance subsystem survives the world travel required by playback. `RefreshList()` is asynchronous; UI checks `IsBusy()` and reads `GetEntries()`. Each entry includes ID, title, UTC timestamp, duration, byte count, and whether it is still being recorded. `Status` contains the most recent operation result. `Play`, `SetPaused`, `SetSpeed`, `Seek`, and `ExitPlayback` report whether the operation was accepted. A seek completes asynchronously.

The game mode uses `ACireReplaySpectator` as `ReplaySpectatorPlayerControllerClass`. Normal match startup may call `StartRecording`; match completion calls `StopRecording`. Allow at least one replicated frame of the final match state before stopping. Exclude automation fixtures and `-CireReplayProbe` from automatic recording. Shutdown also finalizes an active local stream. Play-in-editor recording is deliberately rejected; use Standalone Game or a packaged build.

Recording is configured at up to 30 updates per second, with checkpoints approximately every 10 seconds. Long recordings consume disk space; recordings are retained locally and no cleanup/delete policy is applied by Cire. The engine may report a recording failure if storage becomes unavailable.

UE 5.8 has a frame-zero startup edge case for short, fully buffered recordings: if the playback tick is shorter than the first recorded frame, its driver can wait for unread file bytes even though future packets are already buffered. Cire detects only that stalled state after 250 ms and requests a native seek to the first buffered packet timestamp. The engine still loads and processes all recorded actor data. This recovery is inactive during normal playback, recording, pause, and active seek operations.

Replicated positions, health, match state, champion attacks, skillshots, constructs, summons, and ground effects are reconstructed from the stream. Local UI state, mouse movements, and HUD chat/combat buffers are not a replay record. Some short sounds and cosmetic cues are delivered by owner-specific client RPCs; those may not be recorded for the replay spectator, and seeking intentionally does not replay every skipped one-shot sound. Persistent spell geometry is reconstructed from replicated gameplay actors.

## Validation

`CireReplay::RunReplaySmoke(World)` checks safe IDs, native local streamer availability, and that replay controls cannot alter an ordinary match. It does not start a recording.

Launch a standalone development game with `-CireReplayProbe` for the separate native integration test. This flag records two named fixture heroes in opposite realms, changes their replicated health, finalizes the real file, enumerates its byte count and duration, plays it, pauses, changes speed, seeks backward/forward, verifies both actors and their changed health from the stream, then exits playback. It logs `CIRE_REPLAY_INTEGRATION_PASS` and exits 0 on success; timeout/failure logs `CIRE_REPLAY_INTEGRATION_FAIL` and exits 1. Its native replay is retained for inspection.

The native probe records at 20 fps and plays at 120 fps to cover differing capture/playback cadence and the startup edge case. It restores the original frame limit before a successful exit. `CIRE_REPLAY_NATIVE_BOOTSTRAP` identifies an applied recovery, and bounded `CIRE_REPLAY_PROGRESS` lines report native stream and packet state during a stalled operation.

The implementation was checked against the installed UE 5.8 source: `Engine/GameInstance.h`, `ReplaySubsystem.h`, `Engine/DemoNetDriver.h`, and `NetworkReplayStreaming.h`. Build and native integration results belong in the task's validation report; the presence of this probe is not itself a passing result.
