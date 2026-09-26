#pragma once
// dev-route-tools: the map layout editor's state and actions (CireLayoutEditorHUD.cpp draws and drives it; the
// -CireLayoutGallery session scripts it). See CireMapLayout.h for the data model and Docs/MapLayout.md.
#include "CoreMinimal.h"
#include "CireMapLayout.h"

class ACameraActor;
class ACireMonster;
class UWorld;

/** A picked marker: the whole marker, one path/bounds point, or a vendor sub-handle (sign / stall). */
struct FCireLayoutPick
{
    FString Id;
    int32 Point = INDEX_NONE;
    ECireVendorPart Part = ECireVendorPart::Npc;
    bool IsSet() const { return !Id.IsEmpty(); }
    void Reset() { Id.Reset(); Point = INDEX_NONE; Part = ECireVendorPart::Npc; }
    bool Same(const FCireLayoutPick& O) const { return Id == O.Id && Point == O.Point && Part == O.Part; }
};

/** Preview: one monster walking one path in one realm (navmesh paths between the path points). */
struct FCireLayoutWalker
{
    FString PathId;
    int32 Realm = 0;
    TWeakObjectPtr<ACireMonster> Unit;
    TArray<FVector> Points;
    int32 Next = 1;
    double Finished = -1;
    float Walked = 0.f;
    FVector Last = FVector::ZeroVector;
};

struct FCireLayoutEditorState
{
    FCireMapLayout Layout;
    TArray<FCireMapLayout> History;
    bool bLoaded = false;
    /** Walk view (default): the champion walks the town. Map view: the top-down editor camera. */
    bool bWalk = true;
    /** The realm being viewed (the champion's realm in walk view). */
    int32 Realm = 0;
    /** Setter armed for placement (NAME_None: nothing armed). */
    FName Armed;
    /** Replace armed for the selection: the next click (or R again) moves it. */
    bool bReplace = false;
    /** The monster path / play bounds being chained, and the spawn a new path starts from. */
    FString ChainId, ChainFrom;
    FCireLayoutPick Sel, Hover;
    /** Radius / tier / vendor type the next placed marker gets (per setter radius). */
    TMap<FName, float> NextRadius;
    int32 NextTier = 1;
    // List panel filters: team -1 all, 0 shared, 1 T1, 2 T2; type -1 all, else the setter index.
    int32 TeamFilter = -1, TypeFilter = -1, ListScroll = 0;
    double LastRowClick = -10; FString LastRowId;
    // Validation (light on every edit; Validate adds the navmesh checks).
    TArray<FCireLayoutIssue> Issues;
    bool bIssuesDirty = true, bNavChecked = false, bShowIssues = false;
    double ValidatedAt = -10;
    /** Walked length per realm and path id (cm), from the navmesh when checked, else straight segments. */
    TMap<FString, double> WalkLength[2];
    // Autosave (Saved/MapLayoutDraft.json).
    bool bUnsaved = false;
    double EditedAt = 0, SavedAt = -1;
    /** Gallery / tests: never write the draft file. */
    bool bNoFiles = false;
    // Text entry: 0 none, 1 marker name, 2 layout name (Save As).
    int32 Naming = 0;
    FString NameBuffer;
    bool bLoadList = false;
    // Preview.
    TArray<FCireLayoutWalker> Walkers;
    bool bPreview = false;
    double PreviewStarted = 0;
    float PreviewSpeed = 200.f;
    // Map view camera.
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AActor> PreviousView;
    FVector Focus = FVector::ZeroVector;
    float Yaw = 180.f, Pitch = -62.f, Distance = 6200.f;
    FString Message;
    double MessageAt = -100;
#if !UE_BUILD_SHIPPING
    /** Gallery: a virtual ground point under the pointer. */
    bool bDebugGround = false;
    FVector DebugGround = FVector::ZeroVector;
#endif
};

namespace CireLayoutEditor
{
    /** Apply a change with undo (the draft before it is pushed only when the change reports success). */
    CIRESTEAMSURVIVAL_API bool Edit(FCireLayoutEditorState& E, TFunctionRef<bool(FCireMapLayout&)> Change, double Now);
    CIRESTEAMSURVIVAL_API bool Undo(FCireLayoutEditorState& E, double Now);
    CIRESTEAMSURVIVAL_API void Say(FCireLayoutEditorState& E, const FString& Message, double Now);
    /** Validate the draft; bNav adds navmesh, walkability and sign-room checks in this world. */
    CIRESTEAMSURVIVAL_API void RunValidation(UWorld* World, FCireLayoutEditorState& E, bool bNav);
    /** Preview: a monster walks every monster path in every realm it shows in. */
    CIRESTEAMSURVIVAL_API void StartPreview(UWorld* World, FCireLayoutEditorState& E);
    CIRESTEAMSURVIVAL_API void StopPreview(FCireLayoutEditorState& E);
    CIRESTEAMSURVIVAL_API void TickPreview(UWorld* World, FCireLayoutEditorState& E, float DeltaSeconds);
    /** Apply: MapLayout.json + TownVendors.json, then the compiled march routes and packs applied live and saved. */
    CIRESTEAMSURVIVAL_API bool Apply(UWorld* World, FCireLayoutEditorState& E, FString& OutMessage);
    CIRESTEAMSURVIVAL_API void Load(UWorld* World, FCireLayoutEditorState& E);
    CIRESTEAMSURVIVAL_API void Autosave(FCireLayoutEditorState& E, double Now, bool bForce = false);
}
