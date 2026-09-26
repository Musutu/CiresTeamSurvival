// layout-wiring: what the game reads from the map layout editor's markers (Docs/MapLayout.md "What the game reads").
// Multi-path waves (every Monster Path of a realm, split per spawn), Player Spawn / Respawn / Boss / Rift spots, the
// Play Bounds polygon, their replication block, and the startup document (MapLayout.json over the route file).
#include "CireLanePath.h"
#include "CireGame.h"
#include "CireMapLayout.h"
#include "CireTownMap.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireLayoutRoutes, Log, All);

namespace
{
int32 Realm(int32 Team) { return FMath::Clamp(Team, 0, 1); }
FString GActiveSource = TEXT("route file");
bool InsidePolygon(const TArray<FVector2D>& Poly, const FVector2D& P)
{
    bool bInside = false;
    for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++)
    {
        const FVector2D& A = Poly[I]; const FVector2D& C = Poly[J];
        if ((A.Y > P.Y) != (C.Y > P.Y) && P.X < (C.X - A.X) * (P.Y - A.Y) / (C.Y - A.Y) + A.X) bInside = !bInside;
    }
    return bInside;
}
FVector Grounded(const UWorld* World, int32 Team, const FVector2D& Local, float Z)
{
    FVector W = CireLanePath::ToWorld(Team, Local, 0.f);
    if (CireTownMap::IsActive()) W.Z = CireTownMap::Ground(World, FVector2D(W)) + Z; else W.Z = Z;
    return W;
}
}

int32 CireLanePath::PathCount(const FCireBattlefieldRoutes& R, int32 Team) { return FMath::Max(1, R.Paths[Realm(Team)].Num()); }
int32 CireLanePath::PathCount(const UWorld* World, int32 Team) { return PathCount(Get(World), Team); }
const TArray<FVector2D>& CireLanePath::PathPoints(const FCireBattlefieldRoutes& R, int32 Team, int32 Path)
{
    Team = Realm(Team);
    // Path 0 is always the primary route (LocalPoints): the F8 path editor and every single-route caller edit it.
    if (Path <= 0 || !R.Paths[Team].IsValidIndex(Path) || R.Paths[Team][Path].Points.Num() < 2) return R.LocalPoints[Team];
    return R.Paths[Team][Path].Points;
}
TArray<FCireRouteSpot> CireLanePath::SpawnSpots(const FCireBattlefieldRoutes& R, int32 Team)
{
    Team = Realm(Team);
    if (R.Spawns[Team].Num() > 0) return R.Spawns[Team];
    FCireRouteSpot Only;
    if (R.LocalPoints[Team].Num() > 0) Only.Position = R.LocalPoints[Team][0];
    if (R.LocalPoints[Team].Num() > 1) Only.Yaw = FMath::RadiansToDegrees(FMath::Atan2(R.LocalPoints[Team][1].Y - R.LocalPoints[Team][0].Y, R.LocalPoints[Team][1].X - R.LocalPoints[Team][0].X));
    Only.Name = TEXT("The Breach");
    return {Only};
}
TArray<int32> CireLanePath::PathsOfSpawn(const FCireBattlefieldRoutes& R, int32 Team, int32 Spawn)
{
    Team = Realm(Team);
    TArray<int32> Out;
    if (R.Paths[Team].Num() == 0) { if (Spawn == 0) Out.Add(0); return Out; }
    for (int32 I = 0; I < R.Paths[Team].Num(); ++I) if (R.Paths[Team][I].Spawn == Spawn) Out.Add(I);
    return Out;
}
TArray<double> CireLanePath::PathShares(const FCireBattlefieldRoutes& R, int32 Team)
{
    Team = Realm(Team);
    const int32 Count = PathCount(R, Team);
    TArray<double> Shares; Shares.Init(0., Count);
    if (R.Paths[Team].Num() == 0) { Shares[0] = 1.; return Shares; }
    const TArray<FCireRouteSpot> Spawns = SpawnSpots(R, Team);
    // Spawns that own at least one path split the wave evenly; each spawn splits its share across its paths.
    TArray<int32> Active;
    for (int32 S = 0; S < Spawns.Num(); ++S) if (PathsOfSpawn(R, Team, S).Num() > 0) Active.Add(S);
    if (Active.Num() == 0) { Shares[0] = 1.; return Shares; }
    for (const int32 S : Active)
    {
        const TArray<int32> Owned = PathsOfSpawn(R, Team, S);
        double Total = 0;
        for (const int32 P : Owned) Total += Spawns[S].bWeighted ? FMath::Max(0.f, R.Paths[Team][P].Weight) : 1.f;
        for (const int32 P : Owned)
        {
            const double W = Spawns[S].bWeighted ? FMath::Max(0.f, R.Paths[Team][P].Weight) : 1.f;
            Shares[P] += (Total > 0 ? W / Total : 1. / Owned.Num()) / Active.Num();
        }
    }
    return Shares;
}
int32 CireLanePath::PathForSlot(const FCireBattlefieldRoutes& R, int32 Team, int32 Slot)
{
    const TArray<double> Shares = PathShares(R, Team);
    if (Shares.Num() <= 1) return 0;
    // Largest deficit (share x units so far - units given): deterministic, ties go to the lower path index.
    TArray<int32> Given; Given.Init(0, Shares.Num());
    int32 Pick = 0;
    for (int32 N = 0; N <= FMath::Max(0, Slot); ++N)
    {
        double Best = -TNumericLimits<double>::Max(); Pick = 0;
        for (int32 P = 0; P < Shares.Num(); ++P)
        {
            if (Shares[P] <= 0) continue;
            const double Deficit = Shares[P] * (N + 1) - Given[P];
            if (Deficit > Best + 1e-9) { Best = Deficit; Pick = P; }
        }
        ++Given[Pick];
    }
    return Pick;
}
int32 CireLanePath::NearestPathStart(const FCireBattlefieldRoutes& R, int32 Team, const FVector2D& Local)
{
    int32 Best = 0; double BestD = TNumericLimits<double>::Max();
    for (int32 P = 0; P < PathCount(R, Team); ++P)
    {
        const auto& Points = PathPoints(R, Team, P);
        if (Points.Num() == 0) continue;
        const double D = FVector2D::DistSquared(Points[0], Local);
        if (D < BestD) { BestD = D; Best = P; }
    }
    return Best;
}
FVector CireLanePath::PathStart(const UWorld* World, int32 Team, int32 Path, float Z)
{
    const auto& Points = PathPoints(Get(World), Team, Path);
    return Grounded(World, Realm(Team), Points.Num() ? Points[0] : FVector2D::ZeroVector, FMath::IsFinite(Z) ? Z : 110.f);
}
float CireLanePath::PathLengthOf(const UWorld* World, int32 Team, int32 Path) { return static_cast<float>(PathLength(PathPoints(Get(World), Team, Path))); }
FVector CireLanePath::PointAlongPath(const UWorld* World, int32 Team, int32 Path, float Fraction, float Z)
{
    Team = Realm(Team);
    const auto& Points = PathPoints(Get(World), Team, Path);
    if (Points.Num() == 0) return FVector::ZeroVector;
    double Remaining = PathLength(Points) * FMath::Clamp(FMath::IsFinite(Fraction) ? Fraction : 0.f, 0.f, 1.f);
    const FVector2D O = RealmOrigin(Team);
    for (int32 I = 0; I + 1 < Points.Num(); ++I)
    {
        const double Segment = FVector2D::Distance(Points[I], Points[I + 1]);
        if (Remaining <= Segment) { const FVector2D L = FMath::Lerp(Points[I], Points[I + 1], Segment > 0 ? Remaining / Segment : 0.); return FVector(L.X + O.X, L.Y + O.Y, Z); }
        Remaining -= Segment;
    }
    return FVector(Points.Last().X + O.X, Points.Last().Y + O.Y, Z);
}
float CireLanePath::PathProgress(const UWorld* World, int32 Team, int32 Path, const FVector& Location)
{
    Team = Realm(Team);
    const auto& Points = PathPoints(Get(World), Team, Path);
    const FVector2D P = ToLocal(Team, Location);
    double Best = TNumericLimits<double>::Max(), BestAlong = 0, Walked = 0;
    for (int32 I = 0; I + 1 < Points.Num(); ++I)
    {
        const FVector2D Segment = Points[I + 1] - Points[I]; const double Length = Segment.Size();
        const double Alpha = FMath::Clamp(FVector2D::DotProduct(P - Points[I], Segment) / FMath::Max(1., Segment.SizeSquared()), 0., 1.);
        const double D = FVector2D::DistSquared(P, Points[I] + Segment * Alpha);
        if (D < Best) { Best = D; BestAlong = Walked + Length * Alpha; }
        Walked += Length;
    }
    return Walked > 0 ? static_cast<float>(BestAlong / Walked) : 0.f;
}
FVector2D CireLanePath::NearestOnPolyline(const TArray<FVector2D>& Points, const FVector2D& Local, double* OutDistance)
{
    FVector2D Best = Points.Num() ? Points[0] : Local; double BestD = Points.Num() ? FVector2D::DistSquared(Points[0], Local) : 0.;
    for (int32 I = 0; I + 1 < Points.Num(); ++I)
    {
        const FVector2D Segment = Points[I + 1] - Points[I];
        const double Alpha = FMath::Clamp(FVector2D::DotProduct(Local - Points[I], Segment) / FMath::Max(1., Segment.SizeSquared()), 0., 1.);
        const FVector2D On = Points[I] + Segment * Alpha;
        const double D = FVector2D::DistSquared(Local, On);
        if (D < BestD) { BestD = D; Best = On; }
    }
    if (OutDistance) *OutDistance = FMath::Sqrt(BestD);
    return Best;
}
const TArray<FVector2D>& CireLanePath::UnitPath(const ACireMonster* M)
{
    const int32 Team = M ? Realm(M->Lane) : 0;
    return PathPoints(Get(M ? M->GetWorld() : nullptr), Team, M ? M->LanePath : 0);
}
float CireLanePath::DistanceToUnitPath(const ACireMonster* M, const FVector& Location)
{
    if (!M) return 0.f;
    double D = 0; NearestOnPolyline(UnitPath(M), ToLocal(Realm(M->Lane), Location), &D);
    return static_cast<float>(D);
}
FTransform CireLanePath::PlayerSpawnTransform(const UWorld* World, int32 Team, int32 Slot, float Z)
{
    Team = Realm(Team);
    const auto& R = Get(World);
    Z = FMath::IsFinite(Z) ? Z : 110.f;
    if (R.PlayerSpawns[Team].Num() > 0)
    {
        // One marker per hero slot (Eric places up to 10 per team); fewer markers are shared round-robin with a small spread.
        const int32 Count = R.PlayerSpawns[Team].Num();
        const FCireRouteSpot& S = R.PlayerSpawns[Team][FMath::Abs(Slot) % Count];
        const int32 Lap = FMath::Abs(Slot) / Count;
        const double Yaw = FMath::DegreesToRadians(S.Yaw);
        const FVector2D Right(-FMath::Sin(Yaw), FMath::Cos(Yaw));
        const float Spread = FMath::Min(FMath::Max(S.Radius, 120.f), 400.f);
        const FVector2D Local = S.Position + Right * ((Lap % 2 ? -1.f : 1.f) * ((Lap + 1) / 2) * FMath::Min(Spread, 140.f));
        return FTransform(FRotator(0, S.Yaw, 0), Grounded(World, Team, Local, Z));
    }
    return FTransform(FRotator::ZeroRotator, BasePosition(World, Team, Z) + FVector(0, Slot * 140.f, 0));
}
FVector CireLanePath::RespawnNear(const UWorld* World, int32 Team, const FVector& Near, float Z)
{
    Team = Realm(Team);
    const auto& R = Get(World);
    if (R.Respawns[Team].Num() == 0) return RespawnPosition(World, Team, Z);
    // WoW graveyards: the nearest respawn point to where the hero fell.
    const FVector2D L = ToLocal(Team, Near);
    const FCireRouteSpot* Best = &R.Respawns[Team][0];
    for (const FCireRouteSpot& S : R.Respawns[Team]) if (FVector2D::DistSquared(S.Position, L) < FVector2D::DistSquared(Best->Position, L)) Best = &S;
    return Grounded(World, Team, Best->Position, FMath::IsFinite(Z) ? Z : 110.f);
}
FVector CireLanePath::BossSpawnAt(const UWorld* World, int32 Team, int32 Index, float Z)
{
    Team = Realm(Team);
    const auto& R = Get(World);
    if (R.Bosses[Team].Num() == 0) return BossSpawnPosition(World, Team, Z);
    return Grounded(World, Team, R.Bosses[Team][FMath::Abs(Index) % R.Bosses[Team].Num()].Position, FMath::IsFinite(Z) ? Z : 110.f);
}
bool CireLanePath::RiftTransform(const UWorld* World, int32 Team, FTransform& Out, float Z)
{
    Team = Realm(Team);
    const auto& R = Get(World);
    if (R.Rifts[Team].Num() == 0) return false;
    const FCireRouteSpot& S = R.Rifts[Team][0];
    Out = FTransform(FRotator(0, S.Yaw, 0), Grounded(World, Team, S.Position, Z), FVector(FMath::Max(S.Radius, 100.f)));
    return true;
}
bool CireLanePath::InsidePlayBounds(const FCireBattlefieldRoutes& R, const FVector2D& Local)
{
    return R.PlayBounds.Num() < 3 || InsidePolygon(R.PlayBounds, Local);
}
bool CireLanePath::InsidePlayBounds(const UWorld* World, int32 Team, const FVector& Location)
{
    return InsidePlayBounds(Get(World), ToLocal(Realm(Team), Location));
}
FVector CireLanePath::ClampToPlayBounds(const UWorld* World, int32 Team, const FVector& Location, float Margin)
{
    Team = Realm(Team);
    const auto& R = Get(World);
    const FVector2D L = ToLocal(Team, Location);
    if (R.PlayBounds.Num() < 3 || InsidePolygon(R.PlayBounds, L)) return Location;
    TArray<FVector2D> Ring = R.PlayBounds; Ring.Add(R.PlayBounds[0]);
    FVector2D On = NearestOnPolyline(Ring, L);
    // Step inside by Margin toward the polygon's centroid so the result is strictly inside.
    FVector2D Centroid = FVector2D::ZeroVector; for (const FVector2D& P : R.PlayBounds) Centroid += P; Centroid /= R.PlayBounds.Num();
    On += (Centroid - On).GetSafeNormal() * FMath::Max(Margin, 1.f);
    const FVector2D O = RealmOrigin(Team);
    return FVector(On.X + O.X, On.Y + O.Y, Location.Z);
}

// ------------------------------------------------------------------------------------------------ replication block
// Appended to GameState::LaneLayout after the town tail: a tag, then per realm the spawns, paths and spots, then the
// bounds polygon. Realm 1 is written as "same as realm 0" when the realms are identical (mirrored layouts), which keeps
// the float array well inside the replication array budget.
namespace { constexpr float ExtrasTag = 7331.f, SameAsRealm0 = -1.f; }
void CireLanePath::PackExtras(const FCireBattlefieldRoutes& R, TArray<float>& L)
{
    L.Add(ExtrasTag);
    auto Spots = [&](const TArray<FCireRouteSpot>& List, bool bFlags)
    {
        L.Add(List.Num());
        for (const FCireRouteSpot& S : List) { L.Add(S.Position.X); L.Add(S.Position.Y); L.Add(S.Yaw); L.Add(S.Radius); if (bFlags) L.Add(S.bWeighted ? 1.f : 0.f); }
    };
    for (int32 Team = 0; Team < 2; ++Team)
    {
        const bool bSame = Team == 1 && R.Spawns[1] == R.Spawns[0] && R.Paths[1] == R.Paths[0] && R.PlayerSpawns[1] == R.PlayerSpawns[0] &&
            R.Respawns[1] == R.Respawns[0] && R.Bosses[1] == R.Bosses[0] && R.Rifts[1] == R.Rifts[0];
        if (bSame) { L.Add(SameAsRealm0); continue; }
        Spots(R.Spawns[Team], true);
        L.Add(R.Paths[Team].Num());
        for (const FCireRoutePath& P : R.Paths[Team])
        {
            L.Add(P.Weight); L.Add(P.Spawn);
            // Path 0 rides in LanePoints0/1 already.
            if (&P == &R.Paths[Team][0]) { L.Add(0); continue; }
            L.Add(P.Points.Num());
            for (const FVector2D& V : P.Points) { L.Add(V.X); L.Add(V.Y); }
        }
        Spots(R.PlayerSpawns[Team], false); Spots(R.Respawns[Team], false); Spots(R.Bosses[Team], false); Spots(R.Rifts[Team], false);
    }
    L.Add(R.PlayBounds.Num());
    for (const FVector2D& V : R.PlayBounds) { L.Add(V.X); L.Add(V.Y); }
}
bool CireLanePath::UnpackExtras(const TArray<float>& L, int32 At, FCireBattlefieldRoutes& Out)
{
    FCireBattlefieldRoutes R = Out;
    auto Read = [&](float& V) { if (At >= L.Num() || !FMath::IsFinite(L[At])) return false; V = L[At++]; return true; };
    auto Count = [&](int32& N, int32 Max) { float V = 0; if (!Read(V)) return false; N = FMath::RoundToInt(V); return N >= 0 && N <= Max; };
    auto Spots = [&](TArray<FCireRouteSpot>& List, bool bFlags)
    {
        int32 N = 0; if (!Count(N, FCireBattlefieldRoutes::MaxSpots)) return false;
        List.Reset();
        for (int32 I = 0; I < N; ++I)
        {
            FCireRouteSpot S; float X = 0, Y = 0, F = 0;
            if (!Read(X) || !Read(Y) || !Read(S.Yaw) || !Read(S.Radius) || (bFlags && !Read(F))) return false;
            S.Position = FVector2D(X, Y); S.bWeighted = F > .5f; List.Add(S);
        }
        return true;
    };
    float Tag = 0;
    if (!Read(Tag) || Tag != ExtrasTag) return false;
    for (int32 Team = 0; Team < 2; ++Team)
    {
        if (At < L.Num() && L[At] == SameAsRealm0 && Team == 1)
        {
            ++At; R.Spawns[1] = R.Spawns[0]; R.Paths[1] = R.Paths[0]; R.PlayerSpawns[1] = R.PlayerSpawns[0];
            R.Respawns[1] = R.Respawns[0]; R.Bosses[1] = R.Bosses[0]; R.Rifts[1] = R.Rifts[0];
            if (R.Paths[1].Num() > 0) R.Paths[1][0].Points = R.LocalPoints[1];
            continue;
        }
        if (!Spots(R.Spawns[Team], true)) return false;
        int32 Paths = 0; if (!Count(Paths, FCireBattlefieldRoutes::MaxPaths)) return false;
        R.Paths[Team].Reset();
        for (int32 P = 0; P < Paths; ++P)
        {
            FCireRoutePath Path; float Spawn = 0; int32 N = 0;
            if (!Read(Path.Weight) || !Read(Spawn) || !Count(N, 64)) return false;
            Path.Spawn = FMath::RoundToInt(Spawn);
            for (int32 I = 0; I < N; ++I) { float X = 0, Y = 0; if (!Read(X) || !Read(Y)) return false; Path.Points.Add(FVector2D(X, Y)); }
            if (P == 0) Path.Points = R.LocalPoints[Team];
            R.Paths[Team].Add(Path);
        }
        if (!Spots(R.PlayerSpawns[Team], false) || !Spots(R.Respawns[Team], false) || !Spots(R.Bosses[Team], false) || !Spots(R.Rifts[Team], false)) return false;
    }
    int32 Bounds = 0; if (!Count(Bounds, 64)) return false;
    R.PlayBounds.Reset();
    for (int32 I = 0; I < Bounds; ++I) { float X = 0, Y = 0; if (!Read(X) || !Read(Y)) return false; R.PlayBounds.Add(FVector2D(X, Y)); }
    Out = MoveTemp(R);
    return true;
}

// ------------------------------------------------------------------------------------------------ startup document
namespace
{
/** Developer probes, galleries and labs keep the shipped route file (their fixtures were authored against it). */
bool DeveloperRun()
{
    const FString Args = FCommandLine::Get();
    if (FParse::Param(*Args, TEXT("CireNoMapLayout"))) return true;
    if (FParse::Param(*Args, TEXT("CireUseMapLayout"))) return false;
    for (const TCHAR* Word : {TEXT("Probe"), TEXT("Gallery"), TEXT("Preview"), TEXT("Smoke"), TEXT("BalanceLab"), TEXT("Soak"), TEXT("Tests"), TEXT("Lab"), TEXT("Capture"), TEXT("Interface"), TEXT("Expansion")})
        if (Args.Contains(Word)) return true;
    return false;
}
}
bool CireLanePath::LoadActive(FCireBattlefieldRoutes& Out, FString* Error, FString* Source)
{
    FCireBattlefieldRoutes Base; FString Why;
    const FString RouteFile = FPaths::GetCleanFilename(DataPath());
    if (!LoadFile(Base, &Why)) { if (Error) *Error = Why; return false; }
    Out = Base;
    FString From = RouteFile;
    // One source of truth: the map layout editor's MapLayout.json (Apply writes it). The route file is the provisional
    // default and supplies what the layout does not author (realm bounds, lane width, escort tuning).
    FCireMapLayout Layout; FString LayoutError;
    const FString LayoutPath = CireMapLayout::ActivePath();
    if (!DeveloperRun() && FPaths::FileExists(LayoutPath))
    {
        if (!CireMapLayout::Load(Layout, LayoutPath, &LayoutError))
        {
            UE_LOG(LogCireLayoutRoutes, Warning, TEXT("CIRE_LAYOUT_SKIPPED MapLayout.json does not parse (%s); using %s"), *LayoutError, *RouteFile);
        }
        else if (!CireMapLayout::MatchesActiveMap(Layout))
        {
            UE_LOG(LogCireLayoutRoutes, Display, TEXT("CIRE_LAYOUT_SKIPPED MapLayout.json was authored on the %s map; this match runs the %s map (%s)"),
                *Layout.Map, CireTownMap::IsActive() ? TEXT("castletown") : TEXT("procedural"), *RouteFile);
        }
        else
        {
            FCireBattlefieldRoutes Compiled; TArray<FString> Notes; FString CompileError;
            if (CireMapLayout::CompileRoutes(Layout, Base, Compiled, Notes) && Validate(Compiled, CompileError))
            {
                Out = MoveTemp(Compiled);
                From = FString::Printf(TEXT("MapLayout.json \"%s\" over %s"), *Layout.Name, *RouteFile);
                for (const FString& N : Notes) UE_LOG(LogCireLayoutRoutes, Display, TEXT("CIRE_LAYOUT_NOTE %s"), *N);
            }
            else
            {
                for (const FString& N : Notes) UE_LOG(LogCireLayoutRoutes, Warning, TEXT("CIRE_LAYOUT_NOTE %s"), *N);
                UE_LOG(LogCireLayoutRoutes, Warning, TEXT("CIRE_LAYOUT_REJECTED MapLayout.json cannot run (%s); using %s. Open the layout editor and VALIDATE."),
                    CompileError.IsEmpty() ? TEXT("a realm has no complete monster path") : *CompileError, *RouteFile);
            }
        }
    }
    GActiveSource = From;
    if (Source) *Source = From;
    if (Error) Error->Reset();
    UE_LOG(LogCireLayoutRoutes, Display, TEXT("CIRE_LAYOUT_ACTIVE source=%s paths=%d/%d spawns=%d/%d packs=%d/%d playerSpawns=%d/%d bounds=%d"), *From,
        PathCount(Out, 0), PathCount(Out, 1), SpawnSpots(Out, 0).Num(), SpawnSpots(Out, 1).Num(), BayCount(Out, 0), BayCount(Out, 1),
        Out.PlayerSpawns[0].Num(), Out.PlayerSpawns[1].Num(), Out.PlayBounds.Num());
    return true;
}
FString CireLanePath::ActiveSource() { return GActiveSource; }

// ------------------------------------------------------------------------------------------------ jungle-packs: packs on the wire
// GameState::LanePacks: a header (magic, route revision, pack count of realm 0, of realm 1 or -1 when identical), then
// three ints per pack: [x/10 | y/10] (int16 each), [radius/10 | tier | type | tanks | healers | dps], seed. Split into
// chunks of ChunkInts values, so every replicated array stays far inside the engine's 2048-element budget however
// many packs a layout places.
namespace
{
constexpr int32 PackMagic = 0x4A50, ChunkInts = 1020;
int32 Clamp16(double V) { return FMath::Clamp(FMath::RoundToInt32(V / 10.), -32767, 32767); }
}
void CireLanePath::PackBays(const FCireBattlefieldRoutes& R, uint32 Revision, TArray<TArray<int32>>& OutChunks)
{
    TArray<int32> All;
    const bool bSame = R.Bays[1] == R.Bays[0];
    All.Add(PackMagic); All.Add(static_cast<int32>(Revision)); All.Add(R.Bays[0].Num()); All.Add(bSame ? -1 : R.Bays[1].Num());
    const TArray<FName> Types = CireJunglePacks::PackTypes();
    for (int32 Team = 0; Team < (bSame ? 1 : 2); ++Team)
        for (const FCireChallengeBay& B : R.Bays[Team])
        {
            All.Add(static_cast<int32>((static_cast<uint32>(Clamp16(B.Position.X)) & 0xFFFFu) << 16 | (static_cast<uint32>(Clamp16(B.Position.Y)) & 0xFFFFu)));
            const uint32 Radius = static_cast<uint32>(FMath::Clamp(FMath::RoundToInt(B.Radius / 10.f), 0, 255));
            const uint32 Tier = static_cast<uint32>(FMath::Clamp(B.Tier, 1, 7));
            const uint32 Type = static_cast<uint32>(FMath::Clamp(Types.IndexOfByKey(B.PackType), 0, 63)); // unknown -> 0 is never written: types are normalised
            const FCirePackComposition C = B.HasCompOverride() ? B.Comp : FCirePackComposition{0, 0, 0};
            All.Add(static_cast<int32>(Radius | Tier << 8 | Type << 11 | static_cast<uint32>(C.Tanks & 3) << 17 | static_cast<uint32>(C.Healers & 3) << 19 | static_cast<uint32>(C.Dps & 3) << 21));
            All.Add(static_cast<int32>(B.EffectiveSeed()));
        }
    OutChunks.Reset();
    for (int32 At = 0; At < All.Num(); At += ChunkInts)
    {
        TArray<int32>& Chunk = OutChunks.AddDefaulted_GetRef();
        Chunk.Append(All.GetData() + At, FMath::Min(ChunkInts, All.Num() - At));
    }
}
bool CireLanePath::UnpackBays(const TArray<TArray<int32>>& Chunks, uint32& OutRevision, TArray<FCireChallengeBay> OutBays[2])
{
    TArray<int32> All;
    for (const TArray<int32>& C : Chunks) All.Append(C);
    if (All.Num() < 4 || All[0] != PackMagic || All[2] < 0 || All[3] < -1) return false;
    const bool bSame = All[3] == -1;
    const int32 Counts[2] = {All[2], bSame ? 0 : All[3]};
    if (All.Num() != 4 + 3 * (Counts[0] + Counts[1])) return false;
    OutRevision = static_cast<uint32>(All[1]);
    const TArray<FName> Types = CireJunglePacks::PackTypes();
    int32 At = 4;
    for (int32 Team = 0; Team < (bSame ? 1 : 2); ++Team)
    {
        OutBays[Team].Reset(Counts[Team]);
        for (int32 I = 0; I < Counts[Team]; ++I, At += 3)
        {
            const uint32 W0 = static_cast<uint32>(All[At]), W1 = static_cast<uint32>(All[At + 1]);
            FCireChallengeBay B;
            B.Position = FVector2D(static_cast<int16>(W0 >> 16) * 10., static_cast<int16>(W0 & 0xFFFFu) * 10.);
            B.Radius = FMath::Clamp((W1 & 0xFFu) * 10.f, FCireChallengeBay::MinRadius, FCireChallengeBay::MaxRadius);
            B.Tier = FMath::Clamp(static_cast<int32>((W1 >> 8) & 7u), 1, FCireChallengeBay::MaxTier);
            const int32 Type = static_cast<int32>((W1 >> 11) & 63u);
            B.PackType = Types.IsValidIndex(Type) ? Types[Type] : CireJunglePacks::Mixed;
            B.Comp = {static_cast<int32>((W1 >> 17) & 3u), static_cast<int32>((W1 >> 19) & 3u), static_cast<int32>((W1 >> 21) & 3u)};
            B.Seed = static_cast<uint32>(All[At + 2]);
            OutBays[Team].Add(B);
        }
    }
    if (bSame) OutBays[1] = OutBays[0];
    return true;
}

// ------------------------------------------------------------------------------------------------ jungle-packs: recall points
bool CireLanePath::HasRecallPoint(const UWorld* World, int32 Team) { return Get(World).Recalls[Realm(Team)].Num() > 0; }
FVector CireLanePath::RecallNear(const UWorld* World, int32 Team, const FVector& Near, float Z)
{
    Team = Realm(Team);
    const auto& R = Get(World);
    if (R.Recalls[Team].Num() == 0) return BasePosition(World, Team, Z);
    const FVector2D L = ToLocal(Team, Near);
    const FCireRouteSpot* Best = &R.Recalls[Team][0];
    for (const FCireRouteSpot& S : R.Recalls[Team]) if (FVector2D::DistSquared(S.Position, L) < FVector2D::DistSquared(Best->Position, L)) Best = &S;
    return Grounded(World, Team, Best->Position, FMath::IsFinite(Z) ? Z : 110.f);
}
