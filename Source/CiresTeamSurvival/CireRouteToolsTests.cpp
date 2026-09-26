// dev-route-tools: native checks for the 1..16 challenge pack generalisation, the realm frames and the map layout
// model. Part of -CireCombatExpansionProbe (Tools/RunExpansionChecks.py); logs CIRE_ROUTE_TOOLS_PASS / _FAIL.
#include "CireRouteEditor.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireLoot.h"
#include "CireMapLayout.h"
#include "Rules/CireItemRules.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireRouteTools, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
namespace ML = CireMapLayout;
namespace CI = Cires::Items;
struct FRouteChecker
{
    int32 Count = 0; bool bPass = true;
    void operator()(bool bValue, const FString& Why) { ++Count; if (!bValue) { bPass = false; UE_LOG(LogCireRouteTools, Error, TEXT("CIRE_ROUTE_TOOLS_CHECK_FAIL %s"), *Why); } }
};
bool HasIssue(const TArray<FCireLayoutIssue>& Issues, const TCHAR* Fragment, int32 Team = -1)
{
    for (const FCireLayoutIssue& I : Issues) if (I.Message.Contains(Fragment) && (Team < 0 || I.Team == Team)) return true;
    return false;
}
FString Issues(const TArray<FCireLayoutIssue>& List) { return FString::JoinBy(List, TEXT(" / "), [](const FCireLayoutIssue& I) { return I.Message; }); }
FString PackJson(int32 Count, bool bObjects)
{
    TArray<FString> Items;
    for (int32 I = 0; I < Count; ++I)
    {
        const double X = 30000 - I * 1700, Y = (I % 2 ? 900 : -900);
        Items.Add(bObjects ? FString::Printf(TEXT("{ \"x\": %.0f, \"y\": %.0f, \"radius\": %d, \"tier\": %d }"), X, Y, 300 + I * 50, 1 + I % 10)
                           : FString::Printf(TEXT("[%.0f, %.0f]"), X, Y));
    }
    return TEXT("[") + FString::Join(Items, TEXT(",")) + TEXT("]");
}
/** The shipped route document with its lane 0 "bays" replaced (lane 1 keeps none). */
FString WithBays(const FString& Json, const FString& BaysJson)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) return FString();
    TSharedPtr<FJsonValue> Parsed;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BaysJson), Parsed) || !Parsed) return FString();
    const TArray<TSharedPtr<FJsonValue>>* Lanes = nullptr;
    if (Root->TryGetArrayField(TEXT("lanes"), Lanes) && Lanes && Lanes->Num() == 2) (*Lanes)[0]->AsObject()->SetField(TEXT("bays"), Parsed);
    FString Out; FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Out)); return Out;
}
}

bool CireRouteEditor::RunTests(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    UWorld* World = Mode->GetWorld();
    FRouteChecker Check;
    FString Error;

    // ================================================================ route document: 1..16 packs with radius and tier
    FString Shipped;
    Check(FFileHelper::LoadFileToString(Shipped, *(FPaths::ProjectContentDir() / TEXT("Data/BattlefieldRoutes.json"))), TEXT("the route document loads"));
    FCireBattlefieldRoutes Doc;
    Check(CireLanePath::ParseJson(Shipped, Doc, Error), TEXT("the shipped route document parses: ") + Error);
    Check(CireLanePath::BayCount(Doc, 0) == 3 && CireLanePath::BayCount(Doc, 1) == 3 && Doc.Bays[0].Num() == 0, TEXT("no authored packs: three automatic bays"));
    for (int32 Bay = 1; Bay <= 3; ++Bay)
    {
        const FCireChallengeBay Auto = CireLanePath::BayAt(Doc, 0, Bay);
        Check(Auto.Tier == Bay && Auto.Radius == FCireChallengeBay::DefaultRadius && Auto.Position == CireLanePath::BayPoint(Doc, 0, Bay), FString::Printf(TEXT("automatic bay %d keeps tier = bay and the default radius"), Bay));
    }
    for (const int32 Count : {1, 5, 16})
    {
        FCireBattlefieldRoutes R;
        const bool bOk = CireLanePath::ParseJson(WithBays(Shipped, PackJson(Count, true)), R, Error);
        Check(bOk && CireLanePath::BayCount(R, 0) == Count && CireLanePath::BayCount(R, 1) == 3, FString::Printf(TEXT("%d authored packs parse (%s)"), Count, *Error));
        if (bOk)
        {
            const FCireChallengeBay Last = CireLanePath::BayAt(R, 0, Count);
            Check(Last.Tier == 1 + (Count - 1) % 10 && FMath::IsNearlyEqual(Last.Radius, 300.f + (Count - 1) * 50.f), FString::Printf(TEXT("pack %d keeps its own radius and tier"), Count));
            FCireBattlefieldRoutes Round;
            Check(CireLanePath::ParseJson(CireLanePath::ToJson(R), Round, Error) && CireLanePath::SameLayout(Round, R), FString::Printf(TEXT("%d packs round-trip through the writer"), Count));
        }
    }
    Check(!CireLanePath::ParseJson(WithBays(Shipped, PackJson(17, true)), Doc, Error), TEXT("17 packs are rejected"));
    Check(!CireLanePath::ParseJson(WithBays(Shipped, TEXT("[]")), Doc, Error), TEXT("an empty bays list is rejected (omit it for automatic bays)"));
    Check(!CireLanePath::ParseJson(WithBays(Shipped, TEXT("[{ \"x\": 20000, \"y\": 0, \"radius\": 150 }]")), Doc, Error), TEXT("a radius under 2 m is rejected"));
    Check(!CireLanePath::ParseJson(WithBays(Shipped, TEXT("[{ \"x\": 20000, \"y\": 0, \"tier\": 11 }]")), Doc, Error), TEXT("tier 11 is rejected"));
    Check(!CireLanePath::ParseJson(WithBays(Shipped, TEXT("[{ \"x\": 20000, \"y\": 0, \"colour\": 1 }]")), Doc, Error), TEXT("unknown pack keys are rejected"));
    Check(!CireLanePath::ParseJson(WithBays(Shipped, TEXT("[[20000, 0], [20100, 0]]")), Doc, Error), TEXT("packs closer than 2 m are rejected"));
    {
        FCireBattlefieldRoutes Legacy;
        Check(CireLanePath::ParseJson(WithBays(Shipped, PackJson(3, false)), Legacy, Error) && Legacy.Bays[0].Num() == 3 &&
            Legacy.Bays[0][2].Tier == 3 && Legacy.Bays[0][1].Radius == FCireChallengeBay::DefaultRadius, TEXT("legacy [x, y] bays read as tiers 1..3 with the default radius"));
    }
    {
        // One shared route for both realms ("route"), the authored-once form the writer uses for identical realms.
        FCireBattlefieldRoutes Base; CireLanePath::ParseJson(Shipped, Base, Error);
        Base.Bays[0] = CireLanePath::AutoBays(Base, 0);
        for (FCireChallengeBay& B : Base.Bays[0]) B.Position = FVector2D(FMath::RoundToDouble(B.Position.X), FMath::RoundToDouble(B.Position.Y)); // the writer keeps 0.1 cm
        Base.Bays[1] = Base.Bays[0];
        const FString Json = CireLanePath::ToJson(Base);
        FCireBattlefieldRoutes Shared;
        Check(Json.Contains(TEXT("\"route\"")) && !Json.Contains(TEXT("\"lanes\"")), TEXT("identical realms are written once as a shared route"));
        Check(CireLanePath::ParseJson(Json, Shared, Error) && Shared.LocalPoints[0] == Shared.LocalPoints[1] && Shared.Bays[1] == Base.Bays[0], TEXT("a shared route applies to both realms"));
        FCireBattlefieldRoutes Split = Base; Split.Bays[1].SetNum(2);
        Check(CireLanePath::ToJson(Split).Contains(TEXT("\"lanes\"")), TEXT("realms that differ are written as two lanes"));
        TSharedPtr<FJsonObject> Root; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root);
        if (Root) { Root->SetArrayField(TEXT("lanes"), TArray<TSharedPtr<FJsonValue>>()); FString Both; FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Both));
            Check(!CireLanePath::ParseJson(Both, Shared, Error), TEXT("a document with both a shared route and lanes is rejected")); }
    }
    // Realm frames: one layout, two realms.
    for (const FVector2D Local : {FVector2D(0, 0), FVector2D(12000, -700), FVector2D(-1850, 350)})
    {
        const FVector A = CireLanePath::ToWorld(0, Local, 10.f), B = CireLanePath::ToWorld(1, Local, 10.f);
        Check(CireLanePath::ToLocal(0, A).Equals(Local, .01) && CireLanePath::ToLocal(1, B).Equals(Local, .01), TEXT("realm frames round-trip realm-local points"));
        Check(FVector2D(B - A).Equals(CireLanePath::RealmOrigin(1) - CireLanePath::RealmOrigin(0), .01), TEXT("the same local point sits one realm offset apart in the two realms"));
    }

    // ================================================================ live: 5 packs applied, visuals, replication, spawning
    {
        const FCireBattlefieldRoutes Original = CireLanePath::Get(World);
        ACireWorld* Town = nullptr; for (TActorIterator<ACireWorld> It(World); It; ++It) { Town = *It; break; }
        ON_SCOPE_EXIT { FString Ignore; CireLanePath::ApplyLive(World, Original, &Ignore); if (Town) Town->Tick(.25f); };
        FCireBattlefieldRoutes Five = Original;
        const TArray<FVector2D>& P = Original.LocalPoints[0];
        const int32 Tiers[] = {1, 1, 2, 3, 1};
        for (int32 I = 0; I < 5 && P.Num() > 6; ++I)
        {
            FCireChallengeBay Bay;
            const FVector2D At = P[FMath::Clamp((I + 1) * P.Num() / 7, 1, P.Num() - 2)];
            Bay.Position = FVector2D(At.X, At.Y > 0 ? At.Y - 700. : At.Y + 700.);
            Bay.Radius = 300.f + I * 100.f; Bay.Tier = Tiers[I];
            Five.Bays[0].Add(Bay);
        }
        Five.Bays[1] = Five.Bays[0];
        const bool bApplied = CireLanePath::ApplyLive(World, Five, &Error);
        Check(bApplied, TEXT("five authored packs apply live: ") + Error);
        if (bApplied)
        {
            Check(CireLanePath::BayCount(World, 0) == 5 && CireLanePath::BayCount(World, 1) == 5, TEXT("both realms report five packs"));
            Check(CireLanePath::ChallengeRadius(World, 1, 5) == 700.f && CireLanePath::ChallengeTier(World, 0, 4) == 3, TEXT("per-pack radius and tier are served live"));
            Check(CireLanePath::ChallengePosition(World, 1, 3, 0).Equals(CireLanePath::ToWorld(1, Five.Bays[1][2].Position, 0.f), .1), TEXT("pack positions go through the realm frame"));
            if (auto* State = World->GetGameState<ACireGameState>())
            {
                const TArray<float>& L = State->LaneLayout;
                Check(L.Num() == 5 + 2 * (1 + 5 * 4) && FMath::RoundToInt(L[5]) == 5 && FMath::IsNearlyEqual(L[6 + 4 * 3 + 2], 600.f) && FMath::RoundToInt(L[6 + 4 * 3 + 3]) == 3,
                    TEXT("packs replicate with x, y, radius and tier"));
            }
            if (Town)
            {
                Town->Tick(.25f);
                Check(Town->BayDais && Town->BayDais->GetInstanceCount() == 10 && Town->RouteLabels.Num() == 12, TEXT("the town rebuilds ten pack daises and their labels live"));
            }
            // Spawning: the packs whose tier is unlocked this round appear, one pack id per realm and bay.
            const TArray<ACireMonster*> Stash = Mode->Monsters;
            Mode->Monsters.Reset();
            CireProgression::SpawnPacks(Mode, 1);
            std::vector<int> TierList(std::begin(Tiers), std::end(Tiers));
            const CI::PackSchedule Schedule = CI::RouteSchedule(CireLoot::Get().Schedule, TierList);
            TSet<int32> Expected, Seen[2];
            for (int32 Bay = 1; Bay <= 5; ++Bay) if (CI::BayTier(Schedule, Bay, Mode->Clock.Round(), 1) > 0) Expected.Add(Bay);
            bool bPlaced = true;
            for (ACireMonster* M : Mode->Monsters)
            {
                if (!IsValid(M) || M->PackId < 0) continue;
                const int32 Bay = CireProgression::PackBayOf(M->PackId);
                Seen[FMath::Clamp(M->Lane, 0, 1)].Add(Bay);
                bPlaced &= FVector::Dist2D(M->SpawnPosition, CireLanePath::ChallengePosition(World, M->Lane, Bay, 0)) <= CireLanePath::ChallengeRadius(World, M->Lane, Bay) + 300.f;
            }
            Check(Expected.Num() > 0 && Seen[0].Num() == Expected.Num() && Seen[1].Num() == Expected.Num() && Seen[0].Includes(Expected), FString::Printf(TEXT("unlocked packs spawn in both realms (%d expected, %d / %d)"), Expected.Num(), Seen[0].Num(), Seen[1].Num()));
            Check(bPlaced, TEXT("every pack member stands inside its pack's arena"));
            Check(CireProgression::PackIdFor(3, 1, 16) != CireProgression::PackIdFor(3, 0, 16) && CireProgression::PackBayOf(CireProgression::PackIdFor(3, 1, 16)) == 16, TEXT("pack ids stay unique for 16 bays per realm"));
            for (ACireMonster* M : Mode->Monsters) if (IsValid(M)) M->Destroy();
            Mode->Monsters = Stash;
        }
    }

    // ================================================================ map layout model
    Check(ML::Types().Num() >= 11 && ML::FindType(ML::PlayerSpawn) && ML::FindType(ML::MonsterPath) && ML::FindType(ML::ChallengePack) && ML::FindType(ML::Vendor) &&
        ML::FindType(ML::Objective) && ML::FindType(ML::BossSpawn) && ML::FindType(ML::Rift) && ML::FindType(ML::Respawn) && ML::FindType(ML::PlayBounds) && ML::FindType(ML::Blocker),
        TEXT("the setter table has all eleven setters"));
    {
        TArray<FString> BuiltIn, Loaded;
        for (const FCireMarkerType& T : ML::BuiltInTypes()) BuiltIn.Add(T.Id.ToString());
        for (const FCireMarkerType& T : ML::Types()) Loaded.Add(T.Id.ToString());
        Check(BuiltIn == Loaded, TEXT("MapMarkerTypes.json lists the same setters as the built-in table"));
    }
    const ECireMarkerOwner T1 = ECireMarkerOwner::Team1, T2 = ECireMarkerOwner::Team2, Shared = ECireMarkerOwner::Shared;
    {
        FCireMapLayout L;
        const TArray<FCireLayoutIssue> Empty = ML::Validate(L);
        Check(HasIssue(Empty, TEXT("T1 has no player spawn"), 1) && HasIssue(Empty, TEXT("T2 has no player spawn"), 2) && HasIssue(Empty, TEXT("No monster spawn targets T1"), 1) &&
            HasIssue(Empty, TEXT("No monster spawn targets T2"), 2) && HasIssue(Empty, TEXT("T1 has no objective"), 1), TEXT("an empty layout is flagged per team"));
        // Player spawns: team-owned, mirrored by default.
        const FString Spawn = ML::Place(L, ML::PlayerSpawn, FVector2D(-1700, 0), T1, 30.f);
        const FCireMapMarker* S = ML::Find(L, Spawn);
        const FCireMapMarker* Twin = S ? ML::Find(L, S->Pair) : nullptr;
        Check(L.Markers.Num() == 2 && S && Twin && Twin->Owner == T2 && Twin->Pair == Spawn && Twin->Position == S->Position && Twin->Yaw == 30.f && Twin->bMirror,
            TEXT("a T1 player spawn produces its mirrored T2 twin"));
        Check(ML::DisplayLabel(L, *S) == TEXT("T1 Spawn 1") && ML::DisplayLabel(L, *Twin) == TEXT("T2 Spawn 1"), TEXT("labels carry the team"));
        const FVector W1 = CireLanePath::ToWorld(ML::RealmOf(S->Owner), S->Position), W2 = CireLanePath::ToWorld(ML::RealmOf(Twin->Owner), Twin->Position);
        Check(ML::RealmOf(T1) == 0 && ML::RealmOf(T2) == 1 && FVector2D(W2 - W1).Equals(CireLanePath::RealmOrigin(1) - CireLanePath::RealmOrigin(0), .01), TEXT("mirrored twins stand at the same spot of the other realm"));
        ML::Move(L, Spawn, FVector2D(-1600, 200)); ML::SetYaw(L, Spawn, 90.f);
        Twin = ML::Find(L, ML::Find(L, Spawn)->Pair);
        Check(Twin && Twin->Position == FVector2D(-1600, 200) && Twin->Yaw == 90.f, TEXT("moving or turning a marker keeps its twin in sync"));

        // Objectives, monster spawns and paths (per team, mirrored).
        const FString Goal = ML::Place(L, ML::Objective, FVector2D(-1850, 0), T1);
        Check(ML::Find(L, Goal)->Radius == 450.f && ML::ObjectiveOf(L, T2) && ML::ObjectiveOf(L, T2)->Owner == T2, TEXT("objectives are placed per team"));
        Check(ML::Place(L, ML::Objective, FVector2D(-1000, 0), T1).IsEmpty(), TEXT("one objective per team"));
        const FString Gate = ML::Place(L, ML::MonsterSpawn, FVector2D(9000, 0), T1, 180.f);
        Check(ML::DisplayLabel(L, *ML::Find(L, Gate)).StartsWith(TEXT("Monsters -> T1")) && ML::Find(L, ML::Find(L, Gate)->Pair)->Target == T2, TEXT("monster spawns target their team; the twin targets the other team"));
        FString Road;
        for (const FVector2D Pt : {FVector2D(7000, 0), FVector2D(5000, 400), FVector2D(2500, 0), FVector2D(-1800, 0)})
            Road = ML::ChainPoint(L, ML::MonsterPath, Road, Pt, T1, Gate);
        bool bReaches = false;
        const TArray<FVector2D> Walk = ML::WalkPolyline(L, Road, &bReaches);
        const FCireMapMarker* RoadM = ML::Find(L, Road);
        Check(RoadM && RoadM->Points.Num() == 4 && RoadM->From == Gate && bReaches && Walk.Num() == 5 && Walk[0] == FVector2D(9000, 0) && ML::PathClosed(L, Road),
            TEXT("Set Path chains points from the spawn to the objective"));
        const FCireMapMarker* RoadTwin = RoadM ? ML::Find(L, RoadM->Pair) : nullptr;
        Check(RoadTwin && RoadTwin->From == ML::Find(L, Gate)->Pair && RoadTwin->Target == T2 && RoadTwin->Points == RoadM->Points, TEXT("the mirrored path links the mirrored spawn and attacks T2"));
        Check(ML::PathsFrom(L, Gate).Num() == 1, TEXT("the spawn links to its path"));
        const FString After = ML::ChainPoint(L, ML::MonsterPath, Road, FVector2D(-1900, 100), T1);
        Check(After != Road && ML::Find(L, After) && ML::Find(L, After)->Points.Num() == 1, TEXT("a finished path is not extended: the next press starts a new path"));
        ML::Remove(L, After);
        // A second spawn whose path merges into the main road.
        const FString Side = ML::Place(L, ML::MonsterSpawn, FVector2D(6000, -1100), T1, 90.f);
        FString Branch = ML::ChainPoint(L, ML::MonsterPath, FString(), FVector2D(5200, -800), T1, Side);
        Branch = ML::ChainPoint(L, ML::MonsterPath, Branch, FVector2D(5050, 300), T1);
        const FCireMapMarker* BranchM = ML::Find(L, Branch);
        bool bBranchReaches = false; const TArray<FVector2D> BranchWalk = ML::WalkPolyline(L, Branch, &bBranchReaches);
        Check(BranchM && BranchM->MergeInto == Road && BranchM->Points.Last() == FVector2D(5000, 400) && bBranchReaches && BranchWalk.Last() == FVector2D(-1800, 0),
            TEXT("a path ending on another path merges into it and reaches the objective through it"));
        RoadM = ML::Find(L, Road);
        Check(BranchM && RoadM && ML::Find(L, BranchM->Pair) && ML::Find(L, BranchM->Pair)->MergeInto == RoadM->Pair, TEXT("the mirrored branch merges into the mirrored road"));
        // Remove / replace path points: neighbours re-chain, order is kept.
        ML::RemovePoint(L, Road, 1);
        RoadM = ML::Find(L, Road);
        Check(RoadM->Points.Num() == 3 && RoadM->Points[0] == FVector2D(7000, 0) && RoadM->Points[1] == FVector2D(2500, 0) && ML::Find(L, RoadM->Pair)->Points == RoadM->Points,
            TEXT("removing a path point re-chains its neighbours in both realms"));
        ML::MovePoint(L, Road, 1, FVector2D(3000, -200));
        RoadM = ML::Find(L, Road);
        Check(RoadM->Points[1] == FVector2D(3000, -200) && RoadM->Points[2] == FVector2D(-1800, 0) && ML::Find(L, RoadM->Pair)->Points[1] == FVector2D(3000, -200), TEXT("replacing a path point keeps the chain order"));

        // Challenge packs: numbered per team, renumbered on removal, radius and tier editable.
        TArray<FString> Packs;
        for (int32 I = 0; I < 3; ++I) Packs.Add(ML::Place(L, ML::ChallengePack, FVector2D(6000 - I * 2000, 900), T1));
        ML::SetTier(L, Packs[2], 4); ML::SetRadius(L, Packs[2], 700.f);
        Check(ML::Number(L, Packs[0]) == 1 && ML::Number(L, Packs[2]) == 3 && ML::Number(L, ML::Find(L, Packs[2])->Pair) == 3, TEXT("packs are numbered per team (twins too)"));
        const FString ThirdTwin = ML::Find(L, Packs[2])->Pair;
        ML::Remove(L, Packs[1]);
        Check(!ML::Find(L, Packs[1]) && ML::Number(L, Packs[2]) == 2 && ML::Number(L, ThirdTwin) == 2 && ML::OfType(L, ML::ChallengePack).Num() == 4 &&
            ML::DisplayLabel(L, *ML::Find(L, Packs[2])) == TEXT("T1 Pack 2  Tier 4"), TEXT("removing a pack removes its twin and renumbers the rest"));
        ML::Move(L, Packs[2], FVector2D(1000, -900));
        Check(ML::Find(L, Packs[2])->Tier == 4 && ML::Find(L, Packs[2])->Radius == 700.f && ML::Find(L, ThirdTwin)->Position == FVector2D(1000, -900), TEXT("replacing a pack keeps its tier and radius"));
        ML::SetRadius(L, Packs[2], 50000.f); ML::SetTier(L, Packs[2], 0);
        Check(ML::Find(L, Packs[2])->Radius == FCireChallengeBay::MaxRadius && ML::Find(L, Packs[2])->Tier == 1, TEXT("pack radius and tier are clamped"));

        // A complete, symmetric layout validates clean.
        TArray<FCireLayoutIssue> Found = ML::Validate(L);
        Check(Found.Num() == 0, TEXT("the mirrored layout validates: ") + Issues(Found));

        // Mirror off breaks symmetry on purpose; per-team validation follows.
        const FString SpawnTwin = ML::Find(L, Spawn)->Pair;
        ML::SetMirror(L, Spawn, false);
        Check(!ML::Find(L, Spawn)->bMirror && ML::Find(L, SpawnTwin) && ML::Find(L, SpawnTwin)->Pair.IsEmpty(), TEXT("mirror off keeps both markers, unpaired"));
        ML::Move(L, Spawn, FVector2D(-1500, 0));
        Check(ML::Find(L, SpawnTwin)->Position == FVector2D(-1600, 200), TEXT("unpaired markers move independently"));
        ML::Remove(L, SpawnTwin);
        Found = ML::Validate(L);
        Check(HasIssue(Found, TEXT("T2 has no player spawn"), 2) && !HasIssue(Found, TEXT("T1 has no player spawn")), TEXT("validation is per team"));
        ML::SetMirror(L, Spawn, true);
        Check(ML::Find(L, ML::Find(L, Spawn)->Pair) && ML::Validate(L).Num() == 0, TEXT("mirror on recreates the twin"));
        // Desync is flagged.
        ML::Find(L, ML::Find(L, Spawn)->Pair)->Position.X += 50;
        Check(HasIssue(ML::Validate(L), TEXT("out of sync")), TEXT("a mirrored pair out of sync is flagged"));
        ML::SyncTwin(L, Spawn);
        Check(!HasIssue(ML::Validate(L), TEXT("out of sync")), TEXT("syncing the pair clears it"));

        // Owners and targets.
        ML::SetTarget(L, Gate, T2);
        Check(ML::Find(L, ML::Find(L, Gate)->Pair)->Target == T1 && ML::DisplayLabel(L, *ML::Find(L, Gate)).StartsWith(TEXT("Monsters -> T2")), TEXT("switching a spawn's target swaps its twin's"));
        Found = ML::Validate(L);
        Check(HasIssue(Found, TEXT("does not reach T2")), TEXT("a path that no longer reaches its target's objective is flagged"));
        ML::SetTarget(L, Gate, T1); ML::SetTarget(L, Road, T1); ML::SetTarget(L, Branch, T1);
        const FString Portal = ML::Place(L, ML::Rift, FVector2D(0, 1000), T1);
        ML::SetOwner(L, Portal, Shared);
        Check(ML::Find(L, Portal)->Owner == Shared && ML::OfType(L, ML::Rift).Num() == 1 && ML::ShownInRealm(*ML::Find(L, Portal), 0) && ML::ShownInRealm(*ML::Find(L, Portal), 1),
            TEXT("a shared marker drops its twin and shows in both realms"));
        ML::SetOwner(L, Portal, T2);
        Check(ML::OfType(L, ML::Rift).Num() == 2 && ML::Find(L, ML::Find(L, Portal)->Pair)->Owner == T1, TEXT("a shared marker given to a team gets a twin again"));

        // Removing a spawn unlinks its paths.
        FCireMapLayout Broken = L;
        ML::Remove(Broken, Side);
        Found = ML::Validate(Broken);
        Check(ML::Find(Broken, Branch) && ML::Find(Broken, Branch)->From.IsEmpty() && HasIssue(Found, TEXT("does not start at a monster spawn"), 1), TEXT("a path whose spawn is removed is flagged"));
        FCireMapLayout NoPath = L;
        ML::Remove(NoPath, Branch);
        Check(HasIssue(ML::Validate(NoPath), TEXT("has no path")), TEXT("a spawn with no path is flagged"));

        // Play bounds and world checks.
        FCireMapLayout Bounded = L;
        FString Bounds;
        for (const FVector2D Pt : {FVector2D(-3000, -1300), FVector2D(10000, -1300), FVector2D(10000, 1300), FVector2D(-3000, 1300)}) Bounds = ML::ChainPoint(Bounded, ML::PlayBounds, Bounds, Pt, T1);
        Check(ML::Find(Bounded, Bounds) && ML::Find(Bounded, Bounds)->Owner == Shared && ML::Find(Bounded, Bounds)->Points.Num() == 4 && ML::Validate(Bounded).Num() == 0, TEXT("play bounds are one shared polygon"));
        ML::Move(Bounded, Gate, FVector2D(12000, 0));
        Check(HasIssue(ML::Validate(Bounded), TEXT("outside the play bounds")), TEXT("markers outside the bounds are flagged"));
        FCireLayoutChecks World1;
        World1.OnNavmesh = [](int32 Realm, const FVector2D& P) { return !(Realm == 1 && P.Equals(FVector2D(-1850, 0))); };
        World1.Walkable = [](int32, const FVector2D& A, const FVector2D& B) { return !(A.Equals(FVector2D(3000, -200)) && B.Equals(FVector2D(-1800, 0))); };
        Found = ML::Validate(L, &World1);
        Check(HasIssue(Found, TEXT("off the navmesh (DARKNIGHT)"), 2) && HasIssue(Found, TEXT("is blocked between points")), TEXT("navmesh and walkability checks report the realm"));

        // Vendors: NPC, sign and stall sub-handles; stall blocking a path; every vendor type per team.
        const FString Shop = ML::Place(L, ML::Vendor, FVector2D(-500, -1000), T1, 90.f);
        // Pin the type: the default is the first entry of Vendors.json, whose order the test must not depend on.
        Check(ML::SetKind(L, Shop, TEXT("weaponsmith")), TEXT("a vendor's type can be set"));
        const FCireMapMarker* V = ML::Find(L, Shop);
        const FCireVendorType* VT = ML::FindVendorType(V->Kind);
        Check(VT && V->SignPos.Equals(V->Position + FVector2D(-VT->SignOffset.Y, VT->SignOffset.X), .5) && V->StallPos.Equals(V->Position + FVector2D(0, VT->StallOffset.X), .5) && V->SignYaw == 90.f,
            TEXT("a vendor gets its type's sign and stall spots turned to its facing"));
        ML::MovePart(L, Shop, ECireVendorPart::Sign, FVector2D(-700, -900));
        V = ML::Find(L, Shop);
        Check(V->SignPos == FVector2D(-700, -900) && V->Position == FVector2D(-500, -1000) && ML::Find(L, V->Pair)->SignPos == FVector2D(-700, -900), TEXT("the sign moves on its own (twin too)"));
        ML::SetPartYaw(L, Shop, ECireVendorPart::Stall, 45.f);
        ML::Move(L, Shop, FVector2D(-400, -1000));
        V = ML::Find(L, Shop);
        Check(V->SignPos == FVector2D(-600, -900) && V->StallYaw == 45.f && V->Position == FVector2D(-400, -1000), TEXT("moving the NPC moves the whole group"));
        ML::ResetPart(L, Shop, ECireVendorPart::Sign);
        Check(!ML::Find(L, Shop)->SignPos.Equals(FVector2D(-600, -900)), TEXT("removing the sign handle puts it back at its default spot"));
        Found = ML::Validate(L);
        Check(HasIssue(Found, TEXT("has no armory vendor"), 1) && HasIssue(Found, TEXT("has no arcane vendor"), 2), TEXT("a vendor type missing from a team is flagged"));
        ML::MovePart(L, Shop, ECireVendorPart::Stall, FVector2D(5000, 400));
        Check(HasIssue(ML::Validate(L), TEXT("the stall blocks")), TEXT("a stall standing in a monster path is flagged"));
        FCireLayoutChecks Tight; Tight.SignClear = [](int32, const FVector2D&, float) { return false; };
        Check(HasIssue(ML::Validate(L, &Tight), TEXT("the sign clips")), TEXT("a sign inside town geometry is flagged"));
        const FString Vendors = ML::VendorsJson(L);
        Check(Vendors.Contains(TEXT("\"vendorId\"")) && Vendors.Contains(TEXT("\"npc\": { \"pos\"")) && Vendors.Contains(TEXT("\"sign\": { \"pos\"")) && Vendors.Contains(TEXT("\"size\"")),
            TEXT("TownVendors.json uses the shared vendor-layout schema"));

        // Documents.
        FCireMapLayout Round;
        const bool bParsed = ML::ParseJson(ML::ToJson(L), Round, Error);
        Check(bParsed && Round.Markers.Num() == L.Markers.Num(), TEXT("the layout round-trips through JSON: ") + Error);
        if (bParsed)
        {
            bool bSame = true;
            for (int32 I = 0; I < L.Markers.Num(); ++I)
            {
                const FCireMapMarker& A = L.Markers[I]; const FCireMapMarker& B = Round.Markers[I];
                bSame &= A.Id == B.Id && A.Type == B.Type && A.Owner == B.Owner && A.Target == B.Target && A.Position.Equals(B.Position, .1) && FMath::IsNearlyEqual(A.Yaw, B.Yaw, .1f) &&
                    A.Points.Num() == B.Points.Num() && A.From == B.From && A.MergeInto == B.MergeInto && A.Pair == B.Pair && A.bMirror == B.bMirror && A.Tier == B.Tier &&
                    A.SignPos.Equals(B.SignPos, .1) && A.StallPos.Equals(B.StallPos, .1) && FMath::IsNearlyEqual(A.StallYaw, B.StallYaw, .1f);
                if (!bSame) { UE_LOG(LogCireRouteTools, Warning, TEXT("CIRE_ROUTE_TOOLS_ROUNDTRIP_DIFF %s"), *A.Id); break; }
            }
            Check(bSame, TEXT("every marker field survives the round trip"));
            const int32 Before = Round.Markers.Num();
            const FString Fresh = ML::Place(Round, ML::Respawn, FVector2D(-1000, 500), T1);
            TSet<FString> Unique; for (const FCireMapMarker& M : Round.Markers) Unique.Add(M.Id);
            Check(!Fresh.IsEmpty() && Round.Markers.Num() == Before + 2 && Unique.Num() == Round.Markers.Num(), TEXT("new markers in a reloaded layout get fresh ids"));
            ML::Remove(Round, Fresh);
            Check(ML::Validate(Round).Num() == ML::Validate(L).Num(), TEXT("the reloaded layout validates the same"));
        }
        Check(!ML::ParseJson(TEXT("{ \"schemaVersion\": 1, \"units\": \"centimeters\", \"markers\": [ { \"id\": \"a\", \"type\": \"dragon\", \"x\": 0, \"y\": 0 } ] }"), Round, Error), TEXT("unknown setter types are rejected"));
        Check(!ML::ParseJson(TEXT("{ \"schemaVersion\": 1, \"units\": \"centimeters\", \"markers\": [ { \"id\": \"a\", \"type\": \"vendor\", \"x\": 0, \"y\": 0, \"team\": 3 } ] }"), Round, Error), TEXT("team 3 is rejected"));
        Check(ML::SanitizeName(TEXT(" Castle Town v2! ")) == TEXT("Castle_Town_v2"), TEXT("layout names become safe file names"));

        // Compile: each team's first path is its march route (spawn -> path -> objective), packs keep radius and tier.
        FCireBattlefieldRoutes Compiled; TArray<FString> Notes;
        const bool bCompiled = ML::CompileRoutes(L, CireLanePath::Get(World), Compiled, Notes);
        Check(bCompiled && Compiled.LocalPoints[0].Num() == 4 && Compiled.LocalPoints[0][0] == FVector2D(9000, 0) && Compiled.LocalPoints[0].Last() == FVector2D(-1800, 0) &&
            Compiled.LocalPoints[1] == Compiled.LocalPoints[0], TEXT("the layout compiles into both realms' march routes"));
        Check(Compiled.Bays[0].Num() == 2 && Compiled.Bays[0][1].Tier == 1 && Compiled.Bays[0][1].Radius == FCireChallengeBay::MaxRadius && Compiled.GoalCenter == FVector2D(-1850, 0),
            TEXT("packs and the goal zone compile with their radius and tier"));
        Check(Notes.ContainsByPredicate([](const FString& N) { return N.Contains(TEXT("more paths")); }), TEXT("extra paths are reported, not dropped silently"));
    }
    {
        // The current route seeds a layout and compiles back to the same march.
        const FCireBattlefieldRoutes Live = CireLanePath::Get(World);
        const FCireMapLayout Seeded = ML::FromRoutes(Live);
        FCireBattlefieldRoutes Back; TArray<FString> Notes;
        Check(ML::CompileRoutes(Seeded, Live, Back, Notes) && Back.LocalPoints[0] == Live.LocalPoints[0] && Back.LocalPoints[1] == Live.LocalPoints[1] && Back.Bays[0] == Live.Bays[0],
            TEXT("route -> layout -> route keeps the march points and packs"));
        const TArray<FCireLayoutIssue> Found = ML::Validate(Seeded);
        Check(Found.Num() == 0, TEXT("the seeded layout validates: ") + Issues(Found));
    }

    UE_LOG(LogCireRouteTools, Display, TEXT("CIRE_ROUTE_TOOLS_%s checks=%d"), Check.bPass ? TEXT("PASS") : TEXT("FAIL"), Check.Count);
    return Check.bPass;
}
#endif
