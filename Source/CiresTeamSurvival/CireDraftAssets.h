#pragma once
// champ-select-perf: asynchronous asset streaming for champion select.
//
// The hover path never calls a synchronous LoadObject. Everything goes through one FStreamableManager:
//   - portraits and kit icons: small, requested for the visible page (+ next page), kept once loaded;
//   - backgrounds: requested on hover, kept in an LRU (default 10) and pinned full-resolution while cached, so heroes
//     sharing a painting never reload it and a revisit never re-streams it;
//   - hero bodies: the packages a profile's art binding names (mesh, anims, parts, weapons; their dependencies follow)
//     are loaded in the background after the hover debounce; the stage only spawns the body once they are resident.
//     Completed bodies stay in an LRU (default 6); in-flight requests nobody wants any more are cancelled.
// Accessors return null / false while an asset is still streaming; callers draw a cheap stand-in meanwhile.
#include "CoreMinimal.h"

class UTexture2D;

namespace CireDraftAssets
{
    // ---- lightweight (instant on hover once the page was shown) ----
    /** Portrait texture when resident (and compiled), else null; requests it. */
    CIRESTEAMSURVIVAL_API UTexture2D* Portrait(const FString& ProfileId);
    /** True when the hero has a portrait asset at all (authored or local Paragon capture). */
    CIRESTEAMSURVIVAL_API bool HasPortrait(const FString& ProfileId);
    /** Painted background texture (T_DraftBg_<BgId>) when resident, else null; requests it and touches the LRU. */
    CIRESTEAMSURVIVAL_API UTexture2D* Background(const FString& BgId);
    CIRESTEAMSURVIVAL_API bool HasBackground(const FString& BgId);
    /** Ability icon (/Game/UI/Abilities/T_Ability_<id> or T_<id>) when resident, else null; requests it. */
    CIRESTEAMSURVIVAL_API UTexture2D* KitIcon(const FString& AbilityId);
    /** Queue portraits / backgrounds / icons without drawing them (current + next page, neighbours). */
    CIRESTEAMSURVIVAL_API void PrefetchPortraits(const TArray<FString>& ProfileIds);
    CIRESTEAMSURVIVAL_API void PrefetchBackground(const FString& BgId);
    CIRESTEAMSURVIVAL_API void PrefetchIcons(const TArray<FString>& AbilityIds);

    // ---- heavy (3D body) ----
    /** Object paths the profile's art binding names (Skin "" = default body, else the <profile>@<skin> row too). */
    CIRESTEAMSURVIVAL_API const TArray<FString>& BodyPaths(const FString& ProfileId, const FString& Skin = FString());
    /** Starts (or touches) the async load of a body. High priority for the hovered/selected hero. */
    CIRESTEAMSURVIVAL_API void RequestBody(const FString& ProfileId, const FString& Skin, bool bHighPriority);
    /** True once every package of the body is resident and its meshes finished compiling (the editor builds mesh render
     *  data after load; touching a compiling mesh would block the game thread), or the binding names none. */
    CIRESTEAMSURVIVAL_API bool IsBodyReady(const FString& ProfileId, const FString& Skin = FString());
    /** Still streaming packages (not merely compiling). */
    CIRESTEAMSURVIVAL_API bool IsBodyStreaming(const FString& ProfileId, const FString& Skin = FString());
    CIRESTEAMSURVIVAL_API bool IsBodyRequested(const FString& ProfileId, const FString& Skin = FString());
    /** Cancels in-flight body requests whose key (id or id@skin) is not in Keep. Completed bodies stay cached. */
    CIRESTEAMSURVIVAL_API int32 CancelBodiesExcept(const TSet<FString>& Keep);
    /** Number of body requests currently loading. */
    CIRESTEAMSURVIVAL_API int32 BodiesInFlight();
    CIRESTEAMSURVIVAL_API FString BodyKey(const FString& ProfileId, const FString& Skin);

    // ---- tunables (Content/Data/DraftSelect.json, reloaded with Reload) ----
    struct FTunables
    {
        double HoverDebounceSeconds = .15;   // hover must settle this long before the heavy body load starts
        double ParagonHoverDebounceSeconds = .35; // Paragon bodies are much heavier (editor: mesh rebuild on first load)
        bool bPreloadParagonNeighbours = false;  // neighbour preload skips Paragon bodies (never load one nobody asked for)
        int32 BodyCacheSize = 6;              // completed bodies kept resident
        int32 BackgroundCacheSize = 10;       // backgrounds kept resident (pinned full resolution)
        int32 PreviewPoolSize = 4;            // spawned preview bodies the stage keeps hidden for instant revisits
        int32 NeighbourPreload = 1;           // bodies preloaded either side of the hovered tile (low priority)
        double CrossFadeSeconds = .35;        // background / figure crossfade
        float AsyncLoadingTimeLimitMs = 12.f; // game-thread async-loading budget per frame while champion select is open
    };
    CIRESTEAMSURVIVAL_API const FTunables& Tunables();
    /** Raises s.AsyncLoadingTimeLimit to the tunable while a draft stage lives (ref-counted), restores it after. */
    CIRESTEAMSURVIVAL_API void RetainLoadingBudget(bool bRetain);
    CIRESTEAMSURVIVAL_API void Reload();

    // ---- probe counters ----
    struct FStats
    {
        int32 AsyncRequests = 0, AsyncCancelled = 0, CacheHits = 0, SyncFallbacks = 0;
        int32 PortraitRequests = 0, BackgroundRequests = 0, IconRequests = 0, BodyRequests = 0;
        double RequestMs = 0; // game-thread time spent issuing requests / path lookups
    };
    CIRESTEAMSURVIVAL_API FStats& Stats();

#if !UE_BUILD_SHIPPING
    /** Cache behaviour tests (path index, request / hit / cancel / LRU eviction, completion after a flush). */
    CIRESTEAMSURVIVAL_API bool RunTests();
#endif
}
