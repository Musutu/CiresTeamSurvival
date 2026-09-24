#pragma once
// nav-paths: the in-game path editor's model (F8 > Developer > Paths, Docs/Navigation.md).
// Validation is live: route rules (CireLanePath::Validate), navmesh reachability of every segment
// for both agent sizes, and the town builder's route-clearance rule (which props a segment would
// remove). Applying goes through CireLanePath::ApplyLive (server-authoritative).
#include "CoreMinimal.h"
#include "CireLanePath.h"

class UWorld;

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
};

struct CIRESTEAMSURVIVAL_API FCireRouteValidation
{
    bool bRulesOk = false, bNavAvailable = false;
    FString RulesError;
    TArray<FCireRouteSegmentCheck> Segments[2];
    ECireRouteReach BayReach[2][3] = {};
    int32 BayConflicts[2][3] = {};
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
    CIRESTEAMSURVIVAL_API ECireRouteReach Reach(const UWorld* World, const FVector& From, const FVector& To, float& OutLength);
    CIRESTEAMSURVIVAL_API FLinearColor ReachColor(ECireRouteReach Reach);
    CIRESTEAMSURVIVAL_API const TCHAR* ReachLabel(ECireRouteReach Reach);
    /** Insert a waypoint after Index at the middle of that segment (both realms when bLinked). Returns the new index or INDEX_NONE. */
    CIRESTEAMSURVIVAL_API int32 InsertAfter(FCireBattlefieldRoutes& Draft, int32 Team, int32 Index, bool bLinked);
    /** Delete a waypoint (never the breach spawn or the goal point; at least 3 remain). */
    CIRESTEAMSURVIVAL_API bool Delete(FCireBattlefieldRoutes& Draft, int32 Team, int32 Index, bool bLinked);
    /** Move a waypoint to a realm-local point (mirrored to the other realm when bLinked). */
    CIRESTEAMSURVIVAL_API void MovePoint(FCireBattlefieldRoutes& Draft, int32 Team, int32 Index, const FVector2D& Local, bool bLinked);
    /** Move a challenge bay (materialises the three computed bays as overrides first). */
    CIRESTEAMSURVIVAL_API void MoveBay(const UWorld* World, FCireBattlefieldRoutes& Draft, int32 Team, int32 Tier, const FVector2D& Local, bool bLinked);
}
