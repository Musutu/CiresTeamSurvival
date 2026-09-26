#include "CireArenas.h"
#include "CireGame.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "CireNav.h" // nav-paths

DEFINE_LOG_CATEGORY_STATIC(LogCireArenas, Log, All);

namespace
{
using namespace CireArenas;

const TCHAR* const CastleMaterial = TEXT("/Game/Environment/Town/Materials/MI_TownW_CastleW.MI_TownW_CastleW");
const TCHAR* const StoneMaterial = TEXT("/Game/Environment/Town/Materials/MI_TownW_StoneW.MI_TownW_StoneW");
const TCHAR* const FlagstoneMaterial = TEXT("/Game/Environment/Town/Materials/MI_TownW_Flagstone.MI_TownW_Flagstone");
const TCHAR* const PlazaMaterial = TEXT("/Game/Environment/Town/Materials/MI_TownW_Plaza.MI_TownW_Plaza");
const TCHAR* const GoldMaterial = TEXT("/Game/Art/Materials/M_Gold.M_Gold");
constexpr float CapsuleRadius = 45.f;
constexpr float SpawnClearance = 150.f;
constexpr float CellSize = 25.f;

FPool GPool;
bool GLoaded = false;
bool GRotationValid = false;

struct FServerState { int32 Last = INDEX_NONE; bool bPending = false; };
TMap<TWeakObjectPtr<UWorld>, FServerState> GServer;

// ------------------------------------------------------------------ JSON helpers
double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default)
{
    double V = Default; return O.IsValid() && O->TryGetNumberField(Key, V) && FMath::IsFinite(V) ? V : Default;
}
bool Bool(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool Default)
{
    bool V = Default; return O.IsValid() && O->TryGetBoolField(Key, V) ? V : Default;
}
FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Default = FString())
{
    FString V; return O.IsValid() && O->TryGetStringField(Key, V) ? V : Default;
}
TArray<double> Numbers(const TArray<TSharedPtr<FJsonValue>>& Values)
{
    TArray<double> Out;
    for (const auto& V : Values) { double N = 0; if (V.IsValid() && V->TryGetNumber(N) && FMath::IsFinite(N)) Out.Add(N); else Out.Add(NAN); }
    return Out;
}
TArray<double> Array(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    return O.IsValid() && O->TryGetArrayField(Key, Values) ? Numbers(*Values) : TArray<double>();
}
FVector Vec(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FVector Default)
{
    const TArray<double> A = Array(O, Key);
    return A.Num() >= 3 ? FVector(A[0], A[1], A[2]) : Default;
}
FVector2D Vec2(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FVector2D Default)
{
    const TArray<double> A = Array(O, Key);
    return A.Num() >= 2 ? FVector2D(A[0], A[1]) : Default;
}
FLinearColor Color(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FLinearColor Default)
{
    const TArray<double> A = Array(O, Key);
    return A.Num() >= 3 ? FLinearColor(A[0], A[1], A[2], A.Num() >= 4 ? A[3] : 1.0) : Default;
}
FString ShapePath(const FString& Name)
{
    if (Name.Equals(TEXT("cube"), ESearchCase::IgnoreCase)) return TEXT("/Engine/BasicShapes/Cube.Cube");
    if (Name.Equals(TEXT("cylinder"), ESearchCase::IgnoreCase)) return TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
    if (Name.Equals(TEXT("sphere"), ESearchCase::IgnoreCase)) return TEXT("/Engine/BasicShapes/Sphere.Sphere");
    if (Name.Equals(TEXT("cone"), ESearchCase::IgnoreCase)) return TEXT("/Engine/BasicShapes/Cone.Cone");
    return Name;
}
bool AssetExists(const FString& ObjectPath)
{
    if (ObjectPath.IsEmpty()) return false;
    if (ObjectPath.StartsWith(TEXT("/Engine/"))) return true;
    return FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(ObjectPath));
}
// world-scale: purchased Fab packs are local-only; -CireNoFab (before/after captures) skips candidates inside them so a
// machine with the packs renders exactly what a clean clone renders.
bool IsFabPackPath(const FString& Path)
{
    static const TCHAR* const Roots[] = {TEXT("/Game/CastleTown/"), TEXT("/Game/Fab/"), TEXT("/Game/Forest_VFX/"), TEXT("/Game/Polyphoria/"), TEXT("/Game/FabDerived/")};
    for (const TCHAR* Root : Roots) if (Path.StartsWith(Root)) return true;
    return false;
}
bool CandidateExists(const FString& Candidate)
{
    static const bool bNoFab = FParse::Param(FCommandLine::Get(), TEXT("CireNoFab"));
    if (bNoFab && IsFabPackPath(Candidate)) return false;
    TArray<FString> Parts; Candidate.ParseIntoArray(Parts, TEXT("|"));
    for (const FString& Part : Parts) if (!AssetExists(Part)) return false;
    return Parts.Num() > 0;
}

// ------------------------------------------------------------------ built-in legacy court
void AddLegacySlots(FPool& P)
{
    auto Slot = [&](const TCHAR* Id, const TCHAR* Shape, const TCHAR* Material, FVector Footprint, EShape S, bool bShadow = true)
    {
        if (P.Slots.Contains(Id)) return;
        FSlot X; X.Id = Id; X.Fallback = ShapePath(Shape); X.FallbackMaterial = Material; X.Footprint = Footprint; X.Shape = S; X.bShadow = bShadow;
        P.Slots.Add(X.Id, X);
    };
    Slot(TEXT("legacy_floor"), TEXT("cube"), FlagstoneMaterial, FVector(100, 100, 2), EShape::Box, false);
    Slot(TEXT("legacy_tiles"), TEXT("cube"), PlazaMaterial, FVector(100, 100, 2), EShape::Box, false);
    Slot(TEXT("legacy_wall"), TEXT("cube"), CastleMaterial, FVector(100, 100, 100), EShape::Box);
    Slot(TEXT("legacy_pillar"), TEXT("cylinder"), CastleMaterial, FVector(150, 150, 700), EShape::Round);
    Slot(TEXT("legacy_orb"), TEXT("sphere"), GoldMaterial, FVector(110, 110, 110), EShape::Round, false);
    Slot(TEXT("legacy_ring"), TEXT("cube"), StoneMaterial, FVector(110, 12, 4), EShape::Box, false);
    Slot(TEXT("legacy_ember"), TEXT("cube"), TEXT("/Game/Art/Materials/M_Ember.M_Ember"), FVector(15, 100, 15), EShape::Box, false);
    Slot(TEXT("legacy_dusk"), TEXT("cube"), TEXT("/Game/Art/Materials/M_Dusk.M_Dusk"), FVector(15, 100, 15), EShape::Box, false);
}
FArena LegacyCourt()
{
    // The original arena ("The Sundered Court"), kept as the fallback when the themed pool is unusable.
    FArena A; A.Id = TEXT("sundered_court"); A.Name = TEXT("The Sundered Court"); A.Theme = TEXT("Walled stone court (legacy fallback)");
    A.Subtitle = TEXT("A walled stone court between the realms."); A.bFallbackOnly = true; A.Weight = 0;
    A.HalfExtents = FVector2D(1540, 1470); A.Ground = TEXT("legacy_floor"); A.GroundSize = 12000;
    for (int32 I = 0; I < 5; ++I) { A.Spawns[0].Add(FVector2D(-700, (I - 2) * 200)); A.Spawns[1].Add(FVector2D(700, (I - 2) * 200)); }
    auto Piece = [&](const TCHAR* Slot, FVector L, FVector S, bool bBlocker, float Yaw = 0)
    { FPiece P; P.Slot = Slot; P.Location = L; P.Scale = S; P.bBlocker = bBlocker; P.Yaw = Yaw; A.Pieces.Add(P); };
    Piece(TEXT("legacy_tiles"), FVector(0, 0, .5f), FVector(13, 13, 1), false);
    for (int32 Side : {-1, 1})
    {
        Piece(TEXT("legacy_wall"), FVector(Side * 1650, 0, 0), FVector(1.2f, 32.5f, 3.8f), false);
        Piece(TEXT("legacy_wall"), FVector(0, Side * 1580, 0), FVector(34, 1, 3.8f), false);
        Piece(Side < 0 ? TEXT("legacy_ember") : TEXT("legacy_dusk"), FVector(Side * 1450, 0, 16), FVector(1, 27, 1), false);
        for (int32 X = -1400; X <= 1400; X += 700)
        {
            Piece(TEXT("legacy_pillar"), FVector(X, Side * 1500, 0), FVector::OneVector, false);
            Piece(TEXT("legacy_orb"), FVector(X, Side * 1500, 685), FVector::OneVector, false);
        }
    }
    for (int32 N = 0; N < 32; ++N)
    {
        const float Angle = N * 2 * PI / 32;
        Piece(TEXT("legacy_ring"), FVector(FMath::Cos(Angle) * 550, FMath::Sin(Angle) * 550, 0), FVector::OneVector, false, FMath::RadiansToDegrees(Angle) + 90);
    }
    A.Lighting.SkyMaterial.Empty();
    A.Lighting.Lights.Add({FVector(-800, 0, 400), FLinearColor(.2f, .7f, .65f), 50000, 1600, false});
    A.Lighting.Lights.Add({FVector(800, 0, 400), FLinearColor(1.f, .25f, .15f), 50000, 1600, false});
    A.Ambience = TEXT("arena");
    return A;
}
bool UsesTownLighting(const FArena& A) { return A.Id == TEXT("sundered_court"); }

// ------------------------------------------------------------------ loading
void ParseSlot(const FString& Key, const TSharedPtr<FJsonObject>& O, FPool& P)
{
    FSlot S; S.Id = FName(*Key);
    const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
    if (O->TryGetArrayField(TEXT("candidates"), Candidates))
        for (const auto& V : *Candidates) { FString Path; if (V->TryGetString(Path) && !Path.IsEmpty()) S.Candidates.Add(Path); }
    S.Fallback = ShapePath(Str(O, TEXT("fallback"), TEXT("cube")));
    S.FallbackMaterial = Str(O, TEXT("fallbackMaterial"), StoneMaterial);
    const TSharedPtr<FJsonObject>* Materials = nullptr;
    if (O->TryGetObjectField(TEXT("materials"), Materials))
        for (const auto& Pair : (*Materials)->Values) { FString Path; if (Pair.Value->TryGetString(Path)) S.Materials.Add(FCString::Atoi(*FString(Pair.Key)), Path); }
    S.Footprint = Vec(O, TEXT("footprint"), FVector(100));
    const FString Fit = Str(O, TEXT("fit"), TEXT("footprint"));
    S.Fit = Fit == TEXT("uniform") ? EFit::Uniform : Fit == TEXT("none") ? EFit::None : EFit::Footprint;
    S.Shape = Str(O, TEXT("shape"), TEXT("box")) == TEXT("round") ? EShape::Round : EShape::Box;
    S.Offset = Vec(O, TEXT("offset"), FVector::ZeroVector);
    S.Yaw = Num(O, TEXT("yaw"), 0);
    S.bShadow = Bool(O, TEXT("shadow"), true);
    S.bEssential = Bool(O, TEXT("essential"), false);
    S.CullDistance = Num(O, TEXT("cull"), 0);
    S.Spin = Num(O, TEXT("spin"), 0);
    S.bWPO = Bool(O, TEXT("wpo"), false);
    S.bHidden = Bool(O, TEXT("hidden"), false);
    S.WPODistance = Num(O, TEXT("wpoDistance"), S.WPODistance);
    P.Slots.Add(S.Id, S);
}
void ParseLighting(const TSharedPtr<FJsonObject>& O, FLighting& L)
{
    if (!O.IsValid()) return;
    L.SunPitch = Num(O, TEXT("sunPitch"), L.SunPitch); L.SunYaw = Num(O, TEXT("sunYaw"), L.SunYaw);
    L.SunIntensity = Num(O, TEXT("sunIntensity"), L.SunIntensity); L.SunSourceAngle = Num(O, TEXT("sunSourceAngle"), L.SunSourceAngle);
    L.SunColor = Color(O, TEXT("sunColor"), L.SunColor);
    L.bLightShafts = Bool(O, TEXT("lightShafts"), L.bLightShafts); L.ShaftBloomScale = Num(O, TEXT("shaftBloomScale"), L.ShaftBloomScale);
    L.ShaftThreshold = Num(O, TEXT("shaftThreshold"), L.ShaftThreshold);
    L.VolumetricScattering = Num(O, TEXT("volumetricScattering"), L.VolumetricScattering);
    L.SkyMaterial = Str(O, TEXT("skyMaterial"), L.SkyMaterial); L.SkyTint = Color(O, TEXT("skyTint"), L.SkyTint);
    L.SkyBrightness = Num(O, TEXT("skyBrightness"), L.SkyBrightness); L.SkyYaw = Num(O, TEXT("skyYaw"), L.SkyYaw);
    L.SkyHaze = Color(O, TEXT("skyHaze"), L.SkyHaze); L.SkyHazeStrength = Num(O, TEXT("skyHazeStrength"), L.SkyHazeStrength);
    L.SkyLightIntensity = Num(O, TEXT("skyLightIntensity"), L.SkyLightIntensity); L.SkyLightColor = Color(O, TEXT("skyLightColor"), L.SkyLightColor);
    L.FogDensity = Num(O, TEXT("fogDensity"), L.FogDensity); L.FogFalloff = Num(O, TEXT("fogFalloff"), L.FogFalloff);
    L.FogStart = Num(O, TEXT("fogStart"), L.FogStart); L.FogHeight = Num(O, TEXT("fogHeight"), L.FogHeight);
    L.FogMaxOpacity = Num(O, TEXT("fogMaxOpacity"), L.FogMaxOpacity); L.FogColor = Color(O, TEXT("fogColor"), L.FogColor);
    L.bVolumetricFog = Bool(O, TEXT("volumetricFog"), L.bVolumetricFog);
    L.VolumetricDistribution = Num(O, TEXT("volumetricDistribution"), L.VolumetricDistribution);
    L.VolumetricExtinction = Num(O, TEXT("volumetricExtinction"), L.VolumetricExtinction);
    L.VolumetricAlbedo = Color(O, TEXT("volumetricAlbedo"), L.VolumetricAlbedo);
    L.ExposureBias = Num(O, TEXT("exposureBias"), L.ExposureBias); L.Saturation = Num(O, TEXT("saturation"), L.Saturation);
    L.Contrast = Num(O, TEXT("contrast"), L.Contrast); L.Temperature = Num(O, TEXT("temperature"), L.Temperature);
    L.Bloom = Num(O, TEXT("bloom"), L.Bloom); L.Vignette = Num(O, TEXT("vignette"), L.Vignette);
    L.Gain = Color(O, TEXT("gain"), L.Gain); L.ShadowTint = Color(O, TEXT("shadowTint"), L.ShadowTint);
    L.LightFunction = Str(O, TEXT("lightFunction"), L.LightFunction); L.LightFunctionScale = Num(O, TEXT("lightFunctionScale"), L.LightFunctionScale);
    const TArray<TSharedPtr<FJsonValue>>* Lights = nullptr;
    if (O->TryGetArrayField(TEXT("lights"), Lights))
        for (const auto& V : *Lights)
        {
            const TSharedPtr<FJsonObject>* LO = nullptr; if (!V->TryGetObject(LO)) continue;
            FPointLight PL; PL.Location = Vec(*LO, TEXT("at"), FVector::ZeroVector); PL.Color = Color(*LO, TEXT("color"), PL.Color);
            PL.Intensity = Num(*LO, TEXT("intensity"), PL.Intensity); PL.Radius = Num(*LO, TEXT("radius"), PL.Radius);
            PL.bShadows = Bool(*LO, TEXT("shadows"), false); L.Lights.Add(PL);
        }
}
FBox2D Box2(const TArray<double>& A)
{
    return A.Num() >= 4 ? FBox2D(FVector2D(FMath::Min(A[0], A[2]), FMath::Min(A[1], A[3])), FVector2D(FMath::Max(A[0], A[2]), FMath::Max(A[1], A[3]))) : FBox2D(ForceInit);
}
bool ParseArena(const TSharedPtr<FJsonObject>& O, FArena& A, TArray<FString>& Errors)
{
    A.Id = FName(*Str(O, TEXT("id"))); A.Name = Str(O, TEXT("name")); A.Theme = Str(O, TEXT("theme")); A.Subtitle = Str(O, TEXT("subtitle"));
    A.bEnabled = Bool(O, TEXT("enabled"), true); A.Weight = Num(O, TEXT("weight"), 1);
    A.HalfExtents = Vec2(O, TEXT("halfExtents"), A.HalfExtents);
    A.Ground = FName(*Str(O, TEXT("ground"))); A.Underlay = FName(*Str(O, TEXT("underlay")));
    A.GroundSize = Num(O, TEXT("groundSize"), A.GroundSize);
    A.Ambience = FName(*Str(O, TEXT("ambience"), TEXT("arena"))); A.Music = Str(O, TEXT("music"));
    const TSharedPtr<FJsonObject>* Spawns = nullptr;
    if (O->TryGetObjectField(TEXT("spawns"), Spawns))
        for (int32 Team = 0; Team < 2; ++Team)
        {
            const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
            if ((*Spawns)->TryGetArrayField(Team == 0 ? TEXT("ember") : TEXT("dusk"), List))
                for (const auto& V : *List)
                {
                    const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
                    if (V->TryGetArray(XY)) { const TArray<double> N = Numbers(*XY); if (N.Num() >= 2) A.Spawns[Team].Add(FVector2D(N[0], N[1])); }
                }
        }
    const TArray<TSharedPtr<FJsonValue>>* Pieces = nullptr;
    if (O->TryGetArrayField(TEXT("pieces"), Pieces))
        for (const auto& V : *Pieces)
        {
            // Compact row: [slot, x, y, z, yaw, sx, sy, sz, blocker]
            const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
            if (!V->TryGetArray(Row) || Row->Num() < 9) { Errors.Add(FString::Printf(TEXT("%s: malformed piece row"), *A.Id.ToString())); continue; }
            FPiece P; FString Slot; (*Row)[0]->TryGetString(Slot); P.Slot = FName(*Slot);
            TArray<TSharedPtr<FJsonValue>> Rest((*Row).GetData() + 1, Row->Num() - 1);
            const TArray<double> N = Numbers(Rest);
            P.Location = FVector(N[0], N[1], N[2]); P.Yaw = N[3]; P.Scale = FVector(N[4], N[5], N[6]); P.bBlocker = N[7] > .5;
            if (P.Location.ContainsNaN() || P.Scale.ContainsNaN() || !FMath::IsFinite(P.Yaw) || P.Scale.GetMin() <= 0)
            { Errors.Add(FString::Printf(TEXT("%s: invalid piece values for %s"), *A.Id.ToString(), *Slot)); continue; }
            A.Pieces.Add(P);
        }
    const TArray<TSharedPtr<FJsonValue>>* Scatter = nullptr;
    if (O->TryGetArrayField(TEXT("scatter"), Scatter))
        for (const auto& V : *Scatter)
        {
            const TSharedPtr<FJsonObject>* SO = nullptr; if (!V->TryGetObject(SO)) continue;
            FScatter S; S.Slot = FName(*Str(*SO, TEXT("slot"))); S.Region = Box2(Array(*SO, TEXT("region")));
            S.Count = FMath::Clamp(static_cast<int32>(Num(*SO, TEXT("count"), 0)), 0, 60000); S.Seed = static_cast<int32>(Num(*SO, TEXT("seed"), 1));
            S.ScaleRange = Vec2(*SO, TEXT("scale"), FVector2D(1, 1)); S.ZJitter = Num(*SO, TEXT("zJitter"), 0); S.Z = Num(*SO, TEXT("z"), 0);
            S.bOutsideBounds = Bool(*SO, TEXT("outside"), false); S.BoundsMargin = Num(*SO, TEXT("margin"), 0);
            S.Clearance = Num(*SO, TEXT("clearance"), 0); S.bRandomYaw = Bool(*SO, TEXT("randomYaw"), true); S.bTilt = Bool(*SO, TEXT("tilt"), false);
            const TArray<TSharedPtr<FJsonValue>>* Ex = nullptr;
            if ((*SO)->TryGetArrayField(TEXT("exclude"), Ex))
                for (const auto& E : *Ex) { const TArray<TSharedPtr<FJsonValue>>* R = nullptr; if (E->TryGetArray(R)) S.Exclude.Add(Box2(Numbers(*R))); }
            A.Scatter.Add(S);
        }
    const TSharedPtr<FJsonObject>* Lighting = nullptr;
    if (O->TryGetObjectField(TEXT("lighting"), Lighting)) ParseLighting(*Lighting, A.Lighting);
    const TSharedPtr<FJsonObject>* Minimap = nullptr;
    if (O->TryGetObjectField(TEXT("minimap"), Minimap))
    {
        A.MinimapGround = Color(*Minimap, TEXT("ground"), A.MinimapGround);
        A.MinimapBlocker = Color(*Minimap, TEXT("blocker"), A.MinimapBlocker);
        A.MinimapAccent = Color(*Minimap, TEXT("accent"), A.MinimapAccent);
    }
    return !A.Id.IsNone();
}
void Load(FPool& P)
{
    P = FPool();
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Arenas.json"));
    FString Text; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Path) || Text.Len() > 4 * 1024 * 1024 ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        P.Errors.Add(TEXT("Arenas.json missing or malformed; using the legacy court"));
    else if (static_cast<int32>(Num(Root, TEXT("schemaVersion"), 0)) != 1)
        P.Errors.Add(TEXT("Arenas.json schemaVersion 1 expected; using the legacy court"));
    else
    {
        P.Origin = Vec(Root, TEXT("origin"), P.Origin);
        const TSharedPtr<FJsonObject>* Slots = nullptr;
        if (Root->TryGetObjectField(TEXT("slots"), Slots))
            for (const auto& Pair : (*Slots)->Values) { const TSharedPtr<FJsonObject>* SO = nullptr; if (Pair.Value->TryGetObject(SO)) ParseSlot(FString(Pair.Key), *SO, P); }
        const TArray<TSharedPtr<FJsonValue>>* Arenas = nullptr;
        if (Root->TryGetArrayField(TEXT("arenas"), Arenas))
            for (const auto& V : *Arenas)
            {
                const TSharedPtr<FJsonObject>* AO = nullptr; if (!V->TryGetObject(AO)) continue;
                FArena A; if (ParseArena(*AO, A, P.Errors)) P.Arenas.Add(MoveTemp(A));
            }
    }
    AddLegacySlots(P);
    P.Arenas.Add(LegacyCourt());
    P.FallbackIndex = P.Arenas.Num() - 1;
    for (FArena& A : P.Arenas)
    {
        TSet<FName> Used;
        for (const FPiece& Piece : A.Pieces) Used.Add(Piece.Slot);
        for (const FScatter& S : A.Scatter) Used.Add(S.Slot);
        for (const FName Id : Used) if (const FSlot* S = P.Slots.Find(Id); S && S->bEssential) A.Essential.Add(Id);
    }
    P.bValid = true;
    for (const FString& E : P.Errors) UE_LOG(LogCireArenas, Warning, TEXT("CIRE_ARENA_DATA %s"), *E);
}

// ------------------------------------------------------------------ geometry
FVector2D Rotate2(const FVector2D& V, float Degrees)
{
    const float R = FMath::DegreesToRadians(Degrees);
    return FVector2D(V.X * FMath::Cos(R) - V.Y * FMath::Sin(R), V.X * FMath::Sin(R) + V.Y * FMath::Cos(R));
}
float DistanceTo(const FFootprint& F, const FVector2D& Point)
{
    const FVector2D Local = Rotate2(Point - F.Center, -F.Yaw);
    if (F.bRound) return FMath::Max(0.f, static_cast<float>(Local.Size()) - F.Extent.X);
    const FVector2D D(FMath::Max(0.0, FMath::Abs(Local.X) - F.Extent.X), FMath::Max(0.0, FMath::Abs(Local.Y) - F.Extent.Y));
    return D.Size();
}
float NormalizeYaw180(float Yaw) { float Y = FMath::Fmod(Yaw, 180.f); if (Y < 0) Y += 180.f; return Y; }
}

// =================================================================== public data API
const FPool& CireArenas::Pool(bool bReload)
{
    if (!GLoaded || bReload) { Load(GPool); GLoaded = true; GRotationValid = false; }
    return GPool;
}
const FArena* CireArenas::Get(int32 Index) { const FPool& P = Pool(); return P.Arenas.IsValidIndex(Index) ? &P.Arenas[Index] : nullptr; }
FVector CireArenas::Origin() { return Pool().Origin; }

TArray<CireArenas::FFootprint> CireArenas::Footprints(const FArena& Arena, const FPool& P)
{
    TArray<FFootprint> Out;
    for (const FPiece& Piece : Arena.Pieces)
    {
        if (!Piece.bBlocker) continue;
        const FSlot* Slot = P.Slots.Find(Piece.Slot); if (!Slot) continue;
        FFootprint F; F.Center = FVector2D(Piece.Location); F.Yaw = Piece.Yaw; F.bRound = Slot->Shape == EShape::Round;
        const FVector Size = Slot->Footprint * Piece.Scale;
        F.Extent = F.bRound ? FVector2D(FMath::Max(Size.X, Size.Y) * .5f, FMath::Max(Size.X, Size.Y) * .5f) : FVector2D(Size.X * .5f, Size.Y * .5f);
        F.Height = Size.Z + Piece.Location.Z;
        Out.Add(F);
    }
    return Out;
}

bool CireArenas::SpawnsConnected(const FArena& A, const FPool& P, float Radius, FString* OutError, float* OutFraction)
{
    const TArray<FFootprint> Blockers = Footprints(A, P);
    const int32 W = FMath::CeilToInt(2 * A.HalfExtents.X / CellSize), H = FMath::CeilToInt(2 * A.HalfExtents.Y / CellSize);
    if (W <= 0 || H <= 0 || W * H > 2000000) { if (OutError) *OutError = TEXT("bounds grid invalid"); return false; }
    TArray<uint8> Free; Free.SetNumZeroed(W * H);
    auto CellCenter = [&](int32 X, int32 Y) { return FVector2D(-A.HalfExtents.X + (X + .5f) * CellSize, -A.HalfExtents.Y + (Y + .5f) * CellSize); };
    int32 FreeCount = 0;
    for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
    {
        const FVector2D C = CellCenter(X, Y);
        bool bFree = FMath::Abs(C.X) <= A.HalfExtents.X - Radius && FMath::Abs(C.Y) <= A.HalfExtents.Y - Radius;
        for (int32 I = 0; bFree && I < Blockers.Num(); ++I) if (DistanceTo(Blockers[I], C) < Radius) bFree = false;
        Free[Y * W + X] = bFree; FreeCount += bFree;
    }
    auto CellOf = [&](const FVector2D& Pt) { return FIntPoint(FMath::Clamp(FMath::FloorToInt((Pt.X + A.HalfExtents.X) / CellSize), 0, W - 1), FMath::Clamp(FMath::FloorToInt((Pt.Y + A.HalfExtents.Y) / CellSize), 0, H - 1)); };
    auto Flood = [&](FIntPoint Start, TArray<uint8>& Seen)
    {
        Seen.SetNumZeroed(W * H); TArray<FIntPoint> Queue; int32 Count = 0;
        if (!Free[Start.Y * W + Start.X]) return 0;
        Queue.Add(Start); Seen[Start.Y * W + Start.X] = 1;
        for (int32 Head = 0; Head < Queue.Num(); ++Head)
        {
            const FIntPoint C = Queue[Head]; ++Count;
            const FIntPoint N[] = {{C.X + 1, C.Y}, {C.X - 1, C.Y}, {C.X, C.Y + 1}, {C.X, C.Y - 1}};
            for (const FIntPoint& Q : N)
                if (Q.X >= 0 && Q.Y >= 0 && Q.X < W && Q.Y < H && Free[Q.Y * W + Q.X] && !Seen[Q.Y * W + Q.X]) { Seen[Q.Y * W + Q.X] = 1; Queue.Add(Q); }
        }
        return Count;
    };
    if (A.Spawns[0].IsEmpty() || A.Spawns[1].IsEmpty()) { if (OutError) *OutError = TEXT("no spawns"); return false; }
    TArray<uint8> Seen; const int32 Reached = Flood(CellOf(A.Spawns[0][0]), Seen);
    if (OutFraction) *OutFraction = FreeCount > 0 ? static_cast<float>(Reached) / FreeCount : 0.f;
    for (int32 Team = 0; Team < 2; ++Team) for (const FVector2D& S : A.Spawns[Team])
    {
        const FIntPoint C = CellOf(S);
        if (!Seen[C.Y * W + C.X]) { if (OutError) *OutError = FString::Printf(TEXT("spawn (%.0f,%.0f) is not connected to Ember spawn 0"), S.X, S.Y); return false; }
    }
    const FIntPoint Mid = CellOf(FVector2D::ZeroVector);
    bool bCentre = false;
    for (int32 DY = -4; DY <= 4 && !bCentre; ++DY) for (int32 DX = -4; DX <= 4 && !bCentre; ++DX)
    {
        const int32 X = Mid.X + DX, Y = Mid.Y + DY;
        if (X >= 0 && Y >= 0 && X < W && Y < H && Seen[Y * W + X]) bCentre = true;
    }
    if (!bCentre) { if (OutError) *OutError = TEXT("the arena centre is unreachable from the spawns"); return false; }
    return true;
}

TArray<FString> CireArenas::Validate(const FArena& A, const FPool& P)
{
    TArray<FString> E;
    auto Err = [&](const FString& S) { E.Add(FString::Printf(TEXT("%s: %s"), *A.Id.ToString(), *S)); };
    if (A.Name.IsEmpty()) Err(TEXT("missing name"));
    if (A.HalfExtents.X < 1000 || A.HalfExtents.Y < 1000 || A.HalfExtents.X > 6000 || A.HalfExtents.Y > 6000) Err(TEXT("halfExtents outside 1000..6000 cm"));
    for (int32 Team = 0; Team < 2; ++Team)
        if (A.Spawns[Team].Num() != 5) Err(FString::Printf(TEXT("team %d needs exactly 5 spawns (has %d)"), Team, A.Spawns[Team].Num()));
    if (A.Spawns[0].Num() == A.Spawns[1].Num())
        for (int32 I = 0; I < A.Spawns[0].Num(); ++I)
        {
            const FVector2D S0 = A.Spawns[0][I], S1 = A.Spawns[1][I];
            if (!S0.Equals(FVector2D(-S1.X, S1.Y), 1.0)) Err(FString::Printf(TEXT("spawn %d is not mirrored across the centre line"), I));
            if (S0.X >= -300) Err(FString::Printf(TEXT("Ember spawn %d must lie on the west half"), I));
        }
    const TArray<FFootprint> Blockers = Footprints(A, P);
    for (int32 Team = 0; Team < 2; ++Team) for (int32 I = 0; I < A.Spawns[Team].Num(); ++I)
    {
        const FVector2D S = A.Spawns[Team][I];
        if (FMath::Abs(S.X) > A.HalfExtents.X - SpawnClearance || FMath::Abs(S.Y) > A.HalfExtents.Y - SpawnClearance) Err(FString::Printf(TEXT("team %d spawn %d outside bounds"), Team, I));
        for (const FFootprint& F : Blockers) if (DistanceTo(F, S) < SpawnClearance) { Err(FString::Printf(TEXT("team %d spawn %d is crowded by a blocker"), Team, I)); break; }
        for (int32 J = I + 1; J < A.Spawns[Team].Num(); ++J) if (FVector2D::Distance(S, A.Spawns[Team][J]) < 120) Err(FString::Printf(TEXT("team %d spawns %d/%d overlap"), Team, I, J));
    }
    // Fairness: every blocker has a mirror twin across the centre line (same shape, size, height).
    for (const FFootprint& F : Blockers)
    {
        if (FMath::Abs(F.Center.X) < 1.f && (F.bRound || FMath::IsNearlyZero(NormalizeYaw180(F.Yaw)) || FMath::IsNearlyEqual(NormalizeYaw180(F.Yaw), 90.f, .5f))) continue;
        bool bTwin = false;
        for (const FFootprint& G : Blockers)
        {
            if (G.bRound != F.bRound || !G.Center.Equals(FVector2D(-F.Center.X, F.Center.Y), 5.0) || !FMath::IsNearlyEqual(G.Height, F.Height, 2.f)) continue;
            if (F.bRound) { if (FMath::IsNearlyEqual(G.Extent.X, F.Extent.X, 2.f)) { bTwin = true; break; } continue; }
            const float Want = NormalizeYaw180(-F.Yaw), Got = NormalizeYaw180(G.Yaw);
            const bool bYaw = FMath::Abs(Want - Got) < 1.f || FMath::Abs(Want - Got) > 179.f;
            if (bYaw && G.Extent.Equals(F.Extent, 2.0)) { bTwin = true; break; }
        }
        if (!bTwin) Err(FString::Printf(TEXT("blocker at (%.0f,%.0f) has no mirrored twin"), F.Center.X, F.Center.Y));
    }
    for (const FPiece& Piece : A.Pieces) if (!P.Slots.Contains(Piece.Slot)) { Err(FString::Printf(TEXT("unknown slot %s"), *Piece.Slot.ToString())); break; }
    for (const FScatter& S : A.Scatter)
    {
        if (!P.Slots.Contains(S.Slot)) Err(FString::Printf(TEXT("unknown scatter slot %s"), *S.Slot.ToString()));
        if (!S.Region.bIsValid) Err(TEXT("scatter region invalid"));
    }
    if (!A.Ground.IsNone() && !P.Slots.Contains(A.Ground)) Err(TEXT("unknown ground slot"));
    const FLighting& L = A.Lighting;
    if (L.SunPitch > 0 || L.SunPitch < -90 || L.SunIntensity < 0 || L.SunIntensity > 150 || L.FogDensity < 0 || L.FogDensity > .5f ||
        L.SkyLightIntensity < 0 || L.SkyLightIntensity > 20 || L.ExposureBias < -8 || L.ExposureBias > 8) Err(TEXT("lighting preset out of range"));
    for (const FPointLight& Light : L.Lights) if (Light.Intensity < 0 || Light.Radius <= 0) { Err(TEXT("bad point light")); break; }
    FString PathError; float Fraction = 0;
    if (!SpawnsConnected(A, P, CapsuleRadius + 5.f, &PathError, &Fraction)) Err(TEXT("no path between spawns: ") + PathError);
    else if (Fraction < .9f) Err(FString::Printf(TEXT("only %.0f%% of the floor is reachable (enclosed pockets)"), Fraction * 100));
    return E;
}

TArray<int32> CireArenas::Rotation()
{
    // Validation walks a path grid per arena, so the result is cached per pool load.
    static TArray<int32> Cached; static const FPool* CachedFor = nullptr; static int32 CachedArenas = -1;
    const FPool& P = Pool();
    if (CachedFor == &P && CachedArenas == P.Arenas.Num() && GRotationValid) return Cached;
    TArray<int32> Out;
    for (int32 I = 0; I < P.Arenas.Num(); ++I)
    {
        const FArena& A = P.Arenas[I];
        if (!A.bEnabled || A.bFallbackOnly || A.Weight <= 0 || Validate(A, P).Num() > 0) continue;
        bool bAssets = true;
        for (const FName Id : A.Essential)
        {
            const FSlot& S = P.Slots[Id]; bool bAny = false;
            for (const FString& C : S.Candidates) if (CandidateExists(C) && !C.StartsWith(TEXT("/Engine/"))) { bAny = true; break; }
            if (!bAny) { bAssets = false; UE_LOG(LogCireArenas, Warning, TEXT("CIRE_ARENA_ASSETS_MISSING arena=%s slot=%s (left out of the rotation)"), *A.Id.ToString(), *Id.ToString()); break; }
        }
        if (bAssets) Out.Add(I);
    }
    Cached = Out; CachedFor = &P; CachedArenas = P.Arenas.Num(); GRotationValid = true;
    return Out;
}

int32 CireArenas::PickNext(int32 Previous, FRandomStream* Stream)
{
    const TArray<int32> Options = Rotation();
    if (Options.IsEmpty()) return Pool().FallbackIndex;
    TArray<int32> Choices; float Total = 0;
    for (int32 I : Options) if (I != Previous || Options.Num() == 1) { Choices.Add(I); Total += Pool().Arenas[I].Weight; }
    const float Roll = (Stream ? Stream->FRand() : FMath::FRand()) * Total;
    float Acc = 0;
    for (int32 I : Choices) { Acc += Pool().Arenas[I].Weight; if (Roll < Acc) return I; }
    return Choices.Last();
}

// =================================================================== world queries
int32 CireArenas::CurrentIndex(const UWorld* World)
{
    if (!World) return INDEX_NONE;
    if (const UCireArenaSubsystem* Sub = World->GetSubsystem<UCireArenaSubsystem>(); Sub && Sub->bForced) return Sub->ForcedIndex;
    if (const auto* Mode = World->GetAuthGameMode<ACireGameMode>()) return Mode->ArenaIndex;
    const auto* State = World->GetGameState<ACireGameState>();
    return State ? State->ArenaIndex : INDEX_NONE;
}
const FArena* CireArenas::Current(const UWorld* World) { return Get(CurrentIndex(World)); }
FVector CireArenas::Center(const UWorld*) { return Origin(); }
FVector2D CireArenas::HalfExtents(const UWorld* World)
{
    const FArena* A = Current(World); return A ? A->HalfExtents : FVector2D(1540, 1470);
}
bool CireArenas::InBounds(const UWorld* World, const FVector& Point, float Margin)
{
    const FVector C = Center(World); const FVector2D H = HalfExtents(World);
    return FMath::Abs(Point.X - C.X) <= H.X - Margin && FMath::Abs(Point.Y - C.Y) <= H.Y - Margin;
}
FVector CireArenas::SpawnLocation(int32 Index, int32 Team, int32 Slot, float Z)
{
    const FArena* A = Get(Index); if (!A) A = Get(Pool().FallbackIndex);
    const TArray<FVector2D>& List = A->Spawns[FMath::Clamp(Team, 0, 1)];
    const FVector2D S = List.IsEmpty() ? FVector2D(Team == 0 ? -700 : 700, 0) : List[((Slot % List.Num()) + List.Num()) % List.Num()];
    return Origin() + FVector(S.X, S.Y, Z);
}
FString CireArenas::DisplayName(int32 Index) { const FArena* A = Get(Index); return A ? A->Name : FString(TEXT("The Portal Battlefield")); }
FName CireArenas::ActiveAmbience(const UWorld* World)
{
    const auto* State = World ? World->GetGameState<ACireGameState>() : nullptr;
    const FArena* A = Current(World);
    return A && State && State->Phase == 2 ? A->Ambience : NAME_None;
}
FString CireArenas::ActiveMusic(const UWorld* World)
{
    const auto* State = World ? World->GetGameState<ACireGameState>() : nullptr;
    const FArena* A = Current(World);
    return A && State && State->Phase == 2 ? A->Music : FString();
}

// =================================================================== server flow
void CireArenas::ServerPrepare(ACireGameMode* Mode)
{
    if (!Mode) return;
    FServerState& S = GServer.FindOrAdd(Mode->GetWorld());
    const int32 Next = PickNext(S.Last);
    S.Last = Next; S.bPending = true;
    Mode->ArenaIndex = Next;
    if (auto* State = Mode->GetGameState<ACireGameState>()) State->ArenaIndex = Next;
    UE_LOG(LogCireArenas, Display, TEXT("CIRE_ARENA_PICK index=%d id=%s name=\"%s\""), Next, *Get(Next)->Id.ToString(), *Get(Next)->Name);
    Sync(Mode->GetWorld());
}
void CireArenas::ServerBegin(ACireGameMode* Mode)
{
    if (!Mode) return;
    FServerState& S = GServer.FindOrAdd(Mode->GetWorld());
    if (!S.bPending || !Get(Mode->ArenaIndex)) ServerPrepare(Mode);
    S.bPending = false;
    Sync(Mode->GetWorld());
}
void CireArenas::Sync(UWorld* World)
{
    if (auto* Sub = World ? World->GetSubsystem<UCireArenaSubsystem>() : nullptr) Sub->Tick(0.f);
}
void CireArenas::Force(UWorld* World, int32 Index, bool bVisible)
{
    auto* Sub = World ? World->GetSubsystem<UCireArenaSubsystem>() : nullptr; if (!Sub) return;
    Sub->bForced = true; Sub->ForcedIndex = Index; Sub->bForcedShow = bVisible; Sub->Tick(0.f);
}
void CireArenas::ReleaseForce(UWorld* World)
{
    auto* Sub = World ? World->GetSubsystem<UCireArenaSubsystem>() : nullptr; if (!Sub) return;
    Sub->bForced = false; Sub->ForcedIndex = INDEX_NONE; Sub->Tick(0.f);
}
ACireArenaStage* CireArenas::Stage(const UWorld* World)
{
    const auto* Sub = World ? World->GetSubsystem<UCireArenaSubsystem>() : nullptr; return Sub ? Sub->GetStage() : nullptr;
}

// =================================================================== stage actor
ACireArenaStage::ACireArenaStage()
{
    bReplicates = false; SetReplicatingMovement(false); bNetLoadOnClient = false;
    PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.bStartWithTickEnabled = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("ArenaRoot"));
    RootComponent->SetMobility(EComponentMobility::Static); // static children (cached shadows) must hang off a static root
    Tags.Add(TEXT("CireArena"));
}

namespace
{
struct FResolved { TArray<UStaticMesh*> Meshes; FTransform Local = FTransform::Identity; bool bFallback = false; TArray<UMaterialInterface*> Materials; };
FResolved Resolve(const FSlot& Slot)
{
    FResolved R;
    for (const FString& Candidate : Slot.Candidates)
    {
        if (!CandidateExists(Candidate)) continue;
        TArray<FString> Parts; Candidate.ParseIntoArray(Parts, TEXT("|"));
        TArray<UStaticMesh*> Loaded;
        for (const FString& Part : Parts) if (auto* Mesh = LoadObject<UStaticMesh>(nullptr, *Part)) Loaded.Add(Mesh);
        if (Loaded.Num() == Parts.Num()) { R.Meshes = Loaded; break; }
    }
    if (R.Meshes.IsEmpty())
    {
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Slot.Fallback);
        R.Meshes.Add(Mesh ? Mesh : LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        R.bFallback = true;
    }
    if (R.bFallback) if (auto* M = LoadObject<UMaterialInterface>(nullptr, *Slot.FallbackMaterial)) R.Materials.Add(M);
    if (!R.bFallback)
        for (const auto& Pair : Slot.Materials)
        {
            R.Materials.SetNum(FMath::Max(R.Materials.Num(), Pair.Key + 1));
            R.Materials[Pair.Key] = LoadObject<UMaterialInterface>(nullptr, *Pair.Value);
        }
    // Fit: seat the (multi-part) mesh on the ground, centred in XY, scaled into the authored footprint.
    FBox B(ForceInit);
    for (UStaticMesh* Mesh : R.Meshes) B += Mesh->GetBoundingBox();
    const bool bQuarter = FMath::IsNearlyEqual(FMath::Abs(FMath::Fmod(Slot.Yaw, 180.f)), 90.f, 1.f);
    FVector Size = B.GetSize(); if (bQuarter) Swap(Size.X, Size.Y);
    Size = Size.ComponentMax(FVector(1));
    FVector Scale = FVector::OneVector;
    if (Slot.Fit == EFit::Footprint || R.bFallback) Scale = Slot.Footprint / Size;
    else if (Slot.Fit == EFit::Uniform) Scale = FVector((Slot.Footprint / Size).GetMin());
    if (bQuarter) Swap(Scale.X, Scale.Y);
    const FVector Centre = B.GetCenter();
    const FTransform Seat(FQuat::Identity, FVector(-Centre.X * Scale.X, -Centre.Y * Scale.Y, -B.Min.Z * Scale.Z), Scale);
    R.Local = Slot.Fit == EFit::None && !R.bFallback ? FTransform(FRotator(0, Slot.Yaw, 0), Slot.Offset)
        : Seat * FTransform(FRotator(0, Slot.Yaw, 0), Slot.Offset);
    return R;
}
}

void ACireArenaStage::BuildArena(int32 Index, bool bWithVisuals)
{
    const FPool& P = CireArenas::Pool();
    const FArena* A = CireArenas::Get(Index);
    if (!A) return;
    ArenaIndex = Index; bVisuals = bWithVisuals;
    const double BuildStarted = FPlatformTime::Seconds();
    const FVector O = GetActorLocation();
    TSet<FName> Skip; // development: -CireArenaSkip=slot,slot hides slots to isolate art problems
#if !UE_BUILD_SHIPPING
    { FString List; if (FParse::Value(FCommandLine::Get(), TEXT("CireArenaSkip="), List)) { TArray<FString> Ids; List.ParseIntoArray(Ids, TEXT(",")); for (const FString& Id : Ids) Skip.Add(FName(*Id)); } }
#endif
    TMap<FName, TArray<UHierarchicalInstancedStaticMeshComponent*>> Components;
    TMap<FName, FResolved> Resolved;
    auto Parts = [&](FName SlotId) -> const TArray<UHierarchicalInstancedStaticMeshComponent*>*
    {
        if (auto* Found = Components.Find(SlotId)) return Found;
        const FSlot* Slot = P.Slots.Find(SlotId); if (!Slot) return nullptr;
        FResolved R = Resolve(*Slot);
        if (R.bFallback && Slot->Candidates.Num() > 0) { ++FallbackSlots; MissingSlots.AddUnique(SlotId); }
        TArray<UHierarchicalInstancedStaticMeshComponent*> List;
        for (int32 Part = 0; Part < R.Meshes.Num(); ++Part)
        {
            auto* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, FName(*FString::Printf(TEXT("Arena_%s_%d"), *SlotId.ToString(), Part)));
            C->SetupAttachment(RootComponent); C->SetMobility(EComponentMobility::Static);
            C->SetStaticMesh(R.Meshes[Part]);
            for (int32 I = 0; I < R.Materials.Num(); ++I) if (R.Materials[I]) C->SetMaterial(I, R.Materials[I]);
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(Slot->bShadow);
            C->bAffectDistanceFieldLighting = Slot->bShadow;
            if (Slot->CullDistance > 0) { C->InstanceStartCullDistance = Slot->CullDistance * .8f; C->InstanceEndCullDistance = Slot->CullDistance; }
            C->bEvaluateWorldPositionOffset = Slot->bWPO;
            if (Slot->bWPO) C->SetWorldPositionOffsetDisableDistance(FMath::RoundToInt(Slot->WPODistance));
            C->ComponentTags.Add(SlotId);
            C->RegisterComponent(); AddInstanceComponent(C);
            List.Add(C);
        }
        UE_LOG(LogCireArenas, Verbose, TEXT("CIRE_ARENA_SLOT slot=%s mesh=%s parts=%d local=%s fallback=%d"), *SlotId.ToString(),
            *GetNameSafe(R.Meshes.Num() ? R.Meshes[0] : nullptr), R.Meshes.Num(), *R.Local.ToString(), R.bFallback ? 1 : 0);
        Resolved.Add(SlotId, R);
        return &Components.Add(SlotId, List);
    };
    auto AddTo = [&](FName SlotId, const FTransform& Instance)
    {
        if (const auto* List = Parts(SlotId)) for (auto* C : *List) C->AddInstance(Instance, false);
    };

    // Collision proxies: invisible boxes/cylinders matching each blocker's footprint exactly.
    auto Proxy = [&](const TCHAR* Name, const TCHAR* Mesh)
    {
        auto* C = NewObject<UInstancedStaticMeshComponent>(this, Name);
        C->SetupAttachment(RootComponent); C->SetMobility(EComponentMobility::Static);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
        C->SetCollisionObjectType(ECC_WorldStatic); C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        C->SetCollisionResponseToAllChannels(ECR_Block); C->SetHiddenInGame(true); C->SetVisibility(false); C->SetCastShadow(false);
        // Pure collision: keep it out of shadows, Lumen's distance-field scene, ray tracing and GI.
        C->bAffectDistanceFieldLighting = false; C->bAffectDynamicIndirectLighting = false; C->bAffectIndirectLightingWhileHidden = false;
        C->bCastHiddenShadow = false; C->SetVisibleInRayTracing(false); C->bVisibleInReflectionCaptures = false; C->bVisibleInRealTimeSkyCaptures = false;
        C->ComponentTags.Add(TEXT("CireArenaCollision"));
        C->RegisterComponent(); AddInstanceComponent(C); return C;
    };
    auto* BoxProxy = Proxy(TEXT("ArenaBlockBoxes"), TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* RoundProxy = Proxy(TEXT("ArenaBlockCylinders"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Bounds = Proxy(TEXT("ArenaBounds"), TEXT("/Engine/BasicShapes/Cube.Cube"));
    Bounds->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore); Bounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

    // Floor: a solid collision slab plus the visual ground surface.
    const FVector2D H = A->HalfExtents;
    if (!Skip.Contains(TEXT("bounds_floor"))) Bounds->AddInstance(FTransform(FRotator::ZeroRotator, O + FVector(0, 0, -100), FVector((2 * H.X + 6000) / 100, (2 * H.Y + 6000) / 100, 2)), true);
    if (!Skip.Contains(TEXT("bounds_walls"))) for (int32 Side : {-1, 1})
    {
        Bounds->AddInstance(FTransform(FRotator::ZeroRotator, O + FVector(Side * (H.X + 50), 0, 1000), FVector(1, (2 * H.Y + 200) / 100, 20)), true);
        Bounds->AddInstance(FTransform(FRotator::ZeroRotator, O + FVector(0, Side * (H.Y + 50), 1000), FVector((2 * H.X + 200) / 100, 1, 20)), true);
    }
    auto Surface = [&](FName SlotId, float Z, float Size)
    {
        if (!bWithVisuals || SlotId.IsNone() || Skip.Contains(SlotId) || !Parts(SlotId)) return;
        for (auto* C : Components[SlotId]) C->SetCastShadow(false);
        AddTo(SlotId, Resolved[SlotId].Local * FTransform(FRotator::ZeroRotator, FVector(0, 0, Z), FVector(Size / 100, Size / 100, 1)));
        ++InstanceCount;
    };
    Surface(A->Ground, -2.5f, A->GroundSize);
    Surface(A->Underlay, -12.f, A->GroundSize * 4);

    for (const FPiece& Piece : A->Pieces)
    {
        const FSlot* Slot = P.Slots.Find(Piece.Slot); if (!Slot) continue;
        if (Piece.bBlocker)
        {
            const FVector Size = Slot->Footprint * Piece.Scale;
            const FVector At(Piece.Location.X, Piece.Location.Y, Piece.Location.Z + Size.Z * .5f);
            if (Slot->Shape == EShape::Round)
            { const float D = FMath::Max(Size.X, Size.Y); RoundProxy->AddInstance(FTransform(FRotator(0, Piece.Yaw, 0), At, FVector(D / 100, D / 100, Size.Z / 100)), false); }
            else BoxProxy->AddInstance(FTransform(FRotator(0, Piece.Yaw, 0), At, Size / 100), false);
            ++BlockerCount;
        }
        if (!bWithVisuals || Slot->bHidden || Skip.Contains(Piece.Slot)) continue; // dedicated servers only need the collision proxies
        const auto* List = Parts(Piece.Slot); if (!List) continue;
        const FTransform World = Resolved[Piece.Slot].Local * FTransform(FRotator(0, Piece.Yaw, 0), Piece.Location, Piece.Scale);
        if (Slot->Spin != 0)
        {
            for (auto* C : *List)
            {
                auto* S = NewObject<UStaticMeshComponent>(this); S->SetMobility(EComponentMobility::Movable); S->SetupAttachment(RootComponent);
                S->SetStaticMesh(C->GetStaticMesh()); for (int32 I = 0; I < C->GetNumMaterials(); ++I) S->SetMaterial(I, C->GetMaterial(I));
                S->SetCollisionEnabled(ECollisionEnabled::NoCollision); S->SetRelativeTransform(World); S->RegisterComponent(); AddInstanceComponent(S);
                Spinners.Add(S); SpinRates.Add(Slot->Spin);
            }
            ++InstanceCount; continue;
        }
        AddTo(Piece.Slot, World); ++InstanceCount;
    }
    if (bWithVisuals)
    {
        const TArray<FFootprint> Blockers = Footprints(*A, P);
        for (const FScatter& S : A->Scatter)
        {
            if (Skip.Contains(S.Slot)) continue;
            const auto* List = Parts(S.Slot); if (!List) continue;
            FRandomStream R(S.Seed);
            TArray<FTransform> Batch; Batch.Reserve(S.Count);
            for (int32 Try = 0; Try < S.Count * 3 && Batch.Num() < S.Count; ++Try)
            {
                const FVector2D Pt(R.FRandRange(S.Region.Min.X, S.Region.Max.X), R.FRandRange(S.Region.Min.Y, S.Region.Max.Y));
                if (S.bOutsideBounds && FMath::Abs(Pt.X) < H.X + S.BoundsMargin && FMath::Abs(Pt.Y) < H.Y + S.BoundsMargin) continue;
                bool bSkip = false;
                for (const FBox2D& Ex : S.Exclude) if (Ex.IsInside(Pt)) { bSkip = true; break; }
                if (!bSkip && S.Clearance > 0)
                {
                    for (const FFootprint& F : Blockers) if (DistanceTo(F, Pt) < S.Clearance) { bSkip = true; break; }
                    for (int32 Team = 0; Team < 2 && !bSkip; ++Team) for (const FVector2D& Sp : A->Spawns[Team]) if (FVector2D::Distance(Sp, Pt) < S.Clearance + 100) { bSkip = true; break; }
                }
                if (bSkip) continue;
                const float Scale = R.FRandRange(S.ScaleRange.X, S.ScaleRange.Y);
                const FRotator Rot(S.bTilt ? R.FRandRange(-6.f, 6.f) : 0.f, S.bRandomYaw ? R.FRandRange(0.f, 360.f) : 0.f, S.bTilt ? R.FRandRange(-6.f, 6.f) : 0.f);
                Batch.Add(Resolved[S.Slot].Local * FTransform(Rot, FVector(Pt.X, Pt.Y, S.Z + R.FRandRange(-S.ZJitter, S.ZJitter)), FVector(Scale)));
            }
            for (auto* C : *List) C->AddInstances(Batch, false, false);
            InstanceCount += Batch.Num();
        }
    }
    UE_LOG(LogCireArenas, Display, TEXT("CIRE_ARENA_BUILD id=%s blockers=%d instances=%d components=%d fallback_slots=%d visuals=%d ms=%.1f"),
        *A->Id.ToString(), BlockerCount, InstanceCount, GetComponents().Num(), FallbackSlots, bWithVisuals ? 1 : 0, (FPlatformTime::Seconds() - BuildStarted) * 1000.0);
}

void ACireArenaStage::SetShown(bool bShow)
{
    bShown = bShow;
    SetActorHiddenInGame(!bShow);
    if (bShow && bVisuals) BuildLighting(); else ClearLighting();
}

void ACireArenaStage::BuildLighting()
{
    if (Lighting.Num() > 0) return;
    const FArena* A = CireArenas::Get(ArenaIndex); if (!A) return;
    const FLighting& L = A->Lighting;
    const FVector O = GetActorLocation();
    auto Keep = [&](USceneComponent* C) { C->SetupAttachment(RootComponent); C->RegisterComponent(); AddInstanceComponent(C); Lighting.Add(C); };
    for (const FPointLight& PL : L.Lights)
    {
        auto* Light = NewObject<UPointLightComponent>(this); Light->SetMobility(EComponentMobility::Movable);
        Light->SetRelativeLocation(PL.Location); Light->SetIntensity(PL.Intensity); Light->SetLightColor(PL.Color);
        Light->SetAttenuationRadius(PL.Radius); Light->SetCastShadows(PL.bShadows); Keep(Light);
    }
    if (UsesTownLighting(*A)) return;
    auto* Sun = NewObject<UDirectionalLightComponent>(this, TEXT("ArenaSun"));
    Sun->SetMobility(EComponentMobility::Movable);
    Sun->SetRelativeRotation(FRotator(L.SunPitch, L.SunYaw, 0));
    Sun->SetIntensity(L.SunIntensity); Sun->SetLightColor(L.SunColor); Sun->LightSourceAngle = L.SunSourceAngle;
    Sun->bEnableLightShaftBloom = L.bLightShafts; Sun->BloomScale = L.ShaftBloomScale; Sun->BloomThreshold = L.ShaftThreshold;
    Sun->bEnableLightShaftOcclusion = L.bLightShafts;
    Sun->VolumetricScatteringIntensity = L.VolumetricScattering; Sun->SetCastShadows(true);
    Sun->DynamicShadowDistanceMovableLight = 12000.f;
    if (!L.LightFunction.IsEmpty()) if (auto* F = LoadObject<UMaterialInterface>(nullptr, *L.LightFunction))
    { Sun->SetLightFunctionMaterial(F); Sun->SetLightFunctionScale(FVector(L.LightFunctionScale)); }
    Keep(Sun);
    if (!L.SkyMaterial.IsEmpty()) if (auto* SkyMat = LoadObject<UMaterialInterface>(nullptr, *L.SkyMaterial))
    {
        auto* Dome = NewObject<UStaticMeshComponent>(this, TEXT("ArenaSkyDome"));
        Dome->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        Dome->SetMaterial(0, SkyMat); Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision); Dome->SetCastShadow(false);
        Dome->bAffectDistanceFieldLighting = false; Dome->SetVisibleInRayTracing(false); Dome->bAffectDynamicIndirectLighting = false;
        Dome->SetRelativeLocation(FVector(0, 0, -1500)); Dome->SetRelativeScale3D(FVector(560)); // 280 m: the town (550 m away) stays outside
        Dome->SetScalarParameterValueOnMaterials(TEXT("SkyYawDegrees"), L.SkyYaw);
        Dome->SetScalarParameterValueOnMaterials(TEXT("HazeStrength"), L.SkyHazeStrength);
        Dome->SetVectorParameterValueOnMaterials(TEXT("HorizonHaze"), FVector(L.SkyHaze.R, L.SkyHaze.G, L.SkyHaze.B));
        Dome->SetScalarParameterValueOnMaterials(TEXT("Brightness"), L.SkyBrightness);
        Dome->SetVectorParameterValueOnMaterials(TEXT("SkyTint"), FVector(L.SkyTint.R, L.SkyTint.G, L.SkyTint.B));
        Keep(Dome);
    }
    auto* Sky = NewObject<USkyLightComponent>(this, TEXT("ArenaSkyLight"));
    Sky->SetMobility(EComponentMobility::Movable); Sky->bRealTimeCapture = true;
    Sky->SetIntensity(L.SkyLightIntensity); Sky->SetLightColor(L.SkyLightColor); Keep(Sky); Sky->RecaptureSky();
    auto* Fog = NewObject<UExponentialHeightFogComponent>(this, TEXT("ArenaFog"));
    Fog->SetRelativeLocation(FVector(0, 0, L.FogHeight));
    Fog->SetFogDensity(L.FogDensity); Fog->SetFogHeightFalloff(L.FogFalloff); Fog->SetFogInscatteringColor(L.FogColor);
    Fog->SetStartDistance(L.FogStart); Fog->SetFogMaxOpacity(L.FogMaxOpacity);
    Fog->SetVolumetricFog(L.bVolumetricFog); Fog->SetVolumetricFogScatteringDistribution(L.VolumetricDistribution);
    Fog->SetVolumetricFogExtinctionScale(L.VolumetricExtinction); Fog->SetVolumetricFogAlbedo(L.VolumetricAlbedo.ToFColor(true));
    Keep(Fog);
    auto* Post = NewObject<UPostProcessComponent>(this, TEXT("ArenaPost"));
    Post->bUnbound = true; Post->Priority = 5.f; Post->BlendWeight = 1.f;
    FPostProcessSettings& S = Post->Settings;
    S.bOverride_AutoExposureBias = true; S.AutoExposureBias = L.ExposureBias;
    S.bOverride_ColorSaturation = true; S.ColorSaturation = FVector4(1, 1, 1, L.Saturation);
    S.bOverride_ColorContrast = true; S.ColorContrast = FVector4(1, 1, 1, L.Contrast);
    S.bOverride_ColorGain = true; S.ColorGain = FVector4(L.Gain.R, L.Gain.G, L.Gain.B, 1);
    S.bOverride_ColorGainShadows = true; S.ColorGainShadows = FVector4(L.ShadowTint.R, L.ShadowTint.G, L.ShadowTint.B, 1);
    S.bOverride_WhiteTemp = true; S.WhiteTemp = L.Temperature;
    S.bOverride_BloomIntensity = true; S.BloomIntensity = L.Bloom;
    S.bOverride_VignetteIntensity = true; S.VignetteIntensity = L.Vignette;
    S.bOverride_Sharpen = true; S.Sharpen = .6f; // video-crash: was the global r.Tonemapper.Sharpen=0.6
    Keep(Post);
}

void ACireArenaStage::ClearLighting()
{
    for (UActorComponent* C : Lighting) if (IsValid(C)) C->DestroyComponent();
    Lighting.Reset();
}

void ACireArenaStage::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bShown) return;
    for (int32 I = 0; I < Spinners.Num(); ++I)
        if (IsValid(Spinners[I])) Spinners[I]->AddLocalRotation(FRotator(0, 0, SpinRates[I] * DeltaSeconds));
}

// =================================================================== subsystem
bool UCireArenaSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}
void UCireArenaSubsystem::Deinitialize() { Clear(); Super::Deinitialize(); }

void UCireArenaSubsystem::Tick(float DeltaTime)
{
    UWorld* World = GetWorld(); if (!World) return;
    int32 Want = INDEX_NONE; bool bShow = false;
    if (bForced) { Want = ForcedIndex; bShow = bForcedShow; }
    else if (const auto* State = World->GetGameState<ACireGameState>())
    {
        if (State->Phase == 1 || State->Phase == 2) { Want = CireArenas::CurrentIndex(World); bShow = State->Phase == 2; }
    }
    Apply(Want, bShow);
}

void UCireArenaSubsystem::Apply(int32 Index, bool bShow)
{
    if (!CireArenas::Get(Index)) { Clear(); return; }
    ACireArenaStage* S = Stage.Get();
    if (!S || S->ArenaIndex != Index)
    {
        Clear();
        UWorld* World = GetWorld();
        FActorSpawnParameters Params; Params.Name = MakeUniqueObjectName(World, ACireArenaStage::StaticClass(), TEXT("CireArenaStage"));
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; Params.ObjectFlags |= RF_Transient;
        S = World->SpawnActor<ACireArenaStage>(ACireArenaStage::StaticClass(), CireArenas::Origin(), FRotator::ZeroRotator, Params);
        if (!S) { UE_LOG(LogCireArenas, Error, TEXT("CIRE_ARENA_ERROR stage spawn failed")); return; }
        S->BuildArena(Index, World->GetNetMode() != NM_DedicatedServer);
        CireNav::RefreshActor(S); // nav-paths: the collision proxies carve the arena navmesh (server)
        S->SetShown(false);
        Stage = S; ++BuildCount;
    }
    if (S->bShown != bShow) S->SetShown(bShow);
    const FArena* A = CireArenas::Get(Index);
    HideTown(bShow && S->bVisuals && A && !UsesTownLighting(*A));
}

void UCireArenaSubsystem::Clear()
{
    HideTown(false);
    if (ACireArenaStage* S = Stage.Get()) { S->Destroy(); ++ClearCount; }
    Stage.Reset();
}

void UCireArenaSubsystem::HideTown(bool bHide)
{
    if (bHide == bTownHidden) return;
    bTownHidden = bHide;
    if (!bHide)
    {
        for (auto& C : HiddenTown) if (C.IsValid()) C->SetVisibility(true);
        for (auto& A : HiddenTownActors) if (A.IsValid()) A->SetActorHiddenInGame(false);
        // world-scale: the town colour grade comes back with the town.
        if (UWorld* W = GetWorld()) for (TActorIterator<ACireWorld> It(W); It; ++It)
            for (UActorComponent* C : It->GetComponents()) if (auto* PP = Cast<UPostProcessComponent>(C)) PP->bEnabled = true;
        HiddenTown.Reset(); HiddenTownActors.Reset(); return;
    }
    UWorld* World = GetWorld(); if (!World) return;
    auto Hide = [&](USceneComponent* C) { if (C && C->IsVisible() && !Cast<ACireArenaStage>(C->GetOwner())) { C->SetVisibility(false); HiddenTown.Add(C); } };
    for (TActorIterator<ADirectionalLight> It(World); It; ++It) Hide(It->GetLightComponent());
    for (TActorIterator<ASkyLight> It(World); It; ++It) Hide(It->GetLightComponent());
    for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) Hide(It->GetComponent());
    for (TActorIterator<ACireWorld> It(World); It; ++It)
    {
        for (UActorComponent* C : It->GetComponents()) if (C && C->GetFName() == TEXT("SkyDome")) Hide(Cast<USceneComponent>(C));
        for (UActorComponent* C : It->GetComponents()) if (auto* PP = Cast<UPostProcessComponent>(C)) PP->bEnabled = false; // world-scale: town grade off
        // Everyone is in the arena: stop drawing the town (550 m away) that would show through the sky dome.
        if (!It->IsHidden()) { It->SetActorHiddenInGame(true); HiddenTownActors.Add(*It); }
    }
}
