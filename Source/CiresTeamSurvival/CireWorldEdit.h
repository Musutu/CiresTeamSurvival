#pragma once
// world-editor: choose which pieces of the Medieval Kingdom town are removed (Docs/WorldEditor.md).
//
// A WORLD EDIT SET is a named list of removed town pieces ("units"): a whole building (a pack Level Instance with all its
// nested pieces), or one top-level actor of a realm sublevel (a prop, a tree, a wall) with whatever is attached to it.
// Every unit is keyed by a STABLE id: "<realm sublevel>/<actor name>" (e.g. "SL_Houses/LevelInstance_17") plus the realm
// (0 DAYLIGHT, 1 DARKNIGHT). Both realms stream the same packages, so the same id names the twin in the other realm.
//
// Files: Content/Data/WorldEdits/<name>.json (named sets), Content/Data/WorldEdit.json (the ACTIVE set and the editor's
// tunables), Saved/WorldEditDraft.json (the editor's autosaved draft).
//
// Runtime: the active set applies at town load on the server and on every client, before play, like the trim: removed
// Level Instances are dropped from the streaming queue (or unloaded), removed actors are destroyed. The nav cache key
// carries the set's signature, so the navmesh rebuilds once for a new set. The server replicates the set name and hash
// (ACireWorld); a client loads the same file from its own checkout (like MapLayout.json) and warns on a mismatch.
// A set switched mid-match (cire.WorldEdit <name> / ApplySet) hides + un-collides at runtime and can be switched back.
//
// Editor: the WORLD tab of the map layout editor (CireWorldEditorHUD.cpp): hover highlights a unit, click / Shift-click /
// drag-box select, Delete removes, Ctrl+Z / Ctrl+Y undo / redo, REMOVED list with restore, ghosts, mirror, protection.
// In the editor removal only hides (nothing is destroyed), so everything can come back.
#include "CoreMinimal.h"

class ACireGameMode;
class AActor;
class ULevel;
class UWorld;
struct FCireMapLayout;
struct FCireLayoutIssue;

/** One removed unit. */
struct CIRESTEAMSURVIVAL_API FCireWorldEditEntry
{
    FString Id;                               // "<sublevel>/<actor name>": the realm sublevel without its _CireRealmN suffix
    int32 Realm = 0;
    FString Kind;                             // Building, Fortification, Nature, Prop
    FString Label;                            // Level Instance asset / mesh name
    FVector Local = FVector::ZeroVector;      // realm-local centre (x, y); z relative to the realm offset
    FVector Size = FVector::ZeroVector;       // bounds size (cm)
    int32 Actors = 0, DrawCalls = 0;          // what removing it saves (measured in the editor, per realm copy)
    FString Key() const { return FString::Printf(TEXT("%d|%s"), Realm, *Id); }
    FBox2D Footprint(float Pad = 0.f) const;  // realm-local
};

struct CIRESTEAMSURVIVAL_API FCireWorldEditSet
{
    FString Name;
    FString Map = TEXT("CastleTown");
    TArray<FCireWorldEditEntry> Removed;
    int32 IndexOf(const FString& Key) const;
    bool Contains(const FString& Key) const { return IndexOf(Key) != INDEX_NONE; }
    const FCireWorldEditEntry* Find(const FString& Key) const { const int32 I = IndexOf(Key); return I == INDEX_NONE ? nullptr : &Removed[I]; }
};

/** One undo step: what it removed and what it restored (a batch remove is one step). */
struct CIRESTEAMSURVIVAL_API FCireWorldEditStep
{
    FString What;
    TArray<FCireWorldEditEntry> Removed, Restored;
};

/** The editor's document: the set plus its undo / redo stacks. */
struct CIRESTEAMSURVIVAL_API FCireWorldEditDoc
{
    FCireWorldEditSet Set;
    TArray<FCireWorldEditStep> Undo, Redo;
    int32 Revision = 0;                       // bumps on every change
};

/** Content/Data/WorldEdit.json. */
struct CIRESTEAMSURVIVAL_API FCireWorldEditSettings
{
    FString Active;                                                              // the set matches load ("" = none)
    bool bMirror = true;                                                         // editor default: remove the twin too
    bool bGhosts = false;                                                        // editor default: show removed as ghosts
    TArray<FString> ProtectWords = {TEXT("Castle"), TEXT("Gate"), TEXT("Keep")}; // unit names guarded near markers/routes
    float ProtectPad = 150.f;                                                    // cm around a unit's footprint for the marker checks
    float BoxSelectMaxSize = 8000.f;                                             // box select skips units larger than this (cm)
};

enum class ECireWorldEditGuard : uint8 { Ok, Warn, Protected };
struct CIRESTEAMSURVIVAL_API FCireWorldEditGuard
{
    ECireWorldEditGuard Level = ECireWorldEditGuard::Ok;
    FString Why;
};

/** A removable unit found in the loaded town (editor index / picking). */
struct CIRESTEAMSURVIVAL_API FCireWorldUnit
{
    FString Id;
    int32 Realm = 0;
    TWeakObjectPtr<AActor> Actor;
    bool bLevelInstance = false;
    FString Kind, Label;
    FBox Bounds = FBox(ForceInit);
    int32 Actors = 0, DrawCalls = 0;
    FString Key() const { return FString::Printf(TEXT("%d|%s"), Realm, *Id); }
};

/** What an active set saves (per match, both realms). */
struct CIRESTEAMSURVIVAL_API FCireWorldEditSavings
{
    int32 Units = 0, Actors = 0, DrawCalls = 0;
};

namespace CireWorldEdit
{
    // ---- data ------------------------------------------------------------------------------------------------------------
    CIRESTEAMSURVIVAL_API FString Dir();                                 // Content/Data/WorldEdits
    CIRESTEAMSURVIVAL_API FString NamedPath(const FString& Name);
    CIRESTEAMSURVIVAL_API FString SettingsPath();                        // Content/Data/WorldEdit.json
    CIRESTEAMSURVIVAL_API FString DraftPath();                           // Saved/WorldEditDraft.json
    CIRESTEAMSURVIVAL_API FString SanitizeName(const FString& Name);     // "" when unusable
    CIRESTEAMSURVIVAL_API TArray<FString> ListNamed();
    CIRESTEAMSURVIVAL_API FString ToJson(const FCireWorldEditSet& Set);
    CIRESTEAMSURVIVAL_API bool FromJson(const FString& Json, FCireWorldEditSet& Out, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool Save(const FCireWorldEditSet& Set, const FString& Path, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool Load(FCireWorldEditSet& Out, const FString& Path, FString* Error = nullptr);
    /** Order-independent hash of the removed keys ("" for an empty set). */
    CIRESTEAMSURVIVAL_API FString Hash(const FCireWorldEditSet& Set);
    CIRESTEAMSURVIVAL_API FCireWorldEditSavings Savings(const FCireWorldEditSet& Set);

    CIRESTEAMSURVIVAL_API const FCireWorldEditSettings& Settings();
    CIRESTEAMSURVIVAL_API FCireWorldEditSettings ParseSettings(const FString& Json);
    /** Write WorldEdit.json with a new active set name (keeps the other keys). */
    CIRESTEAMSURVIVAL_API bool SaveActiveName(const FString& Name, FString* Error = nullptr);

    // ---- stable ids -------------------------------------------------------------------------------------------------------
    /** A realm sublevel ("SL_Houses_CireRealm0" -> "SL_Houses", realm 0). False for any other level (Level Instance
        levels, World Partition cells, the persistent level). */
    CIRESTEAMSURVIVAL_API bool ParseRealmLevel(const FString& PackageShortName, FString& OutSublevel, int32& OutRealm);
    CIRESTEAMSURVIVAL_API bool RealmLevel(const ULevel* Level, FString& OutSublevel, int32& OutRealm);
    /** "<sublevel>/<actor name>" for an actor directly in a realm sublevel, else "". */
    CIRESTEAMSURVIVAL_API FString ActorId(const AActor* Actor, int32* OutRealm = nullptr);
    CIRESTEAMSURVIVAL_API FString KindOf(const FString& Label, bool bLevelInstance);

    // ---- editing (pure; the editor and the tests) ---------------------------------------------------------------------
    /** Remove entries not yet removed; one undo step. Returns how many were added. */
    CIRESTEAMSURVIVAL_API int32 Remove(FCireWorldEditDoc& Doc, const TArray<FCireWorldEditEntry>& Entries, const FString& What);
    /** Restore removed entries by key; one undo step. Returns how many. */
    CIRESTEAMSURVIVAL_API int32 Restore(FCireWorldEditDoc& Doc, const TArray<FString>& Keys, const FString& What);
    CIRESTEAMSURVIVAL_API bool Undo(FCireWorldEditDoc& Doc, FString* OutWhat = nullptr);
    CIRESTEAMSURVIVAL_API bool Redo(FCireWorldEditDoc& Doc, FString* OutWhat = nullptr);
    /** Replace the whole set (LOAD / NEW) as one undoable step. */
    CIRESTEAMSURVIVAL_API void ReplaceSet(FCireWorldEditDoc& Doc, const FCireWorldEditSet& Set, const FString& What);
    /** Entries plus their twins in the other realm (same id) where TwinExists says the twin is there. */
    CIRESTEAMSURVIVAL_API TArray<FCireWorldEditEntry> WithTwins(const TArray<FCireWorldEditEntry>& Entries, TFunctionRef<bool(const FString& Key)> TwinExists, int32* OutTwins = nullptr);
    CIRESTEAMSURVIVAL_API FCireWorldEditEntry TwinOf(const FCireWorldEditEntry& Entry);

    // ---- protection and validation ----------------------------------------------------------------------------------------
    /** Can this unit go? Protected: a marker the game needs stands in it (vendor / sign / stall, objective, player / monster
        / boss spawn, challenge pack, respawn, recall point, rift), or it is a castle / gate piece on a monster path.
        Warn: a monster path crosses it, or a castle / gate piece anywhere. */
    CIRESTEAMSURVIVAL_API FCireWorldEditGuard Guard(const FCireMapLayout& Layout, const FCireWorldEditEntry& Entry, const FCireWorldEditSettings& S);
    /** Validate: markers that stood in / on a removed unit (they may float or lose their ground), paths through one. */
    CIRESTEAMSURVIVAL_API TArray<FCireLayoutIssue> Issues(const FCireMapLayout& Layout, const FCireWorldEditSet& Set, const FCireWorldEditSettings& S);

    // ---- the loaded town (editor picking, runtime switch) -----------------------------------------------------------------
    /** Every removable unit of both realms (Level Instances and top-level rendering actors of the realm sublevels). */
    CIRESTEAMSURVIVAL_API void BuildIndex(UWorld* World, TArray<FCireWorldUnit>& Out);
    /** The unit a hit actor belongs to (its outermost pack Level Instance, else its root attach parent); null if none. */
    CIRESTEAMSURVIVAL_API AActor* UnitActorOf(UWorld* World, AActor* Hit);
    CIRESTEAMSURVIVAL_API bool DescribeUnit(UWorld* World, AActor* UnitActor, FCireWorldUnit& Out);
    CIRESTEAMSURVIVAL_API FCireWorldEditEntry EntryOf(const FCireWorldUnit& Unit);
    /** Every actor that belongs to a unit (the Level Instance's contents, nested, or the actor and its attached actors). */
    CIRESTEAMSURVIVAL_API void ForEachUnitActor(UWorld* World, AActor* UnitActor, TFunctionRef<void(AActor*)> Visit);
    /** Reversible removal: hidden, no collision, out of the navmesh (bGhost: drawn, still no collision). */
    CIRESTEAMSURVIVAL_API void SetUnitRemoved(UWorld* World, AActor* UnitActor, bool bRemoved, bool bGhost = false);

    // ---- the active set at runtime (public API for game types: feat/game-profiles) ---------------------------------------
    /** The set this process loads / runs ("" = none): ApplySet / the server's choice / -CireWorldEdit=<name|off> /
        WorldEdit.json "active". */
    CIRESTEAMSURVIVAL_API FString ActiveSet();
    /** Select a set by name ("" or "off" = none). Before the town loads it is what the load applies; after, it is applied
        live (hide + no collision + navmesh update; LIs removed at load come back by reloading). On a server the choice
        replicates to every client. Returns false when the named file is missing or unreadable. */
    CIRESTEAMSURVIVAL_API bool ApplySet(UWorld* World, const FString& Name);
    /** Back to the default set (-CireWorldEdit= / WorldEdit.json "active"), live when the town is loaded. */
    CIRESTEAMSURVIVAL_API bool ApplyDefault(UWorld* World);
    /** feat/game-profiles: register ApplySet / ListNamed with CireGameProfiles (a game type's "worldEdit": "<set>";
        Default = ApplyDefault). Idempotent; runs at engine init and when the town loads. */
    CIRESTEAMSURVIVAL_API void RegisterWithGameProfiles();
    /** The set frozen for this load (empty set when none). */
    CIRESTEAMSURVIVAL_API const FCireWorldEditSet& Current();
    /** The world edit applies in this process (a set with entries, the town, not the layout editor). */
    CIRESTEAMSURVIVAL_API bool Active();
    /** Part of the nav cache key ("" without a set). */
    CIRESTEAMSURVIVAL_API FString Signature();
    /** Client: the server's set (ACireWorld replication), before the town loads or live after. */
    CIRESTEAMSURVIVAL_API void SetFromServer(UWorld* World, const FString& Name, const FString& Hash);
    /** Tests: force a set (nullptr restores the real one). */
    CIRESTEAMSURVIVAL_API void SetOverride(const FCireWorldEditSet* Set);

    // ---- load hooks (CireTownMap::LoadRealms / PrepareRealmLevels) -------------------------------------------------------
    CIRESTEAMSURVIVAL_API void BeginLoad(UWorld* World);
    /** Drop queued loads of removed Level Instances (before they stream). */
    CIRESTEAMSURVIVAL_API int32 FilterLevelInstances(UWorld* World);
    /** A realm level just became visible: remove its units in the set. */
    CIRESTEAMSURVIVAL_API void ApplyLevel(UWorld* World, ULevel* Level);
    /** CIRE_WORLD_EDIT summary (after streaming). */
    CIRESTEAMSURVIVAL_API void LogSummary(UWorld* World);
    /** Is this unit gone in this world (unloaded / destroyed / removed live)? The probe's check. */
    CIRESTEAMSURVIVAL_API bool IsGone(UWorld* World, const FCireWorldEditEntry& Entry);

    /** CIRE_WORLD_EDIT_TESTS_PASS / _FAIL (part of -CireCombatExpansionProbe). */
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
    /** -CireWorldEditProbe: the town-load probe (server and client). Called from ACireWorld::Tick; returns true once done. */
    CIRESTEAMSURVIVAL_API void TickProbe(UWorld* World);
    /** -CireWorldEditGallery: the scripted, captured WORLD tab session (CireWorldEditGallery.cpp). */
    CIRESTEAMSURVIVAL_API void TickGallery(UWorld* World);
}
