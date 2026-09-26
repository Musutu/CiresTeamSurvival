#pragma once
#include "CoreMinimal.h"

/**
 * video-crash: Options > Video, applied safely.
 *
 * Applying a resolution / window-mode change resizes the game viewport, and the viewport then
 * replaces its debug canvas. The HUD draws inside UGameViewportClient::Draw, which holds a raw
 * pointer to that canvas and pushes a transform onto it right after the HUD returns, so applying
 * from a HUD button (as Options used to) freed the canvas under the engine's feet: heap
 * corruption and an access violation in FCanvas::PushAbsoluteTransform (Eric's crash, 2026-09-26).
 *
 * Every change is therefore only *requested* here; the work runs on the core ticker at the start
 * of the next engine frame, never while a viewport or the HUD is drawing. Nothing needs a restart:
 * resolution, window mode, the scalability preset, render scale, VSync and the frame cap all apply
 * live, and a confirmed change is saved to GameUserSettings.ini so the next launch starts with it.
 */
struct FCireVideoState
{
    FIntPoint Resolution = FIntPoint(1600, 900);
    int32 WindowMode = 2;       // EWindowMode: 0 fullscreen, 1 borderless, 2 windowed
    int32 Quality = 3;          // 0 Low .. 3 Epic, 4 Cinematic
    float RenderScale = 100.f;  // r.ScreenPercentage (50..100)
    bool bVSync = true;
    float FrameRateLimit = 0.f; // 0 = uncapped
    bool operator==(const FCireVideoState& O) const
    {
        return Resolution == O.Resolution && WindowMode == O.WindowMode && Quality == O.Quality && FMath::IsNearlyEqual(RenderScale, O.RenderScale, .5f)
            && bVSync == O.bVSync && FMath::IsNearlyEqual(FrameRateLimit, O.FrameRateLimit, .5f);
    }
    FString ToString() const;
};

namespace CireVideo
{
    /** What GameUserSettings holds right now. */
    CIRESTEAMSURVIVAL_API FCireVideoState Current();
    /** Preview: applied on the next engine tick, auto-reverted after ConfirmSeconds unless kept. */
    CIRESTEAMSURVIVAL_API void RequestPreview(const FCireVideoState& State, double ConfirmSeconds = 15.0);
    /** Keep the previewed (or current) settings and save them for the next launch. */
    CIRESTEAMSURVIVAL_API void RequestKeep();
    /** Restore the settings from before the preview (no-op without a preview). */
    CIRESTEAMSURVIVAL_API void RequestRevert();
    /** A preview is live and waiting for Keep / Revert. */
    CIRESTEAMSURVIVAL_API bool IsPreviewPending();
    /** A request is queued for the next tick (the Options page shows "Applying..."). */
    CIRESTEAMSURVIVAL_API bool IsBusy();
    CIRESTEAMSURVIVAL_API double SecondsToRevert();
    /** Applies immediately. Only call outside viewport drawing (ticker, tests, startup). */
    CIRESTEAMSURVIVAL_API void ApplyNow(const FCireVideoState& State, bool bSave);
    /** Apply attempts made while the HUD was drawing (deferred; must stay 0, checked by the video cycle). */
    CIRESTEAMSURVIVAL_API int32 AppliesDuringDraw();
    /** The HUD holds one of these for the whole DrawHUD, so an apply inside it is refused and deferred. */
    struct CIRESTEAMSURVIVAL_API FDrawScope { FDrawScope(); ~FDrawScope(); };
}

namespace CireRenderSanity
{
    /**
     * video-crash: every registered primitive that feeds the renderer's distance-field / GPU
     * scene must have a finite, invertible transform within float range: no NaN, no zero or
     * degenerate scale (hide with visibility instead), no instance flattened to scale 0.
     * Logs CIRE_RENDER_SANITY_BAD lines (actor, component, instance, reason); returns the count.
     */
    CIRESTEAMSURVIVAL_API int32 Scan(class UWorld* World, bool bLogEach = true);
    /** False (with the reason) for a NaN, zero-scale or out-of-float-range transform. */
    CIRESTEAMSURVIVAL_API bool IsRenderSafe(const FTransform& Transform, FString* Why = nullptr);
    /** A mesh whose render-data bounds are NaN gets its asset bounds back (and its components re-created). */
    CIRESTEAMSURVIVAL_API bool RepairMesh(class UStaticMesh* Mesh);
    /** Every loaded, built static mesh not yet checked (runs on a 0.25 s core ticker in game processes). */
    CIRESTEAMSURVIVAL_API int32 RepairLoadedMeshes();
    CIRESTEAMSURVIVAL_API int32 RepairedMeshes();
}

class ACireController;
/** video-crash: -CireVideoCycle regression fixture (CireVideoCycle.cpp, Tools/RunVideoCycle.py). */
namespace CireVideoCycle
{
#if !UE_BUILD_SHIPPING
    bool IsActive();
    void Tick(ACireController* Controller);
#else
    inline bool IsActive() { return false; }
    inline void Tick(ACireController*) {}
#endif
}
