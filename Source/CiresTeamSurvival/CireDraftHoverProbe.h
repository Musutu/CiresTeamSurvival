#pragma once
// champ-select-perf: hover-timing probe for champion select (-CireDraftHoverProbe, Tools/RunDraftHoverProbe.py).
//
// Drives a virtual hover over a fixed list of heroes (the same list before and after an optimisation), exactly like the
// mouse would (the screen's own hover debounce applies), and measures every hover switch:
//   - the longest frame inside the switch window (the stall the player feels) and frames over the hitch threshold,
//   - game-thread milliseconds spent loading the background, portraits and the 3D body (spawn / bind / visuals),
//   - time until the details (splash) show the hovered hero and until the live 3D figure is ready.
// Phases: "browse" (dwell 700 ms per hero), "scrub" (60 ms per hero: a fast mouse sweep across the grid) and
// "revisit" (the first browse heroes again: cache hits) and "linger" (4 s on unseen heroes: time to the live figure). Results: CIRE_DRAFT_HOVER_* log lines and
// Saved/DraftHoverProbe/<stamp>[_tag]/hover.json.
#include "CoreMinimal.h"

/** What the screen reports every frame (cumulative counters; the probe takes deltas). */
struct FCireDraftHoverSample
{
    FString SplashId;           // hero whose details are on screen
    FString StageId;            // hero the 3D stage is showing (or loading)
    bool bStageReady = false;   // live figure drawn for StageId
    bool bFigureShown = false;  // any figure/card for SplashId drawn (portrait card or live body)
    double BackgroundMs = 0, PortraitMs = 0, BodyMs = 0, BindMs = 0, VisualsMs = 0;
    int32 BackgroundLoads = 0, PortraitLoads = 0, BodyShows = 0;
    int32 AsyncRequests = 0, AsyncCancelled = 0, CacheHits = 0; // async loader counters (0 before the optimisation)
};

struct CIRESTEAMSURVIVAL_API FCireDraftHoverProbe
{
    struct FSwitch
    {
        FString Phase, Id;
        double StartedAt = 0, EndsAt = 0;
        double MaxFrameMs = 0; int32 Frames = 0, Hitches = 0;
        double SplashMs = -1, FigureMs = -1, ReadyMs = -1;
        FCireDraftHoverSample Start, End;
    };
    bool bActive = false, bDone = false, bPassed = false;
    double HitchMs = 50.0;
    double WarmupUntil = 0, HardDeadline = 0;
    FString Directory;
    TArray<FSwitch> Plan;
    int32 Current = -1;
    int32 Lag[2] = {-1, -1};
    TArray<double> AllFrames[4];

    /** Builds the fixed plan from the roster order (every 3rd hero browsed, the ones after them scrubbed). */
    void Begin(const TArray<FString>& RosterIds, double Now, const FString& Tag, int32 Count = 30);
    /** Call once per drawn frame. FrameMs = wall time since the previous draw. Returns the hover id to inject. */
    FString Tick(double Now, double FrameMs, const FCireDraftHoverSample& Sample);
    /** Percentile helper (exposed for the native tests). */
    static double Percentile(TArray<double> Values, double Q);
private:
    void Finish(bool bTimedOut);
};
