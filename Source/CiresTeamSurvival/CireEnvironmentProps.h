#pragma once
#include "CoreMinimal.h"
class ACireWorld;
class UWorld;
class UMaterialInterface;

/**
 * Data-driven medieval town layout for both private PvE realms.
 *
 * Content/Data/TownAssetSlots.json maps slot ids (gatehouse, castle_keep, house_a, lamp, ...)
 * to meshes/materials with a guaranteed fallback. Optional overlay manifests
 * Content/Data/TownAssetSlots.<source>.json (e.g. .tripo.json, .fab.json) are merged by
 * "priority" so later art drops in without code changes. Content/Data/TownLayout.json holds
 * districts and placements in realm-local centimetres (local Y relative to the team centre).
 * Every placement is re-checked against the live route, challenge bays, spawn and the realm
 * divider whenever the route revision changes; conflicting pieces are omitted, never moved.
 */
namespace CireEnvironmentProps
{
    /** Re-read the slot manifests and layout (called once per battlefield build). */
    void Reload();
    void Build(ACireWorld* WorldActor);
    void Refresh(ACireWorld* WorldActor);
    int32 InstanceCount(const ACireWorld* WorldActor);
    int32 SuppressedCount(const ACireWorld* WorldActor);
    int32 LightCount(const ACireWorld* WorldActor);
    bool HasSafeClearance(const ACireWorld* WorldActor);

    // nav-paths: route/bay clearance queries for the path editor, and the placed pieces for navigation tests.
    /** Town pieces a route segment (realm-local points) would suppress under the route-clearance rule at LaneWidth. */
    CIRESTEAMSURVIVAL_API int32 RouteConflicts(const UWorld* World, int32 Team, const FVector2D& LocalA, const FVector2D& LocalB, float LaneWidth, TArray<FName>* Slots = nullptr);
    /** Town pieces a challenge bay (realm-local) would suppress (450 cm bay clearance). */
    CIRESTEAMSURVIVAL_API int32 BayConflicts(const UWorld* World, int32 Team, const FVector2D& LocalBay, TArray<FName>* Slots = nullptr);
    struct FPlacedProp { int32 Team = 0; FName Slot; FTransform Transform; FBox LocalBox = FBox(ForceInit); bool bCollision = false; };
    CIRESTEAMSURVIVAL_API TArray<FPlacedProp> PlacedProps(const ACireWorld* WorldActor);
    struct FTownDistrict { FName Id; FString Name; float MinX = 0, MaxX = 0; };
    /** Authored districts ordered from the monster breach to the castle. */
    CIRESTEAMSURVIVAL_API const TArray<FTownDistrict>& Districts();
    /** District id containing a world location in the given team's realm, or NAME_None. */
    CIRESTEAMSURVIVAL_API FName DistrictAt(const UWorld* World, int32 Team, const FVector& Location);
    CIRESTEAMSURVIVAL_API FString DistrictName(FName Id);
    /** Resolved material for a material slot (e.g. "cobblestone_material"), honouring overlays. */
    CIRESTEAMSURVIVAL_API UMaterialInterface* SurfaceMaterial(const TCHAR* SlotId, const TCHAR* FallbackPath);
    /** Which manifest supplied a slot's mesh/material ("base", "fallback", "tripo", "fab", ...). */
    CIRESTEAMSURVIVAL_API FString SlotSource(FName SlotId);
}
