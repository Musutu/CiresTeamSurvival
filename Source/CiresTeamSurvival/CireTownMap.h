#pragma once
// medieval-kingdom: the Medieval Kingdom pack's own assembled town (PL_CastleTown) as the survival map
// (Docs/CastleTown.md).
//
// Each PvE realm is one full copy of the pack's town: its sublevels (not SL_Lighting) are streamed in
// twice at the realm offsets from Content/Data/CastleTown.json, on every peer, with deterministic level
// names. Houses keep their interiors, the castle and the landscape stay as the pack built them; only the
// march route, the breach, the challenge-pack spots and the hero base come from data
// (Content/Data/CastleTownRoutes.json, realm-local centimetres) and are PROVISIONAL until Eric authors them.
//
// Realm frame: realm-local = pack coordinates - frameCenter. World = realm-local + RealmOrigin(Team), where
// RealmOrigin = realmOffset + frameCenter. The procedural town keeps its old frame, origins (0, -/+2100).
// Heights: in the town the ground is the pack's landscape, so realm-local Z values are heights above the
// ground under that point (CireTownMap::Ground); on the procedural town the ground is Z = 0.
//
// -CireExplore: the town with no waves, bots or packs, an auto-drafted champion, fly mode and route marking
// (ExploreTown.cmd). -CireTown forces the town (probes and galleries default to the procedural town);
// -CireProcedural forces the old town.
#include "CoreMinimal.h"

class ACireGameMode;
class ACireHUD;
class UWorld;

struct CIRESTEAMSURVIVAL_API FCireTownLandmark
{
    FString Name;
    FVector2D Local = FVector2D::ZeroVector;
};

/** One realm's look: the realms are identical in layout and differ only in lighting (Eric, 2026-09-26). */
struct CIRESTEAMSURVIVAL_API FCireRealmLighting
{
    FString Name = TEXT("Daylight");
    FString SkyMaterial;                          // the pack's MI_sky_Day / MI_sky_Night on its SM_sphere
    float SunPitch = -55.f, SunYaw = 150.f, SunIntensity = 8.f;
    FLinearColor SunColor = FLinearColor(1.f, .95f, .86f);
    float ExposureBias = 0.f, Saturation = 1.f;
    FLinearColor Tint = FLinearColor::White;
    float TorchIntensity = 0.f, TorchRadius = 1400.f;   // extra warm fill at the pack's torches and lanterns (0 = none)
    FLinearColor TorchColor = FLinearColor(1.f, .62f, .3f);
};

struct CIRESTEAMSURVIVAL_API FCireTownDef
{
    bool bValid = false;
    FString Error;
    FString Pack = TEXT("/Game/CastleTown");
    FString Persistent;
    TArray<FString> Levels;           // long package names streamed per realm
    FVector Offsets[2] = {FVector::ZeroVector, FVector::ZeroVector};
    FCireRealmLighting Lighting[2];
    FVector2D FrameCenter = FVector2D::ZeroVector;
    FVector2D ZRange = FVector2D(-2000, 8000);
    float SkyRadius = 0.f;             // each realm's sky sphere radius (0 = from the route bounds) // ground trace and nav bounds, relative to the realm offset Z
    bool bDefaultMap = true;
    FString RoutesFile = TEXT("Data/CastleTownRoutes.json");
    FVector2D ExploreStart = FVector2D::ZeroVector;
    TArray<FCireTownLandmark> Landmarks;
};

namespace CireTownMap
{
    /** Content/Data/CastleTown.json (parsed once; bValid false when missing or malformed). */
    CIRESTEAMSURVIVAL_API const FCireTownDef& Def();
    /** The Fab pack is installed in this checkout (it is never committed; public clones fall back). */
    CIRESTEAMSURVIVAL_API bool PackAvailable();
    /** Server decision for this process: -CireTown/-CireExplore force it, -CireProcedural and developer probes opt out. */
    CIRESTEAMSURVIVAL_API bool WantTown();
    CIRESTEAMSURVIVAL_API bool IsActive();
    /** Switch the process to the town frame (reloads the route document from the town file). */
    CIRESTEAMSURVIVAL_API void SetActive(bool bActive);
    CIRESTEAMSURVIVAL_API bool IsExplore();
    /** World-space origin of a realm's local frame (Z = realm offset Z). */
    CIRESTEAMSURVIVAL_API FVector RealmOrigin(int32 Team);
    /** Stream both copies of the town into this world and block until they are visible (idempotent). */
    CIRESTEAMSURVIVAL_API bool LoadRealms(UWorld* World);
    CIRESTEAMSURVIVAL_API int32 LoadedLevels(const UWorld* World);
    /** Strip the pack's own sky/sun/grading/cinematics and put the realm-1 copy on lighting channel 1. Idempotent per
        level; ACireWorld calls it again while late Level Instances stream in. Returns the newly prepared level count. */
    CIRESTEAMSURVIVAL_API int32 PrepareRealmLevels(UWorld* World);
    /** Ground height under a world XY (town: downward trace against static geometry, cached; else 0). */
    CIRESTEAMSURVIVAL_API float Ground(const UWorld* World, const FVector2D& WorldXY);
    /** Nav/ground Z range of a realm in world space. */
    CIRESTEAMSURVIVAL_API FVector2D WorldZRange(int32 Team);
    /** Server: decide the map, switch the frame and stream the realms before the world is built. */
    CIRESTEAMSURVIVAL_API void InitializeServer(ACireGameMode* Mode);
    /** Every peer: per-realm sun (lighting channel 0 = realm 0, 1 = realm 1), the pack's sky sphere around each realm,
        a bounded colour grade per realm and warm torch fill. Owner = ACireWorld. */
    CIRESTEAMSURVIVAL_API void BuildRealmLighting(AActor* Owner);
    /** Keep a moving actor's lighting channel on the realm it stands in (heroes, monsters). */
    CIRESTEAMSURVIVAL_API void ApplyActorRealm(AActor* Actor);
    /** Which realm copy a world location belongs to (nearest realm origin). */
    CIRESTEAMSURVIVAL_API int32 RealmAt(const FVector& World);
    /** Server: champions that joined before the town streamed in move to their (grounded) base. */
    CIRESTEAMSURVIVAL_API void PlaceHeroes(ACireGameMode* Mode);
    /** Explore mode tick (server/standalone): returns true when the match loop must not run. */
    CIRESTEAMSURVIVAL_API bool TickExplore(ACireGameMode* Mode, float DeltaSeconds);
}
