// nav-paths: native navigation checks (part of -CireCombatExpansionProbe; alone with -CireNavTests),
// the timed march/performance probe (-CireNavProbe) and the capture gallery (-CireNavGallery).
// Runner: Tools/RunNavChecks.py. See Docs/Navigation.md.
#include "CireNav.h"
#include "CireGame.h"
#include "CireArenas.h"
#include "CireEnvironmentProps.h"
#include "CireLanePath.h"
#include "CireTownMap.h" // medieval-kingdom
#include "CireNPCCombat.h"
#include "CireRouteEditor.h"
#include "CireThreat.h"
#include "CireWaves.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireNavTests, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
ACireWorld* TownActor(UWorld* World)
{
    for (TActorIterator<ACireWorld> It(World); It; ++It) return *It;
    return nullptr;
}
bool OnNav(const UWorld* World, const FVector& P, float Radius = 40.f, const FVector& Extent = FVector(40, 40, 300))
{
    FVector Out; return CireNav::Project(World, P, Out, Extent, Radius);
}
}

bool CireNav::RunTests(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    UWorld* World = Mode->GetWorld();
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool bValue, const FString& Why) { ++Checks; if (!bValue) { bPass = false; UE_LOG(LogCireNavTests, Error, TEXT("CIRE_NAV_CHECK_FAIL %s"), *Why); } };
    const double FlushMs = FlushBuild(World);
    const FCireNavStats& S = Stats(World);
    UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_TEST_BUILD initial_ms=%.1f flush_ms=%.1f tiles_hero=%d tiles_large=%d"), S.InitialBuildMs, FlushMs, S.Tiles[0], S.Tiles[1]);
    Check(HasNavigation(World), TEXT("the server world has navigation data"));
    ANavigationData* Hero = NavData(World, 40.f);
    ANavigationData* Large = NavData(World, 66.f);
    Check(Hero && Large && Hero != Large, TEXT("Hero and Large agent navmeshes both exist"));
    Check(S.Tiles[0] > 20 && S.Tiles[1] > 20, FString::Printf(TEXT("both navmeshes cover the realms with tiles (hero=%d large=%d)"), S.Tiles[0], S.Tiles[1]));

    // ---- coverage and paths in both realms ----------------------------------------------------------
    for (int32 Team = 0; Team < 2; ++Team)
    {
        const float Length = CireLanePath::RouteLength(World, Team);
        int32 Samples = 0, Covered = 0, CoveredLarge = 0;
        for (float D = 0; D <= Length; D += 100.f)
        {
            const FVector P = CireLanePath::PointAlongRoute(World, Team, D / Length, 60.f);
            ++Samples; Covered += OnNav(World, P) ? 1 : 0; CoveredLarge += OnNav(World, P, 66.f, FVector(60, 60, 300)) ? 1 : 0;
        }
        UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_TEST_COVERAGE team=%d samples=%d hero=%d large=%d"), Team, Samples, Covered, CoveredLarge);
        Check(Covered == Samples, FString::Printf(TEXT("team %d: every route sample (100 cm) is on the Hero navmesh (%d/%d)"), Team, Covered, Samples));
        Check(CoveredLarge >= Samples * 97 / 100, FString::Printf(TEXT("team %d: the Large navmesh covers the march road (%d/%d)"), Team, CoveredLarge, Samples));
        const FVector Spawn = CireLanePath::SpawnPosition(World, Team, 110), Goal = CireLanePath::GoalPosition(World, Team, 110);
        for (const float Radius : {40.f, 66.f})
        {
            FVector A, B;
            const bool bA = Project(World, Spawn, A, FVector(80, 80, 300), Radius), bB = Project(World, Goal, B, FVector(80, 80, 300), Radius);
            const FCireNavPath Path = bA && bB ? FindPath(World, A, B, Radius, true) : FCireNavPath();
            UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_TEST_PATH team=%d radius=%.0f valid=%d partial=%d length=%.0f route=%.0f points=%d"), Team, Radius, Path.bValid, Path.bPartial, Path.Length, Length, Path.Points.Num());
            Check(Path.bValid && !Path.bPartial && Path.Length < Length * 1.3f,
                FString::Printf(TEXT("team %d radius %.0f: full path from the breach to the castle gate"), Team, Radius));
        }
        // Heroes spawn in the castle bailey and walk out to the lane.
        {
            FVector A, B;
            const bool bOk = Project(World, Mode->BasePosition(Team), A, FVector(80, 80, 300), 46.f) && Project(World, CireLanePath::PointAlongRoute(World, Team, .5f, 60), B, FVector(80, 80, 300), 46.f);
            const FCireNavPath Path = bOk ? FindPath(World, A, B, 46.f, false) : FCireNavPath();
            Check(Path.bValid && !Path.bPartial, FString::Printf(TEXT("team %d: hero spawn to mid-route path"), Team));
        }
        for (int32 Bay = 1, Bays = CireLanePath::BayCount(World, Team); Bay <= Bays; ++Bay) // dev-route-tools: 1..16 packs
        {
            float Len = 0;
            const FVector Pack = CireLanePath::ChallengePosition(World, Team, Bay, 60);
            const ECireRouteReach Reach = CireRouteEditor::Reach(World, CireLanePath::PointAlongRoute(World, Team, CireLanePath::RouteProgress(World, Team, Pack), 60), Pack, Len);
            Check(Reach == ECireRouteReach::Direct || Reach == ECireRouteReach::Detour, FString::Printf(TEXT("team %d: challenge bay %d reachable from the route (%s)"), Team, Bay, CireRouteEditor::ReachLabel(Reach)));
        }
    }

    // ---- props carve the navmesh (the shrine above all) ---------------------------------------------
    if (ACireWorld* Town = TownActor(World))
    {
        int32 Tested = 0, Blocked = 0, Shrines = 0, ShrinesBlocked = 0;
        for (const auto& P : CireEnvironmentProps::PlacedProps(Town))
        {
            if (!P.bCollision || !P.LocalBox.IsValid) continue;
            const FVector Size = P.LocalBox.GetSize() * P.Transform.GetScale3D();
            if (FMath::Min(Size.X, Size.Y) < 160 || Size.Z < 100) continue;
            // Built to be walked through or under: the gate passages and the covered market hall.
            if (P.Slot == TEXT("gatehouse") || P.Slot == TEXT("castle_gate") || P.Slot == TEXT("market_hall")) continue;
            const FVector Centre = P.Transform.TransformPosition(P.LocalBox.GetCenter() * FVector(1, 1, 0));
            // Ground level only (a walkable roof island is not "the ground under the prop"). A navmesh
            // island sealed inside a hollow building shell does not count: nothing can walk in.
            FVector Inside;
            bool bOnNav = Project(World, FVector(Centre.X, Centre.Y, 20), Inside, FVector(20, 20, 40), 40.f);
            if (bOnNav)
            {
                FVector Road;
                const FVector Near = CireLanePath::PointAlongRoute(World, P.Team, CireLanePath::RouteProgress(World, P.Team, Centre), 60);
                const FCireNavPath Walk = Project(World, Near, Road, FVector(150, 150, 300), 40.f) ? FindPath(World, Road, Inside, 40.f, true) : FCireNavPath();
                bOnNav = Walk.bValid && !Walk.bPartial && FVector::Dist2D(Walk.Points.Last(), Inside) < 60.;
            }
            ++Tested; Blocked += bOnNav ? 0 : 1;
            if (bOnNav)
            {
                FHitResult Hit;
                const bool bHit = World->LineTraceSingleByChannel(Hit, FVector(Centre.X, Centre.Y, 2000), FVector(Centre.X, Centre.Y, -500), ECC_WorldStatic);
                UE_LOG(LogCireNavTests, Warning, TEXT("CIRE_NAV_TEST_PROP_UNCARVED %s team=%d centre=%s size=%s trace=%d z=%.0f comp=%s"), *P.Slot.ToString(), P.Team, *Centre.ToCompactString(), *Size.ToCompactString(),
                    bHit ? 1 : 0, bHit ? Hit.ImpactPoint.Z : 0.f, bHit ? *GetNameSafe(Hit.GetComponent()) : TEXT("-"));
            }
            if (P.Slot == TEXT("shrine")) { ++Shrines; ShrinesBlocked += bOnNav ? 0 : 1; }
        }
        UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_TEST_PROPS tested=%d blocked=%d shrines=%d/%d"), Tested, Blocked, ShrinesBlocked, Shrines);
        Check(Shrines == 2 && ShrinesBlocked == 2, TEXT("the town shrine blocks the navmesh in both realms"));
        Check(Tested > 50 && Blocked >= Tested * 95 / 100, FString::Printf(TEXT("large colliding town pieces carve the navmesh (%d/%d)"), Blocked, Tested));
        // A path past the shrine walks around it, never through it.
        for (const auto& P : CireEnvironmentProps::PlacedProps(Town))
            if (P.Slot == TEXT("shrine"))
            {
                const FVector C = P.Transform.GetLocation();
                FVector A, B;
                const bool bOk = Project(World, C + FVector(-450, 0, 60), A, FVector(120, 120, 300), 40.f) && Project(World, C + FVector(450, 0, 60), B, FVector(120, 120, 300), 40.f);
                const FCireNavPath Path = bOk ? FindPath(World, A, B, 40.f, false) : FCireNavPath();
                bool bClear = Path.bValid;
                for (int32 I = 0; bClear && I + 1 < Path.Points.Num(); ++I)
                    for (int32 K = 0; K <= 10; ++K)
                        if (FVector::Dist2D(FMath::Lerp(Path.Points[I], Path.Points[I + 1], K / 10.f), C) < 90.f) bClear = false;
                Check(bClear, FString::Printf(TEXT("team %d: a path across the square goes around the shrine"), P.Team));
            }
    }
    else Check(false, TEXT("the town world actor exists"));

    // ---- steering fallback without a navmesh (fixtures far above the town keep the straight line) ------
    {
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M = World->SpawnActor<ACireMonster>(FVector(5000, -2100, 5000), FRotator::ZeroRotator, Params);
        if (M)
        {
            M->SetActorTickEnabled(false); M->GetCharacterMovement()->GravityScale = 0.f;
            const FVector Dir = Steer(M, FVector(4000, -2100, 5000));
            Check(Dir.Equals(FVector(-1, 0, 0), .01f), TEXT("steering without navmesh under the agent keeps the straight line"));
            Forget(M); M->Destroy();
        }
        // On the navmesh: steering toward the far side of the shrine does not aim through it.
        // The whole native suite runs inside one frame: give this check a fresh per-frame query budget.
        if (auto* Sub = World->GetSubsystem<UCireNavSubsystem>()) Sub->BudgetUsed = 0;
        auto* N = World->SpawnActor<ACireMonster>(CireLanePath::SpawnPosition(World, 0, 110), FRotator::ZeroRotator, Params);
        if (N)
        {
            N->SetActorTickEnabled(false);
            bool bUsed = false; const FVector Dir = Steer(N, CireLanePath::GoalPosition(World, 0, 110), &bUsed);
            Check(bUsed && !Dir.IsNearlyZero(), TEXT("steering on the navmesh follows a path"));
            Forget(N); N->Destroy();
        }
    }

    // ---- arenas: every arena in the rotation gets navmesh, spawns connect, blockers carve --------------
    for (const int32 Index : CireArenas::Rotation())
    {
        const auto* A = CireArenas::Get(Index);
        if (!A) continue;
        CireArenas::Force(World, Index, false);
        const double Ms = FlushBuild(World);
        int32 SpawnsOn = 0, Pairs = 0, PairsOk = 0;
        for (int32 Team = 0; Team < 2; ++Team)
            for (int32 Slot = 0; Slot < A->Spawns[Team].Num(); ++Slot)
            {
                const FVector P = CireArenas::SpawnLocation(Index, Team, Slot, 60);
                SpawnsOn += OnNav(World, P, 46.f, FVector(80, 80, 300)) ? 1 : 0;
                // Each spawn reaches the arena centre and its mirrored opponent.
                for (const FVector& To : {CireArenas::Origin() + FVector(0, 0, 60), CireArenas::SpawnLocation(Index, 1 - Team, Slot, 60)})
                {
                    FVector From, End;
                    const bool bOk = Project(World, P, From, FVector(80, 80, 300), 46.f) && Project(World, To, End, FVector(250, 250, 400), 46.f);
                    const FCireNavPath Path = bOk ? FindPath(World, From, End, 46.f, false) : FCireNavPath();
                    ++Pairs; PairsOk += Path.bValid && !Path.bPartial ? 1 : 0;
                }
            }
        int32 Blockers = 0, Carved = 0;
        for (const auto& F : CireArenas::Footprints(*A, CireArenas::Pool()))
        {
            if (FMath::Min(F.Extent.X, F.bRound ? F.Extent.X : F.Extent.Y) < 90 || F.Height < 100) continue;
            ++Blockers;
            const FVector C = CireArenas::Origin() + FVector(F.Center.X, F.Center.Y, 20);
            FVector Where, From;
            bool bOn = Project(World, C, Where, FVector(20, 20, 40), 40.f);
            // Recast voxelises surfaces, not solids: a blocker taller than an agent leaves a sealed
            // island inside its walls. Sealed means no path in from the arena floor, which is what matters.
            if (bOn && Project(World, CireArenas::SpawnLocation(Index, 0, 0, 60), From, FVector(80, 80, 300), 40.f))
            {
                const FCireNavPath In = FindPath(World, From, Where, 40.f, true);
                bOn = In.bValid && !In.bPartial && FVector::Dist2D(In.Points.Last(), Where) < 60.;
            }
            Carved += bOn ? 0 : 1;
            if (bOn)
            {
                FHitResult Hit;
                const bool bHit = World->LineTraceSingleByChannel(Hit, C + FVector(0, 0, 2000), C - FVector(0, 0, 500), ECC_WorldStatic);
                UE_LOG(LogCireNavTests, Warning, TEXT("CIRE_NAV_TEST_ARENA_UNCARVED %s centre=%s ext=%s h=%.0f round=%d nav=%s trace=%d z=%.0f comp=%s"), *A->Id.ToString(), *C.ToCompactString(),
                    *F.Extent.ToString(), F.Height, F.bRound ? 1 : 0, *Where.ToCompactString(), bHit ? 1 : 0, bHit ? Hit.ImpactPoint.Z : 0.f, bHit ? *GetNameSafe(Hit.GetComponent()) : TEXT("-"));
            }
        }
        const int32 Spawns = A->Spawns[0].Num() + A->Spawns[1].Num();
        UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_TEST_ARENA %s build_ms=%.0f spawns_on_nav=%d/%d paths=%d/%d blockers_carved=%d/%d"), *A->Id.ToString(), Ms, SpawnsOn, Spawns, PairsOk, Pairs, Carved, Blockers);
        Check(SpawnsOn == Spawns, FString::Printf(TEXT("%s: every hero spawn is on the navmesh"), *A->Id.ToString()));
        Check(Pairs > 0 && PairsOk == Pairs, FString::Printf(TEXT("%s: every spawn paths to the centre and to the opposing spawn (%d/%d)"), *A->Id.ToString(), PairsOk, Pairs));
        Check(Blockers > 0 && Carved >= Blockers * 9 / 10, FString::Printf(TEXT("%s: blockers carve the navmesh (%d/%d)"), *A->Id.ToString(), Carved, Blockers));
    }
    CireArenas::ReleaseForce(World);
    FlushBuild(World);
    Check(!OnNav(World, CireArenas::Origin() + FVector(0, 0, 60), 40.f, FVector(200, 200, 300)), TEXT("clearing the arena removes its navmesh"));

    // ---- path editor: validate, apply live (props re-check clearance, units re-route), save/load -----
    {
        const FCireBattlefieldRoutes Original = CireLanePath::Get(World);
        const uint32 RevisionBefore = CireLanePath::Revision(World);
        ON_SCOPE_EXIT { FString Ignore; CireLanePath::ApplyLive(World, Original, &Ignore); if (ACireWorld* Town = TownActor(World)) Town->Tick(.25f); FlushBuild(World); };
        const FCireRouteValidation Current = CireRouteEditor::Validate(World, Original);
        UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_TEST_EDITOR current=%s ms=%.1f"), *Current.Summary(), Current.Ms);
        Check(Current.Passed(), FString::Printf(TEXT("the authored route validates: %s"), *Current.Summary()));
        FCireBattlefieldRoutes Draft = Original;
        // Move the market-exit waypoint across the square and insert a point; linked realms stay mirrored.
        CireRouteEditor::MovePoint(Draft, 0, 5, Draft.LocalPoints[0][5] + FVector2D(0, -300), true);
        const int32 Inserted = CireRouteEditor::InsertAfter(Draft, 0, 2, true);
        Draft.LaneWidth = 600;
        Check(Inserted == 3 && Draft.LocalPoints[0] == Draft.LocalPoints[1] && Draft.LocalPoints[0].Num() == Original.LocalPoints[0].Num() + 1, TEXT("linked edits keep both realms mirrored"));
        const FCireRouteValidation Edited = CireRouteEditor::Validate(World, Draft);
        UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_TEST_EDITOR edited=%s"), *Edited.Summary());
        Check(Edited.bRulesOk && Edited.Segments[0].Num() == Draft.LocalPoints[0].Num() - 1, TEXT("an edited draft validates segment by segment"));
        FCireBattlefieldRoutes Bad = Draft; Bad.LocalPoints[1][4] = FVector2D(6000, 5000);
        FString Error;
        Check(!CireLanePath::ApplyLive(World, Bad, &Error) && CireLanePath::Revision(World) == RevisionBefore, TEXT("an out-of-realm edit is rejected and the live route is kept"));
        // A live monster re-routes onto the new route without teleporting.
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M = World->SpawnActor<ACireMonster>(CireLanePath::PointAlongRoute(World, 0, .3f, 110), FRotator::ZeroRotator, Params);
        if (M) { M->SetActorTickEnabled(false); M->Lane = 0; CireLanePath::InitializeProgress(M); }
        Check(CireLanePath::ApplyLive(World, Draft, &Error) && CireLanePath::Revision(World) == RevisionBefore + 1, FString::Printf(TEXT("a valid edit applies live: %s"), *Error));
        auto* State = World->GetGameState<ACireGameState>();
        Check(State && State->LanePoints0 == Draft.LocalPoints[0] && State->LaneLayout.Num() >= 7 && FMath::IsNearlyEqual(State->LaneLayout[0], 600.f), TEXT("the edit is published through the game state (points, lane width)"));
        if (ACireWorld* Town = TownActor(World)) { Town->Tick(.25f); Check(Town->RenderedRouteRevision == CireLanePath::Revision(World) && CireEnvironmentProps::HasSafeClearance(Town), TEXT("town pieces re-check route clearance after the edit")); }
        if (M)
        {
            const FVector Before = M->GetActorLocation(); CireLanePath::NextWaypoint(M);
            Check(M->LaneRouteRevision == CireLanePath::Revision(World) && M->GetActorLocation().Equals(Before), TEXT("units reproject onto the edited route without teleporting"));
            M->Destroy();
        }
        FlushBuild(World);
        const FCireRouteValidation Applied = CireRouteEditor::Validate(World, CireLanePath::Get(World));
        UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_TEST_EDITOR applied=%s"), *Applied.Summary());
        Check(Applied.Passed(), FString::Printf(TEXT("after the rebuild every edited segment has a path: %s"), *Applied.Summary()));
        const FString Temp = FPaths::ProjectSavedDir() / TEXT("NavChecks/route-roundtrip.json");
        FCireBattlefieldRoutes Loaded;
        Check(CireLanePath::SaveFile(Draft, &Error, Temp) && CireLanePath::LoadFile(Loaded, &Error, Temp) && CireLanePath::SameLayout(Loaded, Draft), TEXT("save and load round-trip the edited route"));
        FCireBattlefieldRoutes Shipped;
        Check(CireLanePath::LoadFile(Shipped, &Error) && CireLanePath::SameLayout(Shipped, CireLanePath::TownDefaults()), TEXT("BattlefieldRoutes.json equals the editor's town defaults"));
        IFileManager::Get().Delete(*Temp);
    }
    UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_%s checks=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks);
    return bPass;
}

// =============================================================== -CireNavProbe: march + performance
namespace
{
struct FProbeUnit { TWeakObjectPtr<ACireMonster> M; int32 Team = 0; FName Archetype; float SpawnedAt = 0, LeakedAt = -1, Still = 0, MaxStill = 0, Radius = 0; bool bLeaked = false; };
struct FProbe
{
    bool bEnabled = false, bDone = false, bPass = true;
    int32 Stage = 0;
    float Clock = 0, StageAt = 0;
    TArray<FProbeUnit> Units;
    int32 Nudges0 = 0, Marches0 = 0, Despawns0 = 0, Lives0[2] = {0, 0};
    TArray<double> Frames;
    double LastReal = 0, SteerMs0 = 0; int32 Queries0 = 0; int64 SteerCalls0 = 0;
    TArray<FString> Lines;
    FString Summary;
};
FProbe Probe;
void Note(const FString& Line) { UE_LOG(LogCireNavTests, Display, TEXT("%s"), *Line); Probe.Lines.Add(Line); }
void Fail(const FString& Why) { Probe.bPass = false; UE_LOG(LogCireNavTests, Error, TEXT("CIRE_NAV_PROBE_CHECK_FAIL %s"), *Why); Probe.Lines.Add(TEXT("FAIL ") + Why); }
ACireMonster* SpawnMarcher(ACireGameMode* Mode, int32 Team, FName Archetype, int32 Slot)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Start = CireLanePath::SpawnPosition(Mode->GetWorld(), Team);
    const FVector At = CireLanePath::ClampToLane(Mode->GetWorld(), Team, Start + FVector((Slot / 5) * 160.f, (Slot % 5 - 2) * 150.f, 0), 80);
    auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), At, FRotator(0, 180, 0), Params);
    if (!M) return nullptr;
    M->Lane = Team;
    CireNPCCombat::ConfigureArchetype(M, Archetype, 1, 0, 1, false);
    Mode->Monsters.Add(M);
    return M;
}
double Percentile(TArray<double> Values, double P)
{
    if (Values.IsEmpty()) return 0;
    Values.Sort();
    return Values[FMath::Clamp(FMath::FloorToInt(P * (Values.Num() - 1)), 0, Values.Num() - 1)];
}
}

void CireNav::InitializeProbe(ACireGameMode* Mode)
{
    Probe = FProbe();
    Probe.bEnabled = Mode && FParse::Param(FCommandLine::Get(), TEXT("CireNavProbe"));
    if (!Probe.bEnabled) return;
    Mode->BotFillTimer = 0;
    Note(TEXT("CIRE_NAV_PROBE_READY"));
}

bool CireNav::TickProbe(ACireGameMode* Mode, float Delta)
{
    if (!Probe.bEnabled || Probe.bDone || !Mode) return false;
    UWorld* World = Mode->GetWorld();
    auto* State = Mode->GetGameState<ACireGameState>();
    Probe.Clock += Delta;
    Mode->WaveTimer = 1.e6f; // normal waves stay off: the probe spawns its own units
    for (auto* H : Mode->Heroes) if (IsValid(H) && !H->bBot) { H->Draft(2); H->bBot = true; H->bAutoAttack = true; H->HeroName = TEXT("Probe player"); }
    const double Real = FPlatformTime::Seconds();
    const double FrameMs = Probe.LastReal > 0 ? (Real - Probe.LastReal) * 1000.0 : 0.0;
    Probe.LastReal = Real;
    auto Finish = [&]()
    {
        Probe.bDone = true;
        Note(FString::Printf(TEXT("CIRE_NAV_PROBE_%s %s"), Probe.bPass ? TEXT("PASS") : TEXT("FAIL"), *Probe.Summary));
        FString Path;
        if (!FParse::Value(FCommandLine::Get(), TEXT("CireNavProbeSummary="), Path)) Path = FPaths::ProjectSavedDir() / TEXT("NavChecks/probe.txt");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        FFileHelper::SaveStringToFile(FString::Join(Probe.Lines, TEXT("\n")) + TEXT("\n"), *Path);
        FPlatformMisc::RequestExitWithStatus(false, Probe.bPass ? 0 : 1);
    };
    switch (Probe.Stage)
    {
    case 0: // wait for bots, the navmesh and survival
    {
        // medieval-kingdom: the pack town streams its nested Level Instances after startup and the navmesh rebuilds under them.
        if (!Mode->bBotsFilled || Mode->Heroes.Num() < 10 || !IsReady(World) || !State || State->Phase != 0)
        {
            if (Probe.Clock > (CireTownMap::IsActive() ? 400.f : 60.f))
            {
                Fail(FString::Printf(TEXT("setup timed out (bots=%d heroes=%d navReady=%d phase=%d)"), Mode->bBotsFilled ? 1 : 0, Mode->Heroes.Num(), IsReady(World) ? 1 : 0, State ? int32(State->Phase) : -1));
                Finish();
            }
            return false;
        }
        // March test: heroes stand aside (undrafted heroes are ignored by monsters) so only pathing is measured.
        for (auto* H : Mode->Heroes) if (IsValid(H)) { H->bDrafted = false; H->Target = nullptr; H->bAutoAttack = false; }
        CireWaveDirector::RescueCounts(Mode, Probe.Nudges0, Probe.Marches0, Probe.Despawns0);
        Probe.Lives0[0] = State->EmberLives; Probe.Lives0[1] = State->DuskLives;
        const FName Mix[] = {TEXT("hollow_infantry"), TEXT("ironbound_bruiser"), TEXT("hollow_shieldbearer"), TEXT("blight_caster"), TEXT("barbed_hunter"),
            TEXT("hollow_infantry"), TEXT("hollow_siegebreaker"), TEXT("gravemaw_pack_leader"), TEXT("barbed_hunter"), TEXT("ironbound_bruiser")};
        for (int32 Team = 0; Team < 2; ++Team)
            for (int32 I = 0; I < 10; ++I)
                if (auto* M = SpawnMarcher(Mode, Team, Mix[I], I)) { FProbeUnit U; U.M = M; U.Team = Team; U.Archetype = Mix[I]; U.SpawnedAt = Probe.Clock; U.Radius = AgentRadius(M); Probe.Units.Add(U); }
        Note(FString::Printf(TEXT("CIRE_NAV_PROBE_MARCH_START units=%d route=%.0f/%.0f build_ms=%.1f tiles=%d/%d"), Probe.Units.Num(),
            CireLanePath::RouteLength(World, 0), CireLanePath::RouteLength(World, 1), Stats(World).InitialBuildMs, Stats(World).Tiles[0], Stats(World).Tiles[1]));
        ResetQueryStats(World);
        Probe.Stage = 1; Probe.StageAt = Probe.Clock;
        return false;
    }
    case 1: // march: every unit must reach its castle via the navmesh
    {
        int32 Alive = 0;
        for (auto& U : Probe.Units)
        {
            if (U.bLeaked) continue;
            ACireMonster* M = U.M.Get();
            if (!IsValid(M) || M->IsActorBeingDestroyed() || !Mode->Monsters.Contains(M))
            {
                U.bLeaked = true; U.LeakedAt = Probe.Clock;
                Note(FString::Printf(TEXT("CIRE_NAV_PROBE_LEAK team=%d unit=%s radius=%.0f took=%.1f max_still=%.1f"), U.Team, *U.Archetype.ToString(),
                    U.Radius, U.LeakedAt - U.SpawnedAt, U.MaxStill));
                continue;
            }
            ++Alive;
            const bool bStill = M->GetVelocity().Size2D() < 15.f && M->CastingAbility.IsEmpty();
            U.Still = bStill ? U.Still + Delta : 0.f; U.MaxStill = FMath::Max(U.MaxStill, U.Still);
        }
        // world-scale: probe marchers walk at their base speed (they are not wave units, so no pacing multipliers); the
        // time limit follows the route length (240 s for the original 159 m road, ~400 s for the 495 m one).
        const float MarchLimit = FMath::Max(240.f, CireLanePath::RouteLength(World, 0) / 100.f * .8f);
        if (Alive > 0 && Probe.Clock - Probe.StageAt < MarchLimit) return false;
        int32 Nudges = 0, Marches = 0, Despawns = 0; CireWaveDirector::RescueCounts(Mode, Nudges, Marches, Despawns);
        int32 Leaked = 0; float Longest = 0, Sum = 0, MaxStill = 0;
        for (const auto& U : Probe.Units) if (U.bLeaked) { ++Leaked; Longest = FMath::Max(Longest, U.LeakedAt - U.SpawnedAt); Sum += U.LeakedAt - U.SpawnedAt; MaxStill = FMath::Max(MaxStill, U.MaxStill); }
        const FCireNavStats& S = Stats(World);
        Note(FString::Printf(TEXT("CIRE_NAV_PROBE_MARCH leaked=%d/%d avg=%.1f longest=%.1f max_still=%.1f nudges=%d failsafe=%d queries=%d partial=%d failed=%d fallbacks=%d unsticks=%d lives=%d/%d"),
            Leaked, Probe.Units.Num(), Leaked ? Sum / Leaked : 0.f, Longest, MaxStill, Nudges - Probe.Nudges0, (Marches - Probe.Marches0) + (Despawns - Probe.Despawns0),
            S.Queries, S.Partial, S.Failed, S.Fallbacks, S.Unsticks, State ? State->EmberLives : 0, State ? State->DuskLives : 0));
        if (Leaked != Probe.Units.Num()) Fail(FString::Printf(TEXT("only %d/%d marchers reached the castle within %.0f s"), Leaked, Probe.Units.Num(), MarchLimit));
        if (Nudges != Probe.Nudges0) Fail(FString::Printf(TEXT("%d stuck nudges during the march (straight-line stalls)"), Nudges - Probe.Nudges0));
        if (MaxStill > 4.f) Fail(FString::Printf(TEXT("a marcher stood still for %.1f s"), MaxStill));
        Probe.Summary = FString::Printf(TEXT("march=%d/%d avg=%.1fs longest=%.1fs nudges=%d"), Leaked, Probe.Units.Num(), Leaked ? Sum / Leaked : 0.f, Longest, Nudges - Probe.Nudges0);
        // Performance: 10 bot heroes fighting ~40 monsters.
        for (auto* H : Mode->Heroes) if (IsValid(H)) { H->bDrafted = true; H->bAutoAttack = true; }
        Probe.Units.Reset();
        const FName Mix[] = {TEXT("hollow_infantry"), TEXT("ironbound_bruiser"), TEXT("hollow_shieldbearer"), TEXT("blight_caster"), TEXT("barbed_hunter")};
        for (int32 Team = 0; Team < 2; ++Team) for (int32 I = 0; I < 20; ++I) SpawnMarcher(Mode, Team, Mix[I % 5], I);
        ResetQueryStats(World); Probe.Frames.Reset();
        Probe.Stage = 2; Probe.StageAt = Probe.Clock;
        Note(TEXT("CIRE_NAV_PROBE_PERF_START heroes=10 monsters=40"));
        return false;
    }
    case 2: // performance window (60 s simulated)
    {
        if (FrameMs > 0) Probe.Frames.Add(FrameMs);
        if (Probe.Clock - Probe.StageAt < 60) return false;
        const FCireNavStats& S = Stats(World);
        int32 Monsters = 0; for (auto* M : Mode->Monsters) Monsters += IsValid(M) && M->Health > 0 && M->PackId < 0 ? 1 : 0;
        const double Frames = FMath::Max(1, Probe.Frames.Num());
        double Sum = 0; for (double F : Probe.Frames) Sum += F;
        Note(FString::Printf(TEXT("CIRE_NAV_PROBE_PERF frames=%d frame_avg_ms=%.2f frame_p95_ms=%.2f steer_calls=%lld steer_ms_per_frame=%.3f queries=%d queries_per_s=%.1f query_avg_ms=%.3f query_peak_ms=%.3f partial=%d failed=%d deferred=%d unsticks=%d monsters_left=%d"),
            Probe.Frames.Num(), Sum / Frames, Percentile(Probe.Frames, .95), S.SteerCalls, S.SteerMs / Frames, S.Queries, S.Queries / 60.0,
            S.Queries ? S.QueryMs / S.Queries : 0.0, S.PeakQueryMs, S.Partial, S.Failed, S.BudgetDeferred, S.Unsticks, Monsters));
        if (S.Queries && S.QueryMs / S.Queries > 2.0) Fail(TEXT("average path query above 2 ms"));
        if (S.SteerMs / Frames > 4.0) Fail(TEXT("steering cost above 4 ms per frame"));
        Probe.Summary += FString::Printf(TEXT(" perf_steer_ms=%.3f/frame queries=%d avg_query_ms=%.3f"), S.SteerMs / Frames, S.Queries, S.Queries ? S.QueryMs / S.Queries : 0.0);
        // Dynamic arena rebuild while the game keeps ticking (no forced flush).
        CireArenas::Force(World, CireArenas::Rotation().IsEmpty() ? 0 : CireArenas::Rotation()[0], false);
        Probe.Stage = 3; Probe.StageAt = Probe.Clock; Probe.LastReal = FPlatformTime::Seconds(); Probe.SteerMs0 = FPlatformTime::Seconds();
        return false;
    }
    case 3: // arena navmesh appears through dynamic generation
    {
        const FVector Spawn = CireArenas::SpawnLocation(CireArenas::Rotation().IsEmpty() ? 0 : CireArenas::Rotation()[0], 0, 0, 60);
        const bool bReady = IsReady(World) && OnNav(World, Spawn, 46.f, FVector(80, 80, 300));
        if (!bReady && Probe.Clock - Probe.StageAt < 30) return false;
        const double Ms = (FPlatformTime::Seconds() - Probe.SteerMs0) * 1000.0;
        Note(FString::Printf(TEXT("CIRE_NAV_PROBE_ARENA ready=%d wall_ms=%.0f sim_s=%.1f last_rebuild_ms=%.0f"), bReady ? 1 : 0, Ms, Probe.Clock - Probe.StageAt, Stats(World).LastRebuildMs));
        if (!bReady) Fail(TEXT("the arena navmesh did not appear within 30 s of building the arena"));
        CireArenas::ReleaseForce(World);
        Probe.Summary += FString::Printf(TEXT(" arena_ready_ms=%.0f"), Ms);
        Finish();
        return false;
    }
    default: return false;
    }
}
#endif


// =============================================================== -CireNavGallery: rendered evidence
#if !UE_BUILD_SHIPPING
#include "CireHUD.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
namespace
{
struct FNavGallery
{
    bool bChecked = false, bEnabled = false, bDone = false;
    int32 Stage = 0;
    double Started = 0, StageAt = 0;
    FString Directory;
    TArray<FString> Files;
    TWeakObjectPtr<ACameraActor> Camera;
    int32 ArenaIndex = 0;
};
FNavGallery NavGallery;
void NavShot(const FString& Name)
{
    const FString File = FPaths::Combine(NavGallery.Directory, Name);
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    NavGallery.Files.Add(File);
    UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_GALLERY_CAPTURE file=%s"), *File);
}
void ShowNav(UWorld* World, bool bShow) { if (UGameViewportClient* V = World->GetGameViewport()) V->EngineShowFlags.SetNavigation(bShow); }
void Aim(ACameraActor* Camera, const FVector& From, const FVector& To) { Camera->SetActorLocation(From); Camera->SetActorRotation((To - From).Rotation()); }
}
bool CireNav::TickGallery(ACireGameMode* Mode)
{
    if (!NavGallery.bChecked)
    {
        NavGallery.bChecked = true;
        NavGallery.bEnabled = Mode && FParse::Param(FCommandLine::Get(), TEXT("CireNavGallery"));
        if (!NavGallery.bEnabled) return false;
        NavGallery.Started = FPlatformTime::Seconds();
        NavGallery.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NavGallery"), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
        IFileManager::Get().MakeDirectory(*NavGallery.Directory, true);
        Mode->WaveTimer = 1.e6f; Mode->bBotsFilled = true; Mode->BotFillTimer = 1.e6f;
    }
    if (!NavGallery.bEnabled || NavGallery.bDone) return false;
    UWorld* World = Mode->GetWorld();
    Mode->WaveTimer = 1.e6f;
    auto* Controller = World->GetFirstPlayerController();
    auto* Hero = Controller ? Cast<ACireHero>(Controller->GetPawn()) : nullptr;
    auto* HUD = Controller ? Cast<ACireHUD>(Controller->GetHUD()) : nullptr;
    if (!Hero || !HUD) return true;
    if (!Hero->bDrafted) { Hero->Draft(0); Hero->HeroName = TEXT("Eric"); }
    HUD->SetSkillOfferOpen(false);
    const double Now = FPlatformTime::Seconds();
    auto Next = [&]() { NavGallery.StageAt = Now; ++NavGallery.Stage; };
    auto Fail = [&](const TCHAR* Why) { UE_LOG(LogCireNavTests, Error, TEXT("CIRE_NAV_GALLERY_FAIL %s"), Why); NavGallery.bDone = true; FPlatformMisc::RequestExitWithStatus(false, 1); return true; };
    switch (NavGallery.Stage)
    {
    case 0: // shaders and the navmesh first
        if (Now - NavGallery.Started < 6 || (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - NavGallery.Started < 240) || !IsReady(World)) return true;
        if (ACameraActor* Camera = World->SpawnActor<ACameraActor>())
        {
            Camera->GetCameraComponent()->SetFieldOfView(62.f); Camera->GetCameraComponent()->SetAspectRatio(16.f / 9.f);
            NavGallery.Camera = Camera;
        }
        if (!NavGallery.Camera.IsValid()) return Fail(TEXT("camera"));
        // 1: the Hero navmesh over the market, Cooper's Lanes and the town square (Ember realm).
        Aim(NavGallery.Camera.Get(), FVector(1600, -4300, 3900), FVector(5200, -2100, 0));
        Controller->SetViewTarget(NavGallery.Camera.Get()); HUD->bShowHUD = false; ShowNav(World, true);
        Next(); return true;
    case 1:
        if (Now - NavGallery.StageAt < 4) return true;
        NavShot(TEXT("01_navmesh_town.png")); Next(); return true;
    case 2: // 2: the path editor with waypoint 5 being dragged across the market
    {
        if (Now - NavGallery.StageAt < 1.5) return true;
        HUD->bShowHUD = true; ShowNav(World, false);
        HUD->OpenRouteEditor(true);
        HUD->DebugRouteView(FVector(6600, -2100, 0), 5600.f, -60.f, 180.f);
        Next(); return true;
    }
    case 3:
    {
        if (Now - NavGallery.StageAt < 1.0) return true;
        const FVector Drag(5800 + 350, -2100 + 900, 0); // waypoint 5 (5800, 500) pulled toward the north stalls
        HUD->DebugRouteDrag(0, 5, Drag, false);
        FVector2D Screen;
        if (Controller->ProjectWorldLocationToScreen(Drag, Screen)) HUD->DebugSetPointer(Screen / FMath::Max(.01f, HUD->DebugScale()));
        if (Now - NavGallery.StageAt < 4.0) return true;
        NavShot(TEXT("02_path_editor_drag.png")); Next(); return true;
    }
    case 4: // zoomed-out editor: whole route with reachability colours and the minimap overlay
        if (Now - NavGallery.StageAt < 1.0) return true;
        HUD->DebugRouteDrag(0, 5, FVector(5800, -2100 + 500, 0), true);
        HUD->DebugSetPointer(FVector2D(-1, -1));
        HUD->DebugRouteView(FVector(5200, -2100, 0), 15000.f, -80.f, 180.f);
        if (Now - NavGallery.StageAt < 4.0) return true;
        NavShot(TEXT("03_path_editor_overview.png")); Next(); return true;
    case 5: // 3: an arena navmesh (dynamic generation after the arena is built)
    {
        if (Now - NavGallery.StageAt < 1.0) return true;
        HUD->OpenRouteEditor(false);
        const TArray<int32> Rotation = CireArenas::Rotation();
        NavGallery.ArenaIndex = Rotation.Num() > 5 ? Rotation[5] : Rotation.Num() ? Rotation[0] : 0; // Star Station Hangar: hard-edged blockers read best
        CireArenas::Force(World, NavGallery.ArenaIndex, true);
        Controller->SetViewTarget(NavGallery.Camera.Get()); HUD->bShowHUD = false; ShowNav(World, true);
        const FVector O = CireArenas::Origin();
        Aim(NavGallery.Camera.Get(), O + FVector(0, -3900, 4200), O + FVector(0, -200, 0));
        Next(); return true;
    }
    case 6:
    {
        const bool bReady = IsReady(World) && OnNav(World, CireArenas::SpawnLocation(NavGallery.ArenaIndex, 0, 0, 60), 46.f, FVector(80, 80, 300));
        if ((!bReady && Now - NavGallery.StageAt < 20) || Now - NavGallery.StageAt < 5) return true;
        NavShot(TEXT("04_arena_navmesh.png")); Next(); return true;
    }
    default:
    {
        if (Now - NavGallery.StageAt < 2) return true;
        bool bOk = NavGallery.Files.Num() == 4;
        for (const FString& F : NavGallery.Files) bOk &= IFileManager::Get().FileSize(*F) > 10000;
        UE_LOG(LogCireNavTests, Display, TEXT("CIRE_NAV_GALLERY_%s captures=%d dir=%s"), bOk ? TEXT("DONE") : TEXT("INCOMPLETE"), NavGallery.Files.Num(), *NavGallery.Directory);
        NavGallery.bDone = true;
        CireArenas::ReleaseForce(World);
        FPlatformMisc::RequestExitWithStatus(false, bOk ? 0 : 1);
        return true;
    }
    }
}
#endif
