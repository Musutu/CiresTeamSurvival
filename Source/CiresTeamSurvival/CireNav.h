#pragma once
// nav-paths: runtime navigation for the procedurally built town realms and the arenas
// (Docs/Navigation.md).
//
// The town, its props and the arenas are built at runtime from data, so the navmesh is too:
// CireNav::Initialize spawns one NavMeshBoundsVolume per realm and one for the shared arena
// footprint, and the navigation system (RuntimeGeneration=Dynamic, Config/DefaultEngine.ini)
// generates two agent navmeshes over them: "Hero" (heroes, ordinary monsters, 48 cm radius) and
// "Large" (72 cm: the 1.7x Pack Leader, bosses, oversized wave rows). Collision is what carves the
// mesh: town props, walls, the shrine and the arena collision proxies. Summoned walls do not carve
// it on purpose (monsters walk into them and breach them).
//
// Steer() is the one movement primitive monsters and bots use: it returns the 2D direction to feed
// AddMovementInput, following a cached navmesh path to the goal (repathing when the goal moves,
// the unit leaves its corridor or stops making progress, and after navmesh rebuilds). Without a
// navmesh at the agent (clients, test fixtures far above the town) it degrades to the old straight
// line, so every existing behaviour and fixture keeps working. Server only.
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CireNav.generated.h"

class ACharacter;
class ACireGameMode;
class ANavigationData;
class UWorld;

struct CIRESTEAMSURVIVAL_API FCireNavPath
{
    TArray<FVector> Points;
    bool bValid = false, bPartial = false;
    float Length = 0.f;
    /** Straight 2D distance from start to the requested goal (for detour ratios). */
    float Direct = 0.f;
};

struct CIRESTEAMSURVIVAL_API FCireNavStats
{
    double InitialBuildMs = 0.0;       // first full synchronous build (both agents, both realms + arena bounds)
    double LastRebuildMs = 0.0;        // most recent dynamic rebuild (dirty -> idle), wall clock
    int32 Rebuilds = 0;
    int32 Queries = 0, Partial = 0, Failed = 0, Fallbacks = 0, Repaths = 0, Unsticks = 0, BudgetDeferred = 0;
    double QueryMs = 0.0, PeakQueryMs = 0.0;
    double SteerMs = 0.0;              // total time inside Steer (includes queries)
    int64 SteerCalls = 0;
    int32 Tiles[2] = {0, 0};           // Hero, Large tile counts after the last build
    uint32 NavRevision = 0;            // bumps whenever a (re)build completes
};

namespace CireNav
{
    /** Server/standalone: spawn the realm + arena bounds and build both agent navmeshes (blocking, timed). */
    CIRESTEAMSURVIVAL_API void Initialize(UWorld* World);
    /** True when this world has navigation data (false on network clients). */
    CIRESTEAMSURVIVAL_API bool HasNavigation(const UWorld* World);
    /** True while no tile rebuild is pending. */
    CIRESTEAMSURVIVAL_API bool IsReady(const UWorld* World);
    /** Development/tests: process pending dirty areas and block until every navmesh is up to date. Returns ms spent. */
    CIRESTEAMSURVIVAL_API double FlushBuild(UWorld* World);
    /** Navmesh for an agent radius (Hero <= 48 cm, else Large). */
    CIRESTEAMSURVIVAL_API ANavigationData* NavData(const UWorld* World, float AgentRadius = 40.f);
    CIRESTEAMSURVIVAL_API float AgentRadius(const ACharacter* Agent);

    // ---- queries ----
    CIRESTEAMSURVIVAL_API bool Project(const UWorld* World, const FVector& Point, FVector& Out, const FVector& Extent = FVector(60, 60, 260), float AgentRadius = 40.f);
    CIRESTEAMSURVIVAL_API FCireNavPath FindPath(const UWorld* World, const FVector& From, const FVector& To, float AgentRadius = 40.f, bool bAllowPartial = true);
    /** Navmesh raycast: true when the straight line from From to To stays on the navmesh. */
    CIRESTEAMSURVIVAL_API bool Walkable(const UWorld* World, const FVector& From, const FVector& To, float AgentRadius = 40.f);

    // ---- steering (server) ----
    /** Direction toward Goal along the navmesh path (straight line when no navmesh covers the agent). */
    CIRESTEAMSURVIVAL_API FVector Steer(ACharacter* Agent, const FVector& Goal, bool* bUsedNav = nullptr);
    /** The goal Steer is chasing cannot be reached (partial/no path) and has not been for Seconds. */
    CIRESTEAMSURVIVAL_API bool GoalUnreachable(const ACharacter* Agent, float Seconds = 2.f);
    /** Current path of an agent (debug draw). */
    CIRESTEAMSURVIVAL_API bool CurrentPath(const ACharacter* Agent, TArray<FVector>& Out);
    CIRESTEAMSURVIVAL_API void Forget(const ACharacter* Agent);
    /** Invalidate every cached path of this world (route edits, rebuilds). */
    CIRESTEAMSURVIVAL_API void InvalidatePaths(const UWorld* World);
    /**
     * Re-register an actor's instanced collision with the navigation octree after its instances were
     * added one by one (arena stages, re-placed town pieces). An ISM registers with navigation on its
     * first instance, and the octree element can keep that first instance's bounds, so later instances
     * would not carve tiles outside them.
     */
    CIRESTEAMSURVIVAL_API void RefreshActor(AActor* Actor);
    /**
     * Ranged line-of-sight reposition: a navmesh point within Range of Target that can see it
     * (visibility trace from eye height), cheapest to walk to for Shooter. Cached ~1.5 s per shooter.
     */
    CIRESTEAMSURVIVAL_API bool FiringPosition(ACharacter* Shooter, const AActor* Target, float Range, FVector& Out);

    CIRESTEAMSURVIVAL_API const FCireNavStats& Stats(const UWorld* World);
    CIRESTEAMSURVIVAL_API void ResetQueryStats(const UWorld* World);
    /** Development switch (cire.Nav 0/1): false restores the old straight-line steering everywhere. */
    CIRESTEAMSURVIVAL_API bool IsEnabled();

    /** Minimap/editor overlay: navmesh coverage sampled on a grid over a realm (rebuilt when the navmesh changes). */
    struct FCoverage { FBox2D Bounds = FBox2D(ForceInit); float Cell = 100.f; int32 W = 0, H = 0; TArray<uint8> Cells; uint32 NavRevision = MAX_uint32; };
    CIRESTEAMSURVIVAL_API const FCoverage& RealmCoverage(const UWorld* World, int32 Team);

#if !UE_BUILD_SHIPPING
    /** Native checks (part of -CireCombatExpansionProbe): coverage, paths, prop blocking, arena paths, editor. */
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
    /** -CireNavProbe: timed march + performance probe run from the game mode tick. */
    CIRESTEAMSURVIVAL_API void InitializeProbe(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API bool TickProbe(ACireGameMode* Mode, float Delta);
    /** -CireNavGallery: navmesh over the town, the path editor dragging a waypoint, an arena navmesh. */
    CIRESTEAMSURVIVAL_API bool TickGallery(ACireGameMode* Mode);
#endif
}

/** Per-world navigation bookkeeping: build timing, revision tracking and path caches. */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireNavSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCireNavSubsystem, STATGROUP_Tickables); }
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Deinitialize() override;
    bool bBuilding = false;
    double BuildStartedAt = 0.0;
    uint64 BudgetFrame = 0;
    int32 BudgetUsed = 0;
};
