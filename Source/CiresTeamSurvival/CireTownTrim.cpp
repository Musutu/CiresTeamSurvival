// town-trim: CireTownTrim.h
#include "CireTownTrim.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireMapLayout.h"
#include "CireRouteEditMode.h"
#include "CireTownMap.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/AudioComponent.h"
#include "Components/LightComponentBase.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Info.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Particles/ParticleSystemComponent.h"
#include "NiagaraComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTownTrim, Log, All);

// ================================================================================================= geometry
namespace
{
double Cross(const FVector2D& A, const FVector2D& B) { return A.X * B.Y - A.Y * B.X; }
bool SegmentsCross(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
    const FVector2D R = B - A, S = D - C;
    const double Den = Cross(R, S);
    if (FMath::Abs(Den) < 1e-9) return false; // parallel: a touching overlap is caught by the containment tests
    const double T = Cross(C - A, S) / Den, U = Cross(C - A, R) / Den;
    return T >= 0 && T <= 1 && U >= 0 && U <= 1;
}
double PointSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
{
    const FVector2D AB = B - A;
    const double L = AB.SizeSquared();
    const double T = L > 0 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L, 0.0, 1.0) : 0.0;
    return FVector2D::Distance(P, A + AB * T);
}
double SegmentSegment(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
    if (SegmentsCross(A, B, C, D)) return 0;
    return FMath::Min(FMath::Min(PointSegment(A, C, D), PointSegment(B, C, D)), FMath::Min(PointSegment(C, A, B), PointSegment(D, A, B)));
}
}

bool CireTownTrim::InsidePolygon(const TArray<FVector2D>& Poly, const FVector2D& P)
{
    bool bInside = false;
    for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++)
    {
        const FVector2D& A = Poly[I]; const FVector2D& C = Poly[J];
        if ((A.Y > P.Y) != (C.Y > P.Y) && P.X < (C.X - A.X) * (P.Y - A.Y) / (C.Y - A.Y) + A.X) bInside = !bInside;
    }
    return bInside;
}
double CireTownTrim::EdgeDistance(const TArray<FVector2D>& Poly, const FVector2D& P)
{
    double Best = TNumericLimits<double>::Max();
    for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++) Best = FMath::Min(Best, PointSegment(P, Poly[J], Poly[I]));
    return Best;
}
bool CireTownTrim::WithinMargin(const TArray<FVector2D>& Poly, const FVector2D& P, float Margin)
{
    return Poly.Num() < 3 || InsidePolygon(Poly, P) || EdgeDistance(Poly, P) <= Margin;
}
ECireTrimClass CireTownTrim::Classify(const TArray<FVector2D>& Poly, const FBox2D& Box, float Margin)
{
    if (Poly.Num() < 3) return ECireTrimClass::Inside; // no bounds: nothing is trimmed
    const FVector2D Corners[4] = {Box.Min, FVector2D(Box.Max.X, Box.Min.Y), Box.Max, FVector2D(Box.Min.X, Box.Max.Y)};
    int32 CornersIn = 0;
    for (const FVector2D& C : Corners) CornersIn += InsidePolygon(Poly, C) ? 1 : 0;
    bool bVertexIn = false, bCross = false;
    for (const FVector2D& V : Poly) if (V.X > Box.Min.X && V.X < Box.Max.X && V.Y > Box.Min.Y && V.Y < Box.Max.Y) { bVertexIn = true; break; }
    for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num() && !bCross; J = I++)
        for (int32 K = 0; K < 4 && !bCross; ++K) bCross = SegmentsCross(Poly[J], Poly[I], Corners[K], Corners[(K + 1) % 4]);
    if (CornersIn == 4 && !bVertexIn && !bCross) return ECireTrimClass::Inside;
    if (CornersIn > 0 || bVertexIn || bCross) return ECireTrimClass::Straddling;
    // Disjoint: how far is the box from the polygon?
    double Best = TNumericLimits<double>::Max();
    for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++)
        for (int32 K = 0; K < 4; ++K) Best = FMath::Min(Best, SegmentSegment(Poly[J], Poly[I], Corners[K], Corners[(K + 1) % 4]));
    return Best <= Margin ? ECireTrimClass::Straddling : ECireTrimClass::Outside;
}
ECireTrimClass CireTownTrim::ClassifyWorld(const TArray<FVector2D>& Poly, const FBox& World, float Margin)
{
    if (Poly.Num() < 3) return ECireTrimClass::Inside;
    // The realm the box belongs to (both realms are identical copies, so one realm-local polygon cuts both).
    const int32 Realm = CireTownMap::RealmAt(World.GetCenter());
    const FVector2D O = CireLanePath::RealmOrigin(Realm);
    return Classify(Poly, FBox2D(FVector2D(World.Min) - O, FVector2D(World.Max) - O), Margin);
}
TArray<FBox2D> CireTownTrim::RasterBoxes(const TArray<FVector2D>& Poly, float Margin, float Cell, const FBox2D& Clip, int32 MaxBoxes)
{
    TArray<FBox2D> Out;
    if (Poly.Num() < 3) return Out;
    FBox2D Area(ForceInit);
    for (const FVector2D& P : Poly) Area += P;
    Area = Area.ExpandBy(Margin);
    if (Clip.bIsValid) { Area.Min = FVector2D::Max(Area.Min, Clip.Min); Area.Max = FVector2D::Min(Area.Max, Clip.Max); if (Area.Min.X >= Area.Max.X || Area.Min.Y >= Area.Max.Y) return Out; }
    for (int32 Attempt = 0; Attempt < 6; ++Attempt, Cell *= 1.5f)
    {
        Out.Reset();
        const int32 NX = FMath::Max(1, FMath::CeilToInt((Area.Max.X - Area.Min.X) / Cell)), NY = FMath::Max(1, FMath::CeilToInt((Area.Max.Y - Area.Min.Y) / Cell));
        // Rows of kept cells merged into runs, then runs with the same X span stacked into boxes.
        TArray<FBox2D> Open;
        for (int32 Y = 0; Y < NY; ++Y)
        {
            const double Y0 = Area.Min.Y + Y * Cell, Y1 = FMath::Min<double>(Area.Max.Y, Y0 + Cell);
            TArray<FBox2D> Row;
            int32 Start = -1;
            for (int32 X = 0; X <= NX; ++X)
            {
                bool bKeep = false;
                if (X < NX)
                {
                    const double X0 = Area.Min.X + X * Cell, X1 = FMath::Min<double>(Area.Max.X, X0 + Cell);
                    bKeep = Classify(Poly, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), Margin) != ECireTrimClass::Outside;
                }
                if (bKeep && Start < 0) Start = X;
                if (!bKeep && Start >= 0)
                {
                    Row.Add(FBox2D(FVector2D(Area.Min.X + Start * Cell, Y0), FVector2D(FMath::Min<double>(Area.Max.X, Area.Min.X + X * Cell), Y1)));
                    Start = -1;
                }
            }
            TArray<FBox2D> Next;
            for (const FBox2D& R : Row)
            {
                FBox2D* Grow = Open.FindByPredicate([&](const FBox2D& B) { return FMath::IsNearlyEqual(B.Min.X, R.Min.X) && FMath::IsNearlyEqual(B.Max.X, R.Max.X) && FMath::IsNearlyEqual(B.Max.Y, R.Min.Y); });
                if (Grow) { FBox2D G = *Grow; G.Max.Y = R.Max.Y; Next.Add(G); Grow->bIsValid = false; }
                else Next.Add(R);
            }
            for (const FBox2D& B : Open) if (B.bIsValid) Out.Add(B);
            Open = MoveTemp(Next);
        }
        Out.Append(Open);
        if (Out.Num() <= MaxBoxes) return Out;
    }
    Out.Reset(); Out.Add(Area); // too ragged: one box
    return Out;
}

// ================================================================================================= settings and polygon
namespace
{
FCireTrimSettings GSettings;
bool bSettingsLoaded = false;
TArray<FVector2D> GOverride; bool bOverride = false;
TArray<FVector2D> GFrozen; bool bFrozen = false;
FString GFrozenSource;

bool ReadPolygonFile(const FString& Path, TArray<FVector2D>& Out)
{
    FString Json; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Json, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) return false;
    auto Points = [&](const TSharedPtr<FJsonObject>& O)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!O->TryGetArrayField(TEXT("points"), A) || !A) return false;
        Out.Reset();
        for (const auto& V : *A)
        {
            const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
            if (V && V->TryGetArray(P) && P && P->Num() >= 2) Out.Add(FVector2D((*P)[0]->AsNumber(), (*P)[1]->AsNumber()));
        }
        return Out.Num() >= 3;
    };
    if (Points(Root)) return true;
    // A MapLayout.json document: its Play Bounds marker.
    const TArray<TSharedPtr<FJsonValue>>* Markers = nullptr;
    if (Root->TryGetArrayField(TEXT("markers"), Markers) && Markers)
        for (const auto& M : *Markers)
        {
            const TSharedPtr<FJsonObject>* O = nullptr; FString Type;
            if (M && M->TryGetObject(O) && O && (*O)->TryGetStringField(TEXT("type"), Type) && Type == TEXT("playBounds") && Points(*O)) return true;
        }
    return false;
}
TArray<FVector2D> LivePolygon(FString* Source = nullptr)
{
    if (bOverride) { if (Source) *Source = TEXT("test"); return GOverride; }
    FString File;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireTownTrimBounds="), File))
    {
        TArray<FVector2D> P;
        if (FPaths::IsRelative(File)) File = FPaths::ProjectDir() / File;
        if (ReadPolygonFile(File, P)) { if (Source) *Source = File; return P; }
        UE_LOG(LogCireTownTrim, Warning, TEXT("CIRE_TOWN_TRIM_BOUNDS_FILE unreadable or no points: %s"), *File);
    }
    if (Source) *Source = CireLanePath::ActiveSource();
    const TArray<FVector2D>& Bounds = CireLanePath::Get(nullptr).PlayBounds;
    return Bounds.Num() >= 3 ? Bounds : TArray<FVector2D>();
}
bool HasParam(const TCHAR* Name) { return FParse::Param(FCommandLine::Get(), Name); }
}

FCireTrimSettings CireTownTrim::ParseSettings(const FString& Json)
{
    FCireTrimSettings S;
    TSharedPtr<FJsonObject> Root; const TSharedPtr<FJsonObject>* Trim = nullptr;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root || !Root->TryGetObjectField(TEXT("trim"), Trim) || !Trim) return S;
    const TSharedPtr<FJsonObject>& T = *Trim;
    T->TryGetBoolField(TEXT("enabled"), S.bEnabled);
    T->TryGetBoolField(TEXT("keepLandscape"), S.bKeepLandscape);
    auto Num = [&](const TCHAR* Key, float& Out, float Min) { double V = 0; if (T->TryGetNumberField(Key, V) && FMath::IsFinite(V)) Out = FMath::Max(Min, float(V)); };
    Num(TEXT("margin"), S.Margin, 0.f); Num(TEXT("landmarkHeight"), S.LandmarkHeight, 0.f); Num(TEXT("landmarkSize"), S.LandmarkSize, 0.f);
    Num(TEXT("backdropRange"), S.BackdropRange, 0.f); Num(TEXT("navCell"), S.NavCell, 500.f);
    FString Mode;
    if (T->TryGetStringField(TEXT("backdrop"), Mode))
        S.Backdrop = Mode.Equals(TEXT("off"), ESearchCase::IgnoreCase) ? ECireTrimBackdrop::Off : Mode.Equals(TEXT("full"), ESearchCase::IgnoreCase) ? ECireTrimBackdrop::Full : ECireTrimBackdrop::Low;
    auto List = [&](const TCHAR* Key, TArray<FString>& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!T->TryGetArrayField(Key, A) || !A) return;
        Out.Reset(); for (const auto& V : *A) { FString Str; if (V && V->TryGetString(Str) && !Str.IsEmpty()) Out.Add(Str); }
    };
    List(TEXT("backdropLevels"), S.BackdropLevels); List(TEXT("backdropLevelInstances"), S.BackdropLevelInstances);
    return S;
}
const FCireTrimSettings& CireTownTrim::Settings()
{
    if (!bSettingsLoaded)
    {
        bSettingsLoaded = true;
        FString Path = FPaths::ProjectContentDir() / TEXT("Data/CastleTown.json"), Json;
        FParse::Value(FCommandLine::Get(), TEXT("CireTownJson="), Path);
        if (FFileHelper::LoadFileToString(Json, *Path)) GSettings = ParseSettings(Json);
        FString Mode; // A/B runs: -CireTownTrimBackdrop=off|low|full, -CireTownTrimMargin=<cm>
        if (FParse::Value(FCommandLine::Get(), TEXT("CireTownTrimBackdrop="), Mode))
            GSettings.Backdrop = Mode == TEXT("off") ? ECireTrimBackdrop::Off : Mode == TEXT("full") ? ECireTrimBackdrop::Full : ECireTrimBackdrop::Low;
        float Margin = 0; if (FParse::Value(FCommandLine::Get(), TEXT("CireTownTrimMargin="), Margin) && Margin >= 0) GSettings.Margin = Margin;
    }
    return GSettings;
}
const TArray<FVector2D>& CireTownTrim::Polygon()
{
    if (bOverride) return GOverride;
    if (bFrozen) return GFrozen;
    static TArray<FVector2D> Live; Live = LivePolygon();
    return Live;
}
void CireTownTrim::SetPolygonOverride(const TArray<FVector2D>* Poly)
{
    bOverride = Poly != nullptr;
    GOverride = Poly ? *Poly : TArray<FVector2D>();
}
bool CireTownTrim::Active()
{
    if (bOverride) return GOverride.Num() >= 3 && Settings().bEnabled;
    if (!CireTownMap::IsActive() || !Settings().bEnabled || HasParam(TEXT("CireNoTownTrim"))) return false;
    // The editor and explore mode walk the whole town: they load everything and only preview the trim.
    if (CireRouteEditMode::IsActive() || CireTownMap::IsExplore()) return false;
    return Polygon().Num() >= 3;
}
FString CireTownTrim::Signature()
{
    if (!Active()) return FString();
    const FCireTrimSettings& S = Settings();
    FString Out = FString::Printf(TEXT("trim1 margin=%.0f backdrop=%d cell=%.0f poly="), S.Margin, int32(S.Backdrop), S.NavCell);
    for (const FVector2D& P : Polygon()) Out += FString::Printf(TEXT("%.0f,%.0f;"), P.X, P.Y);
    return Out;
}

// ================================================================================================= load and trim
namespace
{
struct FTrimStats
{
    int32 SublevelsSkipped = 0, LevelInstancesSkipped = 0, LevelInstancesBackdrop = 0, LevelInstancesUnknown = 0, LevelInstancesKept = 0;
    int32 Inside = 0, Straddling = 0, Destroyed = 0, Hidden = 0, Backdrop = 0, LandscapeKept = 0;
    double Ms = 0;
};
FTrimStats GStats[2];
TSet<TWeakObjectPtr<ULevel>> TrimmedLevels;
TSet<TWeakObjectPtr<AActor>> DecidedInstances;
bool bWarnedStale = false;

enum class EFate : uint8 { Keep, Destroy, Backdrop };

bool IsInfoActor(const AActor* A)
{
    return A->IsA<AWorldSettings>() || A->IsA<AInfo>() || Cast<ILevelInstanceInterface>(A) != nullptr;
}
bool IsLandscape(const AActor* A) { return A->GetClass()->GetName().Contains(TEXT("Landscape")); }
bool IsLandscapeRoot(const AActor* A) { const FString C = A->GetClass()->GetName(); return C == TEXT("Landscape"); }
bool Renders(const AActor* A)
{
    // Only actors that draw, light, simulate or tick cost anything; the rest (info, markers) is left alone.
    if (A->PrimaryActorTick.bCanEverTick) return true;
    TInlineComponentArray<UActorComponent*> Components(A);
    for (const UActorComponent* C : Components) if (C->IsA<UPrimitiveComponent>() || C->IsA<ULightComponentBase>() || C->IsA<UFXSystemComponent>() || C->IsA<UAudioComponent>()) return true;
    return false;
}
FBox ActorBox(const AActor* A)
{
    FBox Box = A->GetComponentsBoundingBox(true);
    if (!Box.IsValid || Box.GetExtent().GetMax() > 1e7) Box = FBox(A->GetActorLocation() - FVector(50), A->GetActorLocation() + FVector(50));
    return Box;
}
bool NameListed(const FString& Name, const TArray<FString>& List)
{
    for (const FString& Word : List) if (Name.Contains(Word)) return true;
    return false;
}
double LocalDistance(const TArray<FVector2D>& Poly, const FBox& Box)
{
    const int32 Realm = CireTownMap::RealmAt(Box.GetCenter());
    const FVector2D C = FVector2D(Box.GetCenter()) - CireLanePath::RealmOrigin(Realm);
    return FMath::Max(0.0, CireTownTrim::EdgeDistance(Poly, C) - FVector2D(Box.GetExtent()).Size());
}
/** What happens to an outside actor. bRender false (dedicated server): no backdrop. */
EFate OutsideFate(const AActor* A, const FBox& Box, const TArray<FVector2D>& Poly, bool bRender)
{
    const FCireTrimSettings& S = CireTownTrim::Settings();
    if (IsLandscape(A))
    {
        if (IsLandscapeRoot(A)) return EFate::Keep; // the root owns the streaming proxies
        return (S.Backdrop == ECireTrimBackdrop::Off && !S.bKeepLandscape) ? EFate::Destroy : EFate::Keep;
    }
    if (!bRender || S.Backdrop == ECireTrimBackdrop::Off) return EFate::Destroy;
    if (S.BackdropRange > 0 && LocalDistance(Poly, Box) > S.BackdropRange) return EFate::Destroy;
    if (S.Backdrop == ECireTrimBackdrop::Full) return EFate::Backdrop;
    const FVector Size = Box.GetSize();
    return (Size.Z >= S.LandmarkHeight || FMath::Max(Size.X, Size.Y) >= S.LandmarkSize) ? EFate::Backdrop : EFate::Destroy;
}
void MakeBackdrop(AActor* A)
{
    // A silhouette only: it draws, nothing else. No collision, no shadows, the lowest LOD, no tick, no lights, particles
    // or sounds.
    A->SetActorTickEnabled(false);
    A->SetActorEnableCollision(false);
    TInlineComponentArray<UActorComponent*> Components(A);
    for (UActorComponent* C : Components)
    {
        C->SetComponentTickEnabled(false);
        if (auto* Prim = Cast<UPrimitiveComponent>(C))
        {
            Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Prim->SetCanEverAffectNavigation(false);
            Prim->SetCastShadow(false);
            Prim->bAffectDistanceFieldLighting = false;
            if (auto* SM = Cast<UStaticMeshComponent>(Prim); SM && SM->GetStaticMesh() && SM->GetStaticMesh()->GetNumLODs() > 1)
                SM->SetForcedLodModel(SM->GetStaticMesh()->GetNumLODs());
            if (auto* Fx = Cast<UFXSystemComponent>(Prim)) { Fx->Deactivate(); Fx->SetVisibility(false); }
        }
        else if (auto* Light = Cast<ULightComponentBase>(C)) Light->SetVisibility(false);
        else if (auto* Audio = Cast<UAudioComponent>(C)) { Audio->Stop(); Audio->SetAutoActivate(false); }
    }
}
void Remove(AActor* A, FTrimStats& S)
{
    if (A->Destroy()) { ++S.Destroyed; return; }
    // A replicated startup actor on a client: the server's destroy reaches it; meanwhile it is hidden and inert.
    A->SetActorHiddenInGame(true); A->SetActorEnableCollision(false); A->SetActorTickEnabled(false); ++S.Hidden;
}
/** Level Instance decision: false = do not load it. */
bool WantLevelInstance(UWorld* World, ILevelInstanceInterface* LI, const TArray<FVector2D>& Poly, bool bRender, bool& bOutBackdrop, bool& bOutUnknown)
{
    bOutBackdrop = bOutUnknown = false;
    AActor* Actor = CastChecked<AActor>(LI);
    FBox Bounds(ForceInit);
#if WITH_EDITOR
    if (auto* Sub = World->GetSubsystem<ULevelInstanceSubsystem>()) Sub->GetLevelInstanceBounds(LI, Bounds);
#endif
    if (!Bounds.IsValid || Bounds.GetExtent().GetMax() > 1e7) { bOutUnknown = true; return true; } // unknown: load, trim its actors after
    if (CireTownTrim::ClassifyWorld(Poly, Bounds, CireTownTrim::Settings().Margin) != ECireTrimClass::Outside) return true;
    const FCireTrimSettings& S = CireTownTrim::Settings();
    if (bRender && S.Backdrop != ECireTrimBackdrop::Off && NameListed(FPackageName::GetShortName(LI->GetWorldAssetPackage()), S.BackdropLevelInstances)
        && (S.BackdropRange <= 0 || LocalDistance(Poly, Bounds) <= S.BackdropRange))
    { bOutBackdrop = true; return true; }
    return false;
}
void FilterLevel(UWorld* World, ULevel* Level)
{
    auto* Sub = World->GetSubsystem<ULevelInstanceSubsystem>();
    if (!Sub) return;
    const TArray<FVector2D>& Poly = CireTownTrim::Polygon();
    const bool bRender = World->GetNetMode() != NM_DedicatedServer;
    for (AActor* A : Level->Actors)
    {
        auto* LI = Cast<ILevelInstanceInterface>(A);
        if (!LI || DecidedInstances.Contains(A)) continue;
        DecidedInstances.Add(A);
        FTrimStats& S = GStats[CireTownMap::RealmAt(A->GetActorLocation())];
        bool bBackdrop = false, bUnknown = false;
        if (WantLevelInstance(World, LI, Poly, bRender, bBackdrop, bUnknown))
        {
            ++(bBackdrop ? S.LevelInstancesBackdrop : S.LevelInstancesKept); S.LevelInstancesUnknown += bUnknown ? 1 : 0;
            continue;
        }
        if (!Sub->IsLoaded(LI)) { Sub->RequestUnloadLevelInstance(LI); ++S.LevelInstancesSkipped; } // drops the queued load
    }
}
}

void CireTownTrim::BeginLoad(UWorld* World)
{
    GFrozen = LivePolygon(&GFrozenSource); bFrozen = true;
    for (FTrimStats& S : GStats) S = FTrimStats();
    TrimmedLevels.Reset(); DecidedInstances.Reset(); bWarnedStale = false;
    if (Active())
    {
        UE_LOG(LogCireTownTrim, Display, TEXT("CIRE_TOWN_TRIM_BEGIN corners=%d margin=%.0f backdrop=%s source=%s"), GFrozen.Num(), Settings().Margin,
            Settings().Backdrop == ECireTrimBackdrop::Off ? TEXT("off") : Settings().Backdrop == ECireTrimBackdrop::Full ? TEXT("full") : TEXT("low"), *GFrozenSource);
    }
    else
    {
        UE_LOG(LogCireTownTrim, Display, TEXT("CIRE_TOWN_TRIM_OFF corners=%d (no Play Bounds, the layout editor, explore mode or -CireNoTownTrim): the whole town loads"), GFrozen.Num());
    }
}
bool CireTownTrim::ShouldLoadSublevel(const FString& Package, const FVector& RealmOffset)
{
    if (!Active()) return true;
    const FCireTrimSettings& S = Settings();
    FBox Bounds(ForceInit);
    if (!ULevel::GetLevelBoundsFromPackage(FName(*Package), Bounds) || !Bounds.IsValid) return true; // unknown: load, trim after
    const FBox World = Bounds.ShiftBy(RealmOffset);
    if (ClassifyWorld(Polygon(), World, S.Margin) != ECireTrimClass::Outside) return true;
    if (S.Backdrop != ECireTrimBackdrop::Off && NameListed(FPackageName::GetShortName(Package), S.BackdropLevels)) return true;
    ++GStats[CireTownMap::RealmAt(World.GetCenter())].SublevelsSkipped;
    UE_LOG(LogCireTownTrim, Display, TEXT("CIRE_TOWN_TRIM_SKIP_SUBLEVEL %s offset=%s"), *Package, *RealmOffset.ToCompactString());
    return false;
}
int32 CireTownTrim::FilterLevelInstances(UWorld* World)
{
    if (!World || !Active()) return 0;
    const int32 Before = GStats[0].LevelInstancesSkipped + GStats[1].LevelInstancesSkipped;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level != World->PersistentLevel && Level->bIsVisible) FilterLevel(World, Level);
    return GStats[0].LevelInstancesSkipped + GStats[1].LevelInstancesSkipped - Before;
}
void CireTownTrim::TrimLevel(UWorld* World, ULevel* Level)
{
    if (!World || !Level || Level == World->PersistentLevel || !Active() || TrimmedLevels.Contains(Level)) return;
    TrimmedLevels.Add(Level);
    const double Started = FPlatformTime::Seconds();
    FilterLevel(World, Level); // nested Level Instances of this level, before the subsystem streams them
    const TArray<FVector2D>& Poly = Polygon();
    const float Margin = Settings().Margin;
    const bool bRender = World->GetNetMode() != NM_DedicatedServer;
    TArray<AActor*> Doomed, Backdrop;
    int32 Realm = 0;
    for (AActor* A : Level->Actors)
    {
        if (!A || A->IsActorBeingDestroyed() || IsInfoActor(A) || !Renders(A)) continue;
        const FBox Box = ActorBox(A);
        Realm = CireTownMap::RealmAt(Box.GetCenter());
        FTrimStats& S = GStats[Realm];
        switch (ClassifyWorld(Poly, Box, Margin))
        {
        case ECireTrimClass::Inside: ++S.Inside; break;
        case ECireTrimClass::Straddling: ++S.Straddling; break;
        case ECireTrimClass::Outside:
            switch (OutsideFate(A, Box, Poly, bRender))
            {
            case EFate::Destroy: Doomed.Add(A); break;
            case EFate::Backdrop: Backdrop.Add(A); break;
            default: ++S.LandscapeKept; break;
            }
        }
    }
    for (AActor* A : Backdrop) { MakeBackdrop(A); ++GStats[CireTownMap::RealmAt(A->GetActorLocation())].Backdrop; }
    // Attached children first would be destroyed with their parent: destroy from the leaves up.
    for (int32 I = Doomed.Num() - 1; I >= 0; --I) if (IsValid(Doomed[I])) Remove(Doomed[I], GStats[CireTownMap::RealmAt(Doomed[I]->GetActorLocation())]);
    GStats[Realm].Ms += (FPlatformTime::Seconds() - Started) * 1000.0;
    for (auto It = TrimmedLevels.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
}
void CireTownTrim::LogSummary(UWorld* World)
{
    if (!Active()) return;
    for (int32 T = 0; T < 2; ++T)
    {
        const FTrimStats& S = GStats[T];
        UE_LOG(LogCireTownTrim, Display, TEXT("CIRE_TOWN_TRIM realm=%d sublevels_skipped=%d li_skipped=%d li_kept=%d li_backdrop=%d li_bounds_unknown=%d actors_inside=%d straddling=%d destroyed=%d hidden=%d backdrop=%d landscape_kept=%d ms=%.0f"),
            T, S.SublevelsSkipped, S.LevelInstancesSkipped, S.LevelInstancesKept, S.LevelInstancesBackdrop, S.LevelInstancesUnknown, S.Inside, S.Straddling, S.Destroyed, S.Hidden, S.Backdrop, S.LandscapeKept, S.Ms);
    }
}
void CireTownTrim::Tick(UWorld* World)
{
    if (bWarnedStale || !World || !bFrozen || !CireTownMap::IsActive() || CireRouteEditMode::IsActive()) return;
    if (FParse::Param(FCommandLine::Get(), TEXT("CireTownTrimBounds"))) return;
    const TArray<FVector2D>& Now = CireLanePath::Get(World).PlayBounds;
    if (!Active() || Now.Num() < 3 || Now == GFrozen) return;
    bWarnedStale = true;
    UE_LOG(LogCireTownTrim, Warning, TEXT("CIRE_TOWN_TRIM_STALE the match runs other Play Bounds (%d corners) than the town was trimmed to (%d); restart the game to re-trim"), Now.Num(), GFrozen.Num());
}

// ================================================================================================= navmesh
bool CireTownTrim::NavBoxes(int32 Team, const FBox& RealmBox, TArray<FBox>& Out)
{
    Out.Reset();
    if (!Active()) return false;
    const FVector2D O = CireLanePath::RealmOrigin(Team);
    // Clipped to the realm's own nav box: the trim only ever removes navmesh, it never adds any.
    const FBox2D Clip(FVector2D(RealmBox.Min) - O, FVector2D(RealmBox.Max) - O);
    for (const FBox2D& B : RasterBoxes(Polygon(), Settings().Margin, Settings().NavCell, Clip))
        Out.Add(FBox(FVector(B.Min + O, RealmBox.Min.Z), FVector(B.Max + O, RealmBox.Max.Z)));
    return Out.Num() > 0;
}

// ================================================================================================= editor: overlay and preview
namespace
{
struct FPreview
{
    bool bOn = false;
    TWeakObjectPtr<UWorld> World;
    TArray<FVector2D> Poly;
    TArray<TWeakObjectPtr<AActor>> Hidden;
    double ChangedAt = 0;
    int32 Count = 0;
};
FPreview GPreview;

void Unhide()
{
    for (const auto& A : GPreview.Hidden) if (A.IsValid()) A->SetActorHiddenInGame(false);
    GPreview.Hidden.Reset(); GPreview.Count = 0;
}
bool InSkippedInstance(UWorld* World, AActor* A, const TArray<FVector2D>& Poly)
{
    auto* Sub = World->GetSubsystem<ULevelInstanceSubsystem>();
    if (!Sub) return false;
    bool bSkipped = false;
    Sub->ForEachLevelInstanceAncestors(A, [&](ILevelInstanceInterface* LI)
    {
        bool bBackdrop = false, bUnknown = false;
        if (!WantLevelInstance(World, LI, Poly, true, bBackdrop, bUnknown)) { bSkipped = true; return false; }
        return true;
    });
    return bSkipped;
}
void ApplyPreview(UWorld* World)
{
    // What a match would cut: hidden (never destroyed), so the editor keeps the whole town.
    Unhide();
    const TArray<FVector2D>& Poly = GPreview.Poly;
    if (!World || Poly.Num() < 3) return;
    const double Started = FPlatformTime::Seconds();
    const float Margin = CireTownTrim::Settings().Margin;
    TMap<AActor*, bool> InstanceCache;
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level || Level == World->PersistentLevel || !Level->bIsVisible) continue;
        for (AActor* A : Level->Actors)
        {
            if (!A || A->IsHidden() || IsInfoActor(A) || !Renders(A)) continue;
            const FBox Box = ActorBox(A);
            bool bCut = false;
            if (CireTownTrim::ClassifyWorld(Poly, Box, Margin) == ECireTrimClass::Outside) bCut = OutsideFate(A, Box, Poly, true) == EFate::Destroy;
            if (!bCut) bCut = InSkippedInstance(World, A, Poly);
            if (!bCut) continue;
            A->SetActorHiddenInGame(true);
            GPreview.Hidden.Add(A);
        }
    }
    GPreview.Count = GPreview.Hidden.Num();
    UE_LOG(LogCireTownTrim, Display, TEXT("CIRE_TOWN_TRIM_PREVIEW hidden=%d corners=%d ms=%.0f"), GPreview.Count, Poly.Num(), (FPlatformTime::Seconds() - Started) * 1000.0);
}
void DrawOverlay(UWorld* World, UCanvas* Canvas, const TArray<FVector2D>& Poly, int32 Realm)
{
    // A red veil over everything the trim cuts, amber over the margin band that still loads.
    if (!Canvas || !Canvas->SceneView || Poly.Num() < 3) return;
    const float Margin = CireTownTrim::Settings().Margin;
    FBox2D Area(ForceInit); for (const FVector2D& P : Poly) Area += P;
    Area = Area.ExpandBy(Margin + 12000.f);
    const FVector ViewAt = Canvas->SceneView->ViewMatrices.GetViewOrigin();
    const FVector2D ViewLocal = FVector2D(ViewAt) - CireLanePath::RealmOrigin(Realm);
    const double Cell = FMath::Clamp((ViewAt.Z - CireTownMap::Ground(World, FVector2D(ViewAt))) * .04, 600.0, 1500.0);
    const double Reach = FMath::Clamp((ViewAt.Z - CireTownMap::Ground(World, FVector2D(ViewAt))) * 6.0, 18000.0, 45000.0);
    FBox2D View(ViewLocal - FVector2D(Reach), ViewLocal + FVector2D(Reach));
    Area.Min = FVector2D::Max(Area.Min, View.Min); Area.Max = FVector2D::Min(Area.Max, View.Max);
    if (Area.Min.X >= Area.Max.X || Area.Min.Y >= Area.Max.Y) return;
    const int32 NX = FMath::Min(90, FMath::CeilToInt((Area.Max.X - Area.Min.X) / Cell)), NY = FMath::Min(90, FMath::CeilToInt((Area.Max.Y - Area.Min.Y) / Cell));
    TArray<FCanvasUVTri> Red, Amber;
    auto GroundAt = [&](double X, double Y) { return CireTownMap::Ground(World, FVector2D(CireLanePath::ToWorld(Realm, FVector2D(X, Y), 0.f))); };
    auto Corner = [&](double X, double Y, FVector2D& Screen, double FlatZ = TNumericLimits<double>::Lowest())
    {
        const FVector2D Local(X, Y);
        FVector W = CireLanePath::ToWorld(Realm, Local, 0.f);
        W.Z = (FlatZ > TNumericLimits<double>::Lowest() ? FlatZ : CireTownMap::Ground(World, FVector2D(W))) + 40.f;
        const FVector S = Canvas->Project(W);
        Screen = FVector2D(S.X, S.Y);
        return S.Z > 0;
    };
    for (int32 Y = 0; Y < NY; ++Y)
        for (int32 X = 0; X < NX; ++X)
        {
            const double X0 = Area.Min.X + X * Cell, Y0 = Area.Min.Y + Y * Cell;
            const FVector2D Mid(X0 + Cell * .5, Y0 + Cell * .5);
            if (CireTownTrim::InsidePolygon(Poly, Mid)) continue;
            const bool bBand = CireTownTrim::EdgeDistance(Poly, Mid) <= Margin;
            FVector2D A, B, C, D;
            // Flat at the cell's lowest corner: a veil over the ground, never spiking up to a roof or a tower top.
            const double Z = FMath::Min(FMath::Min(GroundAt(X0, Y0), GroundAt(X0 + Cell, Y0)), FMath::Min(GroundAt(X0 + Cell, Y0 + Cell), GroundAt(X0, Y0 + Cell)));
            if (!Corner(X0, Y0, A, Z) || !Corner(X0 + Cell, Y0, B, Z) || !Corner(X0 + Cell, Y0 + Cell, C, Z) || !Corner(X0, Y0 + Cell, D, Z)) continue;
            // A cell grazing the near plane projects into a huge sliver: skip it (its neighbours cover the ground).
            const double Limit = Canvas->ClipX * .2;
            if (FMath::Max(FMath::Max(FVector2D::Distance(A, B), FVector2D::Distance(B, C)), FMath::Max(FVector2D::Distance(C, D), FVector2D::Distance(D, A))) > Limit
                || FMath::Max(FVector2D::Distance(A, C), FVector2D::Distance(B, D)) > Limit) continue;
            const FLinearColor Color = bBand ? FLinearColor(1.f, .62f, .1f, .16f) : FLinearColor(.9f, .08f, .06f, .30f);
            TArray<FCanvasUVTri>& List = bBand ? Amber : Red;
            FCanvasUVTri T1; T1.V0_Pos = A; T1.V1_Pos = B; T1.V2_Pos = C; T1.V0_Color = T1.V1_Color = T1.V2_Color = Color;
            FCanvasUVTri T2; T2.V0_Pos = A; T2.V1_Pos = C; T2.V2_Pos = D; T2.V0_Color = T2.V1_Color = T2.V2_Color = Color;
            List.Add(T1); List.Add(T2);
        }
    for (TArray<FCanvasUVTri>* List : {&Red, &Amber})
    {
        if (List->IsEmpty()) continue;
        FCanvasTriangleItem Tris(*List, GWhiteTexture);
        Tris.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Tris);
    }
    // The border itself, in red.
    for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++)
    {
        const int32 Steps = FMath::Clamp(FMath::CeilToInt(FVector2D::Distance(Poly[J], Poly[I]) / 800.0), 1, 64);
        FVector2D Prev; bool bPrev = false;
        for (int32 K = 0; K <= Steps; ++K)
        {
            const FVector2D P = FMath::Lerp(Poly[J], Poly[I], double(K) / Steps);
            FVector2D S; const bool bOk = Corner(P.X, P.Y, S);
            if (bOk && bPrev && FVector2D::Distance(Prev, S) < Canvas->ClipX * .5) { FCanvasLineItem L(Prev, S); L.SetColor(FLinearColor(1.f, .15f, .1f, .95f)); L.LineThickness = 3.f; Canvas->DrawItem(L); }
            Prev = S; bPrev = bOk;
        }
    }
}
}

TArray<FVector2D> CireTownTrim::LayoutPolygon(const FCireMapLayout& Layout)
{
    for (const FCireMapMarker& M : Layout.Markers) if (M.Type == CireMapLayout::PlayBounds && M.Points.Num() >= 3) return M.Points;
    return TArray<FVector2D>();
}
bool CireTownTrim::IsPreviewing() { return GPreview.bOn; }
void CireTownTrim::StopPreview()
{
    Unhide(); GPreview.bOn = false; GPreview.Poly.Reset();
}
bool CireTownTrim::TogglePreview(UWorld* World, const FCireMapLayout& Layout)
{
    if (GPreview.bOn) { StopPreview(); return false; }
    GPreview.bOn = true; GPreview.World = World; GPreview.Poly = LayoutPolygon(Layout);
    ApplyPreview(World);
    return true;
}
void CireTownTrim::TickEditor(UWorld* World, UCanvas* Canvas, const FCireMapLayout& Layout, int32 Realm)
{
    if (!World) return;
    const TArray<FVector2D> Poly = LayoutPolygon(Layout);
    if (GPreview.bOn)
    {
        if (GPreview.World.Get() != World) { GPreview.Hidden.Reset(); GPreview.World = World; }
        // The draft's bounds changed (a corner moved, added or removed): re-apply once it settles.
        if (Poly != GPreview.Poly) { GPreview.Poly = Poly; GPreview.ChangedAt = World->GetRealTimeSeconds(); }
        if (GPreview.ChangedAt > 0 && World->GetRealTimeSeconds() - GPreview.ChangedAt > .5) { GPreview.ChangedAt = 0; ApplyPreview(World); }
    }
    DrawOverlay(World, Canvas, Poly, Realm);
}
TArray<FCireLayoutIssue> CireTownTrim::EdgeIssues(const FCireMapLayout& L)
{
    TArray<FCireLayoutIssue> Out;
    const TArray<FVector2D> Poly = LayoutPolygon(L);
    if (Poly.Num() < 3) return Out;
    const float Margin = Settings().Margin;
    for (const FCireMapMarker& M : L.Markers)
    {
        if (M.Type == CireMapLayout::PlayBounds || M.Type == CireMapLayout::Blocker) continue;
        TArray<FVector2D> Spots = M.Points.Num() > 0 ? M.Points : TArray<FVector2D>{M.Position};
        if (M.Type == CireMapLayout::Vendor) { Spots.Add(M.SignPos); Spots.Add(M.StallPos); }
        double Nearest = TNumericLimits<double>::Max();
        for (const FVector2D& P : Spots)
            if (InsidePolygon(Poly, P)) Nearest = FMath::Min(Nearest, EdgeDistance(Poly, P) - FMath::Max(0.f, M.Radius));
        if (Nearest < Margin)
            Out.Add({false, M.Owner == ECireMarkerOwner::Team1 ? 1 : M.Owner == ECireMarkerOwner::Team2 ? 2 : 0, M.Id,
                FString::Printf(TEXT("%s is %.0f m from the play bounds edge: the town is trimmed %.0f m past the border, so its surroundings may look cut off"),
                    *CireMapLayout::DisplayLabel(L, M), FMath::Max(0.0, Nearest) / 100.0, Margin / 100.0)});
    }
    return Out;
}

// ================================================================================================= console
namespace
{
FAutoConsoleCommandWithWorldAndArgs GTrimCommand(TEXT("cire.TownTrim"),
    TEXT("cire.TownTrim: the town trim status. cire.TownTrim preview: toggle PREVIEW TRIM with the active layout's Play Bounds (hide/show)."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (Args.Num() > 0 && Args[0] == TEXT("preview"))
        {
            if (GPreview.bOn) { CireTownTrim::StopPreview(); return; }
            GPreview.bOn = true; GPreview.World = World; GPreview.Poly = LivePolygon();
            ApplyPreview(World);
            return;
        }
        UE_LOG(LogCireTownTrim, Display, TEXT("CIRE_TOWN_TRIM_STATUS active=%d corners=%d signature=%s"), CireTownTrim::Active() ? 1 : 0, CireTownTrim::Polygon().Num(), *CireTownTrim::Signature());
        CireTownTrim::LogSummary(World);
    }));
}
