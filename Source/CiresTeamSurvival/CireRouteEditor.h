#pragma once
// nav-paths: the in-game path editor's model (F8 > Developer > Paths, Docs/Navigation.md).
// Validation is live: route rules (CireLanePath::Validate), navmesh reachability of every segment
// for both agent sizes, and the town builder's route-clearance rule (which props a segment would
// remove). Applying goes through CireLanePath::ApplyLive (server-authoritative).
// dev-route-tools: validation now covers 1..16 challenge packs; authoring moved to the map layout editor
// (CireMapLayout.h, CireLayoutEditorHUD.cpp, Docs/MapLayout.md). RunTests covers both.
#include "CoreMinimal.h"
#include "CireLanePath.h"

class UWorld;
class ACireGameMode;

enum class ECireRouteReach : uint8
{
    Unknown,  // no navmesh on this peer
    Direct,   // path length <= 1.2x the straight segment
    Detour,   // a full path exists but it walks around something
    Partial,  // only part of the way (the rest is blocked)
    None      // no path at all
};

struct CIRESTEAMSURVIVAL_API FCireRouteSegmentCheck
{
    ECireRouteReach Reach = ECireRouteReach::Unknown;
    float PathLength = 0.f, Direct = 0.f;
    /** Town pieces the route-clearance rule would remove for this segment at the draft lane width. */
    int32 PropConflicts = 0;
    TArray<FName> Slots;
    /** The navmesh path a Hero-sized unit takes along this segment (editor overlay). */
    TArray<FVector> Path;
};

struct CIRESTEAMSURVIVAL_API FCireRouteValidation
{
    bool bRulesOk = false, bNavAvailable = false;
    FString RulesError;
    TArray<FCireRouteSegmentCheck> Segments[2];
    // dev-route-tools: one entry per challenge pack (1..16, or the three automatic bays).
    TArray<ECireRouteReach> BayReach[2];
    TArray<int32> BayConflicts[2];
    int32 Unreachable = 0, Detours = 0, Conflicts = 0;
    uint32 NavRevision = 0;
    double Ms = 0.0;
    /** Rules pass and every segment has a full navmesh path (prop removals are reported, not fatal). */
    bool Passed() const { return bRulesOk && Unreachable == 0; }
    FString Summary() const;
};

namespace CireRouteEditor
{
    CIRESTEAMSURVIVAL_API FCireRouteValidation Validate(const UWorld* World, const FCireBattlefieldRoutes& Draft);
    CIRESTEAMSURVIVAL_API ECireRouteReach Reach(const UWorld* World, const FVector& From, const FVector& To, float& OutLength, TArray<FVector>* OutPath = nullptr);
    CIRESTEAMSURVIVAL_API FLinearColor ReachColor(ECireRouteReach Reach);
    CIRESTEAMSURVIVAL_API const TCHAR* ReachLabel(ECireRouteReach Reach);
    /** Insert a waypoint after Index at the middle of that segment (both realms when bLinked). Returns the new index or INDEX_NONE. */
    CIRESTEAMSURVIVAL_API int32 InsertAfter(FCireBattlefieldRoutes& Draft, int32 Team, int32 Index, bool bLinked);
    /** Delete a waypoint (never the breach spawn or the goal point; at least 3 remain). */
    CIRESTEAMSURVIVAL_API bool Delete(FCireBattlefieldRoutes& Draft, int32 Team, int32 Index, bool bLinked);
    /** Move a waypoint to a realm-local point (mirrored to the other realm when bLinked). */
    CIRESTEAMSURVIVAL_API void MovePoint(FCireBattlefieldRoutes& Draft, int32 Team, int32 Index, const FVector2D& Local, bool bLinked);
    /** Move a challenge bay (materialises the automatic bays as authored packs first). Bay is 1-based. */
    CIRESTEAMSURVIVAL_API void MoveBay(const UWorld* World, FCireBattlefieldRoutes& Draft, int32 Team, int32 Bay, const FVector2D& Local, bool bLinked);

    // ---- dev-route-tools: readouts for the map layout editor (CireLayoutEditorHUD.cpp) ----
    /** Typical wave march speed (cm/s): the mean MoveSpeed of the wave composition; OutMin/OutMax give the range. */
    CIRESTEAMSURVIVAL_API float MarchSpeed(float* OutMin = nullptr, float* OutMax = nullptr);
    /** "m:ss" */
    CIRESTEAMSURVIVAL_API FString FormatWalkTime(double Seconds);
#if !UE_BUILD_SHIPPING
    /** Native checks (CireRouteToolsTests.cpp): the 1..16 challenge pack generalisation (parse, rules, JSON, replication,
     *  pack spawning, schedules), the realm frames, and the map layout model (typed team-owned markers, mirroring, links,
     *  renumbering, re-chaining, vendors, validation per team, documents, compile). Logs CIRE_ROUTE_TOOLS_PASS. */
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
#endif
}
