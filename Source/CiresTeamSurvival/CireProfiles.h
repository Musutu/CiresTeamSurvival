#pragma once
// game-profiles: named profiles for every F8 editor, and game types (WavePresets.json rows) that bundle a profile of each.
// Docs/RESUME-game-profiles.md.
//
//  Storage (one pattern for every domain): Content/Data/Profiles/<Domain>/<name>.json
//      {"schemaVersion":1, "domain":"Economy", "name":"Rich Start", "values":{...the domain's tunables...}}
//  "Default" is never a file: it is the domain's own data file(s) as they are today (CireDeveloper.ini, LootTables.json +
//  SkillShop.json + Vendors.json pricing, JunglePacks.json, MovementTuning.json, UnitSpacing.json). Loading Default re-reads
//  them, so nothing changes for existing data.
//
//  Game type bundle (WavePresets.json row, every key optional, missing / empty / "Default" = Default):
//      "layout" (Content/Data/MapLayouts/<name>.json), "tuningProfile" (Ability Tuner), "kitProfile" (Hero Creator),
//      "economyProfile", "packProfile", "movementProfile", "matchProfile", "spacingProfile", "worldEdit" (world editor set).
//  Applied in one place, CireGameProfiles::ApplyGameType (match init and the host's game type pick); clients apply the
//  data profiles themselves when ACireGameState::WavePreset replicates (same files on every machine).
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWorld;
class ACireGameMode;
class ACireGameState;
class APlayerController;
struct FCireWavePreset;
struct FCireDeveloperSettings;

namespace CireProfiles
{
    // ---- domains ----
    CIRESTEAMSURVIVAL_API extern const FName Match;     // F8 Match / Spawn-stats / Effects (FCireDeveloperSettings)
    CIRESTEAMSURVIVAL_API extern const FName Economy;   // kill gold + Skill Shop rules + shop pricing
    CIRESTEAMSURVIVAL_API extern const FName Packs;     // challenge-mob stats + per-tier health / damage
    CIRESTEAMSURVIVAL_API extern const FName Movement;  // MovementTuning.json
    CIRESTEAMSURVIVAL_API extern const FName Spacing;   // UnitSpacing.json (boss sizes, capsules, separation)
    CIRESTEAMSURVIVAL_API const TArray<FName>& Domains();
    CIRESTEAMSURVIVAL_API FString DomainLabel(FName Domain);   // "MATCH", "ECONOMY", ...
    CIRESTEAMSURVIVAL_API FString DefaultSource(FName Domain); // "LootTables.json + SkillShop.json + Vendors.json pricing"
    inline const TCHAR* DefaultName = TEXT("Default");
    CIRESTEAMSURVIVAL_API bool IsDefault(const FString& Name);  // "", "Default" (any case)

    // ---- the named-profile store ----
    CIRESTEAMSURVIVAL_API FString Root();                       // Content/Data/Profiles (or the test / probe override)
    CIRESTEAMSURVIVAL_API void SetRootOverride(const FString& Dir); // empty = Content/Data/Profiles
    CIRESTEAMSURVIVAL_API FString SanitizeName(const FString& Name);
    CIRESTEAMSURVIVAL_API FString PathFor(FName Domain, const FString& Name);
    /** "Default" first, then the saved profiles (display names) sorted. */
    CIRESTEAMSURVIVAL_API TArray<FString> List(FName Domain);
    CIRESTEAMSURVIVAL_API bool Exists(FName Domain, const FString& Name);
    CIRESTEAMSURVIVAL_API bool SaveValues(FName Domain, const FString& Name, const TSharedRef<FJsonObject>& Values, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool LoadValues(FName Domain, const FString& Name, TSharedPtr<FJsonObject>& Values, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool Rename(FName Domain, const FString& From, const FString& To, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool Delete(FName Domain, const FString& Name, FString* Error = nullptr);

    // ---- live state ----
    /** The domain's live tunables as profile values. */
    CIRESTEAMSURVIVAL_API TSharedRef<FJsonObject> Capture(FName Domain, UWorld* World);
    /** Applies profile values live (Match: development standalone only, reported as skipped elsewhere). */
    CIRESTEAMSURVIVAL_API bool ApplyValues(FName Domain, UWorld* World, const TSharedRef<FJsonObject>& Values, FString* Error = nullptr);
    /** Re-reads the domain's own data file(s). */
    CIRESTEAMSURVIVAL_API bool ApplyDefault(FName Domain, UWorld* World, FString* Error = nullptr);
    /** Save the live values under a name ("Default" is refused: use the page's SAVE to write the data file). */
    CIRESTEAMSURVIVAL_API bool SaveAs(FName Domain, UWorld* World, const FString& Name, FString* Error = nullptr);
    /** Load a profile (or Default) live and make it the domain's active profile. */
    CIRESTEAMSURVIVAL_API bool Load(FName Domain, UWorld* World, const FString& Name, FString* Error = nullptr);
    /** The active profile's display name ("Default" when none). Process-wide, like the data it names. */
    CIRESTEAMSURVIVAL_API FString Active(FName Domain);
    CIRESTEAMSURVIVAL_API void SetActive(FName Domain, const FString& Name);

    // Match domain helpers (the F8 pages save their draft, not the live overrides).
    CIRESTEAMSURVIVAL_API TSharedRef<FJsonObject> MatchToJson(const FCireDeveloperSettings& Settings);
    CIRESTEAMSURVIVAL_API bool MatchFromJson(const TSharedRef<FJsonObject>& Values, FCireDeveloperSettings& Out, FString* Error = nullptr);

#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
#endif
}

/** One game type's bundle: preset key -> profile name (empty = Default). */
namespace CireGameProfiles
{
    // Bundle keys in WavePresets.json.
    inline const TCHAR* KeyLayout = TEXT("layout");
    inline const TCHAR* KeyTuning = TEXT("tuningProfile");
    inline const TCHAR* KeyKit = TEXT("kitProfile");
    inline const TCHAR* KeyEconomy = TEXT("economyProfile");
    inline const TCHAR* KeyPacks = TEXT("packProfile");
    inline const TCHAR* KeyMovement = TEXT("movementProfile");
    inline const TCHAR* KeyMatch = TEXT("matchProfile");
    inline const TCHAR* KeySpacing = TEXT("spacingProfile");
    inline const TCHAR* KeyWorldEdit = TEXT("worldEdit");
    /** Every bundle key in display order. */
    CIRESTEAMSURVIVAL_API const TArray<FString>& Keys();
    CIRESTEAMSURVIVAL_API FString KeyLabel(const FString& Key);          // "ECONOMY"
    CIRESTEAMSURVIVAL_API FName DomainOfKey(const FString& Key);         // CireProfiles domain, NAME_None for layout/tuning/kit/worldEdit
    CIRESTEAMSURVIVAL_API FString KeyOfDomain(FName Domain);
    /** The preset's choice for a key (kitProfile reads FCireWavePreset::KitProfile). Empty = Default. */
    CIRESTEAMSURVIVAL_API FString Get(const FCireWavePreset& Preset, const FString& Key);
    CIRESTEAMSURVIVAL_API void Set(FCireWavePreset& Preset, const FString& Key, const FString& Name);
    /** The choices a key offers in the editor ("Default" first). */
    CIRESTEAMSURVIVAL_API TArray<FString> Choices(const FString& Key, UWorld* World);
    /** One line for the game type picker: "Economy Rich · Packs Hard" or "All editors on Default". */
    CIRESTEAMSURVIVAL_API FString Summary(const FCireWavePreset& Preset);
    /** What is live now, as a bundle (SAVE CURRENT AS GAME TYPE). */
    CIRESTEAMSURVIVAL_API void CaptureActive(UWorld* World, FCireWavePreset& Into);

    /** Server, the one place: tuning, layout, every data profile, the world edit set. Preset null = everything Default.
     *  bAtInit: called from CireWaveDirector::Initialize (a layout change reloads the routes instead of restarting). */
    CIRESTEAMSURVIVAL_API void ApplyGameType(ACireGameMode* Mode, const FCireWavePreset* Preset, bool bAtInit);
    /** The data profiles only (economy, packs, movement, match, spacing): server and clients. Returns how many changed. */
    CIRESTEAMSURVIVAL_API int32 ApplyDataProfiles(UWorld* World, const FCireWavePreset* Preset, FString* Report = nullptr);
    /** Client: ACireGameState::WavePreset replicated. */
    CIRESTEAMSURVIVAL_API void OnWavePresetReplicated(ACireGameState* State);

    // ---- layout: the game type's named layout replaces MapLayout.json at match time (the editor keeps MapLayout.json) ----
    CIRESTEAMSURVIVAL_API FString LayoutOverride();
    /** MapLayout.json, or the game type's Content/Data/MapLayouts/<name>.json. */
    CIRESTEAMSURVIVAL_API FString RuntimeLayoutPath();

    // ---- world editor (feat/world-editor): a tiny interface, registered by that module when it is merged ----
    using FWorldEditApply = TFunction<bool(UWorld* World, const FString& SetName, FString& Error)>;
    using FWorldEditList = TFunction<TArray<FString>()>;
    CIRESTEAMSURVIVAL_API void RegisterWorldEdit(FWorldEditApply Apply, FWorldEditList List);
    CIRESTEAMSURVIVAL_API FString ActiveWorldEdit();

#if !UE_BUILD_SHIPPING
    /** Network probe fixture (-CireNetServerProbe / -CireClientProbe): a runtime game type with non-default profiles. */
    CIRESTEAMSURVIVAL_API FName NetProbePresetId();
    CIRESTEAMSURVIVAL_API void EnsureNetProbeFixture();
    /** Server: picks the probe game type at match init. */
    CIRESTEAMSURVIVAL_API void InitializeProbePick(ACireGameMode* Mode, FName& Pick);
    CIRESTEAMSURVIVAL_API bool VerifyNetProbe(UWorld* World, FString* Why);
#endif
}
