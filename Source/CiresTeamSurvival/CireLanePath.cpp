#include "CireLanePath.h"
#include "CireGame.h"
#include "CireThreat.h"
#include "CireNPCCombat.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireLanePath, Log, All);

namespace
{
FCireBattlefieldRoutes Defaults()
{
    FCireBattlefieldRoutes R;
    const TArray<FVector2D> Points = {{12000,0},{10800,-650},{8500,-650},{7500,650},{4500,650},{3300,-650},{500,-650},{-1100,0},{-1850,0}};
    R.LocalPoints[0] = R.LocalPoints[1] = Points; return R;
}
FCireBattlefieldRoutes Routes = Defaults();
uint32 RouteRevision = 1;
bool bLoaded = false;
struct FWorldRoutes
{
    FCireBattlefieldRoutes Data;
    uint32 Revision = 0, ReceivedVersion = 0;
};
TMap<TWeakObjectPtr<UWorld>,FWorldRoutes> WorldRoutes;
FWorldRoutes& ForWorld(const UWorld* World)
{
    for (auto It=WorldRoutes.CreateIterator();It;++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    const TWeakObjectPtr<UWorld> Key(const_cast<UWorld*>(World));
    if (auto* Found=WorldRoutes.Find(Key)) return *Found;
    CireLanePath::Get();FWorldRoutes Entry;Entry.Data=Routes;Entry.Revision=RouteRevision;
    return WorldRoutes.Add(Key,MoveTemp(Entry));
}
bool Number(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float& Out, float Min, float Max)
{
    double V = 0;
    if (!O || !O->TryGetNumberField(Key,V) || !FMath::IsFinite(V) || V < Min || V > Max) return false;
    Out = static_cast<float>(V); return true;
}
bool Integer(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, int32& Out, int32 Min, int32 Max)
{
    double V = 0;
    if (!O || !O->TryGetNumberField(Key,V) || !FMath::IsFinite(V) || V < Min || V > Max || V != FMath::FloorToDouble(V)) return false;
    Out = static_cast<int32>(V); return true;
}
bool Keys(const TSharedPtr<FJsonObject>& O, const TSet<FString>& Allowed)
{
    if (!O) return false;
    for (const auto& Pair : O->Values) if (!Allowed.Contains(FString(Pair.Key.ToView()))) return false;
    return true;
}
FVector WorldPoint(int32 Team, FVector2D P, float Z)
{
    return CireLanePath::ToWorld(Team, P, Z); // dev-route-tools: through the realm frame
}
int32 ProjectNext(const TArray<FVector2D>& Points, FVector2D Position)
{
    double Best = TNumericLimits<double>::Max(); int32 Next = 1;
    for (int32 I = 0; I + 1 < Points.Num(); ++I)
    {
        const FVector2D Segment = Points[I + 1] - Points[I];
        const double Alpha = FMath::Clamp(FVector2D::DotProduct(Position - Points[I], Segment) / Segment.SizeSquared(), 0., 1.);
        const double Distance = FVector2D::DistSquared(Position, Points[I] + Segment * Alpha);
        if (Distance < Best) { Best = Distance; Next = I + 1; }
    }
    return Next;
}
double DistanceToPathSquared(const TArray<FVector2D>& Points,FVector2D Position)
{
    double Best=TNumericLimits<double>::Max();
    for(int32 I=0;I+1<Points.Num();++I)
    {
        const FVector2D Segment=Points[I+1]-Points[I];
        const double Alpha=FMath::Clamp(FVector2D::DotProduct(Position-Points[I],Segment)/Segment.SizeSquared(),0.,1.);
        Best=FMath::Min(Best,FVector2D::DistSquared(Position,Points[I]+Segment*Alpha));
    }
    return Best;
}
}

float CireLanePath::CenterY(int32 Team) { return Team == 0 ? -2100.f : 2100.f; }
// dev-route-tools: realm frames. Both realms share one layout, authored once in realm-local coordinates; each realm maps
// it into the world through its own origin. STUB provider: the procedural town's side-by-side offsets (0, -/+2100).
// feat/medieval-kingdom replaces these three bodies with the data-driven frames of the pack town (same signatures).
FVector2D CireLanePath::RealmOrigin(int32 Team) { return FVector2D(0.f, CenterY(FMath::Clamp(Team, 0, 1))); }
FVector2D CireLanePath::ToLocal(int32 Team, const FVector& World) { return FVector2D(World.X, World.Y) - RealmOrigin(Team); }
FVector CireLanePath::ToWorld(int32 Team, const FVector2D& Local, float Z) { const FVector2D P = Local + RealmOrigin(Team); return FVector(P.X, P.Y, Z); }
const FCireBattlefieldRoutes& CireLanePath::Get(const UWorld* World) { if (!bLoaded) Reload(); return World?ForWorld(World).Data:Routes; }
uint32 CireLanePath::Revision(const UWorld* World) { Get(); return World?ForWorld(World).Revision:RouteRevision; }

bool CireLanePath::ParseJson(const FString& Json, FCireBattlefieldRoutes& Out, FString& Error)
{
    auto Fail = [&](const TCHAR* Reason) { Error = Reason; return false; };
    if (Json.Len() > 64 * 1024) return Fail(TEXT("Battlefield routes exceed 64 KB"));
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) return Fail(TEXT("Invalid battlefield route JSON"));
    int32 Schema = 0; FString Units;
    // nav-paths: optional "laneWidth" and "goal" (path editor); per-lane optional "bays".
    if (!Keys(Root,{TEXT("schemaVersion"),TEXT("units"),TEXT("bounds"),TEXT("lanes"),TEXT("route"),TEXT("armoredEscort"),TEXT("laneWidth"),TEXT("goal")}) ||
        !Integer(Root,TEXT("schemaVersion"),Schema,1,1) || !Root->TryGetStringField(TEXT("units"),Units) || Units != TEXT("centimeters"))
        return Fail(TEXT("Expected battlefield route schema 1 in centimeters"));
    const TSharedPtr<FJsonObject>* Bounds = nullptr; const TSharedPtr<FJsonObject>* Escort = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Lanes = nullptr;
    FCireBattlefieldRoutes Candidate;
    if (!Root->TryGetObjectField(TEXT("bounds"),Bounds) || !Bounds ||
        !Keys(*Bounds,{TEXT("minX"),TEXT("maxX"),TEXT("halfWidth")}) ||
        !Number(*Bounds,TEXT("minX"),Candidate.MinX,-10000,-2350) || !Number(*Bounds,TEXT("maxX"),Candidate.MaxX,4000,80000) ||
        !Number(*Bounds,TEXT("halfWidth"),Candidate.HalfWidth,900,1400)) return Fail(TEXT("Invalid lane bounds; realms must remain separate and contain the towns"));
    // nav-paths: lane width and goal zone (defaults keep older documents valid).
    if (Root->HasField(TEXT("laneWidth")) && !Number(Root,TEXT("laneWidth"),Candidate.LaneWidth,360,1000)) return Fail(TEXT("laneWidth must be 360..1000 cm"));
    if (Root->HasField(TEXT("goal")))
    {
        const TSharedPtr<FJsonObject>* Goal = nullptr; float GX = 0, GY = 0, GD = 0, GW = 0;
        if (!Root->TryGetObjectField(TEXT("goal"),Goal) || !Goal || !Keys(*Goal,{TEXT("x"),TEXT("y"),TEXT("depth"),TEXT("width")}) ||
            !Number(*Goal,TEXT("x"),GX,-10000,0) || !Number(*Goal,TEXT("y"),GY,-1400,1400) || !Number(*Goal,TEXT("depth"),GD,300,1600) || !Number(*Goal,TEXT("width"),GW,400,2800))
            return Fail(TEXT("goal needs x, y, depth and width in centimeters"));
        Candidate.GoalCenter = FVector2D(GX,GY); Candidate.GoalSize = FVector2D(GD,GW);
    }
    // dev-route-tools: both realms share one layout, so a route is authored once: "route": { "points", "bays" } in
    // realm-local cm applies to both realms through their origin transforms (CireLanePath::ToWorld). The legacy form,
    // "lanes": [ { "team": 0, ... }, { "team": 1, ... } ], is still read (and written when the realms differ).
    auto ParseLane = [&](const TSharedPtr<FJsonObject>& Lane, int32 Team) -> bool
    {
        const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
        if (!Lane->TryGetArrayField(TEXT("points"),Points) || !Points || Points->Num() < 3 || Points->Num() > 64)
            return Fail(TEXT("Each unique team needs 3..64 local XY points"));
        for (const auto& Point : *Points)
        {
            const TArray<TSharedPtr<FJsonValue>>* XY = nullptr; double X = 0, Y = 0;
            if (!Point || !Point->TryGetArray(XY) || !XY || XY->Num() != 2 || !(*XY)[0]->TryGetNumber(X) || !(*XY)[1]->TryGetNumber(Y) ||
                !FMath::IsFinite(X) || !FMath::IsFinite(Y))
                return Fail(TEXT("Route points must be finite and inside their realm with unit clearance"));
            Candidate.LocalPoints[Team].Add(FVector2D(X,Y));
        }
        // nav-paths: optional challenge bay overrides. dev-route-tools: 1..16 packs, each an object
        // { "x", "y", "radius", "tier" } (radius and tier optional) or a legacy [x, y] pair (tier = its position).
        const TArray<TSharedPtr<FJsonValue>>* Bays = nullptr;
        if (Lane->HasField(TEXT("bays")))
        {
            if (!Lane->TryGetArrayField(TEXT("bays"),Bays) || !Bays || Bays->Num() > FCireBattlefieldRoutes::MaxBays || Bays->Num() == 0)
                return Fail(TEXT("Challenge packs need 1..16 bays"));
            for (int32 Index = 0; Index < Bays->Num(); ++Index)
            {
                const TSharedPtr<FJsonValue>& Bay = (*Bays)[Index];
                FCireChallengeBay Entry; Entry.Tier = FMath::Min(Index + 1, FCireChallengeBay::MaxTier);
                const TArray<TSharedPtr<FJsonValue>>* XY = nullptr; const TSharedPtr<FJsonObject>* Object = nullptr; double X = 0, Y = 0;
                if (Bay && Bay->Type == EJson::Array && Bay->TryGetArray(XY) && XY && XY->Num() == 2 && (*XY)[0]->TryGetNumber(X) && (*XY)[1]->TryGetNumber(Y)) {}
                else if (Bay && Bay->Type == EJson::Object && Bay->TryGetObject(Object) && Object && Keys(*Object,{TEXT("x"),TEXT("y"),TEXT("radius"),TEXT("tier")}) &&
                    (*Object)->TryGetNumberField(TEXT("x"),X) && (*Object)->TryGetNumberField(TEXT("y"),Y))
                {
                    if ((*Object)->HasField(TEXT("radius")) && !Number(*Object,TEXT("radius"),Entry.Radius,FCireChallengeBay::MinRadius,FCireChallengeBay::MaxRadius))
                        return Fail(TEXT("Challenge pack radius must be 200..1500 cm"));
                    if ((*Object)->HasField(TEXT("tier")) && !Integer(*Object,TEXT("tier"),Entry.Tier,1,FCireChallengeBay::MaxTier))
                        return Fail(TEXT("Challenge pack tier must be 1..10"));
                }
                else return Fail(TEXT("Challenge packs are { x, y, radius, tier } objects or [x, y] points"));
                if (!FMath::IsFinite(X) || !FMath::IsFinite(Y)) return Fail(TEXT("Challenge packs are { x, y, radius, tier } objects or [x, y] points"));
                Entry.Position = FVector2D(X,Y);
                Candidate.Bays[Team].Add(Entry);
            }
        }
        return true;
    };
    const TSharedPtr<FJsonObject>* Shared = nullptr;
    if (Root->HasField(TEXT("route")))
    {
        if (Root->HasField(TEXT("lanes"))) return Fail(TEXT("Provide either one shared route or two lane routes, not both"));
        if (!Root->TryGetObjectField(TEXT("route"),Shared) || !Shared || !Keys(*Shared,{TEXT("points"),TEXT("bays")}) || !ParseLane(*Shared,0)) return Error.IsEmpty() ? Fail(TEXT("Invalid shared route")) : false;
        Candidate.LocalPoints[1] = Candidate.LocalPoints[0]; Candidate.Bays[1] = Candidate.Bays[0];
    }
    else
    {
        if (!Root->TryGetArrayField(TEXT("lanes"),Lanes) || !Lanes || Lanes->Num() != 2) return Fail(TEXT("Provide exactly two lane routes"));
        bool Seen[2] = {false,false};
        for (const auto& Value : *Lanes)
        {
            const TSharedPtr<FJsonObject>* Lane = nullptr; int32 Team = -1;
            if (!Value || !Value->TryGetObject(Lane) || !Lane || !Keys(*Lane,{TEXT("team"),TEXT("points"),TEXT("bays")}) ||
                !Integer(*Lane,TEXT("team"),Team,0,1) || Seen[Team]) return Fail(TEXT("Each unique team needs 3..64 local XY points"));
            Seen[Team] = true;
            if (!ParseLane(*Lane,Team)) return false;
        }
    }
    if (!Root->TryGetObjectField(TEXT("armoredEscort"),Escort) || !Escort ||
        !Keys(*Escort,{TEXT("everyWaves"),TEXT("count"),TEXT("leakCost"),TEXT("healthMultiplier"),TEXT("moveSpeed")}) ||
        !Integer(*Escort,TEXT("everyWaves"),Candidate.EscortEveryWaves,0,100) || !Integer(*Escort,TEXT("count"),Candidate.EscortCount,1,4) ||
        !Integer(*Escort,TEXT("leakCost"),Candidate.EscortLeakCost,1,100) ||
        !Number(*Escort,TEXT("healthMultiplier"),Candidate.EscortHealthMultiplier,1,50) || !Number(*Escort,TEXT("moveSpeed"),Candidate.EscortMoveSpeed,50,500))
        return Fail(TEXT("Invalid armored escort schedule or stats"));
    if (!Validate(Candidate,Error)) return false;
    Out = MoveTemp(Candidate); Error.Reset(); return true;
}


// nav-paths: semantic rules shared by the JSON loader and the live path editor.
bool CireLanePath::Validate(const FCireBattlefieldRoutes& R, FString& Error)
{
    auto Fail = [&](const TCHAR* Reason) { Error = Reason; return false; };
    auto Finite2 = [](const FVector2D& P) { return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y); };
    if (!FMath::IsFinite(R.MinX) || !FMath::IsFinite(R.MaxX) || !FMath::IsFinite(R.HalfWidth) || R.MinX < -10000 || R.MinX > -2350 ||
        R.MaxX < 4000 || R.MaxX > 80000 /* world-scale: the realm is 3x longer */ || R.HalfWidth < 900 || R.HalfWidth > 1400)
        return Fail(TEXT("Invalid lane bounds; realms must remain separate and contain the towns"));
    if (!FMath::IsFinite(R.LaneWidth) || R.LaneWidth < 360 || R.LaneWidth > 1000) return Fail(TEXT("laneWidth must be 360..1000 cm"));
    const FVector2D GoalHalf = R.GoalSize * .5;
    if (!Finite2(R.GoalCenter) || !Finite2(R.GoalSize) || R.GoalSize.X < 300 || R.GoalSize.X > 1600 || R.GoalSize.Y < 400 || R.GoalSize.Y > 2 * R.HalfWidth ||
        R.GoalCenter.X - GoalHalf.X < R.MinX || R.GoalCenter.X + GoalHalf.X > -900 || FMath::Abs(R.GoalCenter.Y) + GoalHalf.Y > R.HalfWidth)
        return Fail(TEXT("The castle goal zone must stay inside the castle ward (x <= -900) and the realm"));
    auto InGoal = [&](const FVector2D& P, double Margin)
    { return FMath::Abs(P.X - R.GoalCenter.X) <= GoalHalf.X + Margin && FMath::Abs(P.Y - R.GoalCenter.Y) <= GoalHalf.Y + Margin; };
    for (int32 Team = 0; Team < 2; ++Team)
    {
        const auto& Points = R.LocalPoints[Team];
        if (Points.Num() < 3 || Points.Num() > 64) return Fail(TEXT("Each unique team needs 3..64 local XY points"));
        for (int32 I = 0; I < Points.Num(); ++I)
        {
            const FVector2D& P = Points[I];
            if (!Finite2(P) || P.X < R.MinX + 100 || P.X > R.MaxX - 100 || FMath::Abs(P.Y) > R.HalfWidth - 150)
                return Fail(TEXT("Route points must be finite and inside their realm with unit clearance"));
            if (I > 0 && FVector2D::DistSquared(Points[I - 1], P) < 2500) return Fail(TEXT("Adjacent route points must be at least 50 cm apart"));
        }
        if (Points[0].X < 3000 || !InGoal(Points.Last(), 0))
            return Fail(TEXT("Routes must start outside the town and end inside the castle goal zone"));
        for (int32 I = 0; I + 1 < Points.Num(); ++I)
            if (InGoal(Points[I], 100)) return Fail(TEXT("Only the final point may enter the town defense zone"));
        // dev-route-tools: 0 (three automatic bays) or 1..16 authored packs, each with a radius and a tier.
        const auto& Bays = R.Bays[Team];
        if (Bays.Num() > FCireBattlefieldRoutes::MaxBays) return Fail(TEXT("Challenge packs need 1..16 bays"));
        for (int32 I = 0; I < Bays.Num(); ++I)
        {
            const FCireChallengeBay& Bay = Bays[I]; const FVector2D& B = Bay.Position;
            if (!(Bay.Radius >= FCireChallengeBay::MinRadius && Bay.Radius <= FCireChallengeBay::MaxRadius)) return Fail(TEXT("Challenge pack radius must be 200..1500 cm"));
            if (Bay.Tier < 1 || Bay.Tier > FCireChallengeBay::MaxTier) return Fail(TEXT("Challenge pack tier must be 1..10"));
            if (!Finite2(B) || B.X < R.MinX + 220 || B.X > R.MaxX - 220 || FMath::Abs(B.Y) > R.HalfWidth - 220 || InGoal(B, 200) ||
                FVector2D::DistSquared(B, Points[0]) < FMath::Square(450.))
                return Fail(TEXT("Challenge bays must stay inside the realm, clear of the breach and the castle zone"));
            for (int32 J = 0; J < I; ++J)
                if (FVector2D::DistSquared(B, Bays[J].Position) < FMath::Square(200.)) return Fail(TEXT("Challenge packs must be at least 2 m apart"));
        }
    }
    if (R.EscortEveryWaves < 0 || R.EscortEveryWaves > 100 || R.EscortCount < 1 || R.EscortCount > 4 || R.EscortLeakCost < 1 || R.EscortLeakCost > 100 ||
        !(R.EscortHealthMultiplier >= 1 && R.EscortHealthMultiplier <= 50) || !(R.EscortMoveSpeed >= 50 && R.EscortMoveSpeed <= 500))
        return Fail(TEXT("Invalid armored escort schedule or stats"));
    Error.Reset(); return true;
}
FCireBattlefieldRoutes CireLanePath::TownDefaults()
{
    // world-scale: the three-times-longer town (Tools/AuthorTownLayout.py writes the same route to BattlefieldRoutes.json).
    FCireBattlefieldRoutes R; R.MinX = -2350; R.MaxX = 43700; R.HalfWidth = 1400;
    const TArray<FVector2D> Points = {{43200,0},{41500,0},{40800,-550},{39300,-550},{38000,450},{36500,700},{35200,-250},{33600,-700},{32000,-700},
        {30600,300},{29200,650},{27700,650},{26300,-450},{25000,-450},{24300,0},{22700,0},{21300,-600},{19700,-600},{18300,550},{16900,550},
        {15500,-250},{14000,-250},{12900,500},{11400,500},{10000,-450},{8700,-550},{7400,500},{5800,500},{4700,-550},{3300,-550},{2400,500},
        {1000,500},{0,0},{-1850,0}};
    R.LocalPoints[0] = R.LocalPoints[1] = Points; return R;
}
bool CireLanePath::SameLayout(const FCireBattlefieldRoutes& A, const FCireBattlefieldRoutes& B)
{
    return A.MinX == B.MinX && A.MaxX == B.MaxX && A.HalfWidth == B.HalfWidth && A.LocalPoints[0] == B.LocalPoints[0] && A.LocalPoints[1] == B.LocalPoints[1] &&
        A.LaneWidth == B.LaneWidth && A.GoalCenter == B.GoalCenter && A.GoalSize == B.GoalSize && A.Bays[0] == B.Bays[0] && A.Bays[1] == B.Bays[1] &&
        A.EscortEveryWaves == B.EscortEveryWaves && A.EscortCount == B.EscortCount && A.EscortLeakCost == B.EscortLeakCost &&
        A.EscortHealthMultiplier == B.EscortHealthMultiplier && A.EscortMoveSpeed == B.EscortMoveSpeed;
}
FString CireLanePath::DataPath() { return FPaths::ProjectContentDir() / TEXT("Data/BattlefieldRoutes.json"); }
FString CireLanePath::ToJson(const FCireBattlefieldRoutes& R)
{
    auto N = [](double V)
    {
        const double Rounded = FMath::RoundToDouble(V * 10.) / 10.;
        return FMath::IsNearlyEqual(Rounded, FMath::RoundToDouble(Rounded)) ? FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::RoundToDouble(Rounded))) : FString::Printf(TEXT("%.1f"), Rounded);
    };
    auto List = [&](const TArray<FVector2D>& Points)
    {
        TArray<FString> Items; for (const FVector2D& P : Points) Items.Add(FString::Printf(TEXT("[%s,%s]"), *N(P.X), *N(P.Y)));
        return FString(TEXT("[")) + FString::Join(Items, TEXT(",")) + TEXT("]");
    };
    FString Out = TEXT("{\n  \"schemaVersion\": 1,\n  \"units\": \"centimeters\",\n");
    Out += FString::Printf(TEXT("  \"bounds\": { \"minX\": %s, \"maxX\": %s, \"halfWidth\": %s },\n"), *N(R.MinX), *N(R.MaxX), *N(R.HalfWidth));
    Out += FString::Printf(TEXT("  \"laneWidth\": %s,\n"), *N(R.LaneWidth));
    Out += FString::Printf(TEXT("  \"goal\": { \"x\": %s, \"y\": %s, \"depth\": %s, \"width\": %s },\n"), *N(R.GoalCenter.X), *N(R.GoalCenter.Y), *N(R.GoalSize.X), *N(R.GoalSize.Y));
    // dev-route-tools: authored packs with their radius and tier, one per line.
    auto BayList = [&](const TArray<FCireChallengeBay>& Bays)
    {
        TArray<FString> Items;
        for (const FCireChallengeBay& B : Bays)
            Items.Add(FString::Printf(TEXT("      { \"x\": %s, \"y\": %s, \"radius\": %s, \"tier\": %d }"), *N(B.Position.X), *N(B.Position.Y), *N(B.Radius), B.Tier));
        return FString(TEXT("[\n")) + FString::Join(Items, TEXT(",\n")) + TEXT("\n    ]");
    };
    if (R.LocalPoints[0] == R.LocalPoints[1] && R.Bays[0] == R.Bays[1])
    {
        // Both realms share the layout: author once, in realm-local coordinates.
        Out += FString::Printf(TEXT("  \"route\": {\n    \"points\": %s"), *List(R.LocalPoints[0]));
        if (R.Bays[0].Num() > 0) Out += FString::Printf(TEXT(",\n    \"bays\": %s"), *BayList(R.Bays[0]));
        Out += TEXT("\n  },\n");
    }
    else
    {
        Out += TEXT("  \"lanes\": [\n");
        for (int32 Team = 0; Team < 2; ++Team)
        {
            Out += FString::Printf(TEXT("    { \"team\": %d, \"points\": %s"), Team, *List(R.LocalPoints[Team]));
            if (R.Bays[Team].Num() > 0) Out += FString::Printf(TEXT(",\n    \"bays\": %s"), *BayList(R.Bays[Team]));
            Out += Team == 0 ? TEXT(" },\n") : TEXT(" }\n");
        }
        Out += TEXT("  ],\n");
    }
    Out += FString::Printf(TEXT("  \"armoredEscort\": { \"everyWaves\": %d, \"count\": %d, \"leakCost\": %d, \"healthMultiplier\": %s, \"moveSpeed\": %s }\n}\n"),
        R.EscortEveryWaves, R.EscortCount, R.EscortLeakCost, *N(R.EscortHealthMultiplier), *N(R.EscortMoveSpeed));
    return Out;
}
bool CireLanePath::LoadFile(FCireBattlefieldRoutes& Out, FString* Error, const FString& Path)
{
    FString Json, Why; FCireBattlefieldRoutes Candidate;
    if (!FFileHelper::LoadFileToString(Json, *(Path.IsEmpty() ? DataPath() : Path))) Why = TEXT("BattlefieldRoutes.json could not be read");
    else if (ParseJson(Json, Candidate, Why)) { Out = MoveTemp(Candidate); if (Error) Error->Reset(); return true; }
    if (Error) *Error = Why;
    return false;
}
bool CireLanePath::SaveFile(const FCireBattlefieldRoutes& Document, FString* Error, const FString& Path)
{
    FString Why; FCireBattlefieldRoutes RoundTrip;
    const FString Json = ToJson(Document);
    if (!Validate(Document, Why) || !ParseJson(Json, RoundTrip, Why)) { if (Error) *Error = Why; return false; }
    const FString Target = Path.IsEmpty() ? DataPath() : Path;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
    // Committed data files may be checked out read-only; a developer save is an explicit overwrite.
    if (IFileManager::Get().IsReadOnly(*Target)) FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Target, false);
    if (!FFileHelper::SaveStringToFile(Json, *Target, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { if (Error) *Error = FString::Printf(TEXT("Could not write %s"), *Target); return false; }
    if (Error) Error->Reset();
    return true;
}
bool CireLanePath::ApplyLive(UWorld* World, const FCireBattlefieldRoutes& Document, FString* Error)
{
    FString Why;
    if (!World || !World->GetAuthGameMode<ACireGameMode>()) Why = TEXT("Only the authoritative match may edit routes");
    else if (Validate(Document, Why))
    {
        const auto& Old = Get(World);
        if (Document.MinX != Old.MinX || Document.MaxX != Old.MaxX || Document.HalfWidth != Old.HalfWidth)
            Why = TEXT("Lane bounds changes require a match restart; live edits support paths, lane width, goal zone and bays");
        else
        {
            auto& Entry = ForWorld(World); Entry.Data = Document; ++Entry.Revision;
            PublishState(World->GetGameState<ACireGameState>());
            if (Error) Error->Reset();
            UE_LOG(LogCireLanePath, Display, TEXT("CIRE_LANE_ROUTES_EDITED revision=%u points=%d/%d laneWidth=%.0f"), Entry.Revision, Document.LocalPoints[0].Num(), Document.LocalPoints[1].Num(), Document.LaneWidth);
            return true;
        }
    }
    if (Error) *Error = Why;
    UE_LOG(LogCireLanePath, Warning, TEXT("Route edit rejected; existing routes retained: %s"), *Why);
    return false;
}
FVector CireLanePath::GoalZoneCenter(const UWorld* World, int32 Team, float Z)
{
    Team = FMath::Clamp(Team, 0, 1); return WorldPoint(Team, Get(World).GoalCenter, Z);
}
FVector2D CireLanePath::GoalZoneExtent(const UWorld* World) { return Get(World).GoalSize * .5; }
float CireLanePath::LaneWidth(const UWorld* World) { return Get(World).LaneWidth; }
bool CireLanePath::Reload(FString* Error)
{
    FString Json, Why; FCireBattlefieldRoutes Candidate; bLoaded = true;
    if (!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Data/BattlefieldRoutes.json"))) || !ParseJson(Json,Candidate,Why))
    {
        if (Why.IsEmpty()) Why = TEXT("BattlefieldRoutes.json could not be read");
        if (Error) *Error = Why;
        UE_LOG(LogCireLanePath,Warning,TEXT("Route reload rejected; existing routes retained: %s"),*Why); return false;
    }
    Routes = MoveTemp(Candidate); ++RouteRevision; if (Error) Error->Reset();
    UE_LOG(LogCireLanePath,Display,TEXT("CIRE_LANE_ROUTES_LOADED revision=%u points=%d/%d escortEvery=%d"),RouteRevision,Routes.LocalPoints[0].Num(),Routes.LocalPoints[1].Num(),Routes.EscortEveryWaves);
    return true;
}
bool CireLanePath::Reload(UWorld* World,FString* Error)
{
    FString Why,Json;FCireBattlefieldRoutes Candidate;
    if (!World || !World->GetAuthGameMode<ACireGameMode>()) Why=TEXT("Only the authoritative match may reload routes");
    else if (!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Data/BattlefieldRoutes.json")))) Why=TEXT("BattlefieldRoutes.json could not be read");
    else if (ParseJson(Json,Candidate,Why))
    {
        const auto& Old=Get(World);
        if (Candidate.MinX!=Old.MinX || Candidate.MaxX!=Old.MaxX || Candidate.HalfWidth!=Old.HalfWidth) Why=TEXT("Lane bounds changes require a match restart; live reload supports paths and escort tuning");
        else
        {
            auto& Entry=ForWorld(World);Entry.Data=MoveTemp(Candidate);++Entry.Revision;
            Routes=Entry.Data;++RouteRevision;PublishState(World->GetGameState<ACireGameState>());
            if(Error)Error->Reset();UE_LOG(LogCireLanePath,Display,TEXT("CIRE_LANE_ROUTES_RELOADED revision=%u"),Entry.Revision);return true;
        }
    }
    if(Error)*Error=Why;UE_LOG(LogCireLanePath,Warning,TEXT("Route reload rejected; existing routes retained: %s"),*Why);return false;
}
void CireLanePath::PublishState(ACireGameState* State)
{
    if(!IsValid(State)||!State->HasAuthority())return;
    const auto& R=Get(State->GetWorld());State->LaneBounds=FVector(R.MinX,R.MaxX,R.HalfWidth);
    State->LanePoints0=R.LocalPoints[0];State->LanePoints1=R.LocalPoints[1];State->LaneRouteVersion=Revision(State->GetWorld());
    // nav-paths: lane width, goal zone and bay overrides ride along with the points.
    TArray<float>& L=State->LaneLayout;L.Reset();
    L.Add(R.LaneWidth);L.Add(R.GoalCenter.X);L.Add(R.GoalCenter.Y);L.Add(R.GoalSize.X);L.Add(R.GoalSize.Y);
    // dev-route-tools: per realm the pack count, then x, y, radius and tier of each authored pack.
    for(int32 Team=0;Team<2;++Team){L.Add(R.Bays[Team].Num());for(const FCireChallengeBay& B:R.Bays[Team]){L.Add(B.Position.X);L.Add(B.Position.Y);L.Add(B.Radius);L.Add(B.Tier);}}
    State->ForceNetUpdate();
}
void CireLanePath::ReceiveState(ACireGameState* State)
{
    if(!IsValid(State)||State->HasAuthority()||State->LaneRouteVersion==0||State->LanePoints0.Num()<3||State->LanePoints1.Num()<3||
       State->LanePoints0.Num()>64||State->LanePoints1.Num()>64||State->LaneBounds.ContainsNaN())return;
    auto& Entry=ForWorld(State->GetWorld());if(Entry.ReceivedVersion==State->LaneRouteVersion)return;
    Entry.Data.MinX=State->LaneBounds.X;Entry.Data.MaxX=State->LaneBounds.Y;Entry.Data.HalfWidth=State->LaneBounds.Z;
    Entry.Data.LocalPoints[0]=State->LanePoints0;Entry.Data.LocalPoints[1]=State->LanePoints1;
    // nav-paths: unpack the layout extras (ignored when malformed; the points still apply).
    const TArray<float>& L=State->LaneLayout;
    if(L.Num()>=7)
    {
        int32 At=5;TArray<FCireChallengeBay> Bays[2];bool bOk=true;
        for(int32 Team=0;Team<2&&bOk;++Team)
        {
            const int32 Count=At<L.Num()?FMath::RoundToInt(L[At]):-1;++At;
            if(Count<0||Count>FCireBattlefieldRoutes::MaxBays||At+Count*4>L.Num()){bOk=false;break;}
            for(int32 I=0;I<Count;++I)
            {
                FCireChallengeBay B;B.Position=FVector2D(L[At],L[At+1]);
                B.Radius=FMath::Clamp(L[At+2],FCireChallengeBay::MinRadius,FCireChallengeBay::MaxRadius);
                B.Tier=FMath::Clamp(FMath::RoundToInt(L[At+3]),1,FCireChallengeBay::MaxTier);
                Bays[Team].Add(B);At+=4;
            }
        }
        if(bOk)
        {
            Entry.Data.LaneWidth=L[0];Entry.Data.GoalCenter=FVector2D(L[1],L[2]);Entry.Data.GoalSize=FVector2D(L[3],L[4]);
            Entry.Data.Bays[0]=Bays[0];Entry.Data.Bays[1]=Bays[1];
        }
    }
    Entry.ReceivedVersion=State->LaneRouteVersion;++Entry.Revision;
}
bool CireLanePath::Contains(int32 Team,const FVector& P,float Margin) { return Contains(nullptr,Team,P,Margin); }
bool CireLanePath::Contains(const UWorld* World,int32 Team,const FVector& P,float Margin)
{
    const auto& R = Get(World);
    return Team >= 0 && Team < 2 && !P.ContainsNaN() && FMath::IsFinite(Margin) && Margin >= 0 &&
        P.X >= R.MinX + Margin && P.X <= R.MaxX - Margin && FMath::Abs(P.Y-CenterY(Team)) <= R.HalfWidth-Margin;
}
FVector CireLanePath::ClampToLane(int32 Team,FVector P,float Margin)
{ return ClampToLane(nullptr,Team,P,Margin); }
FVector CireLanePath::ClampToLane(const UWorld* World,int32 Team,FVector P,float Margin)
{
    const auto& R = Get(World); Team = FMath::Clamp(Team,0,1);
    Margin = FMath::Clamp(FMath::IsFinite(Margin)?Margin:0.f,0.f,FMath::Min(R.HalfWidth-1,(R.MaxX-R.MinX)*.5f-1));
    if (P.ContainsNaN()) P = FVector(-1700,CenterY(Team),110);
    P.X = FMath::Clamp(P.X,static_cast<double>(R.MinX+Margin),static_cast<double>(R.MaxX-Margin));
    P.Y = FMath::Clamp(P.Y,static_cast<double>(CenterY(Team)-R.HalfWidth+Margin),static_cast<double>(CenterY(Team)+R.HalfWidth-Margin));
    return P;
}
FVector CireLanePath::SpawnPosition(int32 Team,float Z)
{ return SpawnPosition(nullptr,Team,Z); }
FVector CireLanePath::SpawnPosition(const UWorld* World,int32 Team,float Z)
{
    const auto& R = Get(World); Team = FMath::Clamp(Team,0,1);
    return WorldPoint(Team,R.LocalPoints[Team][0],FMath::IsFinite(Z)?Z:110.f);
}
FVector CireLanePath::ChallengePosition(const UWorld* World,int32 Team,int32 Tier,float Z)
{
    Team=FMath::Clamp(Team,0,1);
    return WorldPoint(Team,BayPoint(Get(World),Team,Tier),FMath::IsFinite(Z)?Z:110.f);
}
FVector2D CireLanePath::BayPoint(const FCireBattlefieldRoutes& R,int32 Team,int32 Bay)
{
    Team=FMath::Clamp(Team,0,1);
    // nav-paths: path-editor bay override. dev-route-tools: 1..16 authored packs.
    if(R.Bays[Team].Num()>0)return R.Bays[Team][FMath::Clamp(Bay,1,R.Bays[Team].Num())-1].Position;
    const int32 Tier=FMath::Clamp(Bay,1,FCireBattlefieldRoutes::AutoBays);
    const auto& Points=R.LocalPoints[Team];
    if(Points.Num()<2)return Points.Num()?Points[0]:FVector2D::ZeroVector; // an unfinished editor draft
    double Length=0;
    for(int32 I=0;I+1<Points.Num();++I)Length+=FVector2D::Distance(Points[I],Points[I+1]);
    double Remaining=Length*(1.-Tier*.25);FVector2D Anchor=Points.Last();
    for(int32 I=0;I+1<Points.Num();++I)
    {
        const double SegmentLength=FVector2D::Distance(Points[I],Points[I+1]);
        if(Remaining<=SegmentLength){Anchor=FMath::Lerp(Points[I],Points[I+1],Remaining/SegmentLength);break;}
        Remaining-=SegmentLength;
    }
    // Opposite-side bays avoid automatically pulling optional packs into the
    // marching column. Search near the tier's path-distance anchor so authored
    // bends are respected without moving the three tiers to the same location.
    FVector2D Best=Anchor;double BestScore=-TNumericLimits<double>::Max();
    for(int32 Side:{-1,1})for(int32 Offset=-4;Offset<=4;++Offset)
    {
        const FVector2D Candidate(FMath::Clamp(Anchor.X+Offset*100.,500.,static_cast<double>(R.MaxX-250)),Side*(R.HalfWidth-220.));
        const double Score=FMath::Sqrt(DistanceToPathSquared(Points,Candidate))-FMath::Abs(Offset)*25.;
        if(Score>BestScore){BestScore=Score;Best=Candidate;}
    }
    return Best;
}
int32 CireLanePath::BayCount(const FCireBattlefieldRoutes& R,int32 Team)
{
    const int32 Authored=R.Bays[FMath::Clamp(Team,0,1)].Num();
    return Authored>0?FMath::Min(Authored,FCireBattlefieldRoutes::MaxBays):FCireBattlefieldRoutes::AutoBays;
}
int32 CireLanePath::BayCount(const UWorld* World,int32 Team){return BayCount(Get(World),Team);}
FCireChallengeBay CireLanePath::BayAt(const FCireBattlefieldRoutes& R,int32 Team,int32 Bay)
{
    Team=FMath::Clamp(Team,0,1);
    if(R.Bays[Team].Num()>0)return R.Bays[Team][FMath::Clamp(Bay,1,R.Bays[Team].Num())-1];
    FCireChallengeBay Auto;Auto.Tier=FMath::Clamp(Bay,1,FCireBattlefieldRoutes::AutoBays);Auto.Position=BayPoint(R,Team,Auto.Tier);
    return Auto;
}
TArray<FCireChallengeBay> CireLanePath::AutoBays(const FCireBattlefieldRoutes& R,int32 Team)
{
    FCireBattlefieldRoutes Computed=R;Computed.Bays[0].Reset();Computed.Bays[1].Reset();
    TArray<FCireChallengeBay> Out;
    for(int32 Bay=1;Bay<=FCireBattlefieldRoutes::AutoBays;++Bay)Out.Add(BayAt(Computed,Team,Bay));
    return Out;
}
float CireLanePath::ChallengeRadius(const UWorld* World,int32 Team,int32 Bay){return BayAt(Get(World),Team,Bay).Radius;}
int32 CireLanePath::ChallengeTier(const UWorld* World,int32 Team,int32 Bay){return BayAt(Get(World),Team,Bay).Tier;}
double CireLanePath::PathLength(const TArray<FVector2D>& Points)
{
    double Length=0;for(int32 I=0;I+1<Points.Num();++I)Length+=FVector2D::Distance(Points[I],Points[I+1]);return Length;
}
TArray<FVector> CireLanePath::RoutePoints(const UWorld* World,int32 Team,float Z)
{
    TArray<FVector> Out;Team=FMath::Clamp(Team,0,1);
    for(const FVector2D& P:Get(World).LocalPoints[Team])Out.Add(WorldPoint(Team,P,FMath::IsFinite(Z)?Z:0.f));
    return Out;
}
float CireLanePath::RouteLength(const UWorld* World,int32 Team)
{
    const auto& Points=Get(World).LocalPoints[FMath::Clamp(Team,0,1)];double Length=0;
    for(int32 I=0;I+1<Points.Num();++I)Length+=FVector2D::Distance(Points[I],Points[I+1]);
    return static_cast<float>(Length);
}
FVector CireLanePath::PointAlongRoute(const UWorld* World,int32 Team,float Fraction,float Z)
{
    Team=FMath::Clamp(Team,0,1);const auto& Points=Get(World).LocalPoints[Team];
    double Remaining=RouteLength(World,Team)*FMath::Clamp(FMath::IsFinite(Fraction)?Fraction:0.f,0.f,1.f);
    for(int32 I=0;I+1<Points.Num();++I)
    {
        const double Segment=FVector2D::Distance(Points[I],Points[I+1]);
        if(Remaining<=Segment)return WorldPoint(Team,FMath::Lerp(Points[I],Points[I+1],Segment>0?Remaining/Segment:0.),Z);
        Remaining-=Segment;
    }
    return WorldPoint(Team,Points.Last(),Z);
}
float CireLanePath::RouteProgress(const UWorld* World,int32 Team,const FVector& Location)
{
    Team=FMath::Clamp(Team,0,1);const auto& Points=Get(World).LocalPoints[Team];
    const FVector2D P(Location.X,Location.Y-CenterY(Team));double Best=TNumericLimits<double>::Max(),BestAlong=0,Walked=0;
    for(int32 I=0;I+1<Points.Num();++I)
    {
        const FVector2D Segment=Points[I+1]-Points[I];const double Length=Segment.Size();
        const double Alpha=FMath::Clamp(FVector2D::DotProduct(P-Points[I],Segment)/FMath::Max(1.,Segment.SizeSquared()),0.,1.);
        const double D=FVector2D::DistSquared(P,Points[I]+Segment*Alpha);
        if(D<Best){Best=D;BestAlong=Walked+Length*Alpha;}
        Walked+=Length;
    }
    return Walked>0?static_cast<float>(BestAlong/Walked):0.f;
}
FVector CireLanePath::GoalPosition(const UWorld* World,int32 Team,float Z)
{
    Team=FMath::Clamp(Team,0,1);return WorldPoint(Team,Get(World).LocalPoints[Team].Last(),Z);
}
void CireLanePath::InitializeProgress(ACireMonster* M)
{
    if (!IsValid(M) || !M->HasAuthority() || M->Lane < 0 || M->Lane > 1) return;
    const auto& R = Get(M->GetWorld()); const FVector P = M->GetActorLocation();
    M->LaneWaypointIndex = ProjectNext(R.LocalPoints[M->Lane],FVector2D(P.X,P.Y-CenterY(M->Lane)));
    M->LaneRouteRevision = Revision(M->GetWorld());
}
FVector CireLanePath::NextWaypoint(ACireMonster* M)
{
    if (!IsValid(M) || M->Lane < 0 || M->Lane > 1) return FVector::ZeroVector;
    const auto& R = Get(M->GetWorld());
    if (M->LaneRouteRevision != Revision(M->GetWorld()) || M->LaneWaypointIndex < 1 || M->LaneWaypointIndex >= R.LocalPoints[M->Lane].Num()) InitializeProgress(M);
    const auto& Points = R.LocalPoints[M->Lane]; const FVector P = M->GetActorLocation();
    const FVector2D Local(P.X,P.Y-CenterY(M->Lane));
    // world-scale: a unit carried forward off its march (escort guards walking beside their escortee, a chase) resumes
    // from where it now is instead of walking back to a stale waypoint; on the 495 m road that walk-back reached the
    // stall failsafe. Progress still never moves backward.
    if (M->HasAuthority()) M->LaneWaypointIndex = FMath::Max(M->LaneWaypointIndex, ProjectNext(Points, Local));
    // nav-paths: 150 cm arrival radius (was 100): navmesh-steered units in a crowd rarely stand exactly on the point.
    while (M->LaneWaypointIndex + 1 < Points.Num() && FVector2D::DistSquared(Local,Points[M->LaneWaypointIndex]) <= FMath::Square(150.f)) ++M->LaneWaypointIndex;
    return WorldPoint(M->Lane,Points[M->LaneWaypointIndex],P.Z);
}
bool CireLanePath::ShouldSpawnEscort(int32 Wave)
{ return ShouldSpawnEscort(nullptr,Wave); }
bool CireLanePath::ShouldSpawnEscort(const UWorld* World,int32 Wave)
{
    const auto& R = Get(World); return Wave > 0 && R.EscortEveryWaves > 0 && Wave % R.EscortEveryWaves == 0;
}
void CireLanePath::ConfigureEscort(ACireMonster* M)
{
    if (!IsValid(M) || !M->HasAuthority() || M->bArmoredEscort || M->PackId >= 0 || M->Health <= 0) return;
    const auto& R = Get(M->GetWorld()); CireNPCCombat::Interrupt(M); CireThreat::Clear(M);
    M->bArmoredEscort = true; M->bEngaged = false; M->bBoss = false; M->CombatArchetype = 0;
    M->MonsterName = TEXT("Armored Escort"); M->MaxHealth = M->Health = FMath::Min(M->MaxHealth*R.EscortHealthMultiplier,1.e9f);
    M->LeakCostOverride=R.EscortLeakCost;
    M->BaseMoveSpeed = R.EscortMoveSpeed; M->GetCharacterMovement()->MaxWalkSpeed = M->BaseMoveSpeed;
    M->EscortCollisionRefreshAt = 0; InitializeProgress(M); RefreshEscortCollision(M); M->ForceNetUpdate();
}
void CireLanePath::RefreshEscortCollision(ACireMonster* M)
{
    if (!IsValid(M) || !M->HasAuthority() || !M->bArmoredEscort) return;
    const float Now = M->GetWorld()->GetTimeSeconds(); if (M->EscortCollisionRefreshAt > Now) return;
    M->EscortCollisionRefreshAt = Now + .5f;
    for (TActorIterator<ACharacter> It(M->GetWorld()); It; ++It)
        if (*It != M && (Cast<ACireHero>(*It) || (Cast<ACireMonster>(*It) && Cast<ACireMonster>(*It)->Lane == M->Lane)))
            M->GetCapsuleComponent()->IgnoreActorWhenMoving(*It,true);
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs RouteReloadCommand(TEXT("cire.Routes"),TEXT("cire.Routes reload: reload authoritative waypoints/escort tuning; bounds require restarting the match."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        if(Args.Num()==1&&Args[0]==TEXT("reload"))CireLanePath::Reload(World);
        else UE_LOG(LogCireLanePath,Display,TEXT("Use: cire.Routes reload"));
    }));
#include "CireConstruct.h"
#include "CireCombatEvents.h"
#include "CireTownGoal.h"
#include "Components/BoxComponent.h"
#include "Misc/ScopeExit.h"

bool CireLanePath::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool Value,const TCHAR* Why) { ++Checks; if (!Value) { bPass=false; UE_LOG(LogCireLanePath,Error,TEXT("CIRE_LANE_CHECK_FAIL %s"),Why); } };
    UWorld* World=Mode->GetWorld();Get(World);const auto SavedWorldRoutes=ForWorld(World);
    const auto Clock = Mode->Clock; const auto Monsters = Mode->Monsters; const auto Heroes = Mode->Heroes;
    auto* State = Mode->GetGameState<ACireGameState>(); if (!State) return false;
    const int32 Lives = State->EmberLives; TArray<AActor*> Actors;
    ON_SCOPE_EXIT
    {
        for (int32 I=Actors.Num()-1;I>=0;--I) if (IsValid(Actors[I])) Actors[I]->Destroy();
        ForWorld(World)=SavedWorldRoutes;PublishState(State);Mode->Clock=Clock;Mode->Monsters=Monsters;Mode->Heroes=Heroes;State->EmberLives=Lives;
    };
    FString Json,Error; FCireBattlefieldRoutes Parsed;
    Check(FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Data/BattlefieldRoutes.json"))) && ParseJson(Json,Parsed,Error),TEXT("authored route document loads"));
    const auto Unchanged=Parsed;
    Check(!ParseJson(TEXT("{broken"),Parsed,Error) && Parsed.LocalPoints[0]==Unchanged.LocalPoints[0],TEXT("invalid JSON leaves candidate unchanged"));
    auto Mutant = [&](TFunctionRef<void(TSharedPtr<FJsonObject>)> Change)
    {
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root) || !Root) return FString();
        Change(Root);FString Result;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Result));return Result;
    };
    Check(!ParseJson(Mutant([](auto Root){Root->GetObjectField(TEXT("bounds"))->SetNumberField(TEXT("halfWidth"),2100);}),Parsed,Error),TEXT("bounds cannot join the private realms"));
    Check(!ParseJson(Mutant([](auto Root){Root->GetArrayField(TEXT("lanes"))[1]->AsObject()->SetNumberField(TEXT("team"),0);}),Parsed,Error),TEXT("duplicate teams rejected"));
    Check(!ParseJson(Mutant([](auto Root)
    {
        auto Lane=Root->GetArrayField(TEXT("lanes"))[0]->AsObject();auto Points=Lane->GetArrayField(TEXT("points"));Points[1]=Points[0];Lane->SetArrayField(TEXT("points"),Points);
    }),Parsed,Error),TEXT("zero length route segments rejected"));
    Check(!ParseJson(Mutant([](auto Root)
    {
        auto Lane=Root->GetArrayField(TEXT("lanes"))[0]->AsObject();auto Points=Lane->GetArrayField(TEXT("points"));Points.Last()=Points[0];Lane->SetArrayField(TEXT("points"),Points);
    }),Parsed,Error),TEXT("route must terminate inside its actual town"));
    Check(!ParseJson(Mutant([](auto Root){Root->GetObjectField(TEXT("armoredEscort"))->SetNumberField(TEXT("healthMultiplier"),1000);}),Parsed,Error),TEXT("escort health scaling is bounded"));
    ForWorld(World).Data=Defaults();++ForWorld(World).Revision;Mode->Clock=Cires::MatchClock();Mode->Monsters.Reset();Mode->Heroes.Reset();State->EmberLives=100;
    Check(Contains(World,0,SpawnPosition(World,0)) && Contains(World,1,SpawnPosition(World,1)) && !Contains(World,0,SpawnPosition(World,1)),TEXT("spawns fit separate realm bounds"));
    Check(!Contains(World,0,FVector(14000,-2100,100)) && Contains(World,0,ClampToLane(World,0,FVector(14000,-9000,100),100),100),TEXT("lane clamp honors editable boundaries and clearance"));
    Check(!ShouldSpawnEscort(World,1) && ShouldSpawnEscort(World,4) && ShouldSpawnEscort(World,8) && !ShouldSpawnEscort(World,0),TEXT("occasional escort schedule defaults to every fourth wave"));
    ForWorld(World).Data.EscortEveryWaves=0;Check(!ShouldSpawnEscort(World,4),TEXT("zero schedule disables escort waves"));ForWorld(World).Data.EscortEveryWaves=4;
    auto* OtherWorld=NewObject<UWorld>(GetTransientPackage());const float OtherY=Get(OtherWorld).LocalPoints[0][2].Y;
    ForWorld(World).Data.LocalPoints[0][2].Y=-500;++ForWorld(World).Revision;PublishState(State);
    Check(Get(OtherWorld).LocalPoints[0][2].Y==OtherY && Routes.LocalPoints[0][2].Y==OtherY,TEXT("live route state is isolated between worlds and startup defaults"));
    Check(State->LanePoints0==Get(World).LocalPoints[0] && State->LanePoints1==Get(World).LocalPoints[1] && State->LaneRouteVersion==Revision(World) && State->LaneBounds.Equals(FVector(-2350,13000,1120)),TEXT("authority publishes path points, bounds and revision together"));
    WorldRoutes.Remove(TWeakObjectPtr<UWorld>(OtherWorld));
    ForWorld(World).Data=Defaults();++ForWorld(World).Revision;
    Check(!Reload(static_cast<UWorld*>(nullptr),&Error),TEXT("live reload requires an authoritative world"));
    for(int32 Tier=1;Tier<=3;++Tier)
    {
        const FVector Bay=ChallengePosition(World,0,Tier);
        Check(Contains(World,0,Bay,200) && DistanceToPathSquared(Get(World).LocalPoints[0],FVector2D(Bay.X,Bay.Y-CenterY(0)))>=FMath::Square(500.),TEXT("challenge bay has realm clearance and stays at least 500 cm off the default march path"));
    }
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Floor=Mode->GetWorld()->SpawnActor<AActor>();
    if (Floor)
    {
        Actors.Add(Floor);auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(8000,1000,50));Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();Floor->SetActorLocation(FVector(5000,-2100,2950));
    }
    auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(SpawnPosition(World,0,3092),FRotator::ZeroRotator,P);
    auto* H=Mode->GetWorld()->SpawnActor<ACireHero>(SpawnPosition(World,0,3092)+FVector(-120,0,0),FRotator::ZeroRotator,P);
    if (!Floor||!M||!H) {Check(false,TEXT("lane fixture actors spawn"));return false;}
    Actors.Add(M);Actors.Add(H);M->Lane=0;M->SetActorTickEnabled(false);Mode->Monsters.Add(M);
    H->TeamId=0;H->Draft(0);H->SetActorTickEnabled(false);H->Health=H->MaxHealth=1000;H->CriticalChance=0;H->ShieldUntil=0;Mode->Heroes.Add(H);
    CireNPCCombat::Configure(M,0,4);const float BasicHealth=M->MaxHealth,FixedDamage=M->Damage;
    const FVector ActualSpawn=M->GetActorLocation(),FirstWaypoint=NextWaypoint(M);
    // NextWaypoint is a planar routing target at the unit's current height.
    // Character movement can adjust the requested spawn Z to its floor gap
    // during initialization; that adjustment must not be treated as a wrong bend.
    const FVector ExpectedWaypoint=WorldPoint(0,Get(World).LocalPoints[0][1],ActualSpawn.Z);
    UE_LOG(LogCireLanePath,Display,TEXT("CIRE_LANE_FIRST_WAYPOINT requested_spawn=%s actual_spawn=%s actual=%s expected=%s index=%d revision=%u"),
        *SpawnPosition(World,0,3092).ToString(),*ActualSpawn.ToString(),*FirstWaypoint.ToString(),*ExpectedWaypoint.ToString(),M->LaneWaypointIndex,M->LaneRouteRevision);
    Check(M->LaneWaypointIndex==1 && FirstWaypoint.Equals(ExpectedWaypoint),TEXT("spawn heads to first bend instead of directly toward town"));
    M->SetActorLocation(WorldPoint(0,Get(World).LocalPoints[0][1],3092));NextWaypoint(M);
    Check(M->LaneWaypointIndex==2,TEXT("arrival advances to the next authored waypoint"));
    M->SetActorLocation(SpawnPosition(World,0,3092));NextWaypoint(M);
    Check(M->LaneWaypointIndex==2,TEXT("progress does not restart when a unit is displaced backward"));
    M->SetActorLocation(FVector(9000,-2750,3092));InitializeProgress(M);Check(M->LaneWaypointIndex==2,TEXT("mid-route spawns project to nearest path segment"));
    const FVector BeforeReload=M->GetActorLocation();ForWorld(World).Data.LocalPoints[0][2].Y=-500;++ForWorld(World).Revision;NextWaypoint(M);
    Check(M->LaneRouteRevision==Revision(World) && M->GetActorLocation().Equals(BeforeReload),TEXT("live route edits reproject progress without teleporting units"));
    ForWorld(World).Data=Defaults();++ForWorld(World).Revision;M->SetActorLocation(SpawnPosition(World,0,3092));InitializeProgress(M);
    CireThreat::Engage(M,H);CireNPCCombat::Tick(M,.01f);
    Check(M->Victim==H,TEXT("ordinary infantry still engage nearby players"));
    ConfigureEscort(M);
    Check(M->bArmoredEscort && M->MaxHealth==BasicHealth*6 && M->Damage==FixedDamage,TEXT("escort increases health without increasing structure damage"));
    const float EscortHealth=M->Health;ConfigureEscort(M);Check(M->Health==EscortHealth,TEXT("escort configuration is idempotent"));
    CireThreat::Taunt(M,H,5);CireCombat::ApplyDamage(H,M,10,TEXT("Escort fixture strike"));
    M->AttackTimer=0;M->AbilityTimer=0;M->ConsumeMovementInputVector();const float HeroHealth=H->Health;
    CireNPCCombat::Tick(M,.01f);
    Check(M->Health==EscortHealth-10 && M->Victim==nullptr && M->Threat.IsEmpty() && H->Health==HeroHealth && M->CastingAbility.IsEmpty(),TEXT("damaged and taunted escorts never retaliate against players"));
    Check(M->GetPendingMovementInputVector().Y<0 && M->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(H),TEXT("escort keeps the winding route and passes player blockers"));
    M->ConsumeMovementInputVector();M->SetActorLocation(FVector(800,-2750,3130));InitializeProgress(M);H->SetActorLocation(FVector(400,-2750,3092));
    FCireConstructSpec WallSpec;WallSpec.MaxHealth=1000;WallSpec.Width=220;
    auto* Wall=ACireConstruct::Spawn(H,WallSpec,FVector(650,-2750,3000),FRotator::ZeroRotator,TEXT("Escort wall fixture"));
    if (Wall) Actors.Add(Wall);M->AttackTimer=0;CireNPCCombat::Tick(M,.01f);
    Check(Wall && Wall->Health==Wall->MaxHealth-FixedDamage && H->Health==HeroHealth,TEXT("escort breaches the blocking wall using fixed damage only"));
    const float WallHealth=Wall?Wall->Health:0;Mode->Clock.BeginIntermission();M->ConsumeMovementInputVector();M->AttackTimer=0;CireNPCCombat::Tick(M,.1f);
    Check(M->GetPendingMovementInputVector().IsNearlyZero() && (!Wall||Wall->Health==WallHealth),TEXT("phase pause stops escort marching and attacks"));
    Mode->Clock=Cires::MatchClock();if(Wall)Wall->Destroy();
    auto* Goal=Mode->GetWorld()->SpawnActor<ACireTownGoal>(FVector(-1850,-2100,3150),FRotator::ZeroRotator,P);
    if(Goal){Actors.Add(Goal);Goal->TeamId=0;}
    M->SetActorLocation(FVector(-1850,-2100,3130));CireNPCCombat::Tick(M,.01f);
    Check(Goal && M->IsActorBeingDestroyed() && State->EmberLives==99 && !Mode->Monsters.Contains(M),TEXT("escort reaching the town leaks once through the existing goal pipeline"));
    Mode->Leak(M);Check(State->EmberLives==99,TEXT("duplicate town overlap cannot debit an escort twice"));
    UE_LOG(LogCireLanePath,Display,TEXT("CIRE_LANE_SMOKE_%s checks=%d"),bPass?TEXT("PASS"):TEXT("FAIL"),Checks);return bPass;
}
#endif
