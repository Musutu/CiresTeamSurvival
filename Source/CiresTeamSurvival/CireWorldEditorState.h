#pragma once
// world-editor: the WORLD tab of the map layout editor (CireWorldEditorHUD.cpp draws and drives it; Docs/WorldEditor.md).
#include "CoreMinimal.h"
#include "CireWorldEdit.h"
#include "CireRouteEditor.h"

class UWorld;
struct FCireLayoutEditorState;

struct FCireWorldEditorState
{
    FCireWorldEditDoc Doc;
    bool bLoaded = false;
    // The loaded town's removable units (rebuilt when levels stream in or out).
    TArray<FCireWorldUnit> Units;
    TMap<FString, int32> ByKey;
    int32 IndexLevels = -1;
    // What the world shows: key -> shown as a ghost (every key here is hidden / ghosted in the world).
    TMap<FString, bool> Applied;
    int32 AppliedRevision = -1;
    bool bAppliedGhosts = false;
    // Options.
    bool bMirror = true, bGhosts = false;
    // Selection (unit keys; a removed unit selected through its ghost or the REMOVED list), hover.
    TArray<FString> Sel;
    FString Hover;
    bool bHoverGhost = false;
    // Box select (screen, logical units).
    bool bPressing = false, bDragging = false;
    FVector2D PressAt = FVector2D::ZeroVector;
    // REMOVED list.
    int32 ListScroll = 0;
    double LastRowClick = -10; FString LastRowKey;
    bool bLoadList = false;
    // Validation: issues and the path reachability of each monster path segment ("realm|path|segment").
    TArray<FCireLayoutIssue> Issues;
    bool bValidated = false;
    TMap<FString, ECireRouteReach> Baseline, LastReach;
    // Autosave (Saved/WorldEditDraft.json).
    bool bUnsaved = false;
    double EditedAt = 0, SavedAt = -1;
    bool bNoFiles = false;
};

namespace CireWorldEditor
{
    /** Load the draft (Saved/WorldEditDraft.json), else the active set, else an empty set. */
    CIRESTEAMSURVIVAL_API void Load(FCireWorldEditorState& W);
    /** Rebuild the unit index when the loaded levels changed (or bForce). */
    CIRESTEAMSURVIVAL_API void RefreshIndex(UWorld* World, FCireWorldEditorState& W, bool bForce = false);
    /** Make the world show the set: removed units hidden (or ghosted), restored ones back. */
    CIRESTEAMSURVIVAL_API void Sync(UWorld* World, FCireWorldEditorState& W);
    /** Bring every removed unit back in the world (leaving the editor / tests). */
    CIRESTEAMSURVIVAL_API void Unsync(UWorld* World, FCireWorldEditorState& W);
    /** Remove the selection (protected units kept, mirrored twins added when on). Returns a message. */
    CIRESTEAMSURVIVAL_API FString RemoveKeys(FCireWorldEditorState& W, const FCireMapLayout& Layout, const TArray<FString>& Keys);
    CIRESTEAMSURVIVAL_API FString RestoreKeys(FCireWorldEditorState& W, const TArray<FString>& Keys);
    /** Validate: marker / path issues of the set, markers off the navmesh, paths blocked or unblocked since the baseline. */
    CIRESTEAMSURVIVAL_API void Validate(UWorld* World, FCireWorldEditorState& W, const FCireMapLayout& Layout, bool bNav);
    CIRESTEAMSURVIVAL_API void Autosave(FCireWorldEditorState& W, double Now, bool bForce = false);
    /** SAVE: Content/Data/WorldEdits/<name>.json and make it the active set. */
    CIRESTEAMSURVIVAL_API bool SaveAndActivate(FCireWorldEditorState& W, const FString& Name, FString& OutMessage);
    /** Esc in the WORLD tab: true when it consumed the key. */
    CIRESTEAMSURVIVAL_API bool Escape(FCireLayoutEditorState& E);
}
