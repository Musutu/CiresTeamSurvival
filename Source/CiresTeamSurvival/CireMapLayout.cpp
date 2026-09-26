// dev-route-tools: map layout data model (typed, team-owned, mirrored, realm-local markers). See CireMapLayout.h.
#include "CireMapLayout.h"
#include "CireLanePath.h"
#include "CireTownMap.h" // medieval-kingdom
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireMapLayout, Log, All);

const FName CireMapLayout::PlayerSpawn(TEXT("playerSpawn"));
const FName CireMapLayout::MonsterSpawn(TEXT("monsterSpawn"));
const FName CireMapLayout::MonsterPath(TEXT("monsterPath"));
const FName CireMapLayout::ChallengePack(TEXT("challengePack"));
const FName CireMapLayout::Vendor(TEXT("vendor"));
const FName CireMapLayout::Objective(TEXT("objective"));
const FName CireMapLayout::BossSpawn(TEXT("bossSpawn"));
const FName CireMapLayout::Rift(TEXT("rift"));
const FName CireMapLayout::Respawn(TEXT("respawn"));
const FName CireMapLayout::PlayBounds(TEXT("playBounds"));
const FName CireMapLayout::Blocker(TEXT("blocker"));

namespace
{
constexpr double MergeSnap = 250.;      // a path point this close to another path's point merges into it
constexpr double ObjectiveSlack = 100.; // a path end this far past the objective radius still reaches it
constexpr double SpawnLinkRange = 4000.; // a new path links to the nearest monster spawn within 40 m

ECireGizmo GizmoOf(const FString& S)
{
    return S == TEXT("ring") ? ECireGizmo::Ring : S == TEXT("path") ? ECireGizmo::Path : S == TEXT("polygon") ? ECireGizmo::Polygon :
        S == TEXT("zone") ? ECireGizmo::Zone : ECireGizmo::Pillar;
}
ECireMarkerOwner OwnerOf(int32 Value) { return Value == 1 ? ECireMarkerOwner::Team1 : Value == 2 ? ECireMarkerOwner::Team2 : ECireMarkerOwner::Shared; }
int32 OwnerValue(ECireMarkerOwner O) { return static_cast<int32>(O); }
bool IsTeam(ECireMarkerOwner O) { return O != ECireMarkerOwner::Shared; }
const FCireMarkerType& TypeOrDefault(FName Id)
{
    static const FCireMarkerType Fallback;
    const FCireMarkerType* T = CireMapLayout::FindType(Id);
    return T ? *T : Fallback;
}
bool SameXY(const FVector2D& A, const FVector2D& B) { return FVector2D::DistSquared(A, B) <= 1.; }
FString MapLink(const FCireMapLayout& L, const FString& LinkId)
{
    const FCireMapMarker* Linked = LinkId.IsEmpty() ? nullptr : CireMapLayout::Find(L, LinkId);
    return Linked && Linked->bMirror && !Linked->Pair.IsEmpty() ? Linked->Pair : FString();
}
FString NewId(FCireMapLayout& L, FName Type) { return FString::Printf(TEXT("%s_%d"), *Type.ToString(), L.NextId++); }
FVector2D MakeXY(const TArray<TSharedPtr<FJsonValue>>& XY) { return XY.Num() >= 2 ? FVector2D(XY[0]->AsNumber(), XY[1]->AsNumber()) : FVector2D::ZeroVector; }
/** Offset (X forward, Y right) turned by a facing. */
FVector2D Turn(const FVector2D& Offset, float YawDeg)
{
    const double R = FMath::DegreesToRadians(YawDeg), C = FMath::Cos(R), S = FMath::Sin(R);
    return FVector2D(Offset.X * C - Offset.Y * S, Offset.X * S + Offset.Y * C);
}
void ApplyVendorDefaults(FCireMapMarker& M)
{
    const FCireVendorType* VT = CireMapLayout::FindVendorType(M.Kind);
    const FCireVendorType Default;
    const FCireVendorType& T = VT ? *VT : Default;
    M.SignPos = M.Position + Turn(T.SignOffset, M.Yaw); M.SignYaw = M.Yaw; M.SignHeight = T.SignHeight;
    M.StallPos = M.Position + Turn(T.StallOffset, M.Yaw); M.StallYaw = M.Yaw; M.StallSize = T.StallSize;
}
bool SegmentsCross(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
    auto Cross = [](const FVector2D& O, const FVector2D& P, const FVector2D& Q) { return (P.X - O.X) * (Q.Y - O.Y) - (P.Y - O.Y) * (Q.X - O.X); };
    const double D1 = Cross(C, D, A), D2 = Cross(C, D, B), D3 = Cross(A, B, C), D4 = Cross(A, B, D);
    return ((D1 > 0) != (D2 > 0)) && ((D3 > 0) != (D4 > 0));
}
bool InsideConvex(const TArray<FVector2D>& Poly, const FVector2D& P)
{
    bool bPos = false, bNeg = false;
    for (int32 I = 0; I < Poly.Num(); ++I)
    {
        const FVector2D& A = Poly[I]; const FVector2D& B = Poly[(I + 1) % Poly.Num()];
        const double C = (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X);
        bPos |= C > 0; bNeg |= C < 0;
    }
    return !(bPos && bNeg);
}
}

// ================================================================================================= setter types
TArray<FCireMarkerType> CireMapLayout::BuiltInTypes()
{
    TArray<FCireMarkerType> T;
    auto Add = [&](FName Id, const TCHAR* Name, const TCHAR* Label, const TCHAR* Icon, FLinearColor Color, ECireGizmo Gizmo) -> FCireMarkerType&
    {
        FCireMarkerType& M = T.AddDefaulted_GetRef();
        M.Id = Id; M.Name = Name; M.Label = Label; M.Icon = Icon; M.Color = Color; M.Gizmo = Gizmo;
        return M;
    };
    { auto& M = Add(PlayerSpawn, TEXT("Player Spawn"), TEXT("Spawn"), TEXT("chieftain_banner"), FLinearColor(.35f, .75f, 1.f), ECireGizmo::Pillar); M.bFacing = true; M.bRadius = true; M.DefaultRadius = 120.f; M.MaxPerOwner = 10; }
    { auto& M = Add(MonsterSpawn, TEXT("Monster Spawn"), TEXT("Monsters"), TEXT("spectral_hunt"), FLinearColor(.86f, .32f, .92f), ECireGizmo::Pillar); M.bFacing = true; M.bNamed = true; M.bTarget = true; M.DefaultRadius = 200.f; }
    { auto& M = Add(MonsterPath, TEXT("Monster Path"), TEXT("Path"), TEXT("centaur_trailblaze"), FLinearColor(1.f, .62f, .18f), ECireGizmo::Path); M.bNamed = true; M.bTarget = true; M.bPoints = true; M.DefaultRadius = 0.f; }
    { auto& M = Add(ChallengePack, TEXT("Challenge Pack"), TEXT("Pack"), TEXT("challenge_of_iron"), FLinearColor(.66f, .46f, .83f), ECireGizmo::Ring); M.bRadius = true; M.bTier = true; M.DefaultRadius = 450.f; M.MaxPerOwner = 16; }
    { auto& M = Add(Vendor, TEXT("Shop / Vendor"), TEXT("Vendor"), TEXT("price_on_every_soul"), FLinearColor(1.f, .82f, .3f), ECireGizmo::Pillar); M.bFacing = true; M.bNamed = true; M.bKind = true; M.DefaultRadius = 120.f; M.Kinds = {TEXT("weaponsmith"), TEXT("armory"), TEXT("arcane")}; }
    { auto& M = Add(Objective, TEXT("Objective / Castle Defend Point"), TEXT("Objective"), TEXT("keeper_beacon"), FLinearColor(.2f, .86f, .7f), ECireGizmo::Ring); M.bRadius = true; M.DefaultRadius = 450.f; M.MaxPerOwner = 1; }
    { auto& M = Add(BossSpawn, TEXT("Boss / Pack Leader Spawn"), TEXT("Boss"), TEXT("chieftain_courage"), FLinearColor(.95f, .26f, .2f), ECireGizmo::Pillar); M.bFacing = true; M.bNamed = true; M.DefaultRadius = 200.f; }
    { auto& M = Add(Rift, TEXT("Rift / Portal / Arena Entrance"), TEXT("Rift"), TEXT("banishment"), FLinearColor(.56f, .36f, 1.f), ECireGizmo::Ring); M.bFacing = true; M.bNamed = true; M.bRadius = true; M.DefaultRadius = 250.f; }
    { auto& M = Add(Respawn, TEXT("Respawn Point / Graveyard"), TEXT("Respawn"), TEXT("keeper_last_light"), FLinearColor(.92f, .95f, .6f), ECireGizmo::Pillar); M.bFacing = true; M.DefaultRadius = 150.f; }
    { auto& M = Add(PlayBounds, TEXT("Play Bounds"), TEXT("Bounds"), TEXT("ashen_square"), FLinearColor(.9f, .9f, .92f), ECireGizmo::Polygon); M.bPoints = true; M.DefaultOwner = ECireMarkerOwner::Shared; M.MaxPerOwner = 1; M.DefaultRadius = 0.f; }
    { auto& M = Add(Blocker, TEXT("No-Spawn / Blocker Zone"), TEXT("Blocker"), TEXT("summoned_wall"), FLinearColor(.85f, .22f, .22f), ECireGizmo::Zone); M.bRadius = true; M.DefaultRadius = 400.f; }
    return T;
}

bool CireMapLayout::ParseTypes(const FString& Json, TArray<FCireMarkerType>& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root || !Root->TryGetArrayField(TEXT("types"), List) || !List)
    { Error = TEXT("MapMarkerTypes.json needs a \"types\" array"); return false; }
    TArray<FCireMarkerType> Types;
    for (const auto& Value : *List)
    {
        const TSharedPtr<FJsonObject> O = Value.IsValid() ? Value->AsObject() : nullptr;
        FString Id;
        if (!O || !O->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty()) { Error = TEXT("every marker type needs an id"); return false; }
        FCireMarkerType M; M.Id = FName(*Id);
        O->TryGetStringField(TEXT("name"), M.Name); O->TryGetStringField(TEXT("label"), M.Label); O->TryGetStringField(TEXT("icon"), M.Icon);
        FString Gizmo; if (O->TryGetStringField(TEXT("gizmo"), Gizmo)) M.Gizmo = GizmoOf(Gizmo);
        const TArray<TSharedPtr<FJsonValue>>* Color = nullptr;
        if (O->TryGetArrayField(TEXT("color"), Color) && Color && Color->Num() >= 3) M.Color = FLinearColor((*Color)[0]->AsNumber(), (*Color)[1]->AsNumber(), (*Color)[2]->AsNumber(), 1.f);
        O->TryGetBoolField(TEXT("facing"), M.bFacing); O->TryGetBoolField(TEXT("named"), M.bNamed); O->TryGetBoolField(TEXT("radius"), M.bRadius);
        O->TryGetBoolField(TEXT("tier"), M.bTier); O->TryGetBoolField(TEXT("target"), M.bTarget); O->TryGetBoolField(TEXT("points"), M.bPoints);
        double Number = 0;
        if (O->TryGetNumberField(TEXT("defaultRadius"), Number)) M.DefaultRadius = FMath::Clamp(static_cast<float>(Number), 0.f, 5000.f);
        if (O->TryGetNumberField(TEXT("maxPerOwner"), Number)) M.MaxPerOwner = FMath::Clamp(static_cast<int32>(Number), 0, 256);
        if (O->TryGetNumberField(TEXT("defaultTeam"), Number)) M.DefaultOwner = OwnerOf(static_cast<int32>(Number));
        const TArray<TSharedPtr<FJsonValue>>* Kinds = nullptr;
        if (O->TryGetArrayField(TEXT("kinds"), Kinds) && Kinds) for (const auto& K : *Kinds) M.Kinds.Add(K->AsString());
        M.bKind = M.Kinds.Num() > 0;
        if (M.Name.IsEmpty()) M.Name = Id;
        if (M.Label.IsEmpty()) M.Label = M.Name;
        for (const FCireMarkerType& Other : Types) if (Other.Id == M.Id) { Error = FString::Printf(TEXT("duplicate marker type %s"), *Id); return false; }
        Types.Add(MoveTemp(M));
    }
    if (Types.Num() == 0) { Error = TEXT("MapMarkerTypes.json lists no types"); return false; }
    Out = MoveTemp(Types); Error.Reset(); return true;
}

const TArray<FCireMarkerType>& CireMapLayout::Types()
{
    static TArray<FCireMarkerType> Table = []
    {
        TArray<FCireMarkerType> Loaded; FString Json, Error;
        const FString Path = FPaths::ProjectContentDir() / TEXT("Data/MapMarkerTypes.json");
        if (FFileHelper::LoadFileToString(Json, *Path) && ParseTypes(Json, Loaded, Error)) return Loaded;
        UE_LOG(LogCireMapLayout, Warning, TEXT("MapMarkerTypes.json not used (%s); built-in setter table"), Error.IsEmpty() ? TEXT("missing") : *Error);
        return BuiltInTypes();
    }();
    return Table;
}
const FCireMarkerType* CireMapLayout::FindType(FName Id)
{
    for (const FCireMarkerType& T : Types()) if (T.Id == Id) return &T;
    return nullptr;
}

// ================================================================================================= vendor types
bool CireMapLayout::ParseVendorTypes(const FString& Json, TArray<FCireVendorType>& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root || !Root->TryGetArrayField(TEXT("vendors"), List) || !List)
    { Error = TEXT("Vendors.json needs a vendors array"); return false; }
    TArray<FCireVendorType> Types;
    for (const auto& Value : *List)
    {
        const TSharedPtr<FJsonObject> O = Value.IsValid() ? Value->AsObject() : nullptr;
        FCireVendorType T;
        if (!O || !O->TryGetStringField(TEXT("id"), T.Id) || T.Id.IsEmpty()) continue;
        if (!O->TryGetStringField(TEXT("name"), T.Name)) T.Name = T.Id;
        const TSharedPtr<FJsonObject>* Part = nullptr; double V = 0;
        const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
        if (O->TryGetObjectField(TEXT("sign"), Part) && Part)
        {
            if ((*Part)->TryGetArrayField(TEXT("offset"), XY) && XY && XY->Num() >= 2) T.SignOffset = MakeXY(*XY);
            if ((*Part)->TryGetNumberField(TEXT("height"), V)) T.SignHeight = FMath::Clamp(static_cast<float>(V), 0.f, 1000.f);
        }
        if (O->TryGetObjectField(TEXT("stall"), Part) && Part)
        {
            if ((*Part)->TryGetArrayField(TEXT("offset"), XY) && XY && XY->Num() >= 2) T.StallOffset = MakeXY(*XY);
            if ((*Part)->TryGetArrayField(TEXT("size"), XY) && XY && XY->Num() >= 2) T.StallSize = MakeXY(*XY);
        }
        Types.Add(MoveTemp(T));
    }
    if (Types.Num() == 0) { Error = TEXT("Vendors.json lists no vendors"); return false; }
    Out = MoveTemp(Types); Error.Reset(); return true;
}
const TArray<FCireVendorType>& CireMapLayout::VendorTypes()
{
    static TArray<FCireVendorType> Table = []
    {
        TArray<FCireVendorType> Loaded; FString Json, Error;
        if (FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/Vendors.json"))) && ParseVendorTypes(Json, Loaded, Error)) return Loaded;
        // The three shops of the design rulings, split by primary stat.
        TArray<FCireVendorType> BuiltIn;
        auto Add = [&](const TCHAR* Id, const TCHAR* Name) { FCireVendorType T; T.Id = Id; T.Name = Name; BuiltIn.Add(T); };
        Add(TEXT("weaponsmith"), TEXT("Weaponsmith (AGI / DPS, melee bruisers)"));
        Add(TEXT("armory"), TEXT("Armory (STR, tanks)"));
        Add(TEXT("arcane"), TEXT("Arcane Shop (INT)"));
        return BuiltIn;
    }();
    return Table;
}
const FCireVendorType* CireMapLayout::FindVendorType(const FString& Id)
{
    for (const FCireVendorType& T : VendorTypes()) if (T.Id == Id) return &T;
    return nullptr;
}

// ================================================================================================= teams, realms
int32 CireMapLayout::RealmOf(ECireMarkerOwner Team) { return Team == ECireMarkerOwner::Team2 ? 1 : 0; }
ECireMarkerOwner CireMapLayout::TeamOfRealm(int32 Realm) { return Realm == 1 ? ECireMarkerOwner::Team2 : ECireMarkerOwner::Team1; }
ECireMarkerOwner CireMapLayout::OtherTeam(ECireMarkerOwner Team)
{
    return Team == ECireMarkerOwner::Team1 ? ECireMarkerOwner::Team2 : Team == ECireMarkerOwner::Team2 ? ECireMarkerOwner::Team1 : ECireMarkerOwner::Shared;
}
bool CireMapLayout::ShownInRealm(const FCireMapMarker& M, int32 Realm) { return M.Owner == ECireMarkerOwner::Shared || RealmOf(M.Owner) == Realm; }
FString CireMapLayout::TeamTag(ECireMarkerOwner Team) { return Team == ECireMarkerOwner::Team1 ? TEXT("T1") : Team == ECireMarkerOwner::Team2 ? TEXT("T2") : TEXT("Shared"); }
FString CireMapLayout::RealmName(int32 Realm) { return Realm == 1 ? TEXT("DARKNIGHT") : TEXT("DAYLIGHT"); }

// ================================================================================================= queries
FCireMapMarker* CireMapLayout::Find(FCireMapLayout& L, const FString& Id)
{
    for (FCireMapMarker& M : L.Markers) if (M.Id == Id) return &M;
    return nullptr;
}
const FCireMapMarker* CireMapLayout::Find(const FCireMapLayout& L, const FString& Id)
{
    for (const FCireMapMarker& M : L.Markers) if (M.Id == Id) return &M;
    return nullptr;
}
int32 CireMapLayout::IndexOf(const FCireMapLayout& L, const FString& Id)
{
    return L.Markers.IndexOfByPredicate([&](const FCireMapMarker& M) { return M.Id == Id; });
}
TArray<const FCireMapMarker*> CireMapLayout::OfType(const FCireMapLayout& L, FName Type, TOptional<ECireMarkerOwner> Owner)
{
    TArray<const FCireMapMarker*> Out;
    for (const FCireMapMarker& M : L.Markers)
        if ((Type.IsNone() || M.Type == Type) && (!Owner.IsSet() || M.Owner == Owner.GetValue())) Out.Add(&M);
    return Out;
}
int32 CireMapLayout::Number(const FCireMapLayout& L, const FString& Id)
{
    const FCireMapMarker* Target = Find(L, Id);
    if (!Target) return 0;
    int32 N = 0;
    for (const FCireMapMarker& M : L.Markers)
    {
        if (M.Type == Target->Type && M.Owner == Target->Owner) ++N;
        if (&M == Target) return N;
    }
    return 0;
}
FString CireMapLayout::DisplayLabel(const FCireMapLayout& L, const FCireMapMarker& M)
{
    const FCireMarkerType& T = TypeOrDefault(M.Type);
    const FString Team = TeamTag(M.Owner), Named = M.Name.IsEmpty() ? FString() : FString::Printf(TEXT("  %s"), *M.Name);
    if (M.Type == MonsterSpawn) return FString::Printf(TEXT("Monsters -> %s%s"), *TeamTag(M.Target), *Named);
    if (M.Type == MonsterPath) return FString::Printf(TEXT("Path -> %s%s"), *TeamTag(M.Target), *Named);
    if (M.Type == ChallengePack) return FString::Printf(TEXT("%s Pack %d  Tier %d"), *Team, Number(L, M.Id), M.Tier);
    if (M.Type == Vendor) return FString::Printf(TEXT("%s %s"), *Team, *(M.Name.IsEmpty() ? M.Kind.Left(1).ToUpper() + M.Kind.Mid(1) : M.Name));
    if (M.Type == Objective) return FString::Printf(TEXT("%s Objective"), *Team);
    if (M.Type == PlayBounds) return TEXT("Play Bounds");
    const FString Stem = M.Owner == ECireMarkerOwner::Shared ? T.Label : Team + TEXT(" ") + T.Label;
    return FString::Printf(TEXT("%s %d%s"), *Stem, Number(L, M.Id), *Named);
}
TArray<const FCireMapMarker*> CireMapLayout::PathsFrom(const FCireMapLayout& L, const FString& SpawnId)
{
    TArray<const FCireMapMarker*> Out;
    for (const FCireMapMarker& M : L.Markers) if (M.Type == MonsterPath && M.From == SpawnId && !SpawnId.IsEmpty()) Out.Add(&M);
    return Out;
}
const FCireMapMarker* CireMapLayout::ObjectiveOf(const FCireMapLayout& L, ECireMarkerOwner Team)
{
    const FCireMapMarker* SharedOne = nullptr;
    for (const FCireMapMarker& M : L.Markers)
        if (M.Type == Objective) { if (M.Owner == Team) return &M; if (M.Owner == ECireMarkerOwner::Shared && !SharedOne) SharedOne = &M; }
    return SharedOne;
}
double CireMapLayout::PolylineLength(const TArray<FVector2D>& P)
{
    double Length = 0; for (int32 I = 0; I + 1 < P.Num(); ++I) Length += FVector2D::Distance(P[I], P[I + 1]); return Length;
}
namespace
{
bool ReachesObjective(const FCireMapLayout& L, const FCireMapMarker& Path, const FVector2D& End)
{
    const FCireMapMarker* Goal = CireMapLayout::ObjectiveOf(L, Path.Target);
    // The objective must stand in the path's own realm: realm-local coordinates repeat in the other realm.
    const bool bSameRealm = Goal && (Path.Owner == ECireMarkerOwner::Shared || CireMapLayout::ShownInRealm(*Goal, CireMapLayout::RealmOf(Path.Owner)));
    return bSameRealm && FVector2D::Distance(End, Goal->Position) <= FMath::Max(Goal->Radius, 50.f) + ObjectiveSlack;
}
}
TArray<FVector2D> CireMapLayout::WalkPolyline(const FCireMapLayout& L, const FString& PathId, bool* bOutReaches)
{
    TArray<FVector2D> Out;
    if (bOutReaches) *bOutReaches = false;
    const FCireMapMarker* Path = Find(L, PathId);
    if (!Path || Path->Type != MonsterPath) return Out;
    if (const FCireMapMarker* Spawn = Path->From.IsEmpty() ? nullptr : Find(L, Path->From)) Out.Add(Spawn->Position);
    for (const FVector2D& P : Path->Points) if (Out.Num() == 0 || !SameXY(Out.Last(), P)) Out.Add(P);
    TSet<FString> Seen; Seen.Add(Path->Id);
    const FCireMapMarker* Current = Path;
    while (!Current->MergeInto.IsEmpty() && Out.Num() > 0)
    {
        const FCireMapMarker* Next = Find(L, Current->MergeInto);
        if (!Next || Next->Type != MonsterPath || Seen.Contains(Next->Id) || Next->Points.Num() == 0) break;
        Seen.Add(Next->Id);
        // Continue from the merge point: the nearest point of the joined path, then its remainder.
        int32 Best = 0; double BestD = TNumericLimits<double>::Max();
        for (int32 I = 0; I < Next->Points.Num(); ++I) { const double D = FVector2D::DistSquared(Next->Points[I], Out.Last()); if (D < BestD) { BestD = D; Best = I; } }
        for (int32 I = Best; I < Next->Points.Num(); ++I) if (!SameXY(Out.Last(), Next->Points[I])) Out.Add(Next->Points[I]);
        Current = Next;
    }
    if (bOutReaches) *bOutReaches = Out.Num() > 0 && ReachesObjective(L, *Path, Out.Last());
    return Out;
}
bool CireMapLayout::InsideBounds(const FCireMapLayout& L, const FVector2D& P, int32 Realm)
{
    for (const FCireMapMarker& B : L.Markers)
    {
        if (B.Type != PlayBounds || B.Points.Num() < 3 || !ShownInRealm(B, Realm)) continue;
        bool bInside = false;
        for (int32 I = 0, J = B.Points.Num() - 1; I < B.Points.Num(); J = I++)
        {
            const FVector2D& A = B.Points[I]; const FVector2D& C = B.Points[J];
            if ((A.Y > P.Y) != (C.Y > P.Y) && P.X < (C.X - A.X) * (P.Y - A.Y) / (C.Y - A.Y) + A.X) bInside = !bInside;
        }
        if (!bInside) return false;
    }
    return true;
}

// ================================================================================================= authoring
namespace
{
FString CreateTwin(FCireMapLayout& L, const FString& Id)
{
    const int32 Index = CireMapLayout::IndexOf(L, Id);
    if (Index == INDEX_NONE || !IsTeam(L.Markers[Index].Owner)) return FString();
    FCireMapMarker Twin = L.Markers[Index];
    Twin.Id = NewId(L, Twin.Type);
    Twin.Owner = CireMapLayout::OtherTeam(Twin.Owner);
    if (IsTeam(Twin.Target)) Twin.Target = CireMapLayout::OtherTeam(Twin.Target);
    Twin.From = MapLink(L, Twin.From); Twin.MergeInto = MapLink(L, Twin.MergeInto);
    Twin.bMirror = true; Twin.Pair = Id;
    L.Markers[Index].bMirror = true; L.Markers[Index].Pair = Twin.Id;
    L.Markers.Insert(Twin, Index + 1); // keep pairs adjacent so numbering stays aligned
    return Twin.Id;
}
}
void CireMapLayout::SyncTwin(FCireMapLayout& L, const FString& Id)
{
    FCireMapMarker* M = Find(L, Id);
    if (!M || !M->bMirror || M->Pair.IsEmpty()) return;
    FCireMapMarker* T = Find(L, M->Pair);
    if (!T) { M->Pair.Reset(); return; }
    const FCireMapMarker Source = *M;
    T->Type = Source.Type; T->Name = Source.Name; T->Position = Source.Position; T->Yaw = Source.Yaw; T->Radius = Source.Radius;
    T->Tier = Source.Tier; T->Kind = Source.Kind; T->Points = Source.Points; T->bMirror = true; T->Pair = Source.Id;
    T->SignPos = Source.SignPos; T->SignYaw = Source.SignYaw; T->SignHeight = Source.SignHeight;
    T->StallPos = Source.StallPos; T->StallYaw = Source.StallYaw; T->StallSize = Source.StallSize;
    T->Owner = OtherTeam(Source.Owner);
    T->Target = IsTeam(Source.Target) ? OtherTeam(Source.Target) : Source.Target;
    T->From = MapLink(L, Source.From); T->MergeInto = MapLink(L, Source.MergeInto);
}
FString CireMapLayout::Place(FCireMapLayout& L, FName Type, const FVector2D& Local, ECireMarkerOwner Owner, float Yaw, bool bMirror)
{
    const FCireMarkerType* T = FindType(Type);
    if (!T || !FMath::IsFinite(Local.X) || !FMath::IsFinite(Local.Y)) return FString();
    if (T->DefaultOwner == ECireMarkerOwner::Shared) Owner = ECireMarkerOwner::Shared;
    if (T->MaxPerOwner > 0 && OfType(L, Type, Owner).Num() >= T->MaxPerOwner) return FString();
    FCireMapMarker M;
    M.Id = NewId(L, Type); M.Type = Type; M.Owner = Owner; M.Target = Owner; M.Position = Local;
    M.Yaw = T->bFacing && FMath::IsFinite(Yaw) ? FRotator::NormalizeAxis(Yaw) : 0.f;
    M.Radius = T->bRadius ? T->DefaultRadius : 0.f;
    M.Tier = T->bTier ? 1 : 0;
    if (T->bKind && T->Kinds.Num() > 0) M.Kind = T->Kinds[0];
    if (Type == Vendor) { if (VendorTypes().Num() > 0) M.Kind = VendorTypes()[0].Id; ApplyVendorDefaults(M); }
    if (T->bPoints) M.Points.Add(Local);
    M.bMirror = bMirror && IsTeam(Owner);
    const FString Id = M.Id;
    L.Markers.Add(M);
    if (T->bNamed && Type != Vendor) Find(L, Id)->Name = FString::Printf(TEXT("%s %d"), *T->Label, Number(L, Id));
    if (Find(L, Id)->bMirror) CreateTwin(L, Id);
    return Id;
}
bool CireMapLayout::Remove(FCireMapLayout& L, const FString& Id)
{
    const FCireMapMarker* M = Find(L, Id);
    if (!M) return false;
    TSet<FString> Gone; Gone.Add(Id);
    if (M->bMirror && !M->Pair.IsEmpty()) Gone.Add(M->Pair);
    L.Markers.RemoveAll([&](const FCireMapMarker& X) { return Gone.Contains(X.Id); });
    for (FCireMapMarker& X : L.Markers)
    {
        if (Gone.Contains(X.From)) X.From.Reset();
        if (Gone.Contains(X.MergeInto)) X.MergeInto.Reset();
        if (Gone.Contains(X.Pair)) { X.Pair.Reset(); }
    }
    return true;
}
bool CireMapLayout::Move(FCireMapLayout& L, const FString& Id, const FVector2D& Local)
{
    FCireMapMarker* M = Find(L, Id);
    if (!M || !FMath::IsFinite(Local.X) || !FMath::IsFinite(Local.Y)) return false;
    const FVector2D Delta = Local - M->Position;
    for (FVector2D& P : M->Points) P += Delta;
    M->Position = Local;
    if (M->Type == Vendor) { M->SignPos += Delta; M->StallPos += Delta; } // a vendor moves as a group
    SyncTwin(L, Id);
    return true;
}
bool CireMapLayout::SetYaw(FCireMapLayout& L, const FString& Id, float Yaw)
{
    FCireMapMarker* M = Find(L, Id); if (!M || !FMath::IsFinite(Yaw)) return false;
    const float Delta = FRotator::NormalizeAxis(FRotator::NormalizeAxis(Yaw) - M->Yaw);
    M->Yaw = FRotator::NormalizeAxis(Yaw);
    if (M->Type == Vendor)
    {
        // Turning the vendor turns its whole group around the NPC.
        M->SignPos = M->Position + Turn(M->SignPos - M->Position, Delta); M->SignYaw = FRotator::NormalizeAxis(M->SignYaw + Delta);
        M->StallPos = M->Position + Turn(M->StallPos - M->Position, Delta); M->StallYaw = FRotator::NormalizeAxis(M->StallYaw + Delta);
    }
    SyncTwin(L, Id); return true;
}
bool CireMapLayout::SetRadius(FCireMapLayout& L, const FString& Id, float Radius)
{
    FCireMapMarker* M = Find(L, Id); if (!M || !FMath::IsFinite(Radius)) return false;
    M->Radius = M->Type == ChallengePack ? FMath::Clamp(Radius, FCireChallengeBay::MinRadius, FCireChallengeBay::MaxRadius) : FMath::Clamp(Radius, 50.f, 5000.f);
    SyncTwin(L, Id); return true;
}
bool CireMapLayout::SetTier(FCireMapLayout& L, const FString& Id, int32 Tier)
{
    FCireMapMarker* M = Find(L, Id); if (!M) return false;
    M->Tier = FMath::Clamp(Tier, 1, FCireChallengeBay::MaxTier); SyncTwin(L, Id); return true;
}
bool CireMapLayout::SetName(FCireMapLayout& L, const FString& Id, const FString& Name)
{
    FCireMapMarker* M = Find(L, Id); if (!M) return false;
    M->Name = Name.Left(40).TrimStartAndEnd(); SyncTwin(L, Id); return true;
}
bool CireMapLayout::SetKind(FCireMapLayout& L, const FString& Id, const FString& Kind)
{
    FCireMapMarker* M = Find(L, Id); if (!M) return false;
    M->Kind = Kind.Left(32).TrimStartAndEnd().ToLower();
    if (M->Type == Vendor) ApplyVendorDefaults(*M);
    SyncTwin(L, Id); return true;
}
bool CireMapLayout::MovePart(FCireMapLayout& L, const FString& Id, ECireVendorPart Part, const FVector2D& Local)
{
    if (Part == ECireVendorPart::Npc) return Move(L, Id, Local);
    FCireMapMarker* M = Find(L, Id);
    if (!M || M->Type != Vendor || !FMath::IsFinite(Local.X) || !FMath::IsFinite(Local.Y)) return false;
    (Part == ECireVendorPart::Sign ? M->SignPos : M->StallPos) = Local;
    SyncTwin(L, Id); return true;
}
bool CireMapLayout::SetPartYaw(FCireMapLayout& L, const FString& Id, ECireVendorPart Part, float Yaw)
{
    if (Part == ECireVendorPart::Npc) return SetYaw(L, Id, Yaw);
    FCireMapMarker* M = Find(L, Id);
    if (!M || M->Type != Vendor || !FMath::IsFinite(Yaw)) return false;
    (Part == ECireVendorPart::Sign ? M->SignYaw : M->StallYaw) = FRotator::NormalizeAxis(Yaw);
    SyncTwin(L, Id); return true;
}
bool CireMapLayout::ResetPart(FCireMapLayout& L, const FString& Id, ECireVendorPart Part)
{
    FCireMapMarker* M = Find(L, Id);
    if (!M || M->Type != Vendor || Part == ECireVendorPart::Npc) return false;
    FCireMapMarker Defaults = *M; ApplyVendorDefaults(Defaults);
    if (Part == ECireVendorPart::Sign) { M->SignPos = Defaults.SignPos; M->SignYaw = Defaults.SignYaw; M->SignHeight = Defaults.SignHeight; }
    else { M->StallPos = Defaults.StallPos; M->StallYaw = Defaults.StallYaw; M->StallSize = Defaults.StallSize; }
    SyncTwin(L, Id); return true;
}
TArray<FVector2D> CireMapLayout::StallCorners(const FCireMapMarker& V)
{
    const FVector2D H = V.StallSize * .5; // width across the facing (Y), depth along it (X)
    return {V.StallPos + Turn(FVector2D(-H.Y, -H.X), V.StallYaw), V.StallPos + Turn(FVector2D(H.Y, -H.X), V.StallYaw),
            V.StallPos + Turn(FVector2D(H.Y, H.X), V.StallYaw), V.StallPos + Turn(FVector2D(-H.Y, H.X), V.StallYaw)};
}
bool CireMapLayout::SetOwner(FCireMapLayout& L, const FString& Id, ECireMarkerOwner Owner)
{
    FCireMapMarker* M = Find(L, Id);
    if (!M) return false;
    if (M->Owner == Owner) return true;
    if (Owner == ECireMarkerOwner::Shared)
    {
        const FString Twin = M->bMirror ? M->Pair : FString();
        M->Owner = Owner; M->bMirror = false; M->Pair.Reset();
        if (!Twin.IsEmpty()) { FCireMapMarker* T = Find(L, Twin); if (T) { T->bMirror = false; T->Pair.Reset(); } Remove(L, Twin); }
        return true;
    }
    const ECireMarkerOwner Before = M->Owner;
    M->Owner = Owner;
    if (M->Target == Before || !IsTeam(M->Target)) M->Target = Owner;
    if (Before == ECireMarkerOwner::Shared) { M->bMirror = true; CreateTwin(L, Id); }
    else SyncTwin(L, Id);
    return true;
}
bool CireMapLayout::SetTarget(FCireMapLayout& L, const FString& Id, ECireMarkerOwner Target)
{
    FCireMapMarker* M = Find(L, Id); if (!M) return false;
    M->Target = Target; SyncTwin(L, Id);
    // A spawn's paths carry its waves: they attack the same team.
    if (M->Type == MonsterSpawn)
    {
        TArray<FString> Paths;
        for (const FCireMapMarker& P : L.Markers) if (P.Type == MonsterPath && P.From == Id) Paths.Add(P.Id);
        for (const FString& P : Paths) { Find(L, P)->Target = Target; SyncTwin(L, P); }
    }
    return true;
}
bool CireMapLayout::SetMirror(FCireMapLayout& L, const FString& Id, bool bMirror)
{
    FCireMapMarker* M = Find(L, Id);
    if (!M) return false;
    if (bMirror)
    {
        if (!IsTeam(M->Owner)) return false;
        if (M->bMirror && !M->Pair.IsEmpty() && Find(L, M->Pair)) return true;
        M->bMirror = true; M->Pair.Reset();
        CreateTwin(L, Id);
        return true;
    }
    const FString Twin = M->Pair;
    M->bMirror = false; M->Pair.Reset();
    if (FCireMapMarker* T = Twin.IsEmpty() ? nullptr : Find(L, Twin)) { T->bMirror = false; T->Pair.Reset(); }
    return true;
}
bool CireMapLayout::PathClosed(const FCireMapLayout& L, const FString& PathId)
{
    const FCireMapMarker* P = Find(L, PathId);
    if (!P || P->Type != MonsterPath || P->Points.Num() == 0) return false;
    return !P->MergeInto.IsEmpty() || ReachesObjective(L, *P, P->Points.Last());
}
namespace
{
// A path's last point next to another path (same target) merges into it and snaps onto that path's point.
void ResolveMerge(FCireMapLayout& L, FCireMapMarker& Path)
{
    Path.MergeInto.Reset();
    if (Path.Type != CireMapLayout::MonsterPath || Path.Points.Num() < 2) return;
    FVector2D& End = Path.Points.Last();
    if (ReachesObjective(L, Path, End)) return;
    for (const FCireMapMarker& Other : L.Markers)
    {
        if (Other.Type != CireMapLayout::MonsterPath || Other.Id == Path.Id || Other.Target != Path.Target || Other.Owner != Path.Owner || Other.MergeInto == Path.Id) continue;
        for (const FVector2D& P : Other.Points)
            if (FVector2D::Distance(P, End) <= MergeSnap) { End = P; Path.MergeInto = Other.Id; return; }
    }
}
}
FString CireMapLayout::ChainPoint(FCireMapLayout& L, FName Type, const FString& PathId, const FVector2D& Local, ECireMarkerOwner Owner, const FString& FromSpawn)
{
    const FCireMarkerType* T = FindType(Type);
    if (!T || !T->bPoints || !FMath::IsFinite(Local.X) || !FMath::IsFinite(Local.Y)) return FString();
    FCireMapMarker* M = PathId.IsEmpty() ? nullptr : Find(L, PathId);
    if (M && (M->Type != Type || (Type == MonsterPath && PathClosed(L, PathId)))) M = nullptr;
    if (!M && Type == PlayBounds) { const auto Existing = OfType(L, PlayBounds); if (Existing.Num() > 0) M = Find(L, Existing[0]->Id); }
    if (!M)
    {
        const FString Id = Place(L, Type, Local, Owner, 0.f, true);
        M = Find(L, Id);
        if (!M) return FString();
        if (Type == MonsterPath)
        {
            // A path starts at a monster spawn: the one given, else the nearest spawn of the same team within 40 m.
            const FCireMapMarker* Spawn = FromSpawn.IsEmpty() ? nullptr : Find(L, FromSpawn);
            if (Spawn && Spawn->Type != MonsterSpawn) Spawn = nullptr;
            if (!Spawn)
            {
                double Best = SpawnLinkRange * SpawnLinkRange;
                for (const FCireMapMarker& S : L.Markers)
                    if (S.Type == MonsterSpawn && S.Owner == M->Owner && FVector2D::DistSquared(S.Position, Local) < Best) { Best = FVector2D::DistSquared(S.Position, Local); Spawn = &S; }
            }
            if (Spawn) { M->From = Spawn->Id; M->Target = Spawn->Target; }
            SyncTwin(L, M->Id);
        }
        return M->Id;
    }
    if (M->Points.Num() >= 64) return FString();
    M->Points.Add(Local);
    M->Position = M->Points[0];
    ResolveMerge(L, *M);
    const FString Id = M->Id;
    SyncTwin(L, Id);
    return Id;
}
bool CireMapLayout::RemovePoint(FCireMapLayout& L, const FString& Id, int32 Index)
{
    FCireMapMarker* M = Find(L, Id);
    if (!M || !M->Points.IsValidIndex(Index)) return false;
    const bool bWasLast = Index == M->Points.Num() - 1;
    M->Points.RemoveAt(Index); // Index-1 and the old Index+1 are now chained directly
    if (M->Points.Num() == 0) return Remove(L, Id);
    M->Position = M->Points[0];
    if (bWasLast) ResolveMerge(L, *M);
    SyncTwin(L, Id);
    return true;
}
bool CireMapLayout::MovePoint(FCireMapLayout& L, const FString& Id, int32 Index, const FVector2D& Local)
{
    FCireMapMarker* M = Find(L, Id);
    if (!M || !M->Points.IsValidIndex(Index) || !FMath::IsFinite(Local.X) || !FMath::IsFinite(Local.Y)) return false;
    M->Points[Index] = Local;
    M->Position = M->Points[0];
    if (Index == M->Points.Num() - 1) ResolveMerge(L, *M);
    SyncTwin(L, Id);
    return true;
}
int32 CireMapLayout::Clear(FCireMapLayout& L, FName Type)
{
    TArray<FString> Ids;
    for (const FCireMapMarker& M : L.Markers) if (Type.IsNone() || M.Type == Type) Ids.Add(M.Id);
    for (const FString& Id : Ids) if (Find(L, Id)) Remove(L, Id);
    return Ids.Num();
}

// ================================================================================================= validation
TArray<FCireLayoutIssue> CireMapLayout::Validate(const FCireMapLayout& L, const FCireLayoutChecks* Checks)
{
    TArray<FCireLayoutIssue> Out;
    auto Issue = [&](bool bError, int32 Team, const FString& Id, const FString& Message) { Out.Add({bError, Team, Id, Message}); };
    for (const ECireMarkerOwner Team : {ECireMarkerOwner::Team1, ECireMarkerOwner::Team2})
    {
        const int32 T = OwnerValue(Team); const FString Tag = TeamTag(Team);
        int32 Players = 0, Spawns = 0;
        for (const FCireMapMarker& M : L.Markers)
        {
            if (M.Type == PlayerSpawn && (M.Owner == Team || M.Owner == ECireMarkerOwner::Shared)) ++Players;
            if (M.Type == MonsterSpawn && M.Target == Team) ++Spawns;
        }
        if (Players == 0) Issue(true, T, FString(), FString::Printf(TEXT("%s has no player spawn"), *Tag));
        if (Spawns == 0) Issue(true, T, FString(), FString::Printf(TEXT("No monster spawn targets %s"), *Tag));
        if (!ObjectiveOf(L, Team)) Issue(true, T, FString(), FString::Printf(TEXT("%s has no objective to defend"), *Tag));
        for (const FCireMapMarker& M : L.Markers)
        {
            if (M.Type == MonsterSpawn && M.Target == Team && PathsFrom(L, M.Id).Num() == 0)
                Issue(true, T, M.Id, FString::Printf(TEXT("%s has no path"), *DisplayLabel(L, M)));
            if (M.Type == MonsterPath && M.Target == Team)
            {
                const FCireMapMarker* Spawn = M.From.IsEmpty() ? nullptr : Find(L, M.From);
                if (!Spawn || Spawn->Type != MonsterSpawn) Issue(true, T, M.Id, FString::Printf(TEXT("%s does not start at a monster spawn"), *DisplayLabel(L, M)));
                bool bReaches = false; WalkPolyline(L, M.Id, &bReaches);
                if (!bReaches) Issue(true, T, M.Id, FString::Printf(TEXT("%s does not reach %s's objective"), *DisplayLabel(L, M), *Tag));
            }
        }
        const int32 Packs = OfType(L, ChallengePack, Team).Num() + OfType(L, ChallengePack, ECireMarkerOwner::Shared).Num();
        if (Packs > FCireBattlefieldRoutes::MaxBays) Issue(true, T, FString(), FString::Printf(TEXT("%s has %d challenge packs (16 at most)"), *Tag, Packs));
        // Every vendor type must be available to each team (once the layout places vendors at all).
        if (OfType(L, Vendor).Num() > 0)
            for (const FCireVendorType& VT : VendorTypes())
            {
                bool bHas = false;
                for (const FCireMapMarker& M : L.Markers) bHas |= M.Type == Vendor && M.Kind == VT.Id && (M.Owner == Team || M.Owner == ECireMarkerOwner::Shared);
                if (!bHas) Issue(true, T, FString(), FString::Printf(TEXT("%s has no %s vendor"), *Tag, *VT.Id));
            }
    }
    for (const FCireMapMarker& M : L.Markers)
    {
        const FString Label = DisplayLabel(L, M);
        if (!FindType(M.Type)) Issue(true, 0, M.Id, FString::Printf(TEXT("%s has an unknown setter type %s"), *M.Id, *M.Type.ToString()));
        if (M.Type == Vendor)
        {
            if (!FindVendorType(M.Kind)) Issue(true, OwnerValue(M.Owner), M.Id, FString::Printf(TEXT("%s has an unknown vendor type (%s)"), *Label, *M.Kind));
            // The stall must not stand in a monster path (units walk the path with a 60 cm body margin).
            const TArray<FVector2D> Stall = StallCorners(M);
            TArray<FVector2D> Grown;
            for (const FVector2D& C : Stall) Grown.Add(M.StallPos + (C - M.StallPos).GetSafeNormal() * ((C - M.StallPos).Size() + 60.));
            for (const FCireMapMarker& P : L.Markers)
            {
                if (P.Type != MonsterPath || !(M.Owner == ECireMarkerOwner::Shared || ShownInRealm(P, RealmOf(M.Owner)))) continue;
                const TArray<FVector2D> Walk = WalkPolyline(L, P.Id);
                bool bBlocks = false;
                for (int32 I = 0; I + 1 < Walk.Num() && !bBlocks; ++I)
                {
                    bBlocks = InsideConvex(Grown, Walk[I]) || InsideConvex(Grown, Walk[I + 1]);
                    for (int32 E = 0; E < Grown.Num() && !bBlocks; ++E) bBlocks = SegmentsCross(Walk[I], Walk[I + 1], Grown[E], Grown[(E + 1) % Grown.Num()]);
                }
                if (bBlocks) { Issue(true, OwnerValue(M.Owner), M.Id, FString::Printf(TEXT("%s: the stall blocks %s"), *Label, *DisplayLabel(L, P))); break; }
            }
            if (Checks && Checks->SignClear)
                for (int32 Realm = 0; Realm < 2; ++Realm)
                    if (ShownInRealm(M, Realm) && !Checks->SignClear(Realm, M.SignPos, M.SignHeight))
                    { Issue(true, OwnerValue(M.Owner), M.Id, FString::Printf(TEXT("%s: the sign clips into the town (%s)"), *Label, *RealmName(Realm))); break; }
        }
        if (M.Type == ChallengePack && (M.Tier < 1 || M.Tier > FCireChallengeBay::MaxTier || M.Radius < FCireChallengeBay::MinRadius || M.Radius > FCireChallengeBay::MaxRadius))
            Issue(true, OwnerValue(M.Owner), M.Id, FString::Printf(TEXT("%s needs a tier 1..10 and a radius of 2..15 m"), *Label));
        if (M.bMirror && IsTeam(M.Owner))
        {
            const FCireMapMarker* T = M.Pair.IsEmpty() ? nullptr : Find(L, M.Pair);
            if (!T) Issue(true, OwnerValue(M.Owner), M.Id, FString::Printf(TEXT("%s is mirrored but its twin is missing"), *Label));
            else
            {
                const bool bSync = T->Type == M.Type && T->Owner == OtherTeam(M.Owner) && T->Pair == M.Id && T->bMirror && SameXY(T->Position, M.Position) &&
                    FMath::IsNearlyEqual(T->Yaw, M.Yaw, .01f) && FMath::IsNearlyEqual(T->Radius, M.Radius, .01f) && T->Tier == M.Tier && T->Kind == M.Kind &&
                    T->Name == M.Name && T->Points == M.Points && T->Target == (IsTeam(M.Target) ? OtherTeam(M.Target) : M.Target) &&
                    T->From == MapLink(L, M.From) && T->MergeInto == MapLink(L, M.MergeInto) &&
                    SameXY(T->SignPos, M.SignPos) && SameXY(T->StallPos, M.StallPos) && T->SignYaw == M.SignYaw && T->StallYaw == M.StallYaw &&
                    T->SignHeight == M.SignHeight && T->StallSize == M.StallSize;
                if (!bSync) Issue(true, OwnerValue(M.Owner), M.Id, FString::Printf(TEXT("%s is out of sync with its mirrored twin"), *Label));
            }
        }
        if (M.Type == PlayBounds) { if (M.Points.Num() < 3) Issue(true, 0, M.Id, TEXT("Play bounds need at least 3 points")); continue; }
        for (int32 Realm = 0; Realm < 2; ++Realm)
        {
            if (!ShownInRealm(M, Realm)) continue;
            TArray<FVector2D> Spots = M.Points.Num() > 0 ? M.Points : TArray<FVector2D>{M.Position};
            for (const FVector2D& P : Spots)
            {
                if (!InsideBounds(L, P, Realm)) { Issue(true, OwnerValue(M.Owner), M.Id, FString::Printf(TEXT("%s is outside the play bounds"), *Label)); break; }
                if (Checks && Checks->OnNavmesh && M.Type != Blocker && !Checks->OnNavmesh(Realm, P))
                { Issue(true, OwnerValue(M.Owner), M.Id, FString::Printf(TEXT("%s is off the navmesh (%s)"), *Label, *RealmName(Realm))); break; }
            }
            if (M.Type == MonsterPath && Checks && Checks->Walkable)
            {
                const TArray<FVector2D> Walk = WalkPolyline(L, M.Id);
                for (int32 I = 0; I + 1 < Walk.Num(); ++I)
                    if (!Checks->Walkable(Realm, Walk[I], Walk[I + 1]))
                    { Issue(true, OwnerValue(M.Owner), M.Id, FString::Printf(TEXT("%s is blocked between points %d and %d (%s)"), *Label, I, I + 1, *RealmName(Realm))); break; }
            }
        }
    }
    return Out;
}

// ================================================================================================= documents
FString CireMapLayout::ToJson(const FCireMapLayout& L)
{
    auto N = [](double V)
    {
        const double R = FMath::RoundToDouble(V * 10.) / 10.;
        return FMath::IsNearlyEqual(R, FMath::RoundToDouble(R)) ? FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::RoundToDouble(R))) : FString::Printf(TEXT("%.1f"), R);
    };
    auto Q = [](const FString& S) { return FString::Printf(TEXT("\"%s\""), *S.ReplaceCharWithEscapedChar()); };
    TArray<FString> Lines;
    for (const FCireMapMarker& M : L.Markers)
    {
        const FCireMarkerType& T = TypeOrDefault(M.Type);
        TArray<FString> F;
        F.Add(FString::Printf(TEXT("\"id\": %s"), *Q(M.Id)));
        F.Add(FString::Printf(TEXT("\"type\": %s"), *Q(M.Type.ToString())));
        if (!M.Name.IsEmpty()) F.Add(FString::Printf(TEXT("\"name\": %s"), *Q(M.Name)));
        F.Add(FString::Printf(TEXT("\"team\": %d"), OwnerValue(M.Owner)));
        if (T.bTarget || M.Target != M.Owner) F.Add(FString::Printf(TEXT("\"target\": %d"), OwnerValue(M.Target)));
        if (M.Points.Num() > 0)
        {
            TArray<FString> P; for (const FVector2D& V : M.Points) P.Add(FString::Printf(TEXT("[%s,%s]"), *N(V.X), *N(V.Y)));
            F.Add(FString::Printf(TEXT("\"points\": [%s]"), *FString::Join(P, TEXT(","))));
        }
        else { F.Add(FString::Printf(TEXT("\"x\": %s"), *N(M.Position.X))); F.Add(FString::Printf(TEXT("\"y\": %s"), *N(M.Position.Y))); }
        if (T.bFacing || M.Yaw != 0.f) F.Add(FString::Printf(TEXT("\"yaw\": %s"), *N(M.Yaw)));
        if (T.bRadius || M.Radius > 0.f) F.Add(FString::Printf(TEXT("\"radius\": %s"), *N(M.Radius)));
        if (T.bTier || M.Tier > 0) F.Add(FString::Printf(TEXT("\"tier\": %d"), M.Tier));
        if (!M.Kind.IsEmpty()) F.Add(FString::Printf(TEXT("\"kind\": %s"), *Q(M.Kind)));
        if (M.Type == Vendor)
        {
            F.Add(FString::Printf(TEXT("\"sign\": { \"x\": %s, \"y\": %s, \"yaw\": %s, \"height\": %s }"), *N(M.SignPos.X), *N(M.SignPos.Y), *N(M.SignYaw), *N(M.SignHeight)));
            F.Add(FString::Printf(TEXT("\"stall\": { \"x\": %s, \"y\": %s, \"yaw\": %s, \"width\": %s, \"depth\": %s }"), *N(M.StallPos.X), *N(M.StallPos.Y), *N(M.StallYaw), *N(M.StallSize.X), *N(M.StallSize.Y)));
        }
        if (!M.From.IsEmpty()) F.Add(FString::Printf(TEXT("\"from\": %s"), *Q(M.From)));
        if (!M.MergeInto.IsEmpty()) F.Add(FString::Printf(TEXT("\"mergeInto\": %s"), *Q(M.MergeInto)));
        F.Add(FString::Printf(TEXT("\"mirror\": %s"), M.bMirror ? TEXT("true") : TEXT("false")));
        if (!M.Pair.IsEmpty()) F.Add(FString::Printf(TEXT("\"pair\": %s"), *Q(M.Pair)));
        Lines.Add(TEXT("    { ") + FString::Join(F, TEXT(", ")) + TEXT(" }"));
    }
    return FString::Printf(TEXT("{\n  \"schemaVersion\": 1,\n  \"units\": \"centimeters\",\n  \"frame\": \"realm-local\",\n  \"name\": %s,\n  \"markers\": [\n%s\n  ]\n}\n"),
        *Q(L.Name), *FString::Join(Lines, TEXT(",\n")));
}
bool CireMapLayout::ParseJson(const FString& Json, FCireMapLayout& Out, FString& Error)
{
    auto Fail = [&](const FString& Why) { Error = Why; return false; };
    if (Json.Len() > 1024 * 1024) return Fail(TEXT("The map layout exceeds 1 MB"));
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) return Fail(TEXT("Invalid map layout JSON"));
    double Schema = 0; FString Units;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || Schema != 1 || !Root->TryGetStringField(TEXT("units"), Units) || Units != TEXT("centimeters"))
        return Fail(TEXT("Expected map layout schema 1 in centimeters"));
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (!Root->TryGetArrayField(TEXT("markers"), List) || !List) return Fail(TEXT("The map layout needs a markers array"));
    FCireMapLayout L;
    Root->TryGetStringField(TEXT("name"), L.Name);
    TSet<FString> Ids;
    for (const auto& Value : *List)
    {
        const TSharedPtr<FJsonObject> O = Value.IsValid() ? Value->AsObject() : nullptr;
        FCireMapMarker M; FString Type;
        if (!O || !O->TryGetStringField(TEXT("id"), M.Id) || M.Id.IsEmpty() || Ids.Contains(M.Id)) return Fail(TEXT("Every marker needs a unique id"));
        if (!O->TryGetStringField(TEXT("type"), Type) || !FindType(FName(*Type))) return Fail(FString::Printf(TEXT("Marker %s has an unknown type"), *M.Id));
        M.Type = FName(*Type);
        double Team = 1, Target = -1, X = 0, Y = 0, Yaw = 0, Radius = 0, Tier = 0;
        O->TryGetNumberField(TEXT("team"), Team);
        if (Team != 0 && Team != 1 && Team != 2) return Fail(FString::Printf(TEXT("Marker %s: team must be 0 (shared), 1 or 2"), *M.Id));
        M.Owner = OwnerOf(static_cast<int32>(Team));
        M.Target = O->TryGetNumberField(TEXT("target"), Target) ? OwnerOf(static_cast<int32>(Target)) : M.Owner;
        O->TryGetStringField(TEXT("name"), M.Name); O->TryGetStringField(TEXT("kind"), M.Kind);
        O->TryGetStringField(TEXT("from"), M.From); O->TryGetStringField(TEXT("mergeInto"), M.MergeInto); O->TryGetStringField(TEXT("pair"), M.Pair);
        O->TryGetBoolField(TEXT("mirror"), M.bMirror);
        const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
        if (O->TryGetArrayField(TEXT("points"), Points) && Points)
        {
            if (Points->Num() > 64) return Fail(FString::Printf(TEXT("Marker %s has more than 64 points"), *M.Id));
            for (const auto& P : *Points)
            {
                const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
                if (!P || !P->TryGetArray(XY) || !XY || XY->Num() != 2) return Fail(FString::Printf(TEXT("Marker %s: points are [x, y] pairs"), *M.Id));
                M.Points.Add(MakeXY(*XY));
            }
            if (M.Points.Num() > 0) M.Position = M.Points[0];
        }
        else if (O->TryGetNumberField(TEXT("x"), X) && O->TryGetNumberField(TEXT("y"), Y)) M.Position = FVector2D(X, Y);
        else return Fail(FString::Printf(TEXT("Marker %s needs x and y (or points)"), *M.Id));
        if (O->TryGetNumberField(TEXT("yaw"), Yaw)) M.Yaw = static_cast<float>(Yaw);
        if (O->TryGetNumberField(TEXT("radius"), Radius)) M.Radius = static_cast<float>(Radius);
        if (O->TryGetNumberField(TEXT("tier"), Tier)) M.Tier = static_cast<int32>(Tier);
        if (M.Type == Vendor)
        {
            ApplyVendorDefaults(M);
            const TSharedPtr<FJsonObject>* Part = nullptr; double A = 0, B = 0;
            if (O->TryGetObjectField(TEXT("sign"), Part) && Part)
            {
                if ((*Part)->TryGetNumberField(TEXT("x"), A) && (*Part)->TryGetNumberField(TEXT("y"), B)) M.SignPos = FVector2D(A, B);
                if ((*Part)->TryGetNumberField(TEXT("yaw"), A)) M.SignYaw = static_cast<float>(A);
                if ((*Part)->TryGetNumberField(TEXT("height"), A)) M.SignHeight = static_cast<float>(A);
            }
            if (O->TryGetObjectField(TEXT("stall"), Part) && Part)
            {
                if ((*Part)->TryGetNumberField(TEXT("x"), A) && (*Part)->TryGetNumberField(TEXT("y"), B)) M.StallPos = FVector2D(A, B);
                if ((*Part)->TryGetNumberField(TEXT("yaw"), A)) M.StallYaw = static_cast<float>(A);
                if ((*Part)->TryGetNumberField(TEXT("width"), A) && (*Part)->TryGetNumberField(TEXT("depth"), B)) M.StallSize = FVector2D(A, B);
            }
        }
        if (!FMath::IsFinite(M.Position.X) || !FMath::IsFinite(M.Position.Y) || !FMath::IsFinite(M.Yaw) || !FMath::IsFinite(M.Radius))
            return Fail(FString::Printf(TEXT("Marker %s has a non-finite value"), *M.Id));
        int32 Suffix = 0;
        if (M.Id.Contains(TEXT("_")) && LexTryParseString(Suffix, *M.Id.RightChop(M.Id.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1)))
            L.NextId = FMath::Max(L.NextId, Suffix + 1);
        Ids.Add(M.Id);
        L.Markers.Add(MoveTemp(M));
    }
    Out = MoveTemp(L); Error.Reset(); return true;
}
FString CireMapLayout::VendorsJson(const FCireMapLayout& L)
{
    TArray<FString> Lines;
    for (const FCireMapMarker& M : L.Markers)
    {
        if (M.Type != Vendor) continue;
        const FString PairField = M.Pair.IsEmpty() ? FString() : FString::Printf(TEXT(" \"pair\": \"%s\","), *M.Pair);
        Lines.Add(FString::Printf(TEXT("    { \"id\": \"%s\", \"vendorId\": \"%s\", \"name\": \"%s\", \"team\": %d,%s\n")
            TEXT("      \"npc\": { \"pos\": [%.0f, %.0f], \"yaw\": %.0f },\n")
            TEXT("      \"sign\": { \"pos\": [%.0f, %.0f, %.0f], \"yaw\": %.0f },\n")
            TEXT("      \"stall\": { \"pos\": [%.0f, %.0f], \"yaw\": %.0f, \"size\": [%.0f, %.0f] } }"),
            *M.Id, *M.Kind.ReplaceCharWithEscapedChar(), *M.Name.ReplaceCharWithEscapedChar(), OwnerValue(M.Owner), *PairField,
            M.Position.X, M.Position.Y, M.Yaw, M.SignPos.X, M.SignPos.Y, M.SignHeight, M.SignYaw, M.StallPos.X, M.StallPos.Y, M.StallYaw, M.StallSize.X, M.StallSize.Y));
    }
    // medieval-kingdom: the frame names the town the spots belong to (CireVendors only reads its own town's file).
    return FString::Printf(TEXT("{\n  \"schemaVersion\": 1,\n  \"units\": \"centimeters\",\n  \"frame\": \"%s\",\n  \"source\": \"MapLayout.json (map layout editor)\",\n  \"vendors\": [\n%s\n  ]\n}\n"),
        CireTownMap::IsActive() ? TEXT("castletown") : TEXT("realm-local"), *FString::Join(Lines, TEXT(",\n")));
}
FString CireMapLayout::ActivePath() { return FPaths::ProjectContentDir() / TEXT("Data/MapLayout.json"); }
FString CireMapLayout::VendorsPath() { return FPaths::ProjectContentDir() / TEXT("Data/TownVendors.json"); }
FString CireMapLayout::DraftPath() { return FPaths::ProjectSavedDir() / TEXT("MapLayoutDraft.json"); }
FString CireMapLayout::SanitizeName(const FString& Name)
{
    FString Out;
    for (const TCHAR C : Name.TrimStartAndEnd()) if (FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('-') || C == TEXT(' ')) Out.AppendChar(C == TEXT(' ') ? TEXT('_') : C);
    return Out.Left(48);
}
FString CireMapLayout::NamedPath(const FString& Name) { return FPaths::ProjectContentDir() / TEXT("Data/MapLayouts") / (SanitizeName(Name) + TEXT(".json")); }
TArray<FString> CireMapLayout::ListNamed()
{
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(FPaths::ProjectContentDir() / TEXT("Data/MapLayouts/*.json")), true, false);
    for (FString& F : Files) F = FPaths::GetBaseFilename(F);
    Files.Sort();
    return Files;
}
bool CireMapLayout::Save(const FCireMapLayout& L, const FString& Path, FString* Error)
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    if (IFileManager::Get().FileExists(*Path) && IFileManager::Get().IsReadOnly(*Path)) FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Path, false);
    if (!FFileHelper::SaveStringToFile(ToJson(L), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { if (Error) *Error = FString::Printf(TEXT("Could not write %s"), *Path); return false; }
    if (Error) Error->Reset();
    return true;
}
bool CireMapLayout::Load(FCireMapLayout& L, const FString& Path, FString* Error)
{
    FString Json, Why; FCireMapLayout Candidate;
    if (!FFileHelper::LoadFileToString(Json, *Path)) Why = FString::Printf(TEXT("%s could not be read"), *FPaths::GetCleanFilename(Path));
    else if (ParseJson(Json, Candidate, Why)) { L = MoveTemp(Candidate); if (Error) Error->Reset(); return true; }
    if (Error) *Error = Why;
    return false;
}

// ================================================================================================= route document
FCireMapLayout CireMapLayout::FromRoutes(const FCireBattlefieldRoutes& R)
{
    FCireMapLayout L; L.Name = TEXT("Current route");
    const bool bSame = R.LocalPoints[0] == R.LocalPoints[1] && R.Bays[0] == R.Bays[1];
    for (int32 Realm = 0; Realm < (bSame ? 1 : 2); ++Realm)
    {
        const ECireMarkerOwner Team = TeamOfRealm(Realm);
        const TArray<FVector2D>& P = R.LocalPoints[Realm];
        if (P.Num() == 0) continue;
        // The hero base of the procedural town (local -1700, 0) facing down the road.
        const FString Hero = Place(L, PlayerSpawn, R.BaseLocal, Team, 0.f, bSame); // medieval-kingdom: the document's base
        if (R.bRespawn) Place(L, Respawn, R.RespawnLocal, Team, 0.f, bSame);
        if (R.bBossSpawn) Place(L, BossSpawn, R.BossLocal, Team, 0.f, bSame);
        const float Face = P.Num() > 1 ? FMath::RadiansToDegrees(FMath::Atan2(P[1].Y - P[0].Y, P[1].X - P[0].X)) : 180.f;
        const FString Spawn = Place(L, MonsterSpawn, P[0], Team, Face, bSame);
        SetName(L, Spawn, TEXT("The Breach"));
        const FString Goal = Place(L, Objective, R.GoalCenter, Team, 0.f, bSame);
        SetRadius(L, Goal, static_cast<float>(FMath::Min(R.GoalSize.X, R.GoalSize.Y) * .5));
        FString Path;
        for (int32 I = 1; I < P.Num(); ++I)
        {
            Path = ChainPoint(L, MonsterPath, Path, P[I], Team, Spawn);
            // Realms that differ get independent markers: drop the automatic twin of a new path.
            if (!bSame && I == 1 && Find(L, Path)) { const FString Twin = Find(L, Path)->Pair; SetMirror(L, Path, false); Remove(L, Twin); }
        }
        if (!Path.IsEmpty()) SetName(L, Path, TEXT("Main road"));
        for (const FCireChallengeBay& Bay : R.Bays[Realm])
        {
            const FString Pack = Place(L, ChallengePack, Bay.Position, Team, 0.f, bSame);
            SetRadius(L, Pack, Bay.Radius); SetTier(L, Pack, Bay.Tier);
        }
        (void)Hero;
    }
    return L;
}
bool CireMapLayout::CompileRoutes(const FCireMapLayout& L, const FCireBattlefieldRoutes& Base, FCireBattlefieldRoutes& Out, TArray<FString>& Notes)
{
    Out = Base;
    bool bOk = true;
    for (int32 Realm = 0; Realm < 2; ++Realm)
    {
        const ECireMarkerOwner Team = TeamOfRealm(Realm);
        TArray<const FCireMapMarker*> Paths;
        for (const FCireMapMarker& M : L.Markers) if (M.Type == MonsterPath && M.Target == Team && ShownInRealm(M, Realm)) Paths.Add(&M);
        TArray<FVector2D> March;
        if (Paths.Num() > 0)
        {
            bool bReaches = false;
            March = WalkPolyline(L, Paths[0]->Id, &bReaches);
            const FCireMapMarker* Goal = ObjectiveOf(L, Team);
            if (!bReaches && Goal) { March.Add(Goal->Position); Notes.Add(FString::Printf(TEXT("%s: %s was closed into the objective"), *TeamTag(Team), *DisplayLabel(L, *Paths[0]))); }
            if (Paths.Num() > 1) Notes.Add(FString::Printf(TEXT("%s: %d more paths are kept in MapLayout.json; today's waves march down %s"), *TeamTag(Team), Paths.Num() - 1, *DisplayLabel(L, *Paths[0])));
        }
        if (March.Num() == 2) March.Insert((March[0] + March[1]) * .5, 1);
        if (March.Num() < 3) { bOk = false; Notes.Add(FString::Printf(TEXT("%s has no monster path to march down"), *TeamTag(Team))); }
        if (March.Num() > 64) { March.SetNum(64); Notes.Add(FString::Printf(TEXT("%s: the march route was cut to 64 points"), *TeamTag(Team))); }
        if (March.Num() >= 3) Out.LocalPoints[Realm] = March;
        TArray<FCireChallengeBay> Bays;
        for (const FCireMapMarker& M : L.Markers)
            if (M.Type == ChallengePack && ShownInRealm(M, Realm))
            {
                if (Bays.Num() >= FCireBattlefieldRoutes::MaxBays) { Notes.Add(FString::Printf(TEXT("%s: challenge packs past 16 were left out"), *TeamTag(Team))); break; }
                FCireChallengeBay Bay; Bay.Position = M.Position; Bay.Radius = FMath::Clamp(M.Radius, FCireChallengeBay::MinRadius, FCireChallengeBay::MaxRadius);
                Bay.Tier = FMath::Clamp(M.Tier, 1, FCireChallengeBay::MaxTier); Bays.Add(Bay);
            }
        Out.Bays[Realm] = Bays;
    }
    if (const FCireMapMarker* Goal = ObjectiveOf(L, ECireMarkerOwner::Team1))
    {
        Out.GoalCenter = Goal->Position;
        const FCireMapMarker* Other = ObjectiveOf(L, ECireMarkerOwner::Team2);
        if (Other && !SameXY(Other->Position, Goal->Position)) Notes.Add(TEXT("The T2 objective differs from T1's; today's goal zone is one realm-local spot (T1's)"));
    }
    // medieval-kingdom: the hero base (player spawn), the respawn point and the boss spawn are one realm-local spot each in
    // the route document (T1's marker, else a shared one); gameplay reads them through CireLanePath.
    auto SpotOf = [&](FName Type) -> const FCireMapMarker*
    {
        const FCireMapMarker* SharedOne = nullptr;
        for (const FCireMapMarker& M : L.Markers)
            if (M.Type == Type) { if (M.Owner == ECireMarkerOwner::Team1) return &M; if (M.Owner == ECireMarkerOwner::Shared && !SharedOne) SharedOne = &M; }
        return SharedOne;
    };
    if (const FCireMapMarker* M = SpotOf(PlayerSpawn)) Out.BaseLocal = M->Position;
    if (const FCireMapMarker* M = SpotOf(Respawn)) { Out.bRespawn = true; Out.RespawnLocal = M->Position; }
    if (const FCireMapMarker* M = SpotOf(BossSpawn)) { Out.bBossSpawn = true; Out.BossLocal = M->Position; }
    return bOk;
}
