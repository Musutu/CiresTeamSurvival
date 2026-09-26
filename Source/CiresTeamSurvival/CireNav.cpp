// nav-paths: runtime navmesh for the town realms and arenas, path queries and steering (Docs/Navigation.md).
#include "CireNav.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireTownMap.h" // medieval-kingdom
#include "CireNavCache.h" // town-perf
#include "CireArenas.h"
#include "Components/BrushComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "AI/NavigationSystemBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireNav, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarNav(TEXT("cire.Nav"), 1, TEXT("1 = monsters and bots follow navmesh paths; 0 = legacy straight-line steering (comparison/debug)."));
TAutoConsoleVariable<int32> CVarNavBudget(TEXT("cire.NavQueryBudget"), 24, TEXT("Maximum steering path queries per frame per world (others keep their previous path for a frame)."));
TAutoConsoleVariable<int32> CVarNavAvoidance(TEXT("cire.NavAvoidance"), 1, TEXT("1 = steered monsters and bots use RVO crowd avoidance."));

struct FAgent
{
    FVector Goal = FVector::ZeroVector;
    TArray<FVector> Points;
    int32 Next = 1, Stalls = 0, Side = 1;
    bool bValid = false, bPartial = false, bNoNav = false;
    double ComputedAt = -100, NoNavUntil = 0, LastSteerAt = -100, AnchorAt = 0, UnstickUntil = 0, UnreachableSince = -1;
    uint32 NavRev = MAX_uint32;
    float Radius = 0;
    FVector Anchor = FVector::ZeroVector, UnstickDir = FVector::ZeroVector;
    // ranged line-of-sight reposition cache
    TWeakObjectPtr<const AActor> FiringTarget;
    FVector Firing = FVector::ZeroVector;
    double FiringAt = -100;
    bool bFiring = false;
};
struct FWorldNav
{
    FCireNavStats Stats;
    TMap<TObjectKey<ACharacter>, FAgent> Agents;
    TArray<TWeakObjectPtr<AActor>> Volumes;
    CireNav::FCoverage Coverage[2];
    bool bInitialized = false;
    double PruneAt = 0;
};
TMap<TObjectKey<UWorld>, FWorldNav> Worlds;

FWorldNav& WorldNav(const UWorld* World) { return Worlds.FindOrAdd(TObjectKey<UWorld>(World)); }
UNavigationSystemV1* Sys(const UWorld* World)
{
    return World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(const_cast<UWorld*>(World)) : nullptr;
}
double Seconds(const UWorld* World) { return World ? World->GetTimeSeconds() : 0.0; }
double DistToSegment2D(const FVector& P, const FVector& A, const FVector& B)
{
    const FVector2D P2(P), A2(A), B2(B), S = B2 - A2;
    const double Len = S.SizeSquared();
    const double T = Len > 1 ? FMath::Clamp(FVector2D::DotProduct(P2 - A2, S) / Len, 0., 1.) : 0.;
    return FVector2D::Distance(P2, A2 + S * T);
}
bool ConsumeBudget(UWorld* World)
{
    auto* Sub = World ? World->GetSubsystem<UCireNavSubsystem>() : nullptr;
    if (!Sub) return true;
    if (Sub->BudgetFrame != GFrameCounter) { Sub->BudgetFrame = GFrameCounter; Sub->BudgetUsed = 0; }
    if (Sub->BudgetUsed >= FMath::Max(1, CVarNavBudget.GetValueOnGameThread())) return false;
    ++Sub->BudgetUsed; return true;
}
void EnsureAvoidance(ACharacter* A)
{
    auto* Move = A->GetCharacterMovement();
    if (!Move || Move->bUseRVOAvoidance || !CVarNavAvoidance.GetValueOnGameThread()) return;
    Move->AvoidanceConsiderationRadius = 280.f;
    Move->AvoidanceWeight = Cast<ACireMonster>(A) ? .5f : .3f;
    Move->SetAvoidanceEnabled(true);
}
AActor* SpawnBounds(UWorld* World, const FBox& Box, const TCHAR* Label)
{
    FActorSpawnParameters Params; Params.bDeferConstruction = true; Params.ObjectFlags |= RF_Transient;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FTransform At(Box.GetCenter());
    auto* Volume = World->SpawnActor<ANavMeshBoundsVolume>(ANavMeshBoundsVolume::StaticClass(), At, Params);
    if (!Volume) return nullptr;
    // A runtime volume has no editor brush model: its bounds come from a box body setup instead
    // (UBrushComponent::CalcBounds falls back to BrushBodySetup). It has no collision.
    UBrushComponent* Brush = Volume->GetBrushComponent();
    auto* Setup = NewObject<UBodySetup>(Brush, NAME_None, RF_Transient);
    const FVector Size = Box.GetSize();
    Setup->AggGeom.BoxElems.Add(FKBoxElem(Size.X, Size.Y, Size.Z));
    Brush->BrushBodySetup = Setup;
    Volume->FinishSpawning(At);
    // Native components registered during spawn (before the body setup existed): refresh the bounds
    // and hand the navigation system the real box.
    Brush->UpdateBounds();
    if (auto* NS = Sys(World)) NS->OnNavigationBoundsUpdated(Volume);
#if WITH_EDITOR
    Volume->SetActorLabel(Label);
#endif
    UE_LOG(LogCireNav, Display, TEXT("CIRE_NAV_BOUNDS %s min=%s max=%s"), Label, *Box.Min.ToCompactString(), *Box.Max.ToCompactString());
    return Volume;
}
void CountTiles(const UWorld* World, FCireNavStats& Stats)
{
    Stats.Tiles[0] = Stats.Tiles[1] = 0;
    if (auto* NS = Sys(World))
        for (ANavigationData* D : NS->NavDataSet)
            if (auto* Recast = Cast<ARecastNavMesh>(D))
                Stats.Tiles[D->GetConfig().AgentRadius > 60.f ? 1 : 0] = Recast->GetNavMeshTilesCount();
}
void Repath(UWorld* World, ACharacter* A, FAgent& G, const FVector& Goal, float Radius, double Now, FWorldNav& N)
{
    ++N.Stats.Repaths;
    const FVector Pos = A->GetActorLocation();
    G.Goal = Goal; G.ComputedAt = Now; G.NavRev = N.Stats.NavRevision; G.Radius = Radius;
    FVector Start;
    if (!CireNav::Project(World, Pos, Start, FVector(80, 80, 300), Radius))
    {
        // No navmesh under the agent (network client, a test fixture above the town, knocked out of bounds):
        // keep the legacy straight line and look again shortly.
        G.bNoNav = true; G.NoNavUntil = Now + 1.0; G.bValid = false; ++N.Stats.Fallbacks; return;
    }
    G.bNoNav = false;
    FVector End;
    bool bEnd = CireNav::Project(World, Goal, End, FVector(120, 120, 400), Radius);
    if (!bEnd) bEnd = CireNav::Project(World, Goal, End, FVector(450, 450, 900), Radius); // goal on top of or inside a prop
    if (!bEnd)
    {
        G.bValid = false; ++N.Stats.Failed;
        if (G.UnreachableSince < 0) G.UnreachableSince = Now;
        return;
    }
    FCireNavPath Path = CireNav::FindPath(World, Start, End, Radius, true);
    if (!Path.bValid || Path.Points.Num() < 2)
    {
        G.bValid = false;
        if (G.UnreachableSince < 0) G.UnreachableSince = Now;
        return;
    }
    G.Points = MoveTemp(Path.Points); G.Next = 1; G.bValid = true; G.bPartial = Path.bPartial;
    const bool bShort = FVector::Dist2D(G.Points.Last(), Goal) > 160.;
    if (Path.bPartial || bShort) { if (G.UnreachableSince < 0) G.UnreachableSince = Now; }
    else G.UnreachableSince = -1;
}
}

// ---------------------------------------------------------------- setup
void CireNav::Initialize(UWorld* World)
{
    if (!World || !World->IsGameWorld()) return;
    UNavigationSystemV1* NS = Sys(World);
    if (!NS) { UE_LOG(LogCireNav, Display, TEXT("CIRE_NAV_SKIPPED netmode=%d (no navigation system on this peer)"), static_cast<int32>(World->GetNetMode())); return; }
    FWorldNav& N = WorldNav(World);
    if (N.bInitialized) return;
    N.bInitialized = true;
    // The Citadel map's saved navigation config predates runtime navigation; nav data for the
    // configured agents (Hero, Large) must be created when the bounds appear.
    if (FBoolProperty* Auto = FindFProperty<FBoolProperty>(UNavigationSystemV1::StaticClass(), TEXT("bAutoCreateNavigationData")))
        Auto->SetPropertyValue_InContainer(NS, true);
    const auto& R = CireLanePath::Get(World);
    CireNavCache::BeginLoad(NS); // town-perf: a cached town navmesh skips the ~1 minute runtime build
    for (int32 Team = 0; Team < 2; ++Team)
    {
        // medieval-kingdom: realm frame and height range from data (the pack town sits on a landscape).
        const FVector2D O = CireLanePath::RealmOrigin(Team), ZR = CireTownMap::WorldZRange(Team);
        const bool bTown = CireTownMap::IsActive();
        // The whole realm floor from the castle ward's edge wall to past the breach, stopping short of the Sundering Cliff.
        FBox Box(FVector(O.X + R.MinX - (bTown ? 300.f : 3200.f), O.Y - R.HalfWidth - 300.f, ZR.X), FVector(O.X + R.MaxX + 400.f, O.Y + R.HalfWidth + 300.f, ZR.Y));
        if (!bTown) { if (Team == 0) Box.Max.Y = FMath::Min(Box.Max.Y, -150.); else Box.Min.Y = FMath::Max(Box.Min.Y, 150.); }
        if (AActor* V = SpawnBounds(World, Box, Team == 0 ? TEXT("CireNavBounds_Ember") : TEXT("CireNavBounds_Dusk"))) N.Volumes.Add(V);
    }
    {
        // Every arena shares one footprint: one volume covers whichever arena is built.
        FVector2D Half(2600, 2000);
        for (const auto& A : CireArenas::Pool().Arenas) { Half.X = FMath::Max(Half.X, A.HalfExtents.X); Half.Y = FMath::Max(Half.Y, A.HalfExtents.Y); }
        const FVector O = CireArenas::Origin();
        const FBox Box(O + FVector(-Half.X - 300, -Half.Y - 300, -400), O + FVector(Half.X + 300, Half.Y + 300, 900));
        if (AActor* V = SpawnBounds(World, Box, TEXT("CireNavBounds_Arena"))) N.Volumes.Add(V);
    }
    const double Started = FPlatformTime::Seconds();
    // Register the new bounds first (nav data sized from them is created here), then build
    // everything, blocking: both agents over both realms (the arena footprint is empty until an arena exists).
    { TRACE_CPUPROFILER_EVENT_SCOPE(CireNav_RegisterBounds); NS->Tick(0.f); } // town-perf: bounds, nav data, octree
    double CacheMs = 0; int32 CacheTiles = 0;
    const bool bCached = CireNavCache::FinishLoad(World, NS, CacheMs, CacheTiles); // town-perf
    if (!bCached) { TRACE_CPUPROFILER_EVENT_SCOPE(CireNav_Build); NS->Build(); }
    N.Stats.InitialBuildMs = (FPlatformTime::Seconds() - Started) * 1000.0;
    if (!bCached) CireNavCache::Save(World, NS); // town-perf: the next start attaches these tiles instead
    ++N.Stats.NavRevision;
    CountTiles(World, N.Stats);
    UE_LOG(LogCireNav, Display, TEXT("CIRE_NAV_READY netmode=%d navdata=%d build_ms=%.1f tiles_hero=%d tiles_large=%d"), static_cast<int32>(World->GetNetMode()),
        NS->NavDataSet.Num(), N.Stats.InitialBuildMs, N.Stats.Tiles[0], N.Stats.Tiles[1]);
}
bool CireNav::HasNavigation(const UWorld* World)
{
    const UNavigationSystemV1* NS = Sys(World);
    if (!NS) return false;
    for (const ANavigationData* D : NS->NavDataSet) if (IsValid(D)) return true;
    return false;
}
bool CireNav::IsReady(const UWorld* World)
{
    UNavigationSystemV1* NS = Sys(World);
    return NS && HasNavigation(World) && !NS->IsNavigationBuildInProgress();
}
double CireNav::FlushBuild(UWorld* World)
{
    UNavigationSystemV1* NS = Sys(World);
    if (!NS) return 0;
    const double Started = FPlatformTime::Seconds();
    for (int32 Pass = 0; Pass < 40; ++Pass)
    {
        NS->Tick(.1f); // bounds updates, octree updates and dirty areas start tile jobs
        for (ANavigationData* D : NS->NavDataSet) if (D) D->EnsureBuildCompletion();
        if (!NS->IsNavigationBuildInProgress() && Pass > 0) break;
    }
    FWorldNav& N = WorldNav(World);
    ++N.Stats.NavRevision; CountTiles(World, N.Stats);
    if (auto* Sub = World->GetSubsystem<UCireNavSubsystem>()) Sub->bBuilding = false;
    return (FPlatformTime::Seconds() - Started) * 1000.0;
}
ANavigationData* CireNav::NavData(const UWorld* World, float Radius)
{
    UNavigationSystemV1* NS = Sys(World);
    if (!NS) return nullptr;
    ANavigationData *Best = nullptr, *Largest = nullptr; float BestR = TNumericLimits<float>::Max(), LargestR = -1;
    for (ANavigationData* D : NS->NavDataSet)
    {
        if (!IsValid(D)) continue;
        const float R = D->GetConfig().AgentRadius;
        // Small tolerance: a 1.15x tank hero (46 cm) or a slightly scaled monster still uses the Hero mesh.
        if (R >= Radius - 6.f && R < BestR) { Best = D; BestR = R; }
        if (R > LargestR) { Largest = D; LargestR = R; }
    }
    return Best ? Best : Largest;
}
float CireNav::AgentRadius(const ACharacter* Agent)
{
    return Agent && Agent->GetCapsuleComponent() ? Agent->GetCapsuleComponent()->GetScaledCapsuleRadius() : 40.f;
}

// ---------------------------------------------------------------- queries
bool CireNav::Project(const UWorld* World, const FVector& Point, FVector& Out, const FVector& Extent, float Radius)
{
    UNavigationSystemV1* NS = Sys(World);
    ANavigationData* D = NavData(World, Radius);
    if (!NS || !D || Point.ContainsNaN()) return false;
    FNavLocation Location;
    if (!NS->ProjectPointToNavigation(Point, Location, Extent, D)) return false;
    Out = Location.Location; return true;
}
FCireNavPath CireNav::FindPath(const UWorld* World, const FVector& From, const FVector& To, float Radius, bool bAllowPartial)
{
    FCireNavPath Out;
    UNavigationSystemV1* NS = Sys(World);
    ANavigationData* D = NavData(World, Radius);
    Out.Direct = static_cast<float>(FVector::Dist2D(From, To));
    if (!NS || !D || From.ContainsNaN() || To.ContainsNaN()) return Out;
    FWorldNav& N = WorldNav(World);
    const double Started = FPlatformTime::Seconds();
    FPathFindingQuery Query(nullptr, *D, From, To, D->GetDefaultQueryFilter());
    Query.SetAllowPartialPaths(bAllowPartial);
    const FPathFindingResult Result = NS->FindPathSync(Query);
    const double Ms = (FPlatformTime::Seconds() - Started) * 1000.0;
    ++N.Stats.Queries; N.Stats.QueryMs += Ms; N.Stats.PeakQueryMs = FMath::Max(N.Stats.PeakQueryMs, Ms);
    if (!Result.IsSuccessful() || !Result.Path.IsValid()) { ++N.Stats.Failed; return Out; }
    for (const FNavPathPoint& P : Result.Path->GetPathPoints()) Out.Points.Add(P.Location);
    Out.bValid = Out.Points.Num() >= 2;
    Out.bPartial = Result.IsPartial();
    Out.Length = static_cast<float>(Result.Path->GetLength());
    if (Out.bPartial) ++N.Stats.Partial;
    return Out;
}
bool CireNav::Walkable(const UWorld* World, const FVector& From, const FVector& To, float Radius)
{
    ANavigationData* D = NavData(World, Radius);
    if (!D) return false;
    FVector Hit;
    return !D->Raycast(From, To, Hit, D->GetDefaultQueryFilter());
}

// ---------------------------------------------------------------- steering
bool CireNav::IsEnabled() { return CVarNav.GetValueOnGameThread() != 0; }

FVector CireNav::Steer(ACharacter* A, const FVector& Goal, bool* bUsedNav)
{
    if (bUsedNav) *bUsedNav = false;
    if (!IsValid(A)) return FVector::ZeroVector;
    const FVector Pos = A->GetActorLocation();
    const FVector Straight = (Goal - Pos).GetSafeNormal2D();
    UWorld* World = A->GetWorld();
    if (!IsEnabled() || !A->HasAuthority() || Goal.ContainsNaN() || !HasNavigation(World)) return Straight;
    FWorldNav& N = WorldNav(World);
    const double Started = FPlatformTime::Seconds();
    ON_SCOPE_EXIT { N.Stats.SteerMs += (FPlatformTime::Seconds() - Started) * 1000.0; ++N.Stats.SteerCalls; };
    EnsureAvoidance(A);
    const double Now = Seconds(World);
    FAgent& G = N.Agents.FindOrAdd(TObjectKey<ACharacter>(A));
    const float Radius = AgentRadius(A);
    // A unit that stopped steering (fighting, casting, paused) starts a fresh progress window.
    if (Now - G.LastSteerAt > .6) { G.Anchor = Pos; G.AnchorAt = Now; G.Stalls = 0; }
    G.LastSteerAt = Now;
    const double DistGoal = FVector::Dist2D(Pos, Goal);
    if (G.UnstickUntil > Now) { if (bUsedNav) *bUsedNav = true; return G.UnstickDir; }
    if (G.bNoNav && G.NoNavUntil > Now) return Straight;
    bool bRepath = !G.bValid || G.NavRev != N.Stats.NavRevision || FMath::Abs(G.Radius - Radius) > 4.f;
    const double GoalMoved = FVector::Dist2D(G.Goal, Goal);
    if (!bRepath && GoalMoved > FMath::Max(100., .2 * DistGoal)) bRepath = true;
    if (!bRepath && Now - G.ComputedAt > (GoalMoved > 40. ? 2.5 : 10.)) bRepath = true;
    if (!bRepath && G.Points.IsValidIndex(G.Next) && G.Next > 0 && DistToSegment2D(Pos, G.Points[G.Next - 1], G.Points[G.Next]) > 260.) bRepath = true;
    // Progress: no headway for 1.2 s repaths; twice in a row sidesteps briefly (crowd/geometry deadlock).
    if (Now - G.AnchorAt >= 1.2)
    {
        const double Moved = FVector::Dist2D(Pos, G.Anchor);
        G.Anchor = Pos; G.AnchorAt = Now;
        if (Moved < 30. && DistGoal > 120.)
        {
            bRepath = true;
            if (++G.Stalls >= 2)
            {
                G.Stalls = 0; ++N.Stats.Unsticks; G.Side = -G.Side;
                const FVector Ahead = G.bValid && G.Points.IsValidIndex(G.Next) ? (G.Points[G.Next] - Pos).GetSafeNormal2D() : Straight;
                G.UnstickDir = (FVector::CrossProduct(Ahead, FVector::UpVector) * G.Side + Ahead * .35f).GetSafeNormal2D();
                G.UnstickUntil = Now + .6; G.bValid = false;
                if (bUsedNav) *bUsedNav = true;
                return G.UnstickDir;
            }
        }
        else G.Stalls = 0;
    }
    if (bRepath)
    {
        if (ConsumeBudget(World)) Repath(World, A, G, Goal, Radius, Now, N);
        else { ++N.Stats.BudgetDeferred; if (!G.bValid) return Straight; }
    }
    if (G.bNoNav || !G.bValid) return Straight;
    if (bUsedNav) *bUsedNav = true;
    const float Accept = FMath::Max(45.f, Radius * 1.1f);
    while (G.Next < G.Points.Num())
    {
        const FVector& P = G.Points[G.Next];
        const double D = FVector::Dist2D(Pos, P);
        bool bPassed = false;
        if (G.Next > 0 && D < 250.)
        {
            const FVector Segment = (P - G.Points[G.Next - 1]).GetSafeNormal2D();
            bPassed = FVector::DotProduct(Pos - P, Segment) > 0.;
        }
        if (D <= Accept || bPassed) ++G.Next; else break;
    }
    if (G.Next >= G.Points.Num())
    {
        // End of a partial path: hold at the closest reachable point instead of grinding into the obstacle.
        if (G.bPartial || FVector::Dist2D(G.Points.Last(), Goal) > 160.) return FVector::ZeroVector;
        return DistGoal > 20. ? Straight : FVector::ZeroVector;
    }
    const FVector Dir = (G.Points[G.Next] - Pos).GetSafeNormal2D();
    return Dir.IsNearlyZero() ? Straight : Dir;
}
bool CireNav::GoalUnreachable(const ACharacter* A, float SecondsUnreachable)
{
    if (!A) return false;
    const FWorldNav* N = Worlds.Find(TObjectKey<UWorld>(A->GetWorld()));
    const FAgent* G = N ? N->Agents.Find(TObjectKey<ACharacter>(A)) : nullptr;
    return G && !G->bNoNav && G->UnreachableSince >= 0 && Seconds(A->GetWorld()) - G->UnreachableSince >= SecondsUnreachable;
}
bool CireNav::CurrentPath(const ACharacter* A, TArray<FVector>& Out)
{
    Out.Reset();
    const FWorldNav* N = A ? Worlds.Find(TObjectKey<UWorld>(A->GetWorld())) : nullptr;
    const FAgent* G = N ? N->Agents.Find(TObjectKey<ACharacter>(A)) : nullptr;
    if (!G || !G->bValid) return false;
    Out.Add(A->GetActorLocation());
    for (int32 I = G->Next; I < G->Points.Num(); ++I) Out.Add(G->Points[I]);
    return Out.Num() >= 2;
}
void CireNav::Forget(const ACharacter* A)
{
    if (!A) return;
    if (FWorldNav* N = Worlds.Find(TObjectKey<UWorld>(A->GetWorld()))) N->Agents.Remove(TObjectKey<ACharacter>(A));
}
void CireNav::InvalidatePaths(const UWorld* World)
{
    if (FWorldNav* N = Worlds.Find(TObjectKey<UWorld>(World))) for (auto& Pair : N->Agents) { Pair.Value.bValid = false; Pair.Value.FiringAt = -100; }
}
void CireNav::RefreshActor(AActor* Actor)
{
    if (!IsValid(Actor) || !HasNavigation(Actor->GetWorld())) return;
    TInlineComponentArray<UInstancedStaticMeshComponent*> Components(Actor);
    for (UInstancedStaticMeshComponent* C : Components)
        if (C && C->IsRegistered() && C->GetInstanceCount() > 0 && C->IsNavigationRelevant()) FNavigationSystem::UpdateComponentData(*C);
}
bool CireNav::FiringPosition(ACharacter* S, const AActor* T, float Range, FVector& Out)
{
    if (!IsValid(S) || !IsValid(T) || !IsEnabled() || !HasNavigation(S->GetWorld())) return false;
    UWorld* World = S->GetWorld();
    FWorldNav& N = WorldNav(World);
    FAgent& G = N.Agents.FindOrAdd(TObjectKey<ACharacter>(S));
    const double Now = Seconds(World);
    if (G.FiringTarget.Get() == T && Now - G.FiringAt < 1.5) { Out = G.Firing; return G.bFiring; }
    G.FiringTarget = T; G.FiringAt = Now; G.bFiring = false;
    const float Radius = AgentRadius(S);
    const FVector Target = T->GetActorLocation(), From = S->GetActorLocation();
    struct FCandidate { FVector P; double Score; };
    TArray<FCandidate> Candidates;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireNavFiring), false, S);
    Query.AddIgnoredActor(T);
    const float Rings[] = {.8f, .55f, .3f};
    for (float Ring : Rings)
        for (int32 Step = 0; Step < 16; ++Step)
        {
            const float Angle = Step * PI / 8.f;
            const FVector Probe = Target + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Range * Ring;
            FVector OnNav;
            if (!Project(World, Probe, OnNav, FVector(120, 120, 400), Radius) || FVector::Dist2D(OnNav, Target) > Range) continue;
            FHitResult Hit;
            if (World->LineTraceSingleByChannel(Hit, OnNav + FVector(0, 0, 130), Target + FVector(0, 0, 35), ECC_Visibility, Query)) continue;
            Candidates.Add({OnNav, FVector::Dist2D(From, OnNav) + (1.f - Ring) * 120.f});
        }
    Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score < B.Score; });
    double Best = TNumericLimits<double>::Max();
    for (int32 I = 0; I < Candidates.Num() && I < 3; ++I)
    {
        const FCireNavPath Path = FindPath(World, From, Candidates[I].P, Radius, false);
        if (Path.bValid && !Path.bPartial && Path.Length < Best) { Best = Path.Length; G.Firing = Candidates[I].P; G.bFiring = true; }
    }
    Out = G.Firing;
    return G.bFiring;
}

// ---------------------------------------------------------------- stats / overlays
const FCireNavStats& CireNav::Stats(const UWorld* World) { return WorldNav(World).Stats; }
void CireNav::ResetQueryStats(const UWorld* World)
{
    FCireNavStats& S = WorldNav(World).Stats;
    S.Queries = S.Partial = S.Failed = S.Fallbacks = S.Repaths = S.Unsticks = S.BudgetDeferred = 0;
    S.QueryMs = S.PeakQueryMs = S.SteerMs = 0; S.SteerCalls = 0;
}
const CireNav::FCoverage& CireNav::RealmCoverage(const UWorld* World, int32 Team)
{
    FWorldNav& N = WorldNav(World);
    FCoverage& C = N.Coverage[FMath::Clamp(Team, 0, 1)];
    const auto& R = CireLanePath::Get(World);
    const FVector2D O = CireLanePath::RealmOrigin(Team); // medieval-kingdom: realm frame from data
    const FBox2D Bounds(FVector2D(O.X + R.MinX, O.Y - R.HalfWidth), FVector2D(O.X + R.MaxX, O.Y + R.HalfWidth));
    if (C.NavRevision == N.Stats.NavRevision && C.W > 0 && C.Bounds.Min == Bounds.Min && C.Bounds.Max == Bounds.Max) return C;
    C.Bounds = Bounds; C.Cell = CireTownMap::IsActive() ? 400.f : 100.f; // medieval-kingdom: the pack town is ~10x larger
    C.W = FMath::CeilToInt((Bounds.Max.X - Bounds.Min.X) / C.Cell); C.H = FMath::CeilToInt((Bounds.Max.Y - Bounds.Min.Y) / C.Cell);
    C.Cells.Init(0, C.W * C.H);
    if (HasNavigation(World))
        for (int32 Y = 0; Y < C.H; ++Y)
            for (int32 X = 0; X < C.W; ++X)
            {
                FVector Out;
                FVector P(Bounds.Min.X + (X + .5f) * C.Cell, Bounds.Min.Y + (Y + .5f) * C.Cell, 60);
                P.Z += CireTownMap::Ground(World, FVector2D(P)); // medieval-kingdom: 0 on the procedural town
                C.Cells[Y * C.W + X] = Project(World, P, Out, FVector(45, 45, 160), 40.f) ? 1 : 0;
            }
    C.NavRevision = N.Stats.NavRevision;
    return C;
}

// ---------------------------------------------------------------- subsystem
bool UCireNavSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld();
}
void UCireNavSubsystem::Deinitialize()
{
    Worlds.Remove(TObjectKey<UWorld>(GetWorld()));
    Super::Deinitialize();
}
void UCireNavSubsystem::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    UNavigationSystemV1* NS = Sys(World);
    FWorldNav* N = Worlds.Find(TObjectKey<UWorld>(World));
    if (!NS || !N || !N->bInitialized) return;
    // Rebuild tracking: every completed (re)build bumps the revision so cached paths refresh.
    const bool bNow = NS->IsNavigationBuildInProgress();
    const double Real = FPlatformTime::Seconds();
    if (bNow && !bBuilding) { bBuilding = true; BuildStartedAt = Real; }
    else if (!bNow && bBuilding)
    {
        bBuilding = false; ++N->Stats.Rebuilds; ++N->Stats.NavRevision;
        N->Stats.LastRebuildMs = (Real - BuildStartedAt) * 1000.0;
        CountTiles(World, N->Stats);
        UE_LOG(LogCireNav, Display, TEXT("CIRE_NAV_REBUILT ms=%.0f tiles_hero=%d tiles_large=%d revision=%u"), N->Stats.LastRebuildMs, N->Stats.Tiles[0], N->Stats.Tiles[1], N->Stats.NavRevision);
    }
    const double Now = World->GetTimeSeconds();
    if (Now < N->PruneAt) return;
    N->PruneAt = Now + 2.0;
    for (auto It = N->Agents.CreateIterator(); It; ++It)
        if (!It.Key().ResolveObjectPtr() || Now - It.Value().LastSteerAt > 30.0) It.RemoveCurrent();
    // A bot taken over by a player keeps full manual control: no crowd avoidance on player pawns.
    for (TCireActorIterator<ACireHero> It(World); It; ++It)
        if (!It->bBot && It->GetCharacterMovement() && It->GetCharacterMovement()->bUseRVOAvoidance) It->GetCharacterMovement()->SetAvoidanceEnabled(false);
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld NavStatsCommand(TEXT("cire.NavStats"), TEXT("Print navmesh build and path query statistics for this world."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        const FCireNavStats& S = CireNav::Stats(World);
        UE_LOG(LogCireNav, Display, TEXT("CIRE_NAV_STATS build_ms=%.1f rebuilds=%d last_rebuild_ms=%.0f tiles=%d/%d queries=%d avg_ms=%.3f peak_ms=%.3f partial=%d failed=%d fallbacks=%d repaths=%d unsticks=%d deferred=%d steer_calls=%lld steer_ms=%.1f"),
            S.InitialBuildMs, S.Rebuilds, S.LastRebuildMs, S.Tiles[0], S.Tiles[1], S.Queries, S.Queries ? S.QueryMs / S.Queries : 0.0, S.PeakQueryMs, S.Partial, S.Failed,
            S.Fallbacks, S.Repaths, S.Unsticks, S.BudgetDeferred, S.SteerCalls, S.SteerMs);
    }));
#endif
