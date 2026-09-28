#pragma once
// kit-editor (Playtest 6, section I): the HERO CREATOR and the kit data it saves.
//
// Champion Select > HERO CREATOR (dev/editor mode). Pick a champion, pick spells from EVERY ability in the game (any
// champion may take any skill) with the Skill Shop's own scroll cards / periodic-table sections / tooltips, and assign them
// to the champion's real skill buttons (action-bar keys 1-6, the ultimate R and the passive) as a base loadout. Loadouts
// are named presets; several per champion, one marked as the default the game uses.
//
// KIT PROFILES span all champions ("Standard", "Hero TD", "Arena PvP", custom...). Each champion can have its presets in
// each profile. A game mode names the profile it uses (WavePresets.json "kitProfile", feat/waves-modes); a champion
// missing from that profile falls back to its "Standard" loadout, then to its built-in kit (no template).
//
// Per champion (profile-independent: it is about the body) the data also stores each ability's EFFECT PLACEMENT: attach
// socket / bone, offset, scale and tint of the caster-attached cast effect, previewed live on the 3D model.
//
// The game uses the data:
//   - CireAbilityDB::Reload -> MergeIntoKits: every loadout skill joins its champion's purchasable Skill Shop list;
//   - ACireHero::DraftProfile -> GrantOnDraft (server): the active profile's default loadout is learned at draft
//     (level 1, in button order) when that champion's "grantOnDraft" is on;
//   - the spell presentation -> SpawnPlacedCast: the cast effect sits where the placement says.
// Everything is data-driven (CireAbilityDB::All, CireChampionRoster::All, read live every frame): new or renamed abilities
// and new champions appear on their own. Content/Data/ChampionKitTemplates.json. Docs/KitEditor.md.
#include "CoreMinimal.h"

class ACireHUD;
class ACireHero;
class ACireController;
class ACireGameMode;
class UFXSystemAsset;
class UFXSystemComponent;
class USkeletalMeshComponent;
class UWorld;
struct FCireAbilityDef;
struct FCireChampionKit;
namespace CireFabVFX { struct FEntry; }

/** Where one ability's cast effect sits on one champion. */
struct CIRESTEAMSURVIVAL_API FCireKitEffectPlacement
{
    /** Anchor key (root, pelvis, chest, head, hand_r, hand_l, foot_r, foot_l) or a literal socket / bone name. Empty = the
     *  default presentation (no override). */
    FString Attach;
    FVector Offset = FVector::ZeroVector;   // cm, champion space: +X forward, +Y right, +Z up
    float Scale = 1.f;                      // multiplies the effect's normal size (0.1 .. 5)
    FLinearColor Tint = FLinearColor(0, 0, 0, 0); // A > 0: recolour (CireFabVFX::Recolor)
    float TintStrength = 1.f;               // 0..1
    bool IsDefault() const;
    bool operator==(const FCireKitEffectPlacement& O) const;
};

/** One named loadout: the ability on each skill button. Slots 0-5 = keys 1-6, 6 = ultimate (R), 7 = passive. */
struct FCireKitLoadout
{
    static constexpr int32 SlotCount = 8, UltimateSlot = 6, PassiveSlot = 7, ActiveSlots = 6;
    FString Name;
    TArray<FString> Slots;                  // SlotCount entries ("" = empty)
    FCireKitLoadout() { Slots.SetNum(SlotCount); }
    /** Skills in grant order: keys 1-6, then R, then the passive button. */
    TArray<FString> Skills() const;
    int32 Count() const;
    bool operator==(const FCireKitLoadout& O) const { return Name == O.Name && Slots == O.Slots; }
};

/** A champion's presets inside one profile. */
struct CIRESTEAMSURVIVAL_API FCireKitChampion
{
    FString DefaultLoadout;                 // name of the preset the game uses
    bool bGrantOnDraft = true;              // start matches with the default loadout (false: its skills are only sold)
    TArray<FCireKitLoadout> Loadouts;
    FString Updated;                        // ISO time of the last save (display only)
    const FCireKitLoadout* Default() const;
    const FCireKitLoadout* Find(const FString& Name) const;
    FCireKitLoadout* Find(const FString& Name);
};

struct CIRESTEAMSURVIVAL_API FCireKitProfile
{
    FString Name;
    TMap<FString, FCireKitChampion> Champions;
};

struct CIRESTEAMSURVIVAL_API FCireKitData
{
    TArray<FCireKitProfile> Profiles;       // "Standard" is always present (first)
    TMap<FString, TMap<FString, FCireKitEffectPlacement>> Effects; // champion -> ability -> placement
    const FCireKitProfile* FindProfile(const FString& Name) const;
    FCireKitProfile* FindProfile(const FString& Name);
};

namespace CireKitEditor
{
    inline const TCHAR* StandardProfile = TEXT("Standard");

    // ---- data (Content/Data/ChampionKitTemplates.json) ----
    CIRESTEAMSURVIVAL_API const FCireKitData& Data();
    CIRESTEAMSURVIVAL_API FString DataPath();
    /** Transactional: a malformed file keeps the previous data. */
    CIRESTEAMSURVIVAL_API bool Reload();
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireKitData& Out, FString& Error);
    CIRESTEAMSURVIVAL_API FString ToJson(const FCireKitData& Data);
    /** Writes the whole file, then reloads the Ability DB so the game uses it at once. */
    CIRESTEAMSURVIVAL_API bool Save(const FCireKitData& Data, FString* Error = nullptr);
    /** Tests: use this data in memory (nullptr returns to the file). */
    CIRESTEAMSURVIVAL_API void DebugOverride(const FCireKitData* Data);

    // ---- profiles and game modes ----
    /** A game mode's "kitProfile" -> the profile to use (empty / unknown -> Standard). */
    CIRESTEAMSURVIVAL_API FString ProfileForMode(const FString& ModeKitProfile);
    /** The profile this match uses: -CireKitProfile=<name>, else the game mode's kitProfile (ModeKitProfile), else Standard. */
    CIRESTEAMSURVIVAL_API FString ActiveProfile(const UWorld* World);
    /** The host's game type (ACireGameState::WavePreset) -> its WavePresets.json "kitProfile" (empty = Standard). */
    CIRESTEAMSURVIVAL_API FString ModeKitProfile(const UWorld* World);
    /** Tests: force the active profile ("" clears). */
    CIRESTEAMSURVIVAL_API void DebugForceProfile(const FString& Profile);
    /** The champion's entry in Profile, falling back to Standard. OutProfile = the profile it came from. */
    CIRESTEAMSURVIVAL_API const FCireKitChampion* ResolveChampion(const FString& Profile, const FString& ChampionId, FString* OutProfile = nullptr);
    /** The default loadout the game uses for this champion in Profile (Standard fallback); nullptr = the built-in kit. */
    CIRESTEAMSURVIVAL_API const FCireKitLoadout* ResolveLoadout(const FString& Profile, const FString& ChampionId, FString* OutProfile = nullptr);

    // ---- skill buttons ----
    enum class EKind : uint8 { Active, Passive, Ultimate };
    CIRESTEAMSURVIVAL_API EKind KindOf(const FString& AbilityId);
    /** Key buttons take actives; R and the passive button take anything (a non-matching kind gets a warning, see
     *  SlotWarning). False only for an ultimate / passive on a key button (they route to their own button). */
    CIRESTEAMSURVIVAL_API bool SlotAccepts(int32 Slot, const FString& AbilityId);
    /** A short warning for a skill on a button of another kind (empty when it matches). */
    CIRESTEAMSURVIVAL_API FString SlotWarning(int32 Slot, const FString& AbilityId);
    /** The button a click on a card fills: Preferred when it accepts the skill, else the kind's first empty button, else
     *  (ultimate / passive) their only button; INDEX_NONE when all six key buttons are taken. */
    CIRESTEAMSURVIVAL_API int32 TargetSlot(const FCireKitLoadout& Loadout, const FString& AbilityId, int32 Preferred = INDEX_NONE);
    /** Puts the ability on the button (moving it if it sat on another one). */
    CIRESTEAMSURVIVAL_API bool Assign(FCireKitLoadout& Loadout, int32 Slot, const FString& AbilityId, FString* Why = nullptr);
    /** Unknown ids and duplicates removed, SlotCount entries, key buttons packed from 1 (the action bar binds actives in
     *  order, so a gap would shift later buttons anyway). */
    CIRESTEAMSURVIVAL_API void Compact(FCireKitLoadout& Loadout);

    // ---- game integration ----
    CIRESTEAMSURVIVAL_API void MergeIntoKits(TMap<FString, FCireChampionKit>& Kits);
    CIRESTEAMSURVIVAL_API int32 GrantOnDraft(ACireHero* Hero);
    CIRESTEAMSURVIVAL_API const FCireKitEffectPlacement* Placement(const FString& ChampionId, const FString& AbilityId);
    CIRESTEAMSURVIVAL_API FName ResolveAttach(const USkeletalMeshComponent* Mesh, const FString& Attach);
    CIRESTEAMSURVIVAL_API UFXSystemComponent* SpawnPlaced(ACireHero* Hero, UFXSystemAsset* System, const FCireKitEffectPlacement& Placement,
        float BaseScale, bool bAutoDestroy, const CireFabVFX::FEntry* Entry = nullptr);
    CIRESTEAMSURVIVAL_API void ApplyPlacement(UFXSystemComponent* Component, ACireHero* Hero, const FCireKitEffectPlacement& Placement, float BaseScale, bool bTint);
    /** Spell presentation hook (client): the cast effect of Skill released near CasterAt, on a champion with a placement. */
    CIRESTEAMSURVIVAL_API UFXSystemComponent* SpawnPlacedCast(UWorld* World, FName Skill, FVector CasterAt, UFXSystemAsset* System, float Scale,
        const CireFabVFX::FEntry* Entry = nullptr);
    CIRESTEAMSURVIVAL_API UFXSystemAsset* CastSystem(const FString& AbilityId, float* OutScale = nullptr, const CireFabVFX::FEntry** OutEntry = nullptr);

    // ---- pool (every ability, the Skill Shop's sections) ----
    CIRESTEAMSURVIVAL_API bool MatchesSearch(const FCireAbilityDef& Def, const FString& Search);
    struct FPoolFilter
    {
        FString Search;
        int32 Kind = -1;               // -1 all, else EKind
        uint32 HiddenSections = 0;     // bit per Skill Shop section
        FString Role;                  // "", "DPS", "TANK", "HEAL" (optional chip, off by default)
        FString OnlyChampion;          // optional: only abilities this champion's class list has (off by default)
    };
    /** Ability DB rows grouped by Skill Shop section index (sorted by name); empty sections omitted. */
    CIRESTEAMSURVIVAL_API TArray<TPair<int32, TArray<const FCireAbilityDef*>>> Pool(const FPoolFilter& Filter);
    /** ability-tuner: how many CireAbilityTuner::OnChanged() notifications the Hero Creator has received (subscribes on
     *  first call). Every open Hero Creator refreshes its cards, live preview effect and selection on each one. */
    CIRESTEAMSURVIVAL_API uint32 TunerStamp();

    // ---- editor UI (CireKitEditorUI.cpp) ----
    CIRESTEAMSURVIVAL_API bool IsAvailable();
    CIRESTEAMSURVIVAL_API bool IsOpen(const ACireHUD* HUD);
    CIRESTEAMSURVIVAL_API void Open(ACireHUD* HUD, bool bOpen, const FString& ChampionId = FString());
    CIRESTEAMSURVIVAL_API void Draw(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller);

#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
#endif
}
