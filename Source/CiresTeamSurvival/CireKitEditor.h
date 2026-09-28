#pragma once
// kit-editor (Playtest 6, section I): the Skill Assignment editor and the per-champion kit templates it saves.
//
// Champion Select > KIT EDITOR (dev/editor mode). Pick a champion, browse the WHOLE ability pool (every row of the
// Ability Database, grouped like the Skill Shop "periodic table", with search and filters), assign a base kit and save it
// as a static template per champion in Content/Data/ChampionKitTemplates.json. The game then uses that template as the
// hero's base kit:
//   - the kit's skills join the champion's purchasable list (CireAbilityDB::Reload merges the templates), and
//   - with "grantOnDraft" (default on) the champion starts the match with the whole kit learned at level 1.
// Per ability, the template also stores the effect placement for THAT champion: the attach socket / bone, an offset, a
// scale and a tint. The spell presentation uses it for the caster-attached cast effect (the Fab "cast" overlay), so the
// flare sits in the right hand, on the head, at the chest... of that body. Previewed live on the 3D model in the editor.
//
// Everything is data-driven: the pool is CireAbilityDB::All() and the champions are CireChampionRoster::All(), so new
// abilities (feat/ability-expansion) and new champions (feat/paragon-champions) appear with no code change.
// Docs/KitEditor.md.
#include "CoreMinimal.h"

class ACireHUD;
class ACireHero;
class ACireController;
class ACireGameMode;
class UFXSystemAsset;
class UFXSystemComponent;
class USkeletalMeshComponent;
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

struct CIRESTEAMSURVIVAL_API FCireKitTemplate
{
    FString ChampionId;
    TArray<FString> BaseKit;                // ability ids, actives first, then the ultimate and the passive
    bool bGrantOnDraft = true;              // start the match with the kit learned (false: sold in the Skill Shop only)
    TMap<FString, FCireKitEffectPlacement> Effects; // ability id -> placement
    FString Updated;                        // ISO time of the last save (display only)
    bool operator==(const FCireKitTemplate& O) const;
};

namespace CireKitEditor
{
    // ---- data (Content/Data/ChampionKitTemplates.json) ----
    CIRESTEAMSURVIVAL_API const TMap<FString, FCireKitTemplate>& Templates();
    CIRESTEAMSURVIVAL_API const FCireKitTemplate* Find(const FString& ChampionId);
    CIRESTEAMSURVIVAL_API FString DataPath();
    /** Transactional: a malformed file keeps the previous templates. */
    CIRESTEAMSURVIVAL_API bool Reload();
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, TMap<FString, FCireKitTemplate>& Out, FString& Error);
    CIRESTEAMSURVIVAL_API FString ToJson(const TMap<FString, FCireKitTemplate>& Templates);
    /** Writes one champion's template (Template.BaseKit empty and no effects = remove it), then reloads the Ability DB so
     *  the game uses it at once. */
    CIRESTEAMSURVIVAL_API bool SaveTemplate(const FCireKitTemplate& Template, FString* Error = nullptr);
    /** Tests: use these templates in memory (no file). bOn=false returns to the file. */
    CIRESTEAMSURVIVAL_API void DebugOverride(const TMap<FString, FCireKitTemplate>* Templates);

    // ---- kit rules (the Skill Shop / draft capacity: 6 actives, 1 passive, 1 ultimate) ----
    enum class EKind : uint8 { Active, Passive, Ultimate };
    CIRESTEAMSURVIVAL_API EKind KindOf(const FString& AbilityId);
    CIRESTEAMSURVIVAL_API int32 Capacity(EKind Kind);
    /** Why Id cannot join Kit (empty when it can): unknown id, duplicate, slot kind full. */
    CIRESTEAMSURVIVAL_API FString AddBlocker(const TArray<FString>& Kit, const FString& Id);
    /** Known, unique ids within capacity, ordered actives -> ultimate -> passive (stable within a kind). */
    CIRESTEAMSURVIVAL_API TArray<FString> Normalize(const TArray<FString>& Kit, TArray<FString>* Dropped = nullptr);

    // ---- game integration ----
    /** CireAbilityDB::Reload: template skills join the champion's purchasable lists (kits are created for champions
     *  without one, e.g. new roster rows). */
    CIRESTEAMSURVIVAL_API void MergeIntoKits(TMap<FString, FCireChampionKit>& Kits);
    /** ACireHero::DraftProfile (server): learns the template kit when grantOnDraft is set. Returns skills granted. */
    CIRESTEAMSURVIVAL_API int32 GrantOnDraft(ACireHero* Hero);
    CIRESTEAMSURVIVAL_API const FCireKitEffectPlacement* Placement(const FString& ChampionId, const FString& AbilityId);
    /** Resolves an anchor key or literal name to a socket / bone of this body (NAME_None: the component origin). */
    CIRESTEAMSURVIVAL_API FName ResolveAttach(const USkeletalMeshComponent* Mesh, const FString& Attach);
    /** Spawns System on the champion per the placement (attached to the resolved socket, offset in champion space,
     *  absolute scale BaseScale x placement scale, recoloured). */
    CIRESTEAMSURVIVAL_API UFXSystemComponent* SpawnPlaced(ACireHero* Hero, UFXSystemAsset* System, const FCireKitEffectPlacement& Placement,
        float BaseScale, bool bAutoDestroy, const CireFabVFX::FEntry* Entry = nullptr);
    /** Re-applies offset / scale / tint to a live component (the editor's live preview). */
    CIRESTEAMSURVIVAL_API void ApplyPlacement(UFXSystemComponent* Component, ACireHero* Hero, const FCireKitEffectPlacement& Placement, float BaseScale, bool bTint);
    /** Spell presentation hook (client): the caster-attached cast effect of Skill released near CasterAt. When a champion
     *  there owns a placement for Skill, spawns the effect on it and returns the component; nullptr = default presentation. */
    CIRESTEAMSURVIVAL_API UFXSystemComponent* SpawnPlacedCast(UWorld* World, FName Skill, FVector CasterAt, UFXSystemAsset* System, float Scale,
        const CireFabVFX::FEntry* Entry = nullptr);
    /** The Fab system a champion's cast of this ability uses (ability entry, then the school set), and its data scale. */
    CIRESTEAMSURVIVAL_API UFXSystemAsset* CastSystem(const FString& AbilityId, float* OutScale = nullptr, const CireFabVFX::FEntry** OutEntry = nullptr);

    // ---- pool browser ----
    struct FPoolSection { FString Id; FString Label; FLinearColor Color; };
    /** Periodic-table sections in Skill Shop order (Abilities.json "section"); unknown sections are appended. */
    CIRESTEAMSURVIVAL_API TArray<FPoolSection> Sections();
    CIRESTEAMSURVIVAL_API FString SectionOf(const FCireAbilityDef& Def);
    /** Case-insensitive search over name, id, school, types, effect tags and section. Empty matches everything. */
    CIRESTEAMSURVIVAL_API bool MatchesSearch(const FCireAbilityDef& Def, const FString& Search);
    struct FPoolFilter
    {
        FString Search;
        int32 Kind = -1;               // -1 all, else EKind
        TSet<FString> HiddenSections;
        FString OnlyChampion;          // non-empty: only abilities this champion may learn today
        FString Role;                  // "", "DPS", "TANK", "HEAL"
    };
    /** Pool grouped by section (each list sorted by name); sections without a match are omitted. */
    CIRESTEAMSURVIVAL_API TArray<TPair<FPoolSection, TArray<const FCireAbilityDef*>>> Pool(const FPoolFilter& Filter);

    // ---- editor UI (CireKitEditorUI.cpp) ----
    /** Dev/editor builds, or -CireKitEditor in a shipping build. */
    CIRESTEAMSURVIVAL_API bool IsAvailable();
    CIRESTEAMSURVIVAL_API bool IsOpen(const ACireHUD* HUD);
    CIRESTEAMSURVIVAL_API void Open(ACireHUD* HUD, bool bOpen, const FString& ChampionId = FString());
    /** Full-screen editor, drawn by the champion-select screen while open. */
    CIRESTEAMSURVIVAL_API void Draw(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller);

#if !UE_BUILD_SHIPPING
    /** Native suite (CireKitEditorTests.cpp). Logs CIRE_KIT_EDITOR_TESTS_PASS/FAIL. */
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
#endif
}
