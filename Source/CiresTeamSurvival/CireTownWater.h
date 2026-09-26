#pragma once
// town-trim: the Medieval Kingdom pack's water (the Water plugin) in both realms (Docs/CastleTown.md "Water").
//
// The pack's SL_Landscape carries one WaterZone and one WaterBodyOcean (its surface at Z = -236 fills the riverbeds and
// the harbour). The ocean centres its mesh and bounds on a location saved when it was authored (the zone at the pack
// origin); every realm copy is streamed 6-12 km away from there, so the ocean's bounds, and with them the water mesh
// tiles, stayed at the pack origin and nothing rendered. Each copy's ocean is re-centred on its own realm's zone.
// Quality: CastleTown.json "water" (cheap defaults: no caustics/underwater post process work, fewer mesh LODs).
// Gameplay: the ocean's collision is query-only for the camera and never affects the navmesh; deep water (below
// "water.deepDepth") is cut out of the navmesh by the WaterBody nav area, shallow water stays walkable (visual only).
#include "CoreMinimal.h"

class ULevel;
class UWorld;

namespace CireTownWater
{
    /** A realm level became visible: re-centre its oceans on their realm's water zone. */
    CIRESTEAMSURVIVAL_API void PrepareLevel(UWorld* World, ULevel* Level);
    /** ACireWorld tick: late zones (World Partition cells) and the water quality cvars. */
    CIRESTEAMSURVIVAL_API void Tick(UWorld* World);
    /** Water surface Z at a world XY, or false when there is no water there (tests, navmesh rules, probes). */
    CIRESTEAMSURVIVAL_API bool SurfaceAt(const UWorld* World, const FVector2D& WorldXY, float& OutZ);
    /** The river/harbour floor under the water at a world XY (the first walkable static hit below the surface). */
    CIRESTEAMSURVIVAL_API bool FloorAt(const UWorld* World, const FVector2D& WorldXY, float SurfaceZ, float& OutZ);
    /** Server, before the navmesh is built: world boxes (floor to surface) where the water is deeper than "water.deepDepth",
        cut out of the navmesh (NavArea_Null) in the realm's play area (the trim polygon, else AreaLocal). Logs the depths. */
    CIRESTEAMSURVIVAL_API TArray<FBox> DeepWaterBoxes(UWorld* World, int32 Team, const FBox2D& AreaLocal);
    /** Everything that shapes the water navmesh rule (part of the nav cache key). */
    CIRESTEAMSURVIVAL_API FString NavSignature();
}
