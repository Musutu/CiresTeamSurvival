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
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
