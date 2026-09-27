#pragma once
// tier-readability (Eric, 2026-09-26): monster tiers read as UI, not as body glow, and named town zones announce the
// tier of the packs they hold.
//
// 1. Pack tier presentation. Jungle-pack monsters no longer wear a glowing rank tint (CireRaces::ApplySkin, the
//    cire.RankBodyColours cvar, default 0). Their tier shows as "T1".."T4" next to the name on the nameplate, the target
//    and focus frames and the unit tooltip, with a WoW quality-coloured border: T1 silver, T2 green, T3 blue, T4 gold.
// 2. Zones. A zone is a named realm-local polygon. When a champion enters one, the HUD shows the WoW zone text
//    ("ENTERING / Market Plaza / Monster Tier: 2"), where the tier is the most common tier of the challenge packs inside
//    it (ties go to the higher tier). Zones come from the map layout's Zone markers (MapLayout.json, the layout editor's
//    Zone setter); a layout without any uses the defaults in Content/Data/TownZones.json for its map; the procedural
//    town falls back to its TownLayout.json districts. Local presentation only (Docs/Zones.md).
#include "CoreMinimal.h"

class ACireMonster;
class UWorld;
struct FCireChallengeBay;
struct FCireMapLayout;

struct CIRESTEAMSURVIVAL_API FCireZone
{
    FString Id, Name;
    /** Realm-local polygon (3+ corners). */
    TArray<FVector2D> Polygon;
    /** Realm it applies to (0 DAYLIGHT, 1 DARKNIGHT), -1 both. */
    int32 Realm = -1;
};

namespace CireZones
{
    // ---- pack tier presentation (world-free) ------------------------------------------------------------------------
    /** A jungle-pack monster's tier (1..4); 0 for everything else (waves, bosses, fixtures). */
    CIRESTEAMSURVIVAL_API int32 TierOf(const ACireMonster* Monster);
    /** "T3" (empty for 0). */
    CIRESTEAMSURVIVAL_API FString TierTag(int32 Tier);
    /** WoW quality colours: T1 silver, T2 green, T3 blue, T4 gold. */
    CIRESTEAMSURVIVAL_API FLinearColor TierColor(int32 Tier);
    /** Nameplate text of a monster: "Grave Hound  T3" (the name alone when it has no tier). */
    CIRESTEAMSURVIVAL_API FString NameplateLabel(const FString& Name, int32 Tier);
    /** Target / focus frame caption of a pack monster: "T3 PACK  /  TANK", "T3 PACK LEADER  /  TANK". */
    CIRESTEAMSURVIVAL_API FString FrameHeader(int32 Tier, bool bLeader, const FString& RoleName);

    // ---- special-state badges (vfx-scale, Eric 2026-09-26: "remove special monster glows ... show those states with icons") --
    // Rare spawns, bonus-loot creatures, lane bosses and enraged monsters no longer glow (no overlay rim, no special skin
    // colour, no Fab aura); the HUD draws a small icon after the name / T# tag instead, on the nameplate, the target and focus
    // frames and the boss frames. Order is fixed: rare, bonus loot, boss, enraged.
    enum class EBadge : uint8 { Rare, BonusLoot, Boss, Enraged, Count };
    /** Badges for a state (SpecialSpawn 1 = rare, 2 = bonus loot). */
    CIRESTEAMSURVIVAL_API TArray<EBadge> Badges(uint8 SpecialSpawn, bool bBoss, bool bEnraged);
    /** Badges of a live monster (nullptr: none). Boss = a lane boss or a boss-class monster. */
    CIRESTEAMSURVIVAL_API TArray<EBadge> BadgesOf(const ACireMonster* Monster);
    /** "RARE", "BONUS LOOT", "BOSS", "ENRAGED" (tooltips, the unit tooltip tag). */
    CIRESTEAMSURVIVAL_API FString BadgeName(EBadge Badge);
    /** Icon colour: rare violet-blue, bonus loot gold, boss red, enraged orange-red. */
    CIRESTEAMSURVIVAL_API FLinearColor BadgeColor(EBadge Badge);

    // ---- zones --------------------------------------------------------------------------------------------------------
    CIRESTEAMSURVIVAL_API bool InsidePolygon(const TArray<FVector2D>& Polygon, const FVector2D& Point);
    /** Most common tier (1..4) in the list; ties go to the higher tier; 0 when the list is empty. */
    CIRESTEAMSURVIVAL_API int32 DominantTier(const TArray<int32>& Tiers);
    /** Dominant tier of the packs whose centre lies inside the zone. */
    CIRESTEAMSURVIVAL_API int32 ZoneTier(const FCireZone& Zone, const TArray<FCireChallengeBay>& Packs);
    /** Per tier 1..4, the packs inside the zone (index 0 unused). */
    CIRESTEAMSURVIVAL_API TArray<int32> TierCounts(const FCireZone& Zone, const TArray<FCireChallengeBay>& Packs);
    /** The banner's second line: "Monster Tier: 3", or "No monster camps". */
    CIRESTEAMSURVIVAL_API FString TierLine(int32 Tier);
    /** Index of the first zone (layout order) containing Local in Realm, or INDEX_NONE. */
    CIRESTEAMSURVIVAL_API int32 IndexAt(const TArray<FCireZone>& Zones, const FVector2D& Local, int32 Realm);

    /** The Zone markers of a layout (3+ points; unnamed ones read "Zone N"). */
    CIRESTEAMSURVIVAL_API TArray<FCireZone> FromLayout(const FCireMapLayout& Layout);
    /** TownZones.json: { "maps": { "<map>": [ { "id", "name", "points": [[x, y], ...] } ] } }. False when malformed. */
    CIRESTEAMSURVIVAL_API bool ParseDefaults(const FString& Json, const FString& Map, TArray<FCireZone>& Out, FString& Error);
    /** The default zones of a map ("castletown" / "procedural"): TownZones.json, else (procedural) the town districts. */
    CIRESTEAMSURVIVAL_API TArray<FCireZone> Defaults(const FString& Map);
    /** Zones of the running map: the active layout's Zone markers, else the defaults. Cached per route revision. */
    CIRESTEAMSURVIVAL_API const TArray<FCireZone>& Active(const UWorld* World);
    /** The zone a champion of Team stands in (world location), or nullptr. */
    CIRESTEAMSURVIVAL_API const FCireZone* At(const UWorld* World, int32 Team, const FVector& Location);
    /** Dominant tier of the live packs of Team's realm inside the zone (replicated packs: valid on clients). */
    CIRESTEAMSURVIVAL_API int32 LiveTier(const UWorld* World, int32 Team, const FCireZone& Zone);
    CIRESTEAMSURVIVAL_API FString DefaultsPath(); // Content/Data/TownZones.json

#if !UE_BUILD_SHIPPING
    /** World-free checks (part of CireRouteEditor::RunTests): tags, colours, labels, dominant tier, polygons, parsing. */
    CIRESTEAMSURVIVAL_API bool RunTests(TArray<FString>& Failures);
#endif
}
