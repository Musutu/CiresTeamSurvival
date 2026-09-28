#pragma once
// ability-tuner (Playtest 6 section M): live per-ability overrides on top of Abilities.json, AbilitiesExpansion.json
// and CastRules. Ships in the final release (never wrapped in !UE_BUILD_SHIPPING). See Docs/AbilityTuner.md.
//
// Model: an override set = { ability id -> FCireAbilityOverride }. Numeric fields are addressed generically by their
// JSON path in the ability row ("base.cooldown", "effects.0.duration", "scaling.primary", "recipe.buffSeconds", ...),
// so every number of every row is editable. Applying an override rebuilds that one row through the normal
// CireAbilityDB parser (validation, AoE scaling, CastRules) and swaps it in place; a "castTime" override then beats
// the cast rule. Reset = the row rebuilt from the untouched file row.
//
// Authority: the server (listen-server host or single player) owns the set; ACireAbilityTunerState replicates it to
// every client, which rebuilds the same rows locally (tooltips, action bars, Skill Shop). Remote clients can never
// tune. The "Allow ability tuning" game option gates the host (on in dev/editor and custom games, off by default in a
// shipping build unless -CireAllowTuning or a mode preset turns it on).
#include "CoreMinimal.h"

class UWorld;
class APlayerController;
class ACireGameMode;
class FJsonObject;
struct FCireAbilityDef;

/** Everything the tuner may change on one ability. Unset members mean "the file value". */
struct CIRESTEAMSURVIVAL_API FCireAbilityOverride
{
    TMap<FString, double> Fields;          // numeric row path -> value
    TOptional<FString> Name, Description;  // display name / tooltip text ("{effect}" is filled in)
    TOptional<bool> bEnabled;              // false: hidden from shops, cannot be cast
    TOptional<float> VfxScale;             // multiplies the ability's Fab VFX scale
    TOptional<FLinearColor> VfxTint;       // recolours the ability's Fab VFX (hue / saturation)
    bool IsEmpty() const { return Fields.IsEmpty() && !Name.IsSet() && !Description.IsSet() && !bEnabled.IsSet() && !VfxScale.IsSet() && !VfxTint.IsSet(); }
    bool operator==(const FCireAbilityOverride& O) const;
};
using FCireTuningSet = TMap<FString, FCireAbilityOverride>;

/** One editable number of an ability row (the generic field editor). */
struct CIRESTEAMSURVIVAL_API FCireTunerField
{
    FString Path, Label, Group, Help;
    double Default = 0, Value = 0, Min = 0, Max = 1, Step = 1;
    bool bInteger = false, bOverridden = false;
};

/** A named profile in a profiles file. */
struct CIRESTEAMSURVIVAL_API FCireTuningProfile
{
    FString Name, Notes;
    FCireTuningSet Abilities;
};

namespace CireAbilityTuner
{
    // ---- the active set (process-wide; the server's copy replicates) -----------------------------------------------
    CIRESTEAMSURVIVAL_API const FCireTuningSet& Active();
    CIRESTEAMSURVIVAL_API const FCireAbilityOverride* Find(const FString& Id);
    /** Bumped on every change (UIs cache on it: Skill Shop, Hero Creator ability cards, tooltips). */
    CIRESTEAMSURVIVAL_API uint32 Version();
    /** Fired after every change of the active set (after the rows were rebuilt). */
    CIRESTEAMSURVIVAL_API FSimpleMulticastDelegate& OnChanged();
    CIRESTEAMSURVIVAL_API bool IsDisabled(const FString& Id);
    /** Tuned VFX of an ability id or display name; false when the ability has no VFX override. */
    CIRESTEAMSURVIVAL_API bool VfxFor(const FString& IdOrName, float& OutScale, FLinearColor& OutTint);

    // ---- local application (no network; used by the server, by OnRep on clients, and by tests) --------------------
    /** Validates + applies one ability's override (empty = reset). On failure nothing changes and Error says why. */
    CIRESTEAMSURVIVAL_API bool ApplyLocal(const FString& Id, const FCireAbilityOverride& Override, FString* Error = nullptr);
    /** Replaces the whole active set (rows not in it return to the file values). Invalid entries are skipped and reported. */
    CIRESTEAMSURVIVAL_API bool ApplySetLocal(const FCireTuningSet& Set, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API void ResetAllLocal();
    /** Builds the row an override would produce, without applying it (tooltip previews, validation). */
    CIRESTEAMSURVIVAL_API bool BuildRow(const FString& Id, const FCireAbilityOverride& Override, FCireAbilityDef& OutRow, FString* Error = nullptr);
    /** Called by CireAbilityDB::Reload: forget cached file rows, apply the startup profile once, re-apply the set. */
    CIRESTEAMSURVIVAL_API void OnDatabaseReloaded();

    // ---- generic field editor ---------------------------------------------------------------------------------------
    /** Every numeric leaf of the ability's file row (plus level-15 scales), with defaults, current values and ranges. */
    CIRESTEAMSURVIVAL_API TArray<FCireTunerField> Fields(const FString& Id);
    /** "effects.0.duration" -> "Effect 1 (slow): duration". */
    CIRESTEAMSURVIVAL_API FString FieldLabel(const FString& Id, const FString& Path);
    /** The untouched file row (name / description defaults); null for an unknown id. */
    CIRESTEAMSURVIVAL_API TSharedPtr<FJsonObject> FileRow(const FString& Id);

    // ---- JSON ---------------------------------------------------------------------------------------------------------
    CIRESTEAMSURVIVAL_API TSharedPtr<FJsonObject> ToJson(const FCireAbilityOverride& Override);
    CIRESTEAMSURVIVAL_API bool FromJson(const TSharedPtr<FJsonObject>& Json, FCireAbilityOverride& Out);
    CIRESTEAMSURVIVAL_API FString SerializeSet(const FCireTuningSet& Set);
    CIRESTEAMSURVIVAL_API bool DeserializeSet(const FString& Text, FCireTuningSet& Out, FString* Error = nullptr);

    // ---- profiles (save / load / export / import) ----------------------------------------------------------------------
    /** Where profiles are written: the editor writes Content/Data/AbilityOverrides.json, a packaged game
     *  <Saved>/Tuning/AbilityOverrides.json (bPackaged picks explicitly; tests use it). */
    CIRESTEAMSURVIVAL_API FString ProfilesPath(bool bPackaged);
    CIRESTEAMSURVIVAL_API FString DefaultProfilesPath();
    CIRESTEAMSURVIVAL_API FString ExportDirectory();
    CIRESTEAMSURVIVAL_API FString ImportDirectory();
    CIRESTEAMSURVIVAL_API bool LoadProfiles(const FString& Path, TArray<FCireTuningProfile>& Out, FString* StartupProfile = nullptr, FString* Error = nullptr);
    /** Adds / replaces one profile in the file (read-modify-write, keeps the others). */
    CIRESTEAMSURVIVAL_API bool SaveProfile(const FString& Path, const FCireTuningProfile& Profile, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool DeleteProfile(const FString& Path, const FString& Name, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool SetStartupProfile(const FString& Path, const FString& Name, FString* Error = nullptr);
    /** All profiles visible to this build: shipped (Content/Data) first, then the user's Saved ones (same name wins). */
    CIRESTEAMSURVIVAL_API TArray<FCireTuningProfile> AllProfiles();
    CIRESTEAMSURVIVAL_API bool FindProfile(const FString& Name, FCireTuningProfile& Out);
    /** One profile as a standalone JSON file (share it; Import reads the same format). */
    CIRESTEAMSURVIVAL_API bool ExportProfile(const FCireTuningProfile& Profile, const FString& FilePath, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool ImportProfile(const FString& FilePath, FCireTuningProfile& Out, FString* Error = nullptr);
    /** Exported / importable profile files (Saved/Tuning/Exports and Saved/Tuning/Imports). */
    CIRESTEAMSURVIVAL_API TArray<FString> ImportCandidates();

    // ---- authority, access and replication ------------------------------------------------------------------------------
    /** The "Allow ability tuning" default: on in the editor and development builds, off in shipping unless -CireAllowTuning. */
    CIRESTEAMSURVIVAL_API bool AllowedByDefault(bool bShippingBuild, const TCHAR* CommandLine);
    CIRESTEAMSURVIVAL_API bool IsAllowed(const UWorld* World);
    /** True for the listen-server host / single player's local controller while tuning is allowed. */
    CIRESTEAMSURVIVAL_API bool CanTune(const APlayerController* Controller, FString* Why = nullptr);
    /** The host may flip the option before the first wave unless a mode preset locked it. */
    CIRESTEAMSURVIVAL_API bool CanChangeAllowed(const APlayerController* Controller, FString* Why = nullptr);
    CIRESTEAMSURVIVAL_API bool SetAllowed(const APlayerController* Controller, bool bAllow, FString* Why = nullptr);
    /** Server-authoritative change: applies locally and replicates (no access check: server code, presets, probes). */
    CIRESTEAMSURVIVAL_API bool CommitAuthority(UWorld* World, const FString& Id, const FCireAbilityOverride& Override, FString* Why = nullptr);
    CIRESTEAMSURVIVAL_API bool CommitSetAuthority(UWorld* World, const FCireTuningSet& Set, const FString& ProfileName, FString* Why = nullptr);
    /** UI entry point: CanTune + CommitAuthority. */
    CIRESTEAMSURVIVAL_API bool Request(const APlayerController* Controller, const FString& Id, const FCireAbilityOverride& Override, FString* Why = nullptr);
    CIRESTEAMSURVIVAL_API bool RequestSet(const APlayerController* Controller, const FCireTuningSet& Set, const FString& ProfileName, FString* Why = nullptr);
    /** Name of the profile the match is running ("" = hand edits / none). */
    CIRESTEAMSURVIVAL_API FString ActiveProfileName(const UWorld* World);
    /** Match start (ACireGameMode::BeginPlay): spawns the replicated state, applies -CireTuningProfile=Name. */
    CIRESTEAMSURVIVAL_API void InitializeServer(ACireGameMode* Mode);
    /** Game-mode preset adapter (feat/waves-modes WavePresets.json): "tuningProfile" (name, "" = none) and
     *  "allowTuning" (-1 keep, 0 off + locked, 1 on). Server only. */
    CIRESTEAMSURVIVAL_API bool ApplyModePreset(UWorld* World, const FString& TuningProfile, int32 AllowTuning, FString* Why = nullptr);
    CIRESTEAMSURVIVAL_API bool ApplyModePresetJson(UWorld* World, const TSharedPtr<FJsonObject>& Preset, FString* Why = nullptr);
    /** Adapter for CireWaveDirector::SelectPreset / match init: reads the preset's raw row in WavePresets.json
     *  ("tuningProfile", "allowTuning"). A preset without them returns a preset-applied profile to the startup set. */
    CIRESTEAMSURVIVAL_API bool ApplyWavePreset(UWorld* World, FName PresetId, FString* Why = nullptr);
    /** Number of connected remote players that receive the set (UI status line). */
    CIRESTEAMSURVIVAL_API int32 RemoteViewers(const UWorld* World);

    // ---- testing helpers (host) -------------------------------------------------------------------------------------------
    /** Spawns an immobile, harmless, very durable target dummy in front of the host's champion. */
    CIRESTEAMSURVIVAL_API bool SpawnTargetDummy(const APlayerController* Controller, FString* Why = nullptr);
    /** Casts the ability now (grants it to the host champion for testing when unknown; refunds cost and cooldown). */
    CIRESTEAMSURVIVAL_API bool CastNow(const APlayerController* Controller, const FString& Id, FString* Why = nullptr);

    // ---- probes -------------------------------------------------------------------------------------------------------------
    /** Network probe: the override the dedicated server pushes, and the client-side verification. */
    CIRESTEAMSURVIVAL_API void PushNetProbeOverride(UWorld* World);
    CIRESTEAMSURVIVAL_API bool VerifyNetProbeOnClient(const APlayerController* Controller, FString* Why = nullptr);
    /** Native checks: apply/reset, validation, CastRules precedence, JSON + profile round trip, shipping-safe paths. */
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
}
