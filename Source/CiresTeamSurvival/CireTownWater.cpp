// town-trim: CireTownWater.h
#include "CireTownWater.h"
#include "CireTownMap.h"
#include "CireTownTrim.h"
#include "CireLanePath.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "WaterBodyOceanComponent.h"
#include "WaterZoneActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTownWater, Log, All);

namespace
{
struct FWaterSettings
{
    float DeepDepth = 120.f;      // cm of water above the floor from which the navmesh is cut (0 = never)
    float NavCell = 400.f;
    TMap<FString, FString> Cvars; // quality ("r.Water.*")
};
const FWaterSettings& Settings()
{
    static FWaterSettings S; static bool bLoaded = false;
    if (bLoaded) return S;
    bLoaded = true;
    FString Json; TSharedPtr<FJsonObject> Root; const TSharedPtr<FJsonObject>* W = nullptr;
    if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/CastleTown.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root
        || !Root->TryGetObjectField(TEXT("water"), W) || !W) return S;
    double V = 0;
    if ((*W)->TryGetNumberField(TEXT("deepDepth"), V) && FMath::IsFinite(V)) S.DeepDepth = float(FMath::Max(0.0, V));
    if ((*W)->TryGetNumberField(TEXT("navCell"), V) && FMath::IsFinite(V)) S.NavCell = float(FMath::Clamp(V, 100.0, 2000.0));
    const TSharedPtr<FJsonObject>* C = nullptr;
    if ((*W)->TryGetObjectField(TEXT("cvars"), C) && C)
        for (const auto& P : (*C)->Values)
        {
            FString Str;
            if (!P.Value->TryGetString(Str)) Str = FString::SanitizeFloat(P.Value->AsNumber());
            S.Cvars.Add(FString(P.Key), Str);
        }
    return S;
}
bool bCvarsApplied = false;
TArray<TWeakObjectPtr<UWaterBodyOceanComponent>> Oceans;
double NextCheck = 0;

bool Recentre(UWaterBodyOceanComponent* Ocean)
{
    // The ocean centres its mesh and bounds on SavedZoneLocation, written when the pack authored it (the zone at the pack
    // origin). A realm copy's zone sits at the realm offset: centre on it, then rebuild the zone's mesh and water info.
    AWaterZone* Zone = Ocean ? Ocean->GetWaterZone() : nullptr;
    FStructProperty* Prop = FindFProperty<FStructProperty>(UWaterBodyOceanComponent::StaticClass(), TEXT("SavedZoneLocation"));
    if (!Zone || !Prop) return false;
    FVector2D* Saved = Prop->ContainerPtrToValuePtr<FVector2D>(Ocean);
    const FVector2D Want(Zone->GetActorLocation());
    if (Saved->Equals(Want, 1.0)) return true;
    const FVector2D Was = *Saved;
    *Saved = Want;
    Ocean->UpdateBounds();
    Ocean->MarkRenderStateDirty();
    Zone->MarkForRebuild(EWaterZoneRebuildFlags::All, Ocean);
    UE_LOG(LogCireTownWater, Display, TEXT("CIRE_TOWN_WATER_RECENTRED %s realm=%d saved=%s zone=%s"), *Ocean->GetOwner()->GetName(), CireTownMap::RealmAt(Zone->GetActorLocation()), *Was.ToString(), *Want.ToString());
    return true;
}
}

void CireTownWater::PrepareLevel(UWorld* World, ULevel* Level)
{
    if (!World || !Level) return;
    for (AActor* A : Level->Actors)
    {
        if (!A) continue;
        TInlineComponentArray<UWaterBodyOceanComponent*> Found(A);
        for (UWaterBodyOceanComponent* O : Found) { Oceans.AddUnique(O); Recentre(O); }
    }
}

void CireTownWater::Tick(UWorld* World)
{
    if (!bCvarsApplied && World && World->GetNetMode() != NM_DedicatedServer)
    {
        bCvarsApplied = true;
        for (const auto& P : Settings().Cvars)
            if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(*P.Key)) { V->Set(*P.Value, ECVF_SetByGameSetting); UE_LOG(LogCireTownWater, Display, TEXT("CIRE_TOWN_WATER_CVAR %s=%s"), *P.Key, *P.Value); }
    }
    // The zone of a World Partition cell may register after its ocean: retry once a second while any is pending.
    if (!World || Oceans.IsEmpty()) return;
    const double Now = FPlatformTime::Seconds();
    if (Now < NextCheck) return;
    NextCheck = Now + 1.0;
    for (int32 I = Oceans.Num() - 1; I >= 0; --I)
    {
        UWaterBodyOceanComponent* O = Oceans[I].Get();
        if (!O) { Oceans.RemoveAt(I); continue; }
        if (Recentre(O) && O->GetWaterZone()) Oceans.RemoveAt(I); // done
    }
}

bool CireTownWater::SurfaceAt(const UWorld* World, const FVector2D& WorldXY, float& OutZ)
{
    OutZ = 0.f;
    if (!World) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CireWaterSurface), false);
    FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    TArray<FHitResult> Hits;
    World->LineTraceMultiByObjectType(Hits, FVector(WorldXY, 50000.0), FVector(WorldXY, -50000.0), Objects, Params);
    for (const FHitResult& H : Hits)
        if (H.GetComponent() && H.GetComponent()->GetCollisionProfileName() == TEXT("WaterBodyCollision")) { OutZ = H.ImpactPoint.Z; return true; }
    return false;
}

bool CireTownWater::FloorAt(const UWorld* World, const FVector2D& WorldXY, float SurfaceZ, float& OutZ)
{
    if (!World) return false;
    TArray<FHitResult> Hits;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CireWaterFloor), false);
    World->LineTraceMultiByObjectType(Hits, FVector(WorldXY, SurfaceZ + 5.0), FVector(WorldXY, SurfaceZ - 5000.0), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
    for (const FHitResult& H : Hits)
    {
        if (H.GetComponent() && H.GetComponent()->GetCollisionProfileName() == TEXT("WaterBodyCollision")) continue;
        if (H.ImpactNormal.Z < .5f) continue;
        OutZ = H.ImpactPoint.Z; return true;
    }
    return false;
}
FString CireTownWater::NavSignature()
{
    return FString::Printf(TEXT("water1 deep=%.0f cell=%.0f"), Settings().DeepDepth, Settings().NavCell);
}
TArray<FBox> CireTownWater::DeepWaterBoxes(UWorld* World, int32 Team, const FBox2D& AreaLocal)
{
    TArray<FBox> Out;
    const FWaterSettings& S = Settings();
    if (!World || S.DeepDepth <= 0) return Out;
    const TArray<FVector2D>& Poly = CireTownTrim::Polygon();
    FBox2D Area = AreaLocal;
    if (CireTownTrim::Active() && Poly.Num() >= 3)
    {
        FBox2D P(ForceInit); for (const FVector2D& V : Poly) P += V;
        P = P.ExpandBy(CireTownTrim::Settings().Margin);
        Area.Min = FVector2D::Max(Area.Min, P.Min); Area.Max = FVector2D::Min(Area.Max, P.Max);
    }
    if (!Area.bIsValid || Area.Min.X >= Area.Max.X || Area.Min.Y >= Area.Max.Y) return Out;
    const double Cell = S.NavCell;
    const int32 NX = FMath::CeilToInt((Area.Max.X - Area.Min.X) / Cell), NY = FMath::CeilToInt((Area.Max.Y - Area.Min.Y) / Cell);
    int32 Wet = 0, Deep = 0; float Deepest = 0.f; TArray<int32> Depths; Depths.Init(0, 6);
    TArray<FBox> Open;
    const FVector2D O = CireLanePath::RealmOrigin(Team);
    for (int32 Y = 0; Y < NY; ++Y)
    {
        TArray<FBox> Row;
        int32 Start = -1; float Lo = 0, Hi = 0;
        for (int32 X = 0; X <= NX; ++X)
        {
            bool bDeep = false; float Surface = 0, Floor = 0;
            if (X < NX)
            {
                const FVector2D W = O + Area.Min + FVector2D((X + .5) * Cell, (Y + .5) * Cell);
                if (SurfaceAt(World, W, Surface) && FloorAt(World, W, Surface, Floor))
                {
                    const float Depth = Surface - Floor;
                    if (Depth > 0) { ++Wet; ++Depths[FMath::Clamp(int32(Depth / 60.f), 0, 5)]; Deepest = FMath::Max(Deepest, Depth); }
                    bDeep = Depth > S.DeepDepth;
                }
            }
            if (bDeep)
            {
                ++Deep;
                if (Start < 0) { Start = X; Lo = Floor; Hi = Surface; } else { Lo = FMath::Min(Lo, Floor); Hi = FMath::Max(Hi, Surface); }
            }
            else if (Start >= 0)
            {
                Row.Add(FBox(FVector(O + Area.Min + FVector2D(Start * Cell, Y * Cell), Lo - 150.f), FVector(O + Area.Min + FVector2D(X * Cell, (Y + 1) * Cell), Hi + 60.f)));
                Start = -1;
            }
        }
        TArray<FBox> Next;
        for (const FBox& R : Row)
        {
            FBox* Grow = Open.FindByPredicate([&](const FBox& B) { return B.IsValid && FMath::IsNearlyEqual(B.Min.X, R.Min.X) && FMath::IsNearlyEqual(B.Max.X, R.Max.X) && FMath::IsNearlyEqual(B.Max.Y, R.Min.Y); });
            if (Grow) { FBox G = *Grow; G.Max.Y = R.Max.Y; G.Min.Z = FMath::Min(G.Min.Z, R.Min.Z); G.Max.Z = FMath::Max(G.Max.Z, R.Max.Z); Next.Add(G); Grow->IsValid = false; }
            else Next.Add(R);
        }
        for (const FBox& B : Open) if (B.IsValid) Out.Add(B);
        Open = MoveTemp(Next);
    }
    Out.Append(Open);
    UE_LOG(LogCireTownWater, Display, TEXT("CIRE_TOWN_WATER_DEPTH realm=%d cells=%d wet=%d deep=%d deepest_cm=%.0f depth_hist_60cm=%d/%d/%d/%d/%d/%d deep_boxes=%d rule=deeper_than_%.0fcm_blocks_nav"),
        Team, NX * NY, Wet, Deep, Deepest, Depths[0], Depths[1], Depths[2], Depths[3], Depths[4], Depths[5], Out.Num(), S.DeepDepth);
    return Out;
}
