#pragma once
// dev-route-tools: the MAP LAYOUT data model behind the map layout editor (-CireRouteEdit, RouteEditor.cmd,
// Docs/MapLayout.md). A layout is a list of typed markers ("setters") in REALM-LOCAL centimetres: both realms
// (DAYLIGHT for Team 1, DARKNIGHT for Team 2) share one town layout, so every marker is authored once and placed
// into its realm through the realm frame (CireLanePath::ToWorld).
//
// Setters come from one data-driven type table (Content/Data/MapMarkerTypes.json; built-in fallback): Player Spawn,
// Monster Spawn, Monster Path, Challenge Pack, Shop / Vendor, Objective, Boss Spawn, Rift / Arena Entrance, Respawn,
// Play Bounds and No-Spawn Blocker zones. Every marker has an Owner (Team 1, Team 2 or Shared); monster spawns and
// paths also have a Target team (the team their waves attack). By default a team marker is MIRRORED: the editor keeps
// a twin for the other team at the same realm-local spot in the other realm (Pair links the two). Turning Mirror off
// on a marker breaks the pair so the layout can be asymmetric on purpose.
//
// Apply compiles the layout into the live route document (BattlefieldRoutes, CireLanePath) for the systems that run
// today: per realm, the first monster path that targets that realm's team becomes its march route (spawn -> path ->
// objective, following merges), its challenge packs become the realm's packs and the Team 1 objective sets the goal
// zone. Every marker is also written to Content/Data/MapLayout.json for the systems that read it next (vendors,
// spawns, respawn, rift, bounds), and the vendor markers to Content/Data/TownVendors.json.
#include "CoreMinimal.h"

struct FCireBattlefieldRoutes;

/** Marker owner. JSON "team": 0 shared, 1 Team 1 (DAYLIGHT realm), 2 Team 2 (DARKNIGHT realm). */
enum class ECireMarkerOwner : uint8 { Shared = 0, Team1 = 1, Team2 = 2 };

/** How a setter is drawn in the world. */
enum class ECireGizmo : uint8 { Pillar, Ring, Path, Polygon, Zone };

struct CIRESTEAMSURVIVAL_API FCireMarkerType
{
    FName Id;
    FString Name;             // "Player Spawn"
    FString Label;            // world label stem: "Spawn", "Pack", "Vendor"
    FString Icon;             // painted ability icon / sigil id for the setter's action-bar skill
    FLinearColor Color = FLinearColor::White;
    ECireGizmo Gizmo = ECireGizmo::Pillar;
    bool bFacing = false, bNamed = false, bRadius = false, bTier = false, bKind = false, bTarget = false, bPoints = false;
    float DefaultRadius = 150.f;
    ECireMarkerOwner DefaultOwner = ECireMarkerOwner::Team1;
    /** Most markers of this type per owner (0 = unlimited). */
    int32 MaxPerOwner = 0;
    /** Vendor kinds and the like (Kind cycles through these). */
    TArray<FString> Kinds;
};

struct CIRESTEAMSURVIVAL_API FCireMapMarker
{
    FString Id;
    FName Type;
    FString Name;
    ECireMarkerOwner Owner = ECireMarkerOwner::Team1;
    /** Monster spawns and paths: the team their waves attack. */
    ECireMarkerOwner Target = ECireMarkerOwner::Team1;
    /** Realm-local centimetres. Paths and bounds keep their points in Points (Position = first point). */
    FVector2D Position = FVector2D::ZeroVector;
    /** Facing in degrees, realm-local (0 = +X). */
    float Yaw = 0.f;
    float Radius = 0.f;
    int32 Tier = 0;
    FString Kind;
    TArray<FVector2D> Points;
    /** Monster path: the monster spawn it starts at, and the path it merges into (empty: it ends at the objective). */
    FString From, MergeInto;
    /** Keep a twin for the other team (default on); Pair is the twin's id. */
    bool bMirror = true;
    FString Pair;
    /** Vendor: the sign anchor (realm-local XY, facing, height above the ground) and the stall footprint (centre, facing,
     *  width x depth), both sub-handles of the vendor group. Kind is the vendor type (Vendors.json id). */
    FVector2D SignPos = FVector2D::ZeroVector, StallPos = FVector2D::ZeroVector, StallSize = FVector2D(220, 120);
    float SignYaw = 0.f, SignHeight = 250.f, StallYaw = 0.f;
    /** layout-wiring: Monster Path share of its spawn's units when the spawn splits by weight (JSON "weight", default 1). */
    float Weight = 1.f;
    /** layout-wiring: Monster Spawn: split its units across its paths by path weight (JSON "split": "weighted") or evenly. */
    bool bSplitWeighted = false;
};

/** Vendor sub-handles. */
enum class ECireVendorPart : uint8 { Npc = 0, Sign = 1, Stall = 2 };

/** dev-route-tools: a vendor type (Content/Data/Vendors.json from feat/vendors; built-in fallback) and its default
 *  sign / stall layout relative to the NPC (X forward, Y right, in cm). */
struct CIRESTEAMSURVIVAL_API FCireVendorType
{
    FString Id, Name;
    FVector2D SignOffset = FVector2D(0, 150);
    float SignHeight = 250.f;
    FVector2D StallOffset = FVector2D(140, 0), StallSize = FVector2D(220, 120);
};

struct CIRESTEAMSURVIVAL_API FCireMapLayout
{
    FString Name = TEXT("Untitled");
    /** layout-wiring: the map the layout was authored on ("castletown" or "procedural"); a layout only drives its own map. */
    FString Map;
    TArray<FCireMapMarker> Markers;
    int32 NextId = 1;
};

struct CIRESTEAMSURVIVAL_API FCireLayoutIssue
{
    bool bError = true;
    /** 0 = both / not team-specific, else the team (1 or 2) it concerns. */
    int32 Team = 0;
    FString MarkerId;
    FString Message;
};

/** Optional world checks for Validate (the editor supplies them; the data model stays world-free for tests). */
struct CIRESTEAMSURVIVAL_API FCireLayoutChecks
{
    /** Realm (0/1), realm-local point -> is it on the navmesh. */
    TFunction<bool(int32, const FVector2D&)> OnNavmesh;
    /** Realm, from, to -> can a monster walk it (full navmesh path). */
    TFunction<bool(int32, const FVector2D&, const FVector2D&)> Walkable;
    /** Realm, sign anchor, height above the ground -> the sign has room (no world geometry inside it). */
    TFunction<bool(int32, const FVector2D&, float)> SignClear;
    /** layout-wiring: compile the layout the way the game will run it (CompileRoutes + the route rules the match enforces).
     *  Returns false with Error when the runtime would reject it; Notes are reported as warnings. */
    TFunction<bool(const FCireMapLayout&, TArray<FString>&, FString&)> Runtime;
};

namespace CireMapLayout
{
    // ---- setter types --------------------------------------------------------------------------------------------
    CIRESTEAMSURVIVAL_API const TArray<FCireMarkerType>& Types();
    CIRESTEAMSURVIVAL_API const FCireMarkerType* FindType(FName Id);
    /** The built-in table (also the fallback when MapMarkerTypes.json is missing or malformed). */
    CIRESTEAMSURVIVAL_API TArray<FCireMarkerType> BuiltInTypes();
    CIRESTEAMSURVIVAL_API bool ParseTypes(const FString& Json, TArray<FCireMarkerType>& Out, FString& Error);
    // Setter ids.
    extern CIRESTEAMSURVIVAL_API const FName PlayerSpawn, MonsterSpawn, MonsterPath, ChallengePack, Vendor, Objective,
        BossSpawn, Rift, Respawn, PlayBounds, Blocker;

    /** Vendor types: Content/Data/Vendors.json ("vendors": [{ "id", "name", "sign": {...}, "stall": {...} }]) or the
     *  built-in three shops (weaponsmith, armory, arcane). */
    CIRESTEAMSURVIVAL_API const TArray<FCireVendorType>& VendorTypes();
    CIRESTEAMSURVIVAL_API const FCireVendorType* FindVendorType(const FString& Id);
    CIRESTEAMSURVIVAL_API bool ParseVendorTypes(const FString& Json, TArray<FCireVendorType>& Out, FString& Error);

    // ---- teams and realms ----------------------------------------------------------------------------------------
    /** Realm a team lives in: Team 1 -> 0 (DAYLIGHT), Team 2 -> 1 (DARKNIGHT). Shared markers show in both. */
    CIRESTEAMSURVIVAL_API int32 RealmOf(ECireMarkerOwner Team);
    CIRESTEAMSURVIVAL_API ECireMarkerOwner TeamOfRealm(int32 Realm);
    CIRESTEAMSURVIVAL_API ECireMarkerOwner OtherTeam(ECireMarkerOwner Team);
    CIRESTEAMSURVIVAL_API bool ShownInRealm(const FCireMapMarker& Marker, int32 Realm);
    CIRESTEAMSURVIVAL_API FString TeamTag(ECireMarkerOwner Team);   // "T1", "T2", "Shared"
    CIRESTEAMSURVIVAL_API FString RealmName(int32 Realm);            // "DAYLIGHT", "DARKNIGHT"

    // ---- queries -------------------------------------------------------------------------------------------------
    CIRESTEAMSURVIVAL_API FCireMapMarker* Find(FCireMapLayout& Layout, const FString& Id);
    CIRESTEAMSURVIVAL_API const FCireMapMarker* Find(const FCireMapLayout& Layout, const FString& Id);
    CIRESTEAMSURVIVAL_API int32 IndexOf(const FCireMapLayout& Layout, const FString& Id);
    /** Markers of a type (NAME_None: all), optionally only one owner. In layout order. */
    CIRESTEAMSURVIVAL_API TArray<const FCireMapMarker*> OfType(const FCireMapLayout& Layout, FName Type, TOptional<ECireMarkerOwner> Owner = {});
    /** 1-based number among the markers of the same type and owner ("PACK 3"): removing one renumbers the rest. */
    CIRESTEAMSURVIVAL_API int32 Number(const FCireMapLayout& Layout, const FString& Id);
    /** "T1 Spawn 2", "Monsters -> T1", "T2 Pack 3 . Tier 2", "Weaponsmith" */
    CIRESTEAMSURVIVAL_API FString DisplayLabel(const FCireMapLayout& Layout, const FCireMapMarker& Marker);
    /** Paths starting at a monster spawn (its links). */
    CIRESTEAMSURVIVAL_API TArray<const FCireMapMarker*> PathsFrom(const FCireMapLayout& Layout, const FString& SpawnId);
    /** The objective the team defends (owned by it, else a shared one). */
    CIRESTEAMSURVIVAL_API const FCireMapMarker* ObjectiveOf(const FCireMapLayout& Layout, ECireMarkerOwner Team);
    /** A path as monsters walk it: its spawn, its points, then the merged path's remainder (merges are followed,
     *  cycles stop). bOutReaches: it ends inside its target team's objective. */
    CIRESTEAMSURVIVAL_API TArray<FVector2D> WalkPolyline(const FCireMapLayout& Layout, const FString& PathId, bool* bOutReaches = nullptr);
    CIRESTEAMSURVIVAL_API double PolylineLength(const TArray<FVector2D>& Points);
    /** Point-in-polygon for the play bounds (realm-local). No bounds marker: everything is inside. */
    CIRESTEAMSURVIVAL_API bool InsideBounds(const FCireMapLayout& Layout, const FVector2D& Local, int32 Realm);

    // ---- authoring (every edit keeps mirrored twins in sync) ------------------------------------------------------
    /** Place a marker of Type at Local. Owner Shared or the type's default owner decide the team; a mirrored team
     *  marker also gets its twin for the other team. Returns the new id (empty when the type is full). */
    CIRESTEAMSURVIVAL_API FString Place(FCireMapLayout& Layout, FName Type, const FVector2D& Local, ECireMarkerOwner Owner, float Yaw = 0.f, bool bMirror = true);
    /** Remove a marker (and its twin). Paths from a removed spawn lose their link; merges into a removed path end. */
    CIRESTEAMSURVIVAL_API bool Remove(FCireMapLayout& Layout, const FString& Id);
    /** Replace: move a marker (paths and bounds move as a whole) keeping everything else. */
    CIRESTEAMSURVIVAL_API bool Move(FCireMapLayout& Layout, const FString& Id, const FVector2D& Local);
    CIRESTEAMSURVIVAL_API bool SetYaw(FCireMapLayout& Layout, const FString& Id, float Yaw);
    CIRESTEAMSURVIVAL_API bool SetRadius(FCireMapLayout& Layout, const FString& Id, float Radius);
    CIRESTEAMSURVIVAL_API bool SetTier(FCireMapLayout& Layout, const FString& Id, int32 Tier);
    CIRESTEAMSURVIVAL_API bool SetName(FCireMapLayout& Layout, const FString& Id, const FString& Name);
    /** Vendor type change: the sign and stall move to that type's default spots. */
    CIRESTEAMSURVIVAL_API bool SetKind(FCireMapLayout& Layout, const FString& Id, const FString& Kind);
    /** layout-wiring: a path's weight (0..100) and a spawn's split mode (by path weight, or even). */
    CIRESTEAMSURVIVAL_API bool SetWeight(FCireMapLayout& Layout, const FString& Id, float Weight);
    CIRESTEAMSURVIVAL_API bool SetSplit(FCireMapLayout& Layout, const FString& Id, bool bWeighted);
    /** Vendor sub-handles: move / rotate the sign or the stall on its own (Npc moves the whole group). */
    CIRESTEAMSURVIVAL_API bool MovePart(FCireMapLayout& Layout, const FString& Id, ECireVendorPart Part, const FVector2D& Local);
    CIRESTEAMSURVIVAL_API bool SetPartYaw(FCireMapLayout& Layout, const FString& Id, ECireVendorPart Part, float Yaw);
    /** Put the sign or the stall back at the vendor type's default spot (Remove on a sub-handle). */
    CIRESTEAMSURVIVAL_API bool ResetPart(FCireMapLayout& Layout, const FString& Id, ECireVendorPart Part);
    /** The stall footprint's four corners (realm-local). */
    CIRESTEAMSURVIVAL_API TArray<FVector2D> StallCorners(const FCireMapMarker& Vendor);
    /** Owner change: a mirrored pair swaps teams together; Shared drops the twin; Shared -> team makes a twin. */
    CIRESTEAMSURVIVAL_API bool SetOwner(FCireMapLayout& Layout, const FString& Id, ECireMarkerOwner Owner);
    CIRESTEAMSURVIVAL_API bool SetTarget(FCireMapLayout& Layout, const FString& Id, ECireMarkerOwner Target);
    /** Mirror on: (re)create the twin. Off: break the pair; both markers stay, independent. */
    CIRESTEAMSURVIVAL_API bool SetMirror(FCireMapLayout& Layout, const FString& Id, bool bMirror);
    /** Copy a marker's shared fields to its twin (owner/target swapped, links mapped to the twins). */
    CIRESTEAMSURVIVAL_API void SyncTwin(FCireMapLayout& Layout, const FString& Id);
    /** Monster Path / Play Bounds: chain a point to the previous one. With an empty PathId a new path starts (from
     *  FromSpawn, else the nearest monster spawn of the owner). Returns the path id. A path point inside its target's
     *  objective ends the path; one on another path of the same target merges into it. */
    CIRESTEAMSURVIVAL_API FString ChainPoint(FCireMapLayout& Layout, FName Type, const FString& PathId, const FVector2D& Local, ECireMarkerOwner Owner, const FString& FromSpawn = FString());
    /** Remove point Index of a path/bounds; the neighbours re-chain. A path with no points left is removed. */
    CIRESTEAMSURVIVAL_API bool RemovePoint(FCireMapLayout& Layout, const FString& Id, int32 Index);
    /** Replace point Index of a path/bounds, keeping the chain order. */
    CIRESTEAMSURVIVAL_API bool MovePoint(FCireMapLayout& Layout, const FString& Id, int32 Index, const FVector2D& Local);
    /** True when the path has ended (reaches its objective or merges). */
    CIRESTEAMSURVIVAL_API bool PathClosed(const FCireMapLayout& Layout, const FString& PathId);
    /** Clear: every marker of one type (NAME_None: all markers). */
    CIRESTEAMSURVIVAL_API int32 Clear(FCireMapLayout& Layout, FName Type = NAME_None);

    // ---- validation ----------------------------------------------------------------------------------------------
    /** Per team: player spawns, monster spawns that target it, an objective, every spawn with a path, every path
     *  reaching ITS objective; mirrored pairs in sync; markers inside the play bounds; with Checks, on the navmesh. */
    CIRESTEAMSURVIVAL_API TArray<FCireLayoutIssue> Validate(const FCireMapLayout& Layout, const FCireLayoutChecks* Checks = nullptr);

    // ---- documents -----------------------------------------------------------------------------------------------
    CIRESTEAMSURVIVAL_API FString ToJson(const FCireMapLayout& Layout);
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireMapLayout& Out, FString& Error);
    /** TownVendors.json, the shared vendor-layout schema feat/vendors reads: vendorId, team, npc {pos, yaw},
     *  sign {pos [x, y, height], yaw}, stall {pos, yaw, size [width, depth]} (realm-local cm, degrees). */
    CIRESTEAMSURVIVAL_API FString VendorsJson(const FCireMapLayout& Layout);
    CIRESTEAMSURVIVAL_API FString ActivePath();                 // Content/Data/MapLayout.json
    CIRESTEAMSURVIVAL_API FString VendorsPath();                // Content/Data/TownVendors.json
    CIRESTEAMSURVIVAL_API FString DraftPath();                  // Saved/MapLayoutDraft.json (autosave)
    CIRESTEAMSURVIVAL_API FString NamedPath(const FString& Name); // Content/Data/MapLayouts/<Name>.json
    CIRESTEAMSURVIVAL_API FString SanitizeName(const FString& Name);
    CIRESTEAMSURVIVAL_API TArray<FString> ListNamed();
    CIRESTEAMSURVIVAL_API bool Save(const FCireMapLayout& Layout, const FString& Path, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool Load(FCireMapLayout& Layout, const FString& Path, FString* Error = nullptr);
    /** layout-wiring: "castletown" or "procedural" for the running map. */
    CIRESTEAMSURVIVAL_API FString ActiveMap();
    /** The layout belongs to the running map (no "map" key = the procedural town, where the editor started). */
    CIRESTEAMSURVIVAL_API bool MatchesActiveMap(const FCireMapLayout& Layout);

    // ---- the live route document ------------------------------------------------------------------------------------
    /** Seed a layout from a route document: per realm a monster spawn at the wave start, its path, the objective at
     *  the goal zone and the challenge packs (identical realms become mirrored pairs). */
    CIRESTEAMSURVIVAL_API FCireMapLayout FromRoutes(const FCireBattlefieldRoutes& Routes);
    /** Compile into a route document (bounds, lane width and escort tuning from Base). layout-wiring: per realm every monster
     *  spawn that targets the realm's team and every path from it (merges followed, closed into the objective), the
     *  challenge packs, the objective (goal zone), player spawns, respawns, boss spawns, rifts and the play bounds.
     *  Notes lists adjustments and what the runtime ignores. Returns false when a realm has no path. */
    CIRESTEAMSURVIVAL_API bool CompileRoutes(const FCireMapLayout& Layout, const FCireBattlefieldRoutes& Base, FCireBattlefieldRoutes& Out, TArray<FString>& Notes);
}
