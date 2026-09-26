#pragma once
#include "CoreMinimal.h"

class ACireMonster;
class ACireGameMode;
class ACireGameState;
class UWorld;

/** dev-route-tools: one authored challenge pack ("bay"): realm-local centre, arena radius and base tier. */
struct FCireChallengeBay
{
    FVector2D Position = FVector2D::ZeroVector;
    /** Arena radius in cm: the dais, the pack's spread and the town-piece clearance around it. */
    float Radius = 450.f;
    /** Base tier 1..10 before round promotions (LootTables.json packSchedule decides when the tier unlocks). */
    int32 Tier = 1;
    static constexpr float DefaultRadius = 450.f, MinRadius = 200.f, MaxRadius = 1500.f;
    static constexpr int32 MaxTier = 10;
    bool operator==(const FCireChallengeBay& Other) const { return Position == Other.Position && Radius == Other.Radius && Tier == Other.Tier; }
    bool operator!=(const FCireChallengeBay& Other) const { return !(*this == Other); }
};

/** layout-wiring: a march path of one realm (realm-local, spawn to goal, merges already followed). */
struct FCireRoutePath
{
    TArray<FVector2D> Points;
    /** Share of its spawn's units when the spawn splits by weight (MapLayout "weight"). */
    float Weight = 1.f;
    /** Index into FCireBattlefieldRoutes::Spawns[Team] (the spawn that owns this path). */
    int32 Spawn = 0;
    FString Id, Name;
    bool operator==(const FCireRoutePath& O) const { return Points == O.Points && Weight == O.Weight && Spawn == O.Spawn && Id == O.Id && Name == O.Name; }
};
/** layout-wiring: an authored spot of one realm (realm-local position, facing, radius). */
struct FCireRouteSpot
{
    FVector2D Position = FVector2D::ZeroVector;
    float Yaw = 0.f, Radius = 0.f;
    /** Monster spawns: split units across the owned paths by path weight (false = evenly). */
    bool bWeighted = false;
    FString Id, Name;
    bool operator==(const FCireRouteSpot& O) const { return Position == O.Position && Yaw == O.Yaw && Radius == O.Radius && bWeighted == O.bWeighted && Id == O.Id && Name == O.Name; }
};

struct FCireBattlefieldRoutes
{
    float MinX = -2350, MaxX = 13000, HalfWidth = 1120;
    // Realm-local points: X along the realm, Y relative to CireLanePath::CenterY(Team). Point 0 is the wave start (the rift).
    TArray<FVector2D> LocalPoints[2];
    // nav-paths: editable lane (road) width, castle goal zone and optional challenge bay overrides (realm-local cm).
    // dev-route-tools: Bays[Team] is empty (three automatic bays placed by path length) or 1..16 authored packs.
    float LaneWidth = 520;
    FVector2D GoalCenter = FVector2D(-1850, 0), GoalSize = FVector2D(900, 1800);
    TArray<FCireChallengeBay> Bays[2];
    static constexpr int32 MaxBays = 16, AutoBays = 3;
    // medieval-kingdom: the pack town's frame ("frame": "castletown"; CireTownMap) and the hero base (realm-local).
    bool bTownFrame = false;
    FVector2D BaseLocal = FVector2D(-1700, 0);
    // Optional spots (realm-local; unset = the old behaviour): hero respawn point (else the base) and boss spawn (else the breach).
    bool bRespawn = false, bBossSpawn = false;
    FVector2D RespawnLocal = FVector2D::ZeroVector, BossLocal = FVector2D::ZeroVector;
    int32 EscortEveryWaves = 4, EscortCount = 1, EscortLeakCost = 1;
    float EscortHealthMultiplier = 6, EscortMoveSpeed = 170;
    // layout-wiring: the map layout editor's markers, compiled per realm (CireMapLayout::CompileRoutes). Empty arrays
    // keep the old single-route behaviour (one spawn at LocalPoints[Team][0], one path = LocalPoints[Team]).
    /** Monster spawns (Monster Spawn markers that target the realm's team). */
    TArray<FCireRouteSpot> Spawns[2];
    /** Every march path. When set, Paths[Team][0].Points == LocalPoints[Team] (LocalPoints stays the primary route). */
    TArray<FCireRoutePath> Paths[2];
    /** Player Spawn (hero spawn + facing), Respawn, Boss / Pack Leader Spawn and Rift / Portal markers. */
    TArray<FCireRouteSpot> PlayerSpawns[2], Respawns[2], Bosses[2], Rifts[2];
    /** Play Bounds polygon (realm-local, shared by both realms; empty = the rectangular realm bounds only). */
    TArray<FVector2D> PlayBounds;
    /** The layout this document was compiled from ("" = the route file alone). */
    FString LayoutName;
    static constexpr int32 MaxPaths = 16, MaxSpawns = 16, MaxSpots = 16;
};

namespace CireLanePath
{
    CIRESTEAMSURVIVAL_API const FCireBattlefieldRoutes& Get(const UWorld* World = nullptr);
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireBattlefieldRoutes& Out, FString& Error);
    CIRESTEAMSURVIVAL_API bool Reload(FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool Reload(UWorld* World, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API void PublishState(ACireGameState* State);
    CIRESTEAMSURVIVAL_API void ReceiveState(ACireGameState* State);
    CIRESTEAMSURVIVAL_API uint32 Revision(const UWorld* World = nullptr);
    CIRESTEAMSURVIVAL_API float CenterY(int32 Team);
    // dev-route-tools + medieval-kingdom: realm frames. The two realms share one layout authored once in realm-local
    // coordinates; each realm maps it into the world through its origin (CireTownMap: CastleTown.json for the pack town,
    // (0, -/+2100) for the procedural town).
    /** World XY of a realm's local origin. */
    CIRESTEAMSURVIVAL_API FVector2D RealmOrigin(int32 Team);
    /** World location -> realm-local XY. */
    CIRESTEAMSURVIVAL_API FVector2D ToLocal(int32 Team, const FVector& World);
    /** Realm-local XY -> world, Z = height above the ground under that point (the pack town's landscape; 0 on the procedural town). */
    CIRESTEAMSURVIVAL_API FVector ToWorld(int32 Team, const FVector2D& Local, float Z = 0);
    /** The team's hero base (spawn, respawn, recall), Z above the ground. */
    CIRESTEAMSURVIVAL_API FVector BasePosition(const UWorld* World, int32 Team, float Z = 110);
    /** Where a dead champion revives ("respawn", else the base), Z above the ground. */
    CIRESTEAMSURVIVAL_API FVector RespawnPosition(const UWorld* World, int32 Team, float Z = 110);
    /** Where wave bosses appear ("boss", else the breach), Z above the ground. */
    CIRESTEAMSURVIVAL_API FVector BossSpawnPosition(const UWorld* World, int32 Team, float Z = 110);
    CIRESTEAMSURVIVAL_API bool Contains(int32 Team, const FVector& Point, float Margin = 0);
    CIRESTEAMSURVIVAL_API bool Contains(const UWorld* World, int32 Team, const FVector& Point, float Margin = 0);
    CIRESTEAMSURVIVAL_API FVector ClampToLane(int32 Team, FVector Point, float Margin = 0);
    CIRESTEAMSURVIVAL_API FVector ClampToLane(const UWorld* World, int32 Team, FVector Point, float Margin = 0);
    CIRESTEAMSURVIVAL_API FVector SpawnPosition(int32 Team, float Z = 110);
    CIRESTEAMSURVIVAL_API FVector SpawnPosition(const UWorld* World, int32 Team, float Z = 110);
    /** World centre of challenge pack Bay (1..BayCount). */
    CIRESTEAMSURVIVAL_API FVector ChallengePosition(const UWorld* World, int32 Team, int32 Bay, float Z = 110);
    /** nav-paths: realm-local challenge bay of a route document (override or computed from path length). Bay is 1-based. */
    CIRESTEAMSURVIVAL_API FVector2D BayPoint(const FCireBattlefieldRoutes& Routes, int32 Team, int32 Bay);
    // dev-route-tools: 1..16 challenge packs per realm, each with its own radius and tier.
    /** Packs in the realm: the authored count, or 3 automatic bays when none are authored. */
    CIRESTEAMSURVIVAL_API int32 BayCount(const FCireBattlefieldRoutes& Routes, int32 Team);
    CIRESTEAMSURVIVAL_API int32 BayCount(const UWorld* World, int32 Team);
    /** Pack Bay (1..BayCount): authored, or the automatic bay (default radius, tier = bay number). */
    CIRESTEAMSURVIVAL_API FCireChallengeBay BayAt(const FCireBattlefieldRoutes& Routes, int32 Team, int32 Bay);
    /** The three automatic bays (75 / 50 / 25 % of the path from the wave start). */
    CIRESTEAMSURVIVAL_API TArray<FCireChallengeBay> AutoBays(const FCireBattlefieldRoutes& Routes, int32 Team);
    CIRESTEAMSURVIVAL_API float ChallengeRadius(const UWorld* World, int32 Team, int32 Bay);
    CIRESTEAMSURVIVAL_API int32 ChallengeTier(const UWorld* World, int32 Team, int32 Bay);
    /** Polyline length in cm. */
    CIRESTEAMSURVIVAL_API double PathLength(const TArray<FVector2D>& Points);
    // Route queries for wave/boss/HUD code (world space, current replicated route of that world).
    /** Ordered marching waypoints from the breach spawn to the castle gate. */
    CIRESTEAMSURVIVAL_API TArray<FVector> RoutePoints(const UWorld* World, int32 Team, float Z = 0);
    CIRESTEAMSURVIVAL_API float RouteLength(const UWorld* World, int32 Team);
    /** Point at Fraction (0 = breach spawn, 1 = castle gate) of the route's path length. Z is absolute (callers pass a unit's height). */
    CIRESTEAMSURVIVAL_API FVector PointAlongRoute(const UWorld* World, int32 Team, float Fraction, float Z = 110);
    /** Path progress 0..1 of the route position nearest to Location (1 = at the castle gate). */
    CIRESTEAMSURVIVAL_API float RouteProgress(const UWorld* World, int32 Team, const FVector& Location);
    /** Centre of the defended castle-gate leak zone (the route's final point). */
    CIRESTEAMSURVIVAL_API FVector GoalPosition(const UWorld* World, int32 Team, float Z = 110);
    CIRESTEAMSURVIVAL_API void InitializeProgress(ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API FVector NextWaypoint(ACireMonster* Monster);
    // nav-paths: route editing (F8 > Paths) and the goal zone.
    /** The authored town route (Content/Data/BattlefieldRoutes.json as shipped): "reset to defaults" in the path editor. */
    CIRESTEAMSURVIVAL_API FCireBattlefieldRoutes TownDefaults();
    /** Every semantic rule ParseJson enforces (bounds, clearance, goal zone, bays, escort ranges). */
    CIRESTEAMSURVIVAL_API bool Validate(const FCireBattlefieldRoutes& Routes, FString& Error);
    CIRESTEAMSURVIVAL_API FString ToJson(const FCireBattlefieldRoutes& Routes);
    CIRESTEAMSURVIVAL_API FString DataPath();
    CIRESTEAMSURVIVAL_API bool LoadFile(FCireBattlefieldRoutes& Out, FString* Error = nullptr, const FString& Path = FString());
    CIRESTEAMSURVIVAL_API bool SaveFile(const FCireBattlefieldRoutes& Routes, FString* Error = nullptr, const FString& Path = FString());
    /** Server-authoritative live edit: validated, published through GameState, props re-check clearance and units re-route. */
    CIRESTEAMSURVIVAL_API bool ApplyLive(UWorld* World, const FCireBattlefieldRoutes& Routes, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool SameLayout(const FCireBattlefieldRoutes& A, const FCireBattlefieldRoutes& B);
    /** Centre and half extents of the castle leak zone (ACireTownGoal follows it). */
    CIRESTEAMSURVIVAL_API FVector GoalZoneCenter(const UWorld* World, int32 Team, float Z = 150);
    CIRESTEAMSURVIVAL_API FVector2D GoalZoneExtent(const UWorld* World);
    CIRESTEAMSURVIVAL_API float LaneWidth(const UWorld* World);
    CIRESTEAMSURVIVAL_API bool ShouldSpawnEscort(int32 Wave);
    CIRESTEAMSURVIVAL_API bool ShouldSpawnEscort(const UWorld* World, int32 Wave);
    CIRESTEAMSURVIVAL_API void ConfigureEscort(ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API void RefreshEscortCollision(ACireMonster* Monster);
    // ---- layout-wiring: multi-path waves and the map layout markers (Docs/MapLayout.md "What the game reads") ----
    /** March paths in the realm (>= 1). */
    CIRESTEAMSURVIVAL_API int32 PathCount(const FCireBattlefieldRoutes& Routes, int32 Team);
    CIRESTEAMSURVIVAL_API int32 PathCount(const UWorld* World, int32 Team);
    /** Realm-local points of path Path (0 = LocalPoints[Team], the primary route). */
    CIRESTEAMSURVIVAL_API const TArray<FVector2D>& PathPoints(const FCireBattlefieldRoutes& Routes, int32 Team, int32 Path);
    /** Monster spawns of the realm (>= 1: the primary route's first point when none are authored). */
    CIRESTEAMSURVIVAL_API TArray<FCireRouteSpot> SpawnSpots(const FCireBattlefieldRoutes& Routes, int32 Team);
    /** Paths a spawn owns (indices into PathPoints). */
    CIRESTEAMSURVIVAL_API TArray<int32> PathsOfSpawn(const FCireBattlefieldRoutes& Routes, int32 Team, int32 Spawn);
    /** Each path's share of a wave (sums to 1): spawns split evenly, each spawn splits across its paths evenly or by weight. */
    CIRESTEAMSURVIVAL_API TArray<double> PathShares(const FCireBattlefieldRoutes& Routes, int32 Team);
    /** Deterministic split: the path unit Slot (0, 1, 2 ... in spawn order) marches down. Largest-deficit apportionment of
     *  PathShares, so every prefix of a wave is as close to the shares as whole units allow. */
    CIRESTEAMSURVIVAL_API int32 PathForSlot(const FCireBattlefieldRoutes& Routes, int32 Team, int32 Slot);
    /** The path whose start is nearest to a realm-local spot (bosses march the path next to their spawn). */
    CIRESTEAMSURVIVAL_API int32 NearestPathStart(const FCireBattlefieldRoutes& Routes, int32 Team, const FVector2D& Local);
    /** World start of a path (its spawn), Z above the ground. */
    CIRESTEAMSURVIVAL_API FVector PathStart(const UWorld* World, int32 Team, int32 Path, float Z = 110);
    CIRESTEAMSURVIVAL_API float PathLengthOf(const UWorld* World, int32 Team, int32 Path);
    CIRESTEAMSURVIVAL_API FVector PointAlongPath(const UWorld* World, int32 Team, int32 Path, float Fraction, float Z = 110);
    CIRESTEAMSURVIVAL_API float PathProgress(const UWorld* World, int32 Team, int32 Path, const FVector& Location);
    /** Nearest realm-local point of a polyline and its distance (cm). */
    CIRESTEAMSURVIVAL_API FVector2D NearestOnPolyline(const TArray<FVector2D>& Points, const FVector2D& Local, double* OutDistance = nullptr);
    /** A unit's own march path (its Lane and LanePath). */
    CIRESTEAMSURVIVAL_API const TArray<FVector2D>& UnitPath(const ACireMonster* Monster);
    /** Distance (cm, planar) from a world location to a unit's march path. */
    CIRESTEAMSURVIVAL_API float DistanceToUnitPath(const ACireMonster* Monster, const FVector& Location);
    /** Hero spawn Slot of a team (Player Spawn markers in order, cycling; else the base with a small spread), with facing. */
    CIRESTEAMSURVIVAL_API FTransform PlayerSpawnTransform(const UWorld* World, int32 Team, int32 Slot, float Z = 110);
    /** Where a dead hero revives: the Respawn marker nearest to Near (else "respawn", else the base). */
    CIRESTEAMSURVIVAL_API FVector RespawnNear(const UWorld* World, int32 Team, const FVector& Near, float Z = 110);
    /** Boss spawn Index (Boss markers cycle; else "boss", else the breach). */
    CIRESTEAMSURVIVAL_API FVector BossSpawnAt(const UWorld* World, int32 Team, int32 Index, float Z = 110);
    /** The realm's Rift / Portal (the PvP transition point). False when none is authored. */
    CIRESTEAMSURVIVAL_API bool RiftTransform(const UWorld* World, int32 Team, FTransform& Out, float Z = 0);
    /** Play Bounds polygon test (realm-local); true when no polygon is authored. */
    CIRESTEAMSURVIVAL_API bool InsidePlayBounds(const FCireBattlefieldRoutes& Routes, const FVector2D& Local);
    CIRESTEAMSURVIVAL_API bool InsidePlayBounds(const UWorld* World, int32 Team, const FVector& Location);
    /** Nearest point inside the play bounds (world, same Z). Unchanged when inside or no polygon. */
    CIRESTEAMSURVIVAL_API FVector ClampToPlayBounds(const UWorld* World, int32 Team, const FVector& Location, float Margin = 0);
    /** Pack an extras block into the replicated float layout / unpack it (exposed for the replication test). */
    CIRESTEAMSURVIVAL_API void PackExtras(const FCireBattlefieldRoutes& Routes, TArray<float>& Out);
    CIRESTEAMSURVIVAL_API bool UnpackExtras(const TArray<float>& In, int32 At, FCireBattlefieldRoutes& Out);
    /** The startup document: the route file (CastleTownRoutes.json / BattlefieldRoutes.json, the provisional default) with
     *  Content/Data/MapLayout.json compiled on top when it belongs to the active map frame. */
    CIRESTEAMSURVIVAL_API bool LoadActive(FCireBattlefieldRoutes& Out, FString* Error = nullptr, FString* Source = nullptr);
    /** Where the active document came from ("MapLayout.json over CastleTownRoutes.json", ...). */
    CIRESTEAMSURVIVAL_API FString ActiveSource();
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
