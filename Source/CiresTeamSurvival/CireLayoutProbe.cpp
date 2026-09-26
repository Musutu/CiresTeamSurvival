// layout-wiring: -CireLayoutProbe, the town march + kite probe. See CireLayoutWiring.h.
#include "CireLayoutWiring.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireLeash.h"
#include "CireMapLayout.h"
#include "CireNav.h"
#include "CireNPCCombat.h"
#include "CireRouteEditor.h"
#include "CireThreat.h"
#include "CireTownMap.h"
#include "CireWaves.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireLayoutProbe, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
namespace ML = CireMapLayout;
struct FMarcher { TWeakObjectPtr<ACireMonster> M; int32 Realm = 0, Path = 0; float SpawnedAt = 0, ArrivedAt = -1; bool bArrived = false, bDied = false; };
struct FKite
{
    int32 Realm = 0; bool bDrag = false;
    TWeakObjectPtr<ACireMonster> M; TWeakObjectPtr<ACireHero> H;
    int32 Stage = 0; float StageAt = 0, MaxOffPath = 0, ReturnedAt = -1, ProgressAtResume = 0;
    TArray<FVector> Route; int32 RouteIndex = 0;
    bool bSawReturn = false, bImmune = false, bThreatKept = false, bResumed = false, bMarchedOn = false, bArrived = false, bDragPlaced = false, bPulled = false;
    float DragAt = 0;
    FString Why;
};
struct FProbe
{
    bool bEnabled = false, bDone = false, bPass = true;
    int32 Stage = 0;
    float Clock = 0, StageAt = 0;
    TArray<FMarcher> Marchers;
    FKite Kites[2];
    int32 Nudges0 = 0, Marches0 = 0, Despawns0 = 0, Returns0 = 0, Arrivals0 = 0, Rescues0 = 0, Lives0[2] = {0, 0};
    TArray<FString> Lines;
    FString Summary;
};
FProbe P;
void Note(const FString& Line) { UE_LOG(LogCireLayoutProbe, Display, TEXT("%s"), *Line); P.Lines.Add(Line); }
void Fail(const FString& Why) { P.bPass = false; UE_LOG(LogCireLayoutProbe, Error, TEXT("CIRE_LAYOUT_PROBE_CHECK_FAIL %s"), *Why); P.Lines.Add(TEXT("FAIL ") + Why); }
bool OnNav(UWorld* World, int32 Realm, const FVector2D& Local, FVector2D& Out)
{
    FVector Projected;
    if (!CireNav::Project(World, CireLanePath::ToWorld(Realm, Local, 60.f), Projected, FVector(200, 200, 600), 45.f)) return false;
    Out = CireLanePath::ToLocal(Realm, Projected);
    return true;
}
bool Reachable(UWorld* World, int32 Realm, const FVector2D& A, const FVector2D& B)
{
    float Length = 0;
    const ECireRouteReach R = CireRouteEditor::Reach(World, CireLanePath::ToWorld(Realm, A, 60.f), CireLanePath::ToWorld(Realm, B, 60.f), Length);
    return R == ECireRouteReach::Direct || R == ECireRouteReach::Detour;
}
FVector2D AlongPolyline(const TArray<FVector2D>& Points, double Fraction, int32* OutSegment = nullptr)
{
    double Remaining = CireLanePath::PathLength(Points) * FMath::Clamp(Fraction, 0., 1.);
    for (int32 I = 0; I + 1 < Points.Num(); ++I)
    {
        const double S = FVector2D::Distance(Points[I], Points[I + 1]);
        if (Remaining <= S) { if (OutSegment) *OutSegment = I; return FMath::Lerp(Points[I], Points[I + 1], S > 0 ? Remaining / S : 0.); }
        Remaining -= S;
    }
    if (OutSegment) *OutSegment = Points.Num() - 2;
    return Points.Last();
}
int32 NearestVertex(const TArray<FVector2D>& Points, const FVector2D& To, int32 From = 1)
{
    int32 Best = From; double BestD = TNumericLimits<double>::Max();
    for (int32 I = From; I + 1 < Points.Num(); ++I) { const double D = FVector2D::DistSquared(Points[I], To); if (D < BestD) { BestD = D; Best = I; } }
    return Best;
}
/** A navmesh point roughly Offset cm off the route near Fraction (any direction that leaves the road), walkable from and
 *  to the route in both realms (the ordinary unit size). */
bool SidePoint(UWorld* World, const TArray<FVector2D>& Route, double Fraction, const TArray<float>& Offsets, FVector2D& Out, FVector2D* OutRoad = nullptr, int32* OutSegment = nullptr)
{
    int32 OffNav = 0, OnRoad = 0, Blocked = 0;
    auto Walk = [&](int32 Realm, const FVector2D& A, const FVector2D& B)
    {
        FVector PA, PB;
        if (!CireNav::Project(World, CireLanePath::ToWorld(Realm, A, 60.f), PA, FVector(200, 200, 600), 40.f) ||
            !CireNav::Project(World, CireLanePath::ToWorld(Realm, B, 60.f), PB, FVector(200, 200, 600), 40.f)) return false;
        const FCireNavPath Path = CireNav::FindPath(World, PA, PB, 40.f, true);
        return Path.bValid && !Path.bPartial && Path.Points.Num() > 0 && FVector::Dist2D(Path.Points.Last(), PB) < 150.;
    };
    for (const double Df : {0., .04, -.04, .08, -.08, .12, -.12})
    {
        int32 Segment = 0;
        const FVector2D At = AlongPolyline(Route, FMath::Clamp(Fraction + Df, .05, .9), &Segment);
        const FVector2D Dir = (Route[Segment + 1] - Route[Segment]).GetSafeNormal();
        for (const float Offset : Offsets)
            for (int32 K = 0; K < 8; ++K)
            {
                // Perpendicular first, then the diagonals and along-road directions.
                const double Angle = HALF_PI + (K / 2) * (PI / 4.) * (K % 2 ? -1. : 1.) + (K % 2 ? PI : 0.);
                const FVector2D Ray = Dir.GetRotated(FMath::RadiansToDegrees(Angle));
                FVector2D Candidate;
                if (!OnNav(World, 0, At + Ray * Offset, Candidate)) { ++OffNav; continue; }
                double D = 0; CireLanePath::NearestOnPolyline(Route, Candidate, &D);
                if (D < FMath::Min(Offset * .4f, 400.f)) { ++OnRoad; continue; }
                if (!Walk(0, At, Candidate) || !Walk(0, Candidate, At) || !Walk(1, At, Candidate)) { ++Blocked; continue; }
                Out = Candidate;
                if (OutRoad) *OutRoad = At;
                if (OutSegment) *OutSegment = Segment;
                return true;
            }
    }
    Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_SIDE_SEARCH fraction=%.2f off_nav=%d on_road=%d blocked=%d"), Fraction, OffNav, OnRoad, Blocked));
    return false;
}
/** The layout: spawn A (the breach) with the main road and a detour that merges back; spawn B beside the road with a
 *  path that joins it. Mirrored to both realms. */
bool BuildLayout(UWorld* World, FCireMapLayout& L, FString& Why)
{
    const FCireBattlefieldRoutes& R = CireLanePath::Get(World);
    const TArray<FVector2D> Route = R.LocalPoints[0];
    if (Route.Num() < 4) { Why = TEXT("the active route is too short"); return false; }
    const ECireMarkerOwner T1 = ECireMarkerOwner::Team1;
    L = FCireMapLayout(); L.Name = TEXT("Layout probe"); L.Map = ML::ActiveMap();
    const FString Goal = ML::Place(L, ML::Objective, R.GoalCenter, T1);
    ML::SetRadius(L, Goal, static_cast<float>(FMath::Min(R.GoalSize.X, R.GoalSize.Y) * .5));
    ML::Place(L, ML::PlayerSpawn, R.BaseLocal, T1, 0.f);
    const FString A = ML::Place(L, ML::MonsterSpawn, Route[0], T1, 0.f); ML::SetName(L, A, TEXT("Probe breach"));
    FString A1;
    for (int32 I = 1; I < Route.Num(); ++I) A1 = ML::ChainPoint(L, ML::MonsterPath, A1, Route[I], T1, A);
    ML::SetName(L, A1, TEXT("Main road"));
    // Detour: off the road at ~35 %, back onto it (a merge) at the road vertex nearest ~55 %.
    FVector2D Detour;
    if (!SidePoint(World, Route, .35, {1400.f, 1000.f, 700.f, 450.f, 300.f}, Detour)) { Why = TEXT("no navmesh detour beside the road at 35%"); return false; }
    const int32 Join = NearestVertex(Route, AlongPolyline(Route, .55), 2);
    FString A2;
    A2 = ML::ChainPoint(L, ML::MonsterPath, A2, Route[1], T1, A);
    A2 = ML::ChainPoint(L, ML::MonsterPath, A2, Detour, T1);
    A2 = ML::ChainPoint(L, ML::MonsterPath, A2, Route[Join], T1);
    ML::SetName(L, A2, TEXT("Detour"));
    // Spawn B: beside the road at ~15 %; its path joins the road at the vertex nearest ~30 %.
    // Its path walks back to the road where the spawn spot was found walkable, then joins the road at the next vertex.
    FVector2D SpawnB, RoadB; int32 SegmentB = 0;
    if (!SidePoint(World, Route, .15, {2200.f, 1600.f, 1100.f, 700.f, 450.f}, SpawnB, &RoadB, &SegmentB)) { Why = TEXT("no navmesh spot for a second spawn beside the road"); return false; }
    const FString B = ML::Place(L, ML::MonsterSpawn, SpawnB, T1, 0.f); ML::SetName(L, B, TEXT("Probe side gate"));
    const int32 JoinB = FMath::Max(NearestVertex(Route, AlongPolyline(Route, .30), 1), SegmentB + 1);
    FString B1;
    B1 = ML::ChainPoint(L, ML::MonsterPath, B1, RoadB, T1, B);
    B1 = ML::ChainPoint(L, ML::MonsterPath, B1, Route[JoinB], T1);
    ML::SetName(L, B1, TEXT("Side road"));
    const FCireMapMarker* PA2 = ML::Find(L, A2); const FCireMapMarker* PB1 = ML::Find(L, B1);
    if (!PA2 || PA2->MergeInto != A1 || !PB1 || PB1->MergeInto != A1) { Why = TEXT("the detour and the side road did not merge into the main road"); return false; }
    Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_LAYOUT spawns=2 paths=3 detour=(%.0f,%.0f) joins=%d spawnB=(%.0f,%.0f) joinsB=%d route_points=%d"), Detour.X, Detour.Y, Join, SpawnB.X, SpawnB.Y, JoinB, Route.Num()));
    return true;
}
ACireHero* KiterFor(ACireGameMode* Mode, int32 Realm)
{
    for (ACireHero* H : Mode->Heroes) if (IsValid(H) && H->TeamId == Realm && !H->bDead) return H;
    return nullptr;
}
void Finish(bool bExit = true)
{
    P.bDone = true;
    Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_%s %s"), P.bPass ? TEXT("PASS") : TEXT("FAIL"), *P.Summary));
    FString Path;
    if (!FParse::Value(FCommandLine::Get(), TEXT("CireLayoutProbeSummary="), Path)) Path = FPaths::ProjectSavedDir() / TEXT("LayoutProbe/probe.txt");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    FFileHelper::SaveStringToFile(FString::Join(P.Lines, TEXT("\n")) + TEXT("\n"), *Path);
    if (bExit) FPlatformMisc::RequestExitWithStatus(false, P.bPass ? 0 : 1);
}
FVector Grounded(UWorld* World, const FVector& At, const ACharacter* C)
{
    FVector Out = At;
    if (CireTownMap::IsActive()) Out.Z = CireTownMap::Ground(World, FVector2D(At)) + C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f;
    return Out;
}
}

void CireLayoutWiring::InitializeProbe(ACireGameMode* Mode)
{
    P = FProbe();
    P.bEnabled = Mode && FParse::Param(FCommandLine::Get(), TEXT("CireLayoutProbe"));
    if (!P.bEnabled) return;
    Mode->BotFillTimer = 0;
    Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_READY map=%s"), *ML::ActiveMap()));
}

bool CireLayoutWiring::TickProbe(ACireGameMode* Mode, float Delta)
{
    if (!P.bEnabled || P.bDone || !Mode) return false;
    UWorld* World = Mode->GetWorld();
    auto* State = Mode->GetGameState<ACireGameState>();
    P.Clock += Delta;
    Mode->WaveTimer = 1.e6f; // no normal waves: the probe spawns director waves itself
    for (auto* H : Mode->Heroes) if (IsValid(H) && !H->bBot) { H->Draft(2); H->bBot = true; H->bAutoAttack = false; H->HeroName = TEXT("Probe player"); }
    switch (P.Stage)
    {
    case 0: // bots, navmesh, survival
    {
        if (!Mode->bBotsFilled || Mode->Heroes.Num() < 10 || !CireNav::IsReady(World) || !State || State->Phase != 0)
        {
            if (P.Clock > (CireTownMap::IsActive() ? 480.f : 90.f)) { Fail(TEXT("setup timed out")); Finish(); }
            return false;
        }
        // Heroes stand aside: undrafted heroes are ignored by monsters, so the march measures pathing alone.
        for (auto* H : Mode->Heroes) if (IsValid(H)) { H->bDrafted = false; H->Target = nullptr; H->bAutoAttack = false; }
        FCireMapLayout Layout; FString Why;
        if (!BuildLayout(World, Layout, Why)) { Fail(Why); Finish(); return false; }
        // The real pipeline: the layout compiles over the running route document, validates and applies live.
        FCireBattlefieldRoutes Compiled; TArray<FString> Notes; FString Error;
        const bool bCompiled = ML::CompileRoutes(Layout, CireLanePath::Get(World), Compiled, Notes) && CireLanePath::Validate(Compiled, Error);
        for (const FString& N : Notes) Note(TEXT("CIRE_LAYOUT_PROBE_NOTE ") + N);
        if (!bCompiled || !CireLanePath::ApplyLive(World, Compiled, &Error)) { Fail(TEXT("the probe layout does not compile/apply: ") + Error); Finish(); return false; }
        int32 Errors = 0; for (const FCireLayoutIssue& I : ML::Validate(Layout)) if (I.bError) { ++Errors; Note(TEXT("CIRE_LAYOUT_PROBE_ISSUE ") + I.Message); }
        if (Errors) Fail(FString::Printf(TEXT("the probe layout has %d validation errors"), Errors));
        for (int32 Realm = 0; Realm < 2; ++Realm)
            Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_PATHS realm=%d paths=%d spawns=%d lengths=%.0f/%.0f/%.0f"), Realm, CireLanePath::PathCount(World, Realm), CireLanePath::SpawnSpots(Compiled, Realm).Num(),
                CireLanePath::PathLengthOf(World, Realm, 0), CireLanePath::PathLengthOf(World, Realm, 1), CireLanePath::PathLengthOf(World, Realm, 2)));
        // The stall failsafe would despawn a slow column (or the kited unit) after 100 + 20 s; the probe measures arrival,
        // so it gets the longest failsafe the data allows. Stuck nudges (the stuck rescue) stay on.
        {
            FCireWaveConfig Relaxed = CireWaveDirector::Config(World); Relaxed.MaxWaveSeconds = 900.f;
            CireWaveDirector::ApplyLive(Mode, Relaxed);
        }
        CireWaveDirector::RescueCounts(Mode, P.Nudges0, P.Marches0, P.Despawns0);
        CireLeash::Counts(P.Returns0, P.Arrivals0, P.Rescues0);
        P.Lives0[0] = State->EmberLives; P.Lives0[1] = State->DuskLives;
        // Two director waves (12 + 12 per realm): the split sends 3 / 3 / 6 units down the three paths of each realm.
        FCireWaveDef Wave; Wave.Label = TEXT("Layout probe"); Wave.Type = ECireWaveType::Custom; Wave.SpawnInterval = .35f; Wave.bMustClear = false;
        auto Unit = [](const TCHAR* Id, int32 Count) { FCireWaveUnit U; U.Archetype = Id; U.Count = Count; return U; };
        Wave.Units = {Unit(TEXT("hollow_infantry"), 4), Unit(TEXT("ironbound_bruiser"), 3), Unit(TEXT("barbed_hunter"), 3), Unit(TEXT("blight_caster"), 2)};
        if (!CireWaveDirector::SpawnNow(Mode, Wave, &Error)) { Fail(TEXT("the probe wave did not queue: ") + Error); Finish(); return false; }
        Note(TEXT("CIRE_LAYOUT_PROBE_MARCH_QUEUED units=12 per realm"));
        P.Stage = 1; P.StageAt = P.Clock;
        return false;
    }
    case 1: // collect the spawned units
    {
        if (CireWaveDirector::HasPendingSpawns(Mode) && P.Clock - P.StageAt < 30) return false;
        for (ACireMonster* M : Mode->Monsters)
            if (IsValid(M) && M->PackId < 0 && !P.Marchers.ContainsByPredicate([&](const FMarcher& X) { return X.M == M; }))
            { FMarcher X; X.M = M; X.Realm = M->Lane; X.Path = M->LanePath; X.SpawnedAt = P.Clock; P.Marchers.Add(X); }
        int32 Per[2][3] = {{0, 0, 0}, {0, 0, 0}};
        for (const FMarcher& X : P.Marchers) if (X.Path >= 0 && X.Path < 3) ++Per[X.Realm][X.Path];
        Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_SPAWNED units=%d realm0=%d/%d/%d realm1=%d/%d/%d"), P.Marchers.Num(), Per[0][0], Per[0][1], Per[0][2], Per[1][0], Per[1][1], Per[1][2]));
        for (int32 Realm = 0; Realm < 2; ++Realm)
            if (Per[Realm][0] != 3 || Per[Realm][1] != 3 || Per[Realm][2] != 6) Fail(FString::Printf(TEXT("realm %d split %d/%d/%d, expected 3/3/6"), Realm, Per[Realm][0], Per[Realm][1], Per[Realm][2]));
        P.Stage = 2; P.StageAt = P.Clock;
        return false;
    }
    case 2: // march: every unit must arrive at its realm's castle
    {
        int32 Alive = 0;
        for (FMarcher& X : P.Marchers)
        {
            if (X.bArrived || X.bDied) continue;
            ACireMonster* M = X.M.Get();
            if (!IsValid(M) || M->IsActorBeingDestroyed() || !Mode->Monsters.Contains(M))
            {
                // Leaked at the goal (Mode::Leak removes it) unless it died on the way.
                if (IsValid(M) && M->Health <= 0) X.bDied = true; else { X.bArrived = true; X.ArrivedAt = P.Clock; }
                continue;
            }
            ++Alive;
        }
        const float Limit = FMath::Max(300.f, CireLanePath::PathLengthOf(World, 0, 1) / 100.f * 1.2f);
        if (Alive > 0 && P.Clock - P.StageAt < Limit) return false;
        for (const FMarcher& X : P.Marchers)
            if (!X.bArrived && !X.bDied && X.M.IsValid())
            {
                const ACireMonster* M = X.M.Get();
                const FVector2D L = CireLanePath::ToLocal(X.Realm, M->GetActorLocation());
                Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_STRAGGLER realm=%d path=%d at=(%.0f,%.0f) progress=%.3f waypoint=%d off_path=%.0f speed=%.0f leash=%d victim=%d"), X.Realm, X.Path, L.X, L.Y,
                    CireLanePath::PathProgress(World, X.Realm, X.Path, M->GetActorLocation()), M->LaneWaypointIndex, CireLanePath::DistanceToUnitPath(M, M->GetActorLocation()),
                    M->GetVelocity().Size2D(), M->LeashState, M->Victim ? 1 : 0));
            }
        int32 Arrived[2][3] = {{0, 0, 0}, {0, 0, 0}}, Total = 0; float Longest = 0;
        for (const FMarcher& X : P.Marchers) if (X.bArrived && X.Path >= 0 && X.Path < 3) { ++Arrived[X.Realm][X.Path]; ++Total; Longest = FMath::Max(Longest, X.ArrivedAt - X.SpawnedAt); }
        int32 Nudges = 0, Marches = 0, Despawns = 0; CireWaveDirector::RescueCounts(Mode, Nudges, Marches, Despawns);
        int32 Returns = 0, Arrivals = 0, Rescues = 0; CireLeash::Counts(Returns, Arrivals, Rescues);
        Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_MARCH arrived=%d/%d realm0=%d/%d/%d realm1=%d/%d/%d longest=%.1fs nudges=%d failsafe=%d leash_returns=%d lives=%d/%d"),
            Total, P.Marchers.Num(), Arrived[0][0], Arrived[0][1], Arrived[0][2], Arrived[1][0], Arrived[1][1], Arrived[1][2], Longest, Nudges - P.Nudges0,
            (Marches - P.Marches0) + (Despawns - P.Despawns0), Returns - P.Returns0, State->EmberLives, State->DuskLives));
        if (Total != P.Marchers.Num()) Fail(FString::Printf(TEXT("only %d/%d marchers reached the castle within %.0f s"), Total, P.Marchers.Num(), Limit));
        for (int32 Realm = 0; Realm < 2; ++Realm) for (int32 Path = 0; Path < 3; ++Path)
            if (Arrived[Realm][Path] == 0) Fail(FString::Printf(TEXT("nothing arrived down path %d of realm %d"), Path, Realm));
        if (Despawns != P.Despawns0) Fail(FString::Printf(TEXT("%d marchers were despawned by the stall failsafe"), Despawns - P.Despawns0));
        if (Returns != P.Returns0) Fail(FString::Printf(TEXT("%d leash returns during a march with no targets (stuck units must never leash)"), Returns - P.Returns0));
        if (State->EmberLives != P.Lives0[0] - Arrived[0][0] - Arrived[0][1] - Arrived[0][2] || State->DuskLives != P.Lives0[1] - Arrived[1][0] - Arrived[1][1] - Arrived[1][2])
            Fail(TEXT("each arrival must cost its realm exactly one life"));
        P.Summary = FString::Printf(TEXT("march=%d/%d paths=3x2 spawns=2x2 longest=%.0fs nudges=%d"), Total, P.Marchers.Num(), Longest, Nudges - P.Nudges0);
        // Kite: one fresh unit per realm; realm 0 is walked off its path by a kiting hero, realm 1 is dragged past its leash.
        FCireWaveDef One; One.Label = TEXT("Kite probe"); One.Type = ECireWaveType::Custom; One.SpawnInterval = 0; One.bMustClear = false;
        FCireWaveUnit U; U.Archetype = TEXT("ironbound_bruiser"); U.Count = 1; One.Units = {U};
        FString Error;
        const int32 Before = Mode->Monsters.Num();
        if (!CireWaveDirector::SpawnNow(Mode, One, &Error)) { Fail(TEXT("kite unit did not queue: ") + Error); Finish(); return false; }
        for (int32 I = 0; I < 8 && CireWaveDirector::HasPendingSpawns(Mode); ++I) CireWaveDirector::TickSurvival(Mode, .5f);
        for (int32 I = Before; I < Mode->Monsters.Num(); ++I)
        {
            ACireMonster* M = Mode->Monsters[I];
            if (!IsValid(M) || M->PackId >= 0) continue;
            FKite& K = P.Kites[FMath::Clamp(M->Lane, 0, 1)];
            K.Realm = M->Lane; K.bDrag = M->Lane == 1; K.M = M; K.H = KiterFor(Mode, M->Lane);
            M->MaxHealth = M->Health = 1.e6f; // the kite measures the leash, not a kill
        }
        P.Stage = 3; P.StageAt = P.Clock;
        return false;
    }
    case 3: // kite both realms
    {
        bool bAllDone = true;
        for (FKite& K : P.Kites)
        {
            ACireMonster* M = K.M.Get(); ACireHero* H = K.H.Get();
            if (K.Stage >= 9) continue;
            bAllDone = false;
            const float Age = P.Clock - K.StageAt;
            if (!IsValid(M) || !Mode->Monsters.Contains(M))
            {
                if (K.Stage >= 5 && (!IsValid(M) || M->Health > 0)) { K.bArrived = true; K.Stage = 9; Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_KITE_ARRIVED realm=%d"), K.Realm)); }
                else { Fail(FString::Printf(TEXT("kite unit of realm %d vanished at stage %d (valid=%d health=%.0f)"), K.Realm, K.Stage, IsValid(M) ? 1 : 0, IsValid(M) ? M->Health : -1.f)); K.Stage = 9; }
                continue;
            }
            if (!IsValid(H)) { Fail(FString::Printf(TEXT("no kiting hero in realm %d"), K.Realm)); K.Stage = 9; continue; }
            const float OffPath = CireLanePath::DistanceToUnitPath(M, M->GetActorLocation());
            const float Radius = CireLeash::RadiusFor(CireLeash::Rules(), M);
            K.MaxOffPath = FMath::Max(K.MaxOffPath, OffPath);
            switch (K.Stage)
            {
            case 0: // let it march a little, then the kiter steps in next to it
                if (Age < 6.f) break;
                {
                    // A passive kiter: no auto attack, no pending swing, no kit (Executioner would kill the unit outright).
                    H->bBot = false; H->bDrafted = true; H->bAutoAttack = false; H->Health = H->MaxHealth = 1.e6f; H->bDead = false;
                    H->PendingAttackTarget.Reset(); H->Target = nullptr; H->Skills.Reset(); H->Cooldowns.Reset();
                    const FVector At = Grounded(World, M->GetActorLocation() + M->GetActorForwardVector() * 250.f, H);
                    H->GetCharacterMovement()->StopMovementImmediately();
                    H->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
                    CireThreat::Engage(M, H); CireThreat::AddRaw(M, H, 500.f); CireThreat::Select(M);
                    // The kite destination: a navmesh spot well outside the leash zone.
                    const FVector2D Here = CireLanePath::ToLocal(K.Realm, M->GetActorLocation());
                    FVector2D Best; bool bFound = false;
                    for (int32 Ring = 0; Ring < 3 && !bFound; ++Ring)
                        for (int32 A = 0; A < 16 && !bFound; ++A)
                        {
                            const double Angle = A * PI / 8., Dist = 3200. + Ring * 900.;
                            FVector2D C;
                            if (!OnNav(World, K.Realm, Here + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Dist, C)) continue;
                            double D = 0; CireLanePath::NearestOnPolyline(CireLanePath::UnitPath(M), C, &D);
                            if (D < Radius + 400.) continue;
                            FVector From; CireNav::Project(World, H->GetActorLocation(), From, FVector(200, 200, 600), 45.f);
                            const FCireNavPath Path = CireNav::FindPath(World, From, CireLanePath::ToWorld(K.Realm, C, 60.f), 45.f, false);
                            if (!Path.bValid || Path.Points.Num() < 2) continue;
                            K.Route = Path.Points; bFound = true; Best = C;
                        }
                    if (!bFound) K.bDrag = true; // no walkable kite route here: drag instead
                    K.Why = K.bDrag ? TEXT("drag") : TEXT("walk");
                    Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_KITE_START realm=%d mode=%s victim=%d radius=%.0f to=(%.0f,%.0f)"), K.Realm, *K.Why, M->Victim == H ? 1 : 0, Radius, Best.X, Best.Y));
                    K.Stage = 1; K.StageAt = P.Clock;
                }
                break;
            case 1: // lead the unit away (walk) or push it out (drag) until the leash fires
            {
                if (CireLeash::IsReturning(M))
                {
                    K.bSawReturn = true; K.bImmune = !CireLeash::AllowDamage(M); K.bThreatKept = M->Threat.Contains(H);
                    // The kiter leaves: he keeps his threat but stands far outside the zone, so the unit must march on.
                    if (!K.Route.IsEmpty()) H->SetActorLocation(Grounded(World, K.Route.Last(), H), false, nullptr, ETeleportType::TeleportPhysics);
                    else H->SetActorLocation(Grounded(World, CireLanePath::BasePosition(World, K.Realm), H), false, nullptr, ETeleportType::TeleportPhysics);
                    Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_KITE_LEASHED realm=%d mode=%s off_path=%.0f max_off_path=%.0f after=%.1fs immune=%d threat_kept=%d"), K.Realm, *K.Why,
                        OffPath, K.MaxOffPath, Age, K.bImmune ? 1 : 0, K.bThreatKept ? 1 : 0));
                    K.Stage = 2; K.StageAt = P.Clock;
                    break;
                }
                if (Age > 45.f) { Fail(FString::Printf(TEXT("realm %d: the unit was never leashed (max %.0f cm off its path)"), K.Realm, K.MaxOffPath)); K.Stage = 9; break; }
                if (!K.bDrag)
                {
                    // The kiter backs away along the navmesh at walking pace, staying ahead of the unit.
                    const float Step = 330.f * Delta;
                    FVector Pos = H->GetActorLocation();
                    float Left = Step;
                    while (Left > 0 && K.RouteIndex < K.Route.Num())
                    {
                        const FVector Target = Grounded(World, K.Route[K.RouteIndex], H);
                        const float D = static_cast<float>(FVector::Dist2D(Pos, Target));
                        if (D <= Left) { Pos = Target; Left -= D; ++K.RouteIndex; }
                        else { Pos += (Target - Pos).GetSafeNormal2D() * Left; Left = 0; }
                    }
                    if (FVector::Dist2D(H->GetActorLocation(), M->GetActorLocation()) < 500.f)
                        H->SetActorLocation(Grounded(World, Pos, H), false, nullptr, ETeleportType::TeleportPhysics);
                    if (K.RouteIndex >= K.Route.Num() && Age > 25.f) K.bDrag = true; // at the end of the kite route: drag the rest
                }
                else
                {
                    // Dragged (a pull or a knockback): the kiter stands on the path, inside the zone, with top threat; the unit
                    // is chasing him when it is pulled 3+ m past its leash radius onto the navmesh.
                    const TArray<FVector2D>& Path = CireLanePath::UnitPath(M);
                    const FVector2D Here = CireLanePath::ToLocal(K.Realm, M->GetActorLocation());
                    const FVector2D On = CireLanePath::NearestOnPolyline(Path, Here);
                    if (!K.bDragPlaced)
                    {
                        H->GetCharacterMovement()->StopMovementImmediately();
                        H->SetActorLocation(Grounded(World, CireLanePath::ToWorld(K.Realm, On, 0), H), false, nullptr, ETeleportType::TeleportPhysics);
                        M->LeashReengageAt = 0;
                        CireThreat::AddRaw(M, H, 1000.f); CireThreat::Select(M);
                        K.bDragPlaced = true; K.DragAt = P.Clock;
                    }
                    else if (!K.bPulled && P.Clock - K.DragAt > 1.5f && M->LeashState == 1)
                    {
                        FVector2D Pull = FVector2D::ZeroVector; bool bFound = false;
                        for (int32 Ring = 0; Ring < 4 && !bFound; ++Ring)
                            for (int32 A = 0; A < 16 && !bFound; ++A)
                            {
                                const double Angle = A * PI / 8.;
                                FVector2D C;
                                if (!OnNav(World, K.Realm, On + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (Radius + 300. + Ring * 300.), C)) continue;
                                double D = 0; CireLanePath::NearestOnPolyline(Path, C, &D);
                                if (D > Radius + 100.) { Pull = C; bFound = true; }
                            }
                        if (!bFound) { Pull = On + FVector2D(0, 1) * (Radius + 400.f); }
                        M->SetActorLocation(Grounded(World, CireLanePath::ToWorld(K.Realm, Pull, 0), M), false, nullptr, ETeleportType::TeleportPhysics);
                        K.bPulled = true;
                        Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_KITE_PULLED realm=%d to=(%.0f,%.0f) on_nav=%d chasing=%d"), K.Realm, Pull.X, Pull.Y, bFound ? 1 : 0, M->Victim == H ? 1 : 0));
                    }
                    else if (!K.bPulled && P.Clock - K.DragAt > 6.f) { K.bDragPlaced = false; } // not chasing yet: place the kiter again
                }
                break;
            }
            case 2: // walking back: evading, regenerating, not pursuing
                if (!CireLeash::IsReturning(M))
                {
                    K.bResumed = OffPath < 450.f;
                    K.ReturnedAt = P.Clock; K.ProgressAtResume = CireLanePath::PathProgress(World, K.Realm, M->LanePath, M->GetActorLocation());
                    Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_KITE_RETURNED realm=%d after=%.1fs off_path=%.0f returns=%d victim=%d threat_kept=%d"), K.Realm, Age, OffPath, M->LeashReturns,
                        M->Victim == H ? 1 : 0, M->Threat.Contains(H) ? 1 : 0));
                    if (!K.bResumed) Fail(FString::Printf(TEXT("realm %d: the unit ended its return %.0f cm off its path"), K.Realm, OffPath));
                    K.Stage = 3; K.StageAt = P.Clock;
                }
                else if (Age > 40.f) { Fail(FString::Printf(TEXT("realm %d: the unit did not get back to its path within 40 s"), K.Realm)); K.Stage = 9; }
                break;
            case 3: // it marches on toward the castle (the kiter stays out of its zone)
            {
                const float Progress = CireLanePath::PathProgress(World, K.Realm, M->LanePath, M->GetActorLocation());
                if (Progress > K.ProgressAtResume + .02f)
                {
                    K.bMarchedOn = true;
                    Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_KITE_RESUMED realm=%d progress=%.3f->%.3f"), K.Realm, K.ProgressAtResume, Progress));
                    K.Stage = 5; K.StageAt = P.Clock;
                }
                else if (Age > 60.f)
                {
                    const FVector2D L = CireLanePath::ToLocal(K.Realm, M->GetActorLocation());
                    Fail(FString::Printf(TEXT("realm %d: the unit did not march on after returning (at=(%.0f,%.0f) progress=%.3f waypoint=%d speed=%.0f leash=%d victim=%d kiter_off_path=%.0f)"),
                        K.Realm, L.X, L.Y, Progress, M->LaneWaypointIndex, M->GetVelocity().Size2D(), M->LeashState, M->Victim ? 1 : 0, CireLanePath::DistanceToUnitPath(M, H->GetActorLocation())));
                    K.Stage = 9;
                }
                break;
            }
            case 5: // and arrives
                if (Age > FMath::Max(240.f, CireLanePath::PathLengthOf(World, K.Realm, M->LanePath) / 100.f)) { Fail(FString::Printf(TEXT("realm %d: the kited unit never arrived"), K.Realm)); K.Stage = 9; }
                break;
            default: break;
            }
        }
        if (!bAllDone) return false;
        int32 Returns = 0, Arrivals = 0, Rescues = 0; CireLeash::Counts(Returns, Arrivals, Rescues);
        int32 Leashed = 0;
        for (const FKite& K : P.Kites)
        {
            Note(FString::Printf(TEXT("CIRE_LAYOUT_PROBE_KITE realm=%d mode=%s leashed=%d immune=%d threat_kept=%d back_on_path=%d marched_on=%d arrived=%d max_off_path=%.0f"), K.Realm, *K.Why,
                K.bSawReturn ? 1 : 0, K.bImmune ? 1 : 0, K.bThreatKept ? 1 : 0, K.bResumed ? 1 : 0, K.bMarchedOn ? 1 : 0, K.bArrived ? 1 : 0, K.MaxOffPath));
            Leashed += K.bSawReturn && K.bResumed && K.bMarchedOn && K.bArrived ? 1 : 0;
            if (!K.bThreatKept && K.bSawReturn) Fail(FString::Printf(TEXT("realm %d: the leash dropped threat"), K.Realm));
        }
        if (Leashed != 2) Fail(FString::Printf(TEXT("%d/2 kited units returned to their path and marched on to the castle"), Leashed));
        P.Summary += FString::Printf(TEXT(" kites=%d/2 leash_returns=%d rescues=%d"), Leashed, Returns - P.Returns0, Rescues - P.Rescues0);
        Finish();
        return false;
    }
    default: return false;
    }
}
#endif
