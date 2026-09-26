#pragma once
// town-trim: cut the Medieval Kingdom town down to Eric's Play Bounds (Docs/CastleTown.md "Trim to Play Bounds").
//
// The Play Bounds marker is one shared, realm-local polygon, so one trim applies to both realms through the realm frames
// (CireLanePath::ToLocal / RealmOrigin). Everything that lies entirely outside the polygon grown by "trim.margin" is:
//   1. LOAD: never loaded. Whole sublevels (package bounds) and the pack's nested Level Instances (their asset bounds) are
//      skipped before they stream; straddling sublevels load and then lose their outside actors.
//   2. RENDER/TICK: an outside actor that did load is destroyed, or kept as a cheap BACKDROP ("trim.backdrop"): no
//      collision, no shadows, lowest LOD, no tick, no lights or particles. A dedicated server keeps no backdrop.
//   3. NAVMESH: the realm's nav bounds become the polygon (plus margin) rasterized into a few boxes, and the nav cache key
//      carries the trim signature, so editing the bounds rebuilds the navmesh.
// No Play Bounds (or fewer than 3 corners) means no trim. The map layout editor (-CireRouteEdit) and explore mode load
// everything; the editor shows the cut as a red overlay and PREVIEW TRIM hides it live (reversible).
#include "CoreMinimal.h"

class ACireGameMode;
class UCanvas;
class ULevel;
class UWorld;
struct FCireMapLayout;
struct FCireLayoutIssue;

enum class ECireTrimBackdrop : uint8 { Off, Low, Full };
enum class ECireTrimClass : uint8 { Inside, Straddling, Outside };

/** Content/Data/CastleTown.json "trim". */
struct CIRESTEAMSURVIVAL_API FCireTrimSettings
{
    bool bEnabled = true;
    float Margin = 1500.f;                       // cm past the border that still loads, so nothing pops at the edge
    ECireTrimBackdrop Backdrop = ECireTrimBackdrop::Low;
    float LandmarkHeight = 1800.f;               // low: an outside actor this tall (cm) is kept as a silhouette...
    float LandmarkSize = 4000.f;                 // ...or this long horizontally (walls, the mountain)
    float BackdropRange = 0.f;                   // 0 = any distance; else backdrop only within this many cm of the polygon
    float NavCell = 4000.f;                      // nav bounds raster cell (cm)
    TArray<FString> BackdropLevels = {TEXT("SL_Landscape"), TEXT("SL_Mountain")};   // sublevels that load even when outside
    TArray<FString> BackdropLevelInstances = {TEXT("Castle"), TEXT("Tower"), TEXT("Gate")}; // LI assets that load as backdrop
    bool bKeepLandscape = true;                  // off only: the landscape outside is kept anyway (the ground under the edge)
};

namespace CireTownTrim
{
    // ---- geometry (realm-local, pure; tests) ------------------------------------------------------------------------
    CIRESTEAMSURVIVAL_API bool InsidePolygon(const TArray<FVector2D>& Poly, const FVector2D& P);
    /** Distance from P to the polygon's outline (0 on it). */
    CIRESTEAMSURVIVAL_API double EdgeDistance(const TArray<FVector2D>& Poly, const FVector2D& P);
    /** Inside or within Margin of the polygon. */
    CIRESTEAMSURVIVAL_API bool WithinMargin(const TArray<FVector2D>& Poly, const FVector2D& P, float Margin);
    /** A realm-local box: Inside (entirely inside the polygon), Outside (farther than Margin from it) or Straddling. */
    CIRESTEAMSURVIVAL_API ECireTrimClass Classify(const TArray<FVector2D>& Poly, const FBox2D& Box, float Margin);
    /** A world box, in the realm its centre belongs to (CireTownMap::RealmAt), through that realm's frame. */
    CIRESTEAMSURVIVAL_API ECireTrimClass ClassifyWorld(const TArray<FVector2D>& Poly, const FBox& World, float Margin);
    /** The polygon plus Margin rasterized into Cell-sized rows and merged into boxes (realm-local), clipped to Clip. */
    CIRESTEAMSURVIVAL_API TArray<FBox2D> RasterBoxes(const TArray<FVector2D>& Poly, float Margin, float Cell, const FBox2D& Clip, int32 MaxBoxes = 48);

    // ---- the trim for this process ------------------------------------------------------------------------------------
    CIRESTEAMSURVIVAL_API const FCireTrimSettings& Settings();
    CIRESTEAMSURVIVAL_API FCireTrimSettings ParseSettings(const FString& CastleTownJson);
    /** The polygon the trim uses (frozen when the town starts loading): -CireTownTrimBounds=<file> or the active layout's
        Play Bounds. Empty = no trim. */
    CIRESTEAMSURVIVAL_API const TArray<FVector2D>& Polygon();
    /** The trim applies to this process: the town is active, a polygon exists, not the layout editor / explore mode,
        "trim.enabled" and no -CireNoTownTrim. */
    CIRESTEAMSURVIVAL_API bool Active();
    /** Everything that shapes the trimmed navmesh ("" without a trim); part of the nav cache key. */
    CIRESTEAMSURVIVAL_API FString Signature();
    /** Tests: force a polygon (nullptr restores the real one). */
    CIRESTEAMSURVIVAL_API void SetPolygonOverride(const TArray<FVector2D>* Poly);

    // ---- load (CireTownMap::LoadRealms / PrepareRealmLevels) --------------------------------------------------------------
    /** Freeze the polygon and reset the counters (LoadRealms start). */
    CIRESTEAMSURVIVAL_API void BeginLoad(UWorld* World);
    /** A realm sublevel entirely outside (package bounds) is not streamed, unless it is a backdrop level. */
    CIRESTEAMSURVIVAL_API bool ShouldLoadSublevel(const FString& Package, const FVector& RealmOffset);
    /** Cancel the queued loads of Level Instances entirely outside (before ULevelInstanceSubsystem streams them). */
    CIRESTEAMSURVIVAL_API int32 FilterLevelInstances(UWorld* World);
    /** A realm level just became visible: destroy its outside actors or keep them as backdrop. */
    CIRESTEAMSURVIVAL_API void TrimLevel(UWorld* World, ULevel* Level);
    /** CIRE_TOWN_TRIM summary (after streaming). */
    CIRESTEAMSURVIVAL_API void LogSummary(UWorld* World);
    /** ACireWorld tick: warn once when the match runs other Play Bounds than the ones the town was trimmed to. */
    CIRESTEAMSURVIVAL_API void Tick(UWorld* World);

    // ---- navmesh ---------------------------------------------------------------------------------------------------------
    /** The realm's nav bounds cut to the trim (world boxes). False: no trim, keep RealmBox. */
    CIRESTEAMSURVIVAL_API bool NavBoxes(int32 Team, const FBox& RealmBox, TArray<FBox>& Out);

    // ---- map layout editor -----------------------------------------------------------------------------------------------
    /** The draft's Play Bounds polygon (empty without one). */
    CIRESTEAMSURVIVAL_API TArray<FVector2D> LayoutPolygon(const FCireMapLayout& Layout);
    /** Every editor frame: keep PREVIEW TRIM in step with the draft, and draw the red overlay outside the bounds. */
    CIRESTEAMSURVIVAL_API void TickEditor(UWorld* World, UCanvas* Canvas, const FCireMapLayout& Layout, int32 Realm);
    CIRESTEAMSURVIVAL_API bool IsPreviewing();
    /** Toggle PREVIEW TRIM: hide (not destroy) everything the trim would cut; off restores it all. Returns the new state. */
    CIRESTEAMSURVIVAL_API bool TogglePreview(UWorld* World, const FCireMapLayout& Layout);
    CIRESTEAMSURVIVAL_API void StopPreview();
    /** Validate: markers inside the bounds but within the trim margin of the edge (notes, not errors). */
    CIRESTEAMSURVIVAL_API TArray<FCireLayoutIssue> EdgeIssues(const FCireMapLayout& Layout);

    /** CIRE_TOWN_TRIM_TESTS_PASS / _FAIL (part of -CireCombatExpansionProbe). */
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
}
