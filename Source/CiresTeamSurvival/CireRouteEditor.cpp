// nav-paths: path editor model: live validation (rules, navmesh reachability, route clearance) and draft edits.
// dev-route-tools: authoring operations on the shared route, undo history, readout helpers and the draft file.
#include "CireRouteEditor.h"
#include "CireEnvironmentProps.h"
#include "CireNav.h"
#include "CireNPCArchetypes.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
FVector WorldOf(int32 Team, const FVector2D& Local, float Z = 60.f) { return CireLanePath::ToWorld(Team, Local, Z); }
ECireRouteReach Worst(ECireRouteReach A, ECireRouteReach B) { return static_cast<uint8>(A) >= static_cast<uint8>(B) ? A : B; }
}

ECireRouteReach CireRouteEditor::Reach(const UWorld* World, const FVector& From, const FVector& To, float& OutLength, TArray<FVector>* OutPath)
{
    OutLength = 0.f;
    if (OutPath) OutPath->Reset();
    if (!CireNav::HasNavigation(World)) return ECireRouteReach::Unknown;
    ECireRouteReach Result = ECireRouteReach::Direct;
    // Both agent sizes: ordinary units and heroes (Hero mesh) and the Pack Leader / oversized rows (Large mesh).
    for (const float Radius : {40.f, 66.f})
    {
        FVector A, B;
        if (!CireNav::Project(World, From, A, FVector(150, 150, 400), Radius) || !CireNav::Project(World, To, B, FVector(150, 150, 400), Radius))
            return ECireRouteReach::None;
        const FCireNavPath Path = CireNav::FindPath(World, A, B, Radius, true);
        ECireRouteReach Here = ECireRouteReach::Direct;
        if (!Path.bValid) Here = ECireRouteReach::None;
        else if (Path.bPartial || FVector::Dist2D(Path.Points.Last(), B) > 120.) Here = ECireRouteReach::Partial;
        else if (Path.Length > FMath::Max(1.f, Path.Direct) * 1.2f + 60.f) Here = ECireRouteReach::Detour;
        if (Radius < 50.f) { OutLength = Path.Length; if (OutPath) *OutPath = Path.Points; }
        Result = Worst(Result, Here);
    }
    return Result;
}

FCireRouteValidation CireRouteEditor::Validate(const UWorld* World, const FCireBattlefieldRoutes& Draft)
{
    FCireRouteValidation V;
    const double Started = FPlatformTime::Seconds();
    V.bRulesOk = CireLanePath::Validate(Draft, V.RulesError);
    V.bNavAvailable = CireNav::HasNavigation(World);
    V.NavRevision = World ? CireNav::Stats(World).NavRevision : 0;
    for (int32 Team = 0; Team < 2; ++Team)
    {
        const auto& Points = Draft.LocalPoints[Team];
        for (int32 I = 0; I + 1 < Points.Num(); ++I)
        {
            FCireRouteSegmentCheck S;
            S.Direct = static_cast<float>(FVector2D::Distance(Points[I], Points[I + 1]));
            S.Reach = Reach(World, WorldOf(Team, Points[I]), WorldOf(Team, Points[I + 1]), S.PathLength, &S.Path);
            S.PropConflicts = CireEnvironmentProps::RouteConflicts(World, Team, Points[I], Points[I + 1], Draft.LaneWidth, &S.Slots);
            V.Unreachable += S.Reach == ECireRouteReach::None || S.Reach == ECireRouteReach::Partial ? 1 : 0;
            V.Detours += S.Reach == ECireRouteReach::Detour ? 1 : 0;
            V.Conflicts += S.PropConflicts;
            V.Segments[Team].Add(MoveTemp(S));
        }
        // dev-route-tools: every pack (1..16, or the three automatic bays).
        const int32 Count = Points.Num() > 1 ? CireLanePath::BayCount(Draft, Team) : 0;
        for (int32 Bay = 1; Bay <= Count; ++Bay)
        {
            const FCireChallengeBay Pack = CireLanePath::BayAt(Draft, Team, Bay);
            // A pack must be able to walk home from the lane: path from the nearest route point to the bay.
            FVector2D Nearest = Points[0]; double Best = TNumericLimits<double>::Max();
            for (int32 I = 0; I + 1 < Points.Num(); ++I)
            {
                const FVector2D Seg = Points[I + 1] - Points[I];
                const double T = FMath::Clamp(FVector2D::DotProduct(Pack.Position - Points[I], Seg) / FMath::Max(1., Seg.SizeSquared()), 0., 1.);
                const FVector2D P = Points[I] + Seg * T;
                if (FVector2D::DistSquared(P, Pack.Position) < Best) { Best = FVector2D::DistSquared(P, Pack.Position); Nearest = P; }
            }
            float Length = 0;
            const ECireRouteReach BayReach = Reach(World, WorldOf(Team, Nearest), WorldOf(Team, Pack.Position), Length);
            V.BayReach[Team].Add(BayReach);
            V.BayConflicts[Team].Add(CireEnvironmentProps::BayConflicts(World, Team, Pack.Position, nullptr, Pack.Radius));
            if (BayReach == ECireRouteReach::None || BayReach == ECireRouteReach::Partial) ++V.Unreachable;
        }
    }
    V.Ms = (FPlatformTime::Seconds() - Started) * 1000.0;
    return V;
}

FString FCireRouteValidation::Summary() const
{
    if (!bRulesOk) return FString::Printf(TEXT("INVALID: %s"), *RulesError);
    const FString Nav = bNavAvailable ? FString::Printf(TEXT("%d unreachable, %d detours"), Unreachable, Detours) : FString(TEXT("no navmesh on this peer"));
    return FString::Printf(TEXT("%s  |  %s  |  %d town pieces in the lane"), Passed() ? TEXT("VALID") : TEXT("BLOCKED"), *Nav, Conflicts);
}

FLinearColor CireRouteEditor::ReachColor(ECireRouteReach R)
{
    switch (R)
    {
    case ECireRouteReach::Direct: return FLinearColor(.25f, .85f, .45f, 1);
    case ECireRouteReach::Detour: return FLinearColor(.95f, .8f, .25f, 1);
    case ECireRouteReach::Partial: return FLinearColor(.95f, .45f, .15f, 1);
    case ECireRouteReach::None: return FLinearColor(.9f, .18f, .2f, 1);
    default: return FLinearColor(.55f, .58f, .6f, 1);
    }
}
const TCHAR* CireRouteEditor::ReachLabel(ECireRouteReach R)
{
    switch (R)
    {
    case ECireRouteReach::Direct: return TEXT("direct");
    case ECireRouteReach::Detour: return TEXT("detour");
    case ECireRouteReach::Partial: return TEXT("partial");
    case ECireRouteReach::None: return TEXT("no path");
    default: return TEXT("unknown");
    }
}

int32 CireRouteEditor::InsertAfter(FCireBattlefieldRoutes& D, int32 Team, int32 Index, bool bLinked)
{
    Team = FMath::Clamp(Team, 0, 1);
    auto& Points = D.LocalPoints[Team];
    if (Points.Num() >= 64 || !Points.IsValidIndex(Index) || Index + 1 >= Points.Num()) return INDEX_NONE;
    const FVector2D Mid = (Points[Index] + Points[Index + 1]) * .5;
    Points.Insert(Mid, Index + 1);
    if (bLinked) D.LocalPoints[1 - Team] = Points;
    return Index + 1;
}
bool CireRouteEditor::Delete(FCireBattlefieldRoutes& D, int32 Team, int32 Index, bool bLinked)
{
    Team = FMath::Clamp(Team, 0, 1);
    auto& Points = D.LocalPoints[Team];
    if (Points.Num() <= 3 || Index <= 0 || Index >= Points.Num() - 1) return false;
    Points.RemoveAt(Index);
    if (bLinked) D.LocalPoints[1 - Team] = Points;
    return true;
}
void CireRouteEditor::MovePoint(FCireBattlefieldRoutes& D, int32 Team, int32 Index, const FVector2D& Local, bool bLinked)
{
    Team = FMath::Clamp(Team, 0, 1);
    if (!D.LocalPoints[Team].IsValidIndex(Index)) return;
    D.LocalPoints[Team][Index] = Local;
    if (bLinked) D.LocalPoints[1 - Team] = D.LocalPoints[Team];
}
void CireRouteEditor::MoveBay(const UWorld*, FCireBattlefieldRoutes& D, int32 Team, int32 Bay, const FVector2D& Local, bool bLinked)
{
    Team = FMath::Clamp(Team, 0, 1);
    // dev-route-tools: dragging an automatic bay turns the three into authored packs first.
    for (int32 T = 0; T < 2; ++T)
        if (D.Bays[T].Num() == 0) D.Bays[T] = CireLanePath::AutoBays(D, T);
    if (!D.Bays[Team].IsValidIndex(Bay - 1)) return;
    D.Bays[Team][Bay - 1].Position = Local;
    if (bLinked) D.Bays[1 - Team] = D.Bays[Team];
}

// ---------------------------------------------------------------------------------------------- dev-route-tools
float CireRouteEditor::MarchSpeed(float* OutMin, float* OutMax)
{
    const auto& Db = CireNPCArchetypes::Get();
    float Sum = 0, Min = TNumericLimits<float>::Max(), Max = 0; int32 Count = 0;
    for (const FName Id : Db.WaveComposition)
        if (const FCireNPCArchetype* A = CireNPCArchetypes::Find(Id))
            if (A->MoveSpeed > 1.f) { Sum += A->MoveSpeed; Min = FMath::Min(Min, A->MoveSpeed); Max = FMath::Max(Max, A->MoveSpeed); ++Count; }
    const float Typical = Count > 0 ? Sum / Count : 200.f;
    if (OutMin) *OutMin = Count > 0 ? Min : Typical;
    if (OutMax) *OutMax = Count > 0 ? Max : Typical;
    return Typical;
}
FString CireRouteEditor::FormatWalkTime(double Seconds)
{
    const int32 Total = FMath::Max(0, FMath::RoundToInt(FMath::IsFinite(Seconds) ? Seconds : 0.));
    return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
}
