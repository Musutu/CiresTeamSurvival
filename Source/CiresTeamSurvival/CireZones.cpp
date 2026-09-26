// tier-readability: pack tier presentation and named town zones (CireZones.h, Docs/Zones.md).
#include "CireZones.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireMapLayout.h"
#include "CireEnvironmentProps.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireZones, Log, All);

// ============================================================================================ tier presentation
int32 CireZones::TierOf(const ACireMonster* M)
{
    return M && M->PackId >= 0 && M->Tier > 0 ? FMath::Clamp(M->Tier, 1, 4) : 0;
}
FString CireZones::TierTag(int32 Tier) { return Tier > 0 ? FString::Printf(TEXT("T%d"), FMath::Clamp(Tier, 1, 4)) : FString(); }
FLinearColor CireZones::TierColor(int32 Tier)
{
    switch (FMath::Clamp(Tier, 0, 4))
    {
    case 1: return FLinearColor(.80f, .82f, .86f, 1.f);  // silver (WoW common / rare-elite frame)
    case 2: return FLinearColor(.18f, .95f, .20f, 1.f);  // uncommon green
    case 3: return FLinearColor(.25f, .58f, 1.f, 1.f);   // rare blue
    case 4: return FLinearColor(1.f, .76f, .10f, 1.f);   // elite gold
    default: return FLinearColor(.82f, .8f, .74f, 1.f);
    }
}
FString CireZones::NameplateLabel(const FString& Name, int32 Tier)
{
    return Tier > 0 ? FString::Printf(TEXT("%s  %s"), *Name, *TierTag(Tier)) : Name;
}
FString CireZones::FrameHeader(int32 Tier, bool bLeader, const FString& RoleName)
{
    const FString Head = TierTag(Tier) + (bLeader ? TEXT(" PACK LEADER") : TEXT(" PACK"));
    return RoleName.IsEmpty() ? Head : Head + TEXT("  /  ") + RoleName.ToUpper();
}

// ============================================================================================ zones
bool CireZones::InsidePolygon(const TArray<FVector2D>& Poly, const FVector2D& P)
{
    if (Poly.Num() < 3) return false;
    bool bIn = false;
    for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++)
        if ((Poly[I].Y > P.Y) != (Poly[J].Y > P.Y) && P.X < (Poly[J].X - Poly[I].X) * (P.Y - Poly[I].Y) / (Poly[J].Y - Poly[I].Y) + Poly[I].X) bIn = !bIn;
    return bIn;
}
int32 CireZones::DominantTier(const TArray<int32>& Tiers)
{
    int32 Counts[5] = {};
    for (const int32 T : Tiers) if (T >= 1) ++Counts[FMath::Min(T, 4)];
    int32 Best = 0;
    for (int32 T = 1; T <= 4; ++T) if (Counts[T] > 0 && Counts[T] >= Counts[Best]) Best = T; // >=: ties go to the higher tier
    return Best;
}
TArray<int32> CireZones::TierCounts(const FCireZone& Zone, const TArray<FCireChallengeBay>& Packs)
{
    TArray<int32> Counts; Counts.Init(0, 5);
    for (const FCireChallengeBay& B : Packs) if (InsidePolygon(Zone.Polygon, B.Position)) ++Counts[FMath::Clamp(B.Tier, 1, 4)];
    return Counts;
}
int32 CireZones::ZoneTier(const FCireZone& Zone, const TArray<FCireChallengeBay>& Packs)
{
    TArray<int32> Tiers;
    for (const FCireChallengeBay& B : Packs) if (InsidePolygon(Zone.Polygon, B.Position)) Tiers.Add(FMath::Clamp(B.Tier, 1, 4));
    return DominantTier(Tiers);
}
FString CireZones::TierLine(int32 Tier) { return Tier > 0 ? FString::Printf(TEXT("Monster Tier: %d"), FMath::Clamp(Tier, 1, 4)) : FString(TEXT("No monster camps")); }
int32 CireZones::IndexAt(const TArray<FCireZone>& Zones, const FVector2D& Local, int32 Realm)
{
    for (int32 I = 0; I < Zones.Num(); ++I)
        if ((Zones[I].Realm < 0 || Zones[I].Realm == Realm) && InsidePolygon(Zones[I].Polygon, Local)) return I;
    return INDEX_NONE;
}

TArray<FCireZone> CireZones::FromLayout(const FCireMapLayout& L)
{
    TArray<FCireZone> Out;
    int32 Number = 0;
    for (const FCireMapMarker& M : L.Markers)
    {
        if (M.Type != CireMapLayout::Zone) continue;
        ++Number;
        if (M.Points.Num() < 3) continue;
        FCireZone& Z = Out.AddDefaulted_GetRef();
        Z.Id = M.Id; Z.Name = M.Name.IsEmpty() ? FString::Printf(TEXT("Zone %d"), Number) : M.Name; Z.Polygon = M.Points;
        Z.Realm = M.Owner == ECireMarkerOwner::Shared ? -1 : CireMapLayout::RealmOf(M.Owner);
    }
    return Out;
}
bool CireZones::ParseDefaults(const FString& Json, const FString& Map, TArray<FCireZone>& Out, FString& Error)
{
    Out.Reset();
    TSharedPtr<FJsonObject> Root; const TSharedPtr<FJsonObject>* Maps = nullptr;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root || !Root->TryGetObjectField(TEXT("maps"), Maps) || !Maps)
    { Error = TEXT("TownZones.json needs a \"maps\" object"); return false; }
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (!(*Maps)->TryGetArrayField(Map, List) || !List) { Error.Reset(); return true; } // no zones for this map
    for (const auto& Value : *List)
    {
        const TSharedPtr<FJsonObject> O = Value.IsValid() ? Value->AsObject() : nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
        FCireZone Z;
        if (!O || !O->TryGetStringField(TEXT("name"), Z.Name) || Z.Name.IsEmpty() || !O->TryGetArrayField(TEXT("points"), Points) || !Points || Points->Num() < 3 || Points->Num() > 64)
        { Error = TEXT("every zone needs a name and 3..64 points"); Out.Reset(); return false; }
        O->TryGetStringField(TEXT("id"), Z.Id);
        for (const auto& P : *Points)
        {
            const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
            if (!P.IsValid() || !P->TryGetArray(XY) || !XY || XY->Num() != 2) { Error = FString::Printf(TEXT("zone %s: points are [x, y] pairs"), *Z.Name); Out.Reset(); return false; }
            Z.Polygon.Add(FVector2D((*XY)[0]->AsNumber(), (*XY)[1]->AsNumber()));
        }
        if (Z.Id.IsEmpty()) Z.Id = Z.Name;
        Out.Add(MoveTemp(Z));
    }
    Error.Reset();
    return true;
}
FString CireZones::DefaultsPath() { return FPaths::ProjectContentDir() / TEXT("Data/TownZones.json"); }
TArray<FCireZone> CireZones::Defaults(const FString& Map)
{
    TArray<FCireZone> Out; FString Json, Error;
    if (FFileHelper::LoadFileToString(Json, *DefaultsPath()) && !ParseDefaults(Json, Map, Out, Error))
        UE_LOG(LogCireZones, Warning, TEXT("TownZones.json not used (%s)"), *Error);
    if (Out.Num() == 0 && Map == TEXT("procedural"))
        // The procedural town's districts are bands along X (realm-local X = world X there).
        for (const auto& D : CireEnvironmentProps::Districts())
        {
            FCireZone& Z = Out.AddDefaulted_GetRef();
            Z.Id = D.Id.ToString(); Z.Name = D.Name;
            Z.Polygon = {FVector2D(D.MinX, -100000.), FVector2D(D.MaxX, -100000.), FVector2D(D.MaxX, 100000.), FVector2D(D.MinX, 100000.)};
        }
    return Out;
}
const TArray<FCireZone>& CireZones::Active(const UWorld* World)
{
    static TArray<FCireZone> Cached;
    static uint32 CachedRevision = MAX_uint32;
    static FString CachedMap;
    const uint32 Revision = CireLanePath::Revision(World);
    const FString Map = CireMapLayout::ActiveMap();
    if (Revision == CachedRevision && Map == CachedMap) return Cached;
    CachedRevision = Revision; CachedMap = Map;
    FCireMapLayout Layout; FString Error;
    Cached.Reset();
    if (CireMapLayout::Load(Layout, CireMapLayout::ActivePath(), &Error) && CireMapLayout::MatchesActiveMap(Layout)) Cached = FromLayout(Layout);
    const TCHAR* Source = TEXT("MapLayout.json");
    if (Cached.Num() == 0) { Cached = Defaults(Map); Source = TEXT("TownZones.json / districts"); }
    UE_LOG(LogCireZones, Display, TEXT("CIRE_ZONES map=%s zones=%d source=%s"), *Map, Cached.Num(), Source);
    return Cached;
}
const FCireZone* CireZones::At(const UWorld* World, int32 Team, const FVector& Location)
{
    const int32 Realm = FMath::Clamp(Team, 0, 1);
    const TArray<FCireZone>& Zones = Active(World);
    const int32 Index = IndexAt(Zones, CireLanePath::ToLocal(Realm, Location), Realm);
    return Zones.IsValidIndex(Index) ? &Zones[Index] : nullptr;
}
int32 CireZones::LiveTier(const UWorld* World, int32 Team, const FCireZone& Zone)
{
    const int32 Realm = FMath::Clamp(Team, 0, 1);
    return ZoneTier(Zone, CireLanePath::Get(World).Bays[Realm]);
}

// ============================================================================================ tests
#if !UE_BUILD_SHIPPING
bool CireZones::RunTests(TArray<FString>& Failures)
{
    const int32 Before = Failures.Num();
    auto Check = [&](bool bOk, const FString& What) { if (!bOk) Failures.Add(TEXT("zones: ") + What); };
    // Tier presentation.
    Check(TierTag(1) == TEXT("T1") && TierTag(4) == TEXT("T4") && TierTag(0).IsEmpty() && TierTag(9) == TEXT("T4"), TEXT("tier tags T1..T4"));
    Check(NameplateLabel(TEXT("Grave Hound"), 3) == TEXT("Grave Hound  T3") && NameplateLabel(TEXT("Grave Hound"), 0) == TEXT("Grave Hound"), TEXT("nameplate shows T# next to the name"));
    Check(FrameHeader(2, false, TEXT("Tank")) == TEXT("T2 PACK  /  TANK") && FrameHeader(4, true, TEXT("Healer")) == TEXT("T4 PACK LEADER  /  HEALER"), TEXT("frame header"));
    bool bDistinct = true;
    for (int32 A = 1; A <= 4; ++A) for (int32 B = A + 1; B <= 4; ++B) bDistinct &= !TierColor(A).Equals(TierColor(B), .05f);
    Check(bDistinct && TierColor(2).G > TierColor(2).R && TierColor(3).B > TierColor(3).R && TierColor(4).R > TierColor(4).B, TEXT("tier colours: silver, green, blue, gold"));
    // Dominant tier: the most common, ties to the higher tier, none = 0.
    Check(DominantTier({}) == 0 && DominantTier({1, 1, 2}) == 1 && DominantTier({1, 2, 2, 4}) == 2 && DominantTier({1, 3}) == 3 && DominantTier({2, 2, 4, 4, 1}) == 4,
        TEXT("dominant tier (most common, ties to the higher)"));
    Check(TierLine(3) == TEXT("Monster Tier: 3") && TierLine(0) == TEXT("No monster camps"), TEXT("banner tier line"));
    // Zones and packs.
    FCireZone Square; Square.Name = TEXT("Square"); Square.Polygon = {FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 1000), FVector2D(0, 1000)};
    FCireZone Ell; Ell.Name = TEXT("Ell"); Ell.Realm = 1; Ell.Polygon = {FVector2D(1000, 0), FVector2D(3000, 0), FVector2D(3000, 2000), FVector2D(2000, 2000), FVector2D(2000, 1000), FVector2D(1000, 1000)};
    Check(InsidePolygon(Ell.Polygon, FVector2D(1500, 500)) && !InsidePolygon(Ell.Polygon, FVector2D(1500, 1500)) && InsidePolygon(Ell.Polygon, FVector2D(2500, 1500)), TEXT("concave polygon test"));
    TArray<FCireChallengeBay> Packs;
    auto Pack = [&](double X, double Y, int32 Tier) { FCireChallengeBay B; B.Position = FVector2D(X, Y); B.Tier = Tier; Packs.Add(B); };
    Pack(100, 100, 2); Pack(200, 900, 2); Pack(800, 800, 4); Pack(1500, 500, 3); Pack(2500, 1500, 3); Pack(2500, 500, 1); Pack(1500, 1500, 4); // last one lies outside both
    Check(ZoneTier(Square, Packs) == 2 && ZoneTier(Ell, Packs) == 3, TEXT("zone tier = the most common pack tier inside it"));
    const TArray<int32> Counts = TierCounts(Ell, Packs);
    Check(Counts.Num() == 5 && Counts[1] == 1 && Counts[3] == 2 && Counts[4] == 0, TEXT("per-tier pack counts of a zone"));
    FCireZone Empty; Empty.Polygon = {FVector2D(5000, 5000), FVector2D(6000, 5000), FVector2D(6000, 6000)};
    Check(ZoneTier(Empty, Packs) == 0, TEXT("a zone without packs has no tier"));
    const TArray<FCireZone> Zones = {Square, Ell};
    Check(IndexAt(Zones, FVector2D(500, 500), 0) == 0 && IndexAt(Zones, FVector2D(1500, 500), 1) == 1 && IndexAt(Zones, FVector2D(1500, 500), 0) == INDEX_NONE &&
        IndexAt(Zones, FVector2D(-10, 5), 0) == INDEX_NONE, TEXT("zone lookup respects the polygon and the realm"));
    // TownZones.json parsing.
    TArray<FCireZone> Parsed; FString Error;
    Check(ParseDefaults(TEXT("{\"maps\":{\"castletown\":[{\"id\":\"a\",\"name\":\"Alpha\",\"points\":[[0,0],[10,0],[0,10]]}]}}"), TEXT("castletown"), Parsed, Error) &&
        Parsed.Num() == 1 && Parsed[0].Name == TEXT("Alpha") && Parsed[0].Polygon.Num() == 3 && Parsed[0].Realm == -1, TEXT("TownZones.json parses (") + Error + TEXT(")"));
    Check(ParseDefaults(TEXT("{\"maps\":{}}"), TEXT("castletown"), Parsed, Error) && Parsed.Num() == 0, TEXT("a map without zones is fine"));
    Check(!ParseDefaults(TEXT("{\"maps\":{\"castletown\":[{\"name\":\"Bad\",\"points\":[[0,0],[1,1]]}]}}"), TEXT("castletown"), Parsed, Error), TEXT("a zone needs 3 points"));
    // The shipped defaults: every zone of the town parses, and the committed layout's packs give each zone a tier.
    FString Json;
    if (FFileHelper::LoadFileToString(Json, *DefaultsPath()))
    {
        Check(ParseDefaults(Json, TEXT("castletown"), Parsed, Error) && Parsed.Num() >= 4, TEXT("shipped TownZones.json has the town's zones (") + Error + TEXT(")"));
        TSet<FString> Names; for (const FCireZone& Z : Parsed) Names.Add(Z.Name);
        Check(Names.Num() == Parsed.Num(), TEXT("town zone names are unique"));
    }
    else Check(false, TEXT("Content/Data/TownZones.json is missing"));
    // Layout zone markers.
    FCireMapLayout L;
    FString Id = CireMapLayout::ChainPoint(L, CireMapLayout::Zone, FString(), FVector2D(0, 0), ECireMarkerOwner::Shared);
    Id = CireMapLayout::ChainPoint(L, CireMapLayout::Zone, Id, FVector2D(1000, 0), ECireMarkerOwner::Shared);
    Id = CireMapLayout::ChainPoint(L, CireMapLayout::Zone, Id, FVector2D(1000, 1000), ECireMarkerOwner::Shared);
    CireMapLayout::SetName(L, Id, TEXT("Test Yard"));
    const FString Open = CireMapLayout::ChainPoint(L, CireMapLayout::Zone, FString(), FVector2D(5000, 0), ECireMarkerOwner::Shared); // a second zone with 1 point
    const TArray<FCireZone> FromL = FromLayout(L);
    Check(!Open.IsEmpty() && Open != Id && FromL.Num() == 1 && FromL[0].Name == TEXT("Test Yard") && FromL[0].Polygon.Num() == 3 && FromL[0].Realm == -1,
        TEXT("layout Zone markers become zones (3+ points; each chain is its own zone)"));
    FCireMapLayout Back; FString Why;
    Check(CireMapLayout::ParseJson(CireMapLayout::ToJson(L), Back, Why) && FromLayout(Back).Num() == 1 && FromLayout(Back)[0].Name == TEXT("Test Yard"), TEXT("zone markers round-trip through MapLayout.json (") + Why + TEXT(")"));
    bool bFlagged = false;
    for (const FCireLayoutIssue& Issue : CireMapLayout::Validate(L)) bFlagged |= Issue.MarkerId == Open && Issue.bError;
    Check(bFlagged, TEXT("Validate flags a zone with fewer than 3 points"));
    return Failures.Num() == Before;
}
#endif
