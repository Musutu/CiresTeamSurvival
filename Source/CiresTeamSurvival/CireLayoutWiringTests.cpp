// layout-wiring: native checks for what the game reads from the map layout editor (Docs/MapLayout.md "What the game
// reads"): the deterministic path split, multi-path compile with merges, marker-driven spawns (monster spawns, bosses,
// heroes, respawns, rift, bounds), the realm transforms of every path, replication of the extras, the vendor realm
// numbers, and Validate flagging what the runtime cannot use. Part of -CireCombatExpansionProbe; logs
// CIRE_LAYOUT_WIRING_PASS / _FAIL. The leash has its own checks (CireLeash::RunTests, CIRE_LEASH_TESTS_PASS).
#include "CireLayoutWiring.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireLeash.h"
#include "CireMapLayout.h"
#include "CireNPCArchetypes.h"
#include "CireVendors.h"
#include "CireWaves.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireLayoutWiring, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
namespace ML = CireMapLayout;
struct FChecker
{
    int32 Count = 0; bool bPass = true;
    void operator()(bool bValue, const FString& Why) { ++Count; if (!bValue) { bPass = false; UE_LOG(LogCireLayoutWiring, Error, TEXT("CIRE_LAYOUT_WIRING_CHECK_FAIL %s"), *Why); } }
};
FCireRoutePath MakePath(int32 Spawn, float Weight, TArray<FVector2D> Points) { FCireRoutePath P; P.Spawn = Spawn; P.Weight = Weight; P.Points = MoveTemp(Points); return P; }
TArray<int32> Split(const FCireBattlefieldRoutes& R, int32 Team, int32 From, int32 Units)
{
    TArray<int32> Counts; Counts.Init(0, CireLanePath::PathCount(R, Team));
    for (int32 S = From; S < From + Units; ++S) ++Counts[CireLanePath::PathForSlot(R, Team, S)];
    return Counts;
}
/** Two spawns, three paths (one merges into another), objective, hero spawns, respawns, boss, rift and bounds, mirrored. */
FCireMapLayout TestLayout(FString* OutA1 = nullptr, FString* OutA2 = nullptr, FString* OutB1 = nullptr)
{
    const ECireMarkerOwner T1 = ECireMarkerOwner::Team1;
    FCireMapLayout L; L.Name = TEXT("Wiring test"); L.Map = ML::ActiveMap();
    const FString Goal = ML::Place(L, ML::Objective, FVector2D(-1850, 0), T1);
    ML::SetRadius(L, Goal, 450.f);
    const FString A = ML::Place(L, ML::MonsterSpawn, FVector2D(40000, -600), T1, 180.f); ML::SetName(L, A, TEXT("North gate"));
    const FString B = ML::Place(L, ML::MonsterSpawn, FVector2D(40000, 700), T1, 180.f); ML::SetName(L, B, TEXT("South gate"));
    FString A1;
    for (const FVector2D& P : {FVector2D(38000, -600), FVector2D(20000, -400), FVector2D(5000, 0), FVector2D(-1850, 0)}) A1 = ML::ChainPoint(L, ML::MonsterPath, A1, P, T1, A);
    FString A2;
    for (const FVector2D& P : {FVector2D(36000, -1000), FVector2D(24000, -1000), FVector2D(20100, -350)}) A2 = ML::ChainPoint(L, ML::MonsterPath, A2, P, T1, A);
    FString B1;
    for (const FVector2D& P : {FVector2D(38000, 800), FVector2D(10000, 600), FVector2D(-1800, 100)}) B1 = ML::ChainPoint(L, ML::MonsterPath, B1, P, T1, B);
    ML::SetName(L, A1, TEXT("Main road")); ML::SetName(L, A2, TEXT("Tanners' lane")); ML::SetName(L, B1, TEXT("South road"));
    ML::Place(L, ML::PlayerSpawn, FVector2D(-1200, -300), T1, 0.f);
    ML::Place(L, ML::PlayerSpawn, FVector2D(-1200, 300), T1, 10.f);
    ML::Place(L, ML::Respawn, FVector2D(-1000, 0), T1);
    ML::Place(L, ML::Respawn, FVector2D(20000, 1000), T1);
    ML::Place(L, ML::BossSpawn, FVector2D(41000, -500), T1, 180.f);
    ML::Place(L, ML::Rift, FVector2D(0, 1000), T1, 90.f);
    FString Bounds;
    for (const FVector2D& P : {FVector2D(-2300, -1390), FVector2D(43600, -1390), FVector2D(43600, 1390), FVector2D(-2300, 1390)})
        Bounds = ML::ChainPoint(L, ML::PlayBounds, Bounds, P, ECireMarkerOwner::Shared);
    if (OutA1) *OutA1 = A1; if (OutA2) *OutA2 = A2; if (OutB1) *OutB1 = B1;
    return L;
}
}

bool CireLayoutWiring::RunTests(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecker Check;
    UWorld* World = Mode->GetWorld();

    // ---------------------------------------------------------------- the path split (pure)
    {
        FCireBattlefieldRoutes R;
        const TArray<FVector2D> Main = {{9000, 0}, {4000, 0}, {-1850, 0}};
        R.LocalPoints[0] = Main;
        R.Spawns[0] = {FCireRouteSpot(), FCireRouteSpot()};
        R.Paths[0] = {MakePath(0, 1, Main), MakePath(0, 1, {{9000, 0}, {5000, 500}, {-1850, 0}}), MakePath(1, 1, {{8000, 800}, {-1850, 0}})};
        const TArray<double> Shares = CireLanePath::PathShares(R, 0);
        Check(Shares.Num() == 3 && FMath::IsNearlyEqual(Shares[0], .25) && FMath::IsNearlyEqual(Shares[1], .25) && FMath::IsNearlyEqual(Shares[2], .5),
            TEXT("two spawns share a wave evenly; a spawn splits evenly across its paths"));
        Check(Split(R, 0, 0, 8) == TArray<int32>({2, 2, 4}), TEXT("8 units: 2 / 2 / 4"));
        Check(Split(R, 0, 0, 4) == TArray<int32>({1, 1, 2}) && Split(R, 0, 5, 8) == TArray<int32>({2, 2, 4}), TEXT("every prefix and window keeps the shares"));
        Check(CireLanePath::PathForSlot(R, 0, 11) == CireLanePath::PathForSlot(R, 0, 11) && CireLanePath::PathForSlot(R, 0, 0) != CireLanePath::PathForSlot(R, 0, 1),
            TEXT("the split is deterministic and alternates paths"));
        R.Spawns[0][0].bWeighted = true; R.Paths[0][0].Weight = 3; R.Paths[0][1].Weight = 1;
        Check(Split(R, 0, 0, 8) == TArray<int32>({3, 1, 4}), TEXT("weighted split: 3 : 1 inside the spawn's half"));
        R.Paths[0][1].Weight = 0;
        Check(Split(R, 0, 0, 8) == TArray<int32>({4, 0, 4}), TEXT("a zero-weight path gets no units"));
        FCireBattlefieldRoutes Single; Single.LocalPoints[0] = Main;
        Check(CireLanePath::PathCount(Single, 0) == 1 && CireLanePath::PathForSlot(Single, 0, 7) == 0 && CireLanePath::SpawnSpots(Single, 0).Num() == 1 &&
            CireLanePath::SpawnSpots(Single, 0)[0].Position == Main[0], TEXT("a route document without markers keeps one path from one spawn"));
        Check(CireLanePath::NearestPathStart(R, 0, FVector2D(8100, 700)) == 2, TEXT("a boss marches the path that starts next to it"));
    }

    // ---------------------------------------------------------------- compile: every path, merges, spots
    FCireBattlefieldRoutes Base;
    const bool bBase = CireLanePath::LoadFile(Base, nullptr, FPaths::ProjectContentDir() / TEXT("Data/BattlefieldRoutes.json"));
    Check(bBase, TEXT("the procedural route file loads as the compile base"));
    FString A1, A2, B1;
    const FCireMapLayout L = TestLayout(&A1, &A2, &B1);
    FCireBattlefieldRoutes C; TArray<FString> Notes; FString Error;
    const bool bCompiled = bBase && ML::CompileRoutes(L, Base, C, Notes);
    Check(bCompiled && CireLanePath::Validate(C, Error), TEXT("the two-spawn, three-path layout compiles into a valid route document: ") + Error);
    if (bCompiled)
    {
        Check(C.Spawns[0].Num() == 2 && C.Paths[0].Num() == 3 && C.Spawns[1].Num() == 2 && C.Paths[1].Num() == 3, TEXT("both realms get 2 spawns and 3 paths"));
        Check(C.Paths[0][0].Points == C.LocalPoints[0] && C.Paths[0][0].Spawn == 0 && C.Paths[0][1].Spawn == 0 && C.Paths[0][2].Spawn == 1, TEXT("each path belongs to its spawn; path 0 is the primary route"));
        const TArray<FVector2D>& Merged = C.Paths[0][1].Points;
        Check(Merged.Num() >= 5 && Merged[0] == FVector2D(40000, -600) && Merged.Contains(FVector2D(5000, 0)) && Merged.Last() == FVector2D(-1850, 0),
            TEXT("a path that merges into another continues along it to the objective"));
        Check(C.Paths[0][2].Points[0] == FVector2D(40000, 700) && C.Paths[0][2].Points.Last() == FVector2D(-1800, 100), TEXT("the second spawn's path starts at its spawn and ends in the goal"));
        Check(C.Paths[1] == C.Paths[0] && C.Spawns[1] == C.Spawns[0] || (C.Paths[1].Num() == 3 && C.Paths[1][1].Points == C.Paths[0][1].Points), TEXT("the mirrored realm marches the same realm-local paths"));
        Check(C.PlayerSpawns[0].Num() == 2 && C.PlayerSpawns[0][1].Yaw == 10.f && C.Respawns[0].Num() == 2 && C.Bosses[0].Num() == 1 && C.Rifts[0].Num() == 1 && C.PlayBounds.Num() == 4,
            TEXT("player spawns (with facing), respawns, boss, rift and bounds compile"));
        Check(C.GoalCenter == FVector2D(-1850, 0) && C.GoalSize == Base.GoalSize && C.BaseLocal == FVector2D(-1200, -300), TEXT("the objective keeps the goal zone; the first player spawn is the base"));
        // Replication: the extras block round-trips (mirrored realms are sent once).
        TArray<float> Packed; CireLanePath::PackExtras(C, Packed);
        FCireBattlefieldRoutes Received; Received.LocalPoints[0] = C.LocalPoints[0]; Received.LocalPoints[1] = C.LocalPoints[1];
        const bool bUnpacked = CireLanePath::UnpackExtras(Packed, 0, Received);
        bool bSame = bUnpacked && Received.Paths[0].Num() == 3 && Received.Paths[1].Num() == 3 && Received.PlayBounds == C.PlayBounds && Received.PlayerSpawns[1].Num() == 2;
        for (int32 P = 0; bSame && P < 3; ++P) bSame &= Received.Paths[1][P].Points == C.Paths[1][P].Points && Received.Paths[0][P].Spawn == C.Paths[0][P].Spawn;
        Check(bSame, TEXT("every path, spawn and spot replicates to clients"));
        Check(Packed.Num() < 1200, FString::Printf(TEXT("the replicated block stays small (%d floats)"), Packed.Num()));
        FCireBattlefieldRoutes Asym = C; Asym.Paths[1].RemoveAt(2); Asym.Spawns[1].RemoveAt(1);
        TArray<float> PackedAsym; CireLanePath::PackExtras(Asym, PackedAsym);
        FCireBattlefieldRoutes ReceivedAsym; ReceivedAsym.LocalPoints[0] = Asym.LocalPoints[0]; ReceivedAsym.LocalPoints[1] = Asym.LocalPoints[1];
        Check(CireLanePath::UnpackExtras(PackedAsym, 0, ReceivedAsym) && ReceivedAsym.Paths[1].Num() == 2 && ReceivedAsym.Paths[0].Num() == 3, TEXT("asymmetric realms replicate separately"));
        TArray<float> Broken = Packed; Broken.SetNum(Broken.Num() / 2);
        FCireBattlefieldRoutes Kept = Received;
        Check(!CireLanePath::UnpackExtras(Broken, 0, Kept) && Kept.Paths[0].Num() == 3, TEXT("a truncated block is rejected and the old layout kept"));
        // Realm transforms: the same path, one realm offset apart.
        const FVector W0 = CireLanePath::ToWorld(0, C.Paths[0][1].Points[2], 0), W1 = CireLanePath::ToWorld(1, C.Paths[1][1].Points[2], 0);
        Check(FVector2D(W1 - W0).Equals(CireLanePath::RealmOrigin(1) - CireLanePath::RealmOrigin(0), .5) && CireLanePath::ToLocal(1, W1).Equals(C.Paths[1][1].Points[2], .5),
            TEXT("each realm marches its paths through its own frame"));
        // Bounds.
        Check(CireLanePath::InsidePlayBounds(C, FVector2D(0, 0)) && !CireLanePath::InsidePlayBounds(C, FVector2D(0, 1395)), TEXT("the play bounds polygon contains the town"));
    }

    // ---------------------------------------------------------------- documents and validation stay in sync with the runtime
    {
        FCireMapLayout Round; FString ParseError;
        FCireMapLayout W = L; ML::SetSplit(W, ML::OfType(W, ML::MonsterSpawn)[0]->Id, true); ML::SetWeight(W, A2, 2.5f);
        const bool bRound = ML::ParseJson(ML::ToJson(W), Round, ParseError);
        const FCireMapMarker* RoundA2 = bRound ? ML::Find(Round, A2) : nullptr;
        const TArray<const FCireMapMarker*> RoundSpawns = ML::OfType(Round, ML::MonsterSpawn);
        Check(bRound && RoundA2 && RoundA2->Weight == 2.5f && RoundSpawns.Num() > 0 && RoundSpawns[0]->bSplitWeighted && Round.Map == ML::ActiveMap(),
            FString::Printf(TEXT("weight, split and the map survive the JSON round trip (parsed=%d weight=%.2f split=%d map=%s error=%s)"), bRound ? 1 : 0, RoundA2 ? RoundA2->Weight : -1.f,
                RoundSpawns.Num() ? (RoundSpawns[0]->bSplitWeighted ? 1 : 0) : -1, *Round.Map, *ParseError));
        Check(ML::Find(W, ML::Find(W, A2)->Pair)->Weight == 2.5f, TEXT("weights mirror to the twin path"));
        Check(ML::MatchesActiveMap(Round), TEXT("a layout drives the map it was authored on"));
        Round.Map = ML::ActiveMap() == TEXT("castletown") ? TEXT("procedural") : TEXT("castletown");
        Check(!ML::MatchesActiveMap(Round), TEXT("a layout of the other map is ignored by this match"));
        FCireLayoutChecks Runtime;
        Runtime.Runtime = [&](const FCireMapLayout& X, TArray<FString>& N, FString& E) { FCireBattlefieldRoutes Out; return ML::CompileRoutes(X, Base, Out, N) && CireLanePath::Validate(Out, E); };
        int32 Errors = 0; for (const FCireLayoutIssue& I : ML::Validate(L, &Runtime)) Errors += I.bError ? 1 : 0;
        Check(Errors == 0, TEXT("the test layout validates with the runtime check"));
        FCireMapLayout Bad = L;
        ML::MovePoint(Bad, B1, 1, FVector2D(10000, 5000)); // outside the procedural realm (half width 14 m)
        bool bFlagged = false; for (const FCireLayoutIssue& I : ML::Validate(Bad, &Runtime)) bFlagged |= I.bError && I.Message.Contains(TEXT("The game cannot run this layout"));
        Check(bFlagged, TEXT("Validate flags a layout the runtime would reject"));
        FCireMapLayout Cross = L;
        ML::SetMirror(Cross, B1, false); ML::Find(Cross, B1)->Target = ECireMarkerOwner::Team2;
        bFlagged = false; for (const FCireLayoutIssue& I : ML::Validate(Cross)) bFlagged |= I.bError && I.Message.Contains(TEXT("waves attack the team of the realm"));
        Check(bFlagged, TEXT("a path that targets the other realm's team is flagged"));
        FCireMapLayout Goals = L;
        const FString G1 = ML::ObjectiveOf(Goals, ECireMarkerOwner::Team1)->Id;
        ML::SetMirror(Goals, G1, false); ML::Move(Goals, ML::ObjectiveOf(Goals, ECireMarkerOwner::Team2)->Id, FVector2D(-1500, 200));
        bFlagged = false; for (const FCireLayoutIssue& I : ML::Validate(Goals)) bFlagged |= I.bError && I.Message.Contains(TEXT("objectives differ"));
        Check(bFlagged, TEXT("differing objectives are flagged (one goal zone per realm-local spot)"));
        // Vendors: TownVendors.json names the REALM (CireVendors' numbering).
        FCireMapLayout V; V.Name = TEXT("Vendors");
        ML::Place(V, ML::Vendor, FVector2D(-1000, 500), ECireMarkerOwner::Team1, 0.f);
        TArray<FCireVendorSpot> Spots; FString VendorError;
        Check(CireVendors::ParseSpots(ML::VendorsJson(V), Spots, VendorError) && Spots.Num() == 2 && Spots[0].Team == 0 && Spots[1].Team == 1,
            TEXT("Apply writes T1's merchants to the DAYLIGHT realm (0) and T2's to DARKNIGHT (1)"));
    }

    // ---------------------------------------------------------------- marker-driven spawns in the world
    if (bCompiled)
    {
        auto* State = Mode->GetGameState<ACireGameState>();
        const FCireBattlefieldRoutes Saved = CireLanePath::Get(World);
        const auto SavedClock = Mode->Clock; const auto SavedMonsters = Mode->Monsters;
        TArray<AActor*> Spawned;
        ON_SCOPE_EXIT
        {
            for (auto* M : Mode->Monsters) if (IsValid(M) && !SavedMonsters.Contains(M)) Spawned.AddUnique(M);
            for (AActor* A : Spawned) if (IsValid(A)) { if (auto* M = Cast<ACireMonster>(A)) CireWaveDirector::Forget(M); A->Destroy(); }
            Mode->Monsters = SavedMonsters; Mode->Clock = SavedClock;
            CireWaveDirector::Initialize(Mode);
            FString RestoreError; CireLanePath::ApplyLive(World, Saved, &RestoreError);
        };
        FCireBattlefieldRoutes Live = C;
        Live.MinX = Saved.MinX; Live.MaxX = Saved.MaxX; Live.HalfWidth = Saved.HalfWidth;
        const bool bLive = CireLanePath::ApplyLive(World, Live, &Error);
        Check(bLive, TEXT("the compiled layout applies live: ") + Error);
        if (bLive)
        {
            Check(State && State->LaneLayout.Contains(7331.f), TEXT("the server publishes the extras block"));
            // Heroes: Player Spawn markers in order with their facing; the respawn nearest the fall; the arena portal.
            const FTransform S0 = CireLanePath::PlayerSpawnTransform(World, 0, 0), S1 = CireLanePath::PlayerSpawnTransform(World, 1, 1), S2 = CireLanePath::PlayerSpawnTransform(World, 0, 2);
            Check(CireLanePath::ToLocal(0, S0.GetLocation()).Equals(FVector2D(-1200, -300), 1.) && CireLanePath::ToLocal(1, S1.GetLocation()).Equals(FVector2D(-1200, 300), 1.) &&
                FMath::IsNearlyEqual(S1.Rotator().Yaw, 10.f, .5f), TEXT("heroes spawn at their team's Player Spawn markers with the marker's facing"));
            Check(!CireLanePath::ToLocal(0, S2.GetLocation()).Equals(FVector2D(-1200, -300), 1.) && CireLanePath::ToLocal(0, S2.GetLocation()).Equals(FVector2D(-1200, -300), 300.),
                TEXT("more heroes than markers share them with a spread"));
            Check(CireLanePath::ToLocal(0, CireLanePath::RespawnNear(World, 0, CireLanePath::ToWorld(0, FVector2D(19000, 500)))).Equals(FVector2D(20000, 1000), 1.) &&
                CireLanePath::ToLocal(1, CireLanePath::RespawnNear(World, 1, CireLanePath::ToWorld(1, FVector2D(0, 0)))).Equals(FVector2D(-1000, 0), 1.),
                TEXT("a dead hero revives at the respawn marker nearest to where he fell"));
            FTransform Rift;
            Check(CireLanePath::RiftTransform(World, 1, Rift) && CireLanePath::ToLocal(1, Rift.GetLocation()).Equals(FVector2D(0, 1000), 1.), TEXT("each realm has its arena portal at the Rift marker"));
            // Monsters: a wave of 8 per realm spreads 2 / 2 / 4 over the three paths from the two spawns.
            Mode->Clock = Cires::MatchClock(); Mode->Monsters.Reset();
            CireWaveDirector::Initialize(Mode);
            FCireWaveDef Wave; Wave.Label = TEXT("Layout wiring"); Wave.Type = ECireWaveType::Custom; Wave.SpawnInterval = 0;
            FCireWaveUnit Unit; Unit.Archetype = TEXT("hollow_infantry"); Unit.Count = 8; Wave.Units = {Unit};
            Check(CireWaveDirector::SpawnNow(Mode, Wave, &Error), TEXT("the test wave queues: ") + Error);
            for (int32 I = 0; I < 64 && CireWaveDirector::HasPendingSpawns(Mode); ++I) CireWaveDirector::TickSurvival(Mode, 1.f);
            int32 PerPath[2][3] = {{0, 0, 0}, {0, 0, 0}}; bool bAtStart = true, bLeashed = true, bHeading = true;
            for (ACireMonster* M : Mode->Monsters)
            {
                if (!IsValid(M) || M->PackId >= 0) continue;
                Spawned.AddUnique(M); M->SetActorTickEnabled(false);
                if (M->LanePath < 0 || M->LanePath > 2) { bAtStart = false; continue; }
                ++PerPath[M->Lane][M->LanePath];
                bAtStart &= CireLanePath::PathProgress(World, M->Lane, M->LanePath, M->GetActorLocation()) < .03f &&
                    FVector2D::Distance(CireLanePath::ToLocal(M->Lane, M->GetActorLocation()), CireLanePath::PathPoints(CireLanePath::Get(World), M->Lane, M->LanePath)[0]) < 500.;
                bLeashed &= M->bPathLeash && CireLeash::Applies(M);
                // Its next waypoint is on its own path.
                const FVector Next = CireLanePath::NextWaypoint(M);
                double D = 0; CireLanePath::NearestOnPolyline(CireLanePath::UnitPath(M), CireLanePath::ToLocal(M->Lane, Next), &D);
                bHeading &= D < 1.;
            }
            Check(PerPath[0][0] == 2 && PerPath[0][1] == 2 && PerPath[0][2] == 4 && PerPath[1][0] == 2 && PerPath[1][1] == 2 && PerPath[1][2] == 4,
                FString::Printf(TEXT("each realm splits 8 units 2/2/4 over its three paths (got %d/%d/%d and %d/%d/%d)"), PerPath[0][0], PerPath[0][1], PerPath[0][2], PerPath[1][0], PerPath[1][1], PerPath[1][2]));
            Check(bAtStart, TEXT("every unit appears at its own path's spawn"));
            Check(bLeashed, TEXT("every wave unit carries its path leash"));
            Check(bHeading, TEXT("every unit steers along its own path"));
            const TMap<int32, int32> Counts = CireWaveDirector::PathSpawnCounts(Mode, 0);
            Check(Counts.FindRef(2) == 4, TEXT("the director reports units per path"));
            // A boss appears at the Boss marker and marches the path that starts nearest to it.
            FCireWaveDef BossWave = Wave; BossWave.Label = TEXT("Layout wiring boss");
            FCireWaveUnit Boss; Boss.Archetype = CireNPCArchetypes::Get().WaveBoss; Boss.bBoss = true; Boss.Count = 1; BossWave.Units = {Boss};
            const int32 Before = Mode->Monsters.Num();
            Check(CireWaveDirector::SpawnNow(Mode, BossWave, &Error), TEXT("the boss wave queues: ") + Error);
            for (int32 I = 0; I < 16 && CireWaveDirector::HasPendingSpawns(Mode); ++I) CireWaveDirector::TickSurvival(Mode, 1.f);
            bool bBossOk = Mode->Monsters.Num() == Before + 2;
            for (int32 I = Before; I < Mode->Monsters.Num(); ++I)
            {
                ACireMonster* M = Mode->Monsters[I]; if (!IsValid(M)) { bBossOk = false; continue; }
                Spawned.AddUnique(M); M->SetActorTickEnabled(false);
                bBossOk &= M->bBoss && M->LanePath == 0 && FVector2D::Distance(CireLanePath::ToLocal(M->Lane, M->GetActorLocation()), FVector2D(41000, -500)) < 400.;
            }
            Check(bBossOk, TEXT("bosses spawn at the Boss marker of each realm and march the nearest path"));
        }
    }
    UE_LOG(LogCireLayoutWiring, Display, TEXT("CIRE_LAYOUT_WIRING_%s checks=%d"), Check.bPass ? TEXT("PASS") : TEXT("FAIL"), Check.Count);
    return Check.bPass;
}
#endif
