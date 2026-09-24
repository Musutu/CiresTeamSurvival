#pragma once
/*
 * ============================================================================================
 *  CireKeybindings — data-driven action -> key map (WoW default layout) and action-bar slots.
 *  Core only: no drawing. The keybinding screen and action bars are drawn by the HUD (wow-ui).
 * ============================================================================================
 *
 *  ACTIONS
 *    Every gameplay input is a named action (FName), e.g. "MoveForward", "StrafeLeft", "Jump",
 *    "TargetNextEnemy", "ActionBar1_Slot3". CireKeybindings::Actions() lists them in display
 *    order with a display name and a category (Movement, Combat, Targeting, Interface, ActionBar1..3).
 *    Each action has two bindings: index 0 = primary, 1 = secondary. A binding is an
 *    FCireKeyChord: one FKey plus required Shift/Ctrl/Alt modifiers (e.g. Shift+Tab, Alt+2).
 *
 *  READING INPUT (controller side)
 *    Bindings.WasPressed(PC, "Jump")   - chord went down this frame (modifiers must match exactly)
 *    Bindings.IsDown(PC, "MoveForward")- held; an unmodified chord ignores extra held modifiers
 *    Bindings.WasReleased(PC, "Jump")
 *    ACireController reads every gameplay key through these; nothing else hard-codes EKeys.
 *    Fixed, non-rebindable keys: Escape (cancel/menu, and cancels capture), the chat text editor
 *    (Enter/Tab/Backspace while typing), LMB/RMB (camera, click, aim) and the mouse wheel (zoom).
 *
 *  REBINDING (for the keybinding screen)
 *    Bindings.BeginCapture(Action, Index)     - start listening for the next key/chord
 *    Bindings.IsCapturing(), CaptureAction(), CaptureIndex()
 *    Bindings.TickCapture(PC, Result)          - call every frame while capturing (the controller
 *        already does this and suspends gameplay input). Esc cancels, Backspace/Delete unbinds,
 *        any other key binds (with held modifiers; a lone modifier binds on release).
 *    Bindings.Bind(Action, Index, Chord, Policy, &Conflict) - direct bind; Policy Swap moves the
 *        replaced chord onto the conflicting action, Unbind clears it. Result reports conflicts.
 *    Bindings.Unbind(Action, Index), FindConflict(Chord, Ignore...), ResetToDefaults().
 *    Bindings.Label(Action)  -> short WoW-style label of the primary (else secondary) key:
 *        "1", "S-1", "C-2", "A-3", "M4", "Tab", "S-Tab", "Spc". FullLabel(...) gives "Shift+Tab".
 *
 *  ACTION BARS (3 bars x 12 slots)
 *    Actions ActionBar{1..3}_Slot{1..12}. Pressing a slot's chord casts the ability placed in it.
 *    Defaults: bar 1 = 1..6 (active skills in learn order), slot 7 = passive (not castable),
 *    slot 8 = R (ultimate), slots 9..12 = 7,8,9,0. Bar 2 = Shift+1..6, bar 3 = Alt+1..6
 *    (Ctrl+N is avoided because Ctrl alone is Dodge roll). Other slots start unbound.
 *    Placement store (per champion profile id, saved in the profile):
 *        Bindings.AssignSlot(ProfileId, Slot, AbilityId) / ClearSlot (explicitly empty) /
 *        ResetSlot (back to the automatic default) / SlotAssignment(ProfileId, Slot).
 *    Resolution: CireKeybindings::ResolveSlot(Bindings, Hero, Slot) -> index into Hero.Skills or
 *        INDEX_NONE; SlotAbilityId(...) -> the ability id shown in that slot (may be unlearned).
 *
 *  PERSISTENCE
 *    Stored inside Saved/Config/CireUI.ini (FCireUISettings::Keybindings) in section
 *    [CireUI.Keybindings] (KeybindingsVersion=1, "Action=Primary|Secondary", chord text like
 *    "Shift+Tab") and [CireUI.ActionBars.<profile>] ("ActionBar1_Slot2=ability_id", "-" = empty).
 *    Profiles without these sections (UI schema <= 3) load the WoW defaults. Unknown actions and
 *    unparsable chords are ignored per entry, keeping defaults for the rest.
 *
 *  See Docs/Keybindings.md.
 */
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class ACireHero;
class FConfigFile;

struct CIRESTEAMSURVIVAL_API FCireKeyChord
{
    FKey Key;
    bool bShift = false, bCtrl = false, bAlt = false;
    FCireKeyChord() = default;
    FCireKeyChord(const FKey& InKey, bool Shift = false, bool Ctrl = false, bool Alt = false)
        : Key(InKey), bShift(Shift), bCtrl(Ctrl), bAlt(Alt) {}
    bool IsBound() const { return Key.IsValid(); }
    bool operator==(const FCireKeyChord& O) const { return Key == O.Key && bShift == O.bShift && bCtrl == O.bCtrl && bAlt == O.bAlt; }
    bool operator!=(const FCireKeyChord& O) const { return !(*this == O); }
    /** "Shift+Tab", "Alt+Two", "LeftControl"; empty when unbound. */
    FString ToString() const;
    static bool Parse(const FString& Text, FCireKeyChord& Out);
    /** Short WoW-style label: "S-1", "C-2", "A-3", "M4", "Tab", "Spc". */
    FString ShortLabel() const;
    /** Human label: "Shift+Tab", "Mouse 4", "Space". */
    FString LongLabel() const;
};

enum class ECireBindCategory : uint8 { Movement, Combat, Targeting, Interface, ActionBar1, ActionBar2, ActionBar3 };

struct FCireActionInfo
{
    FName Id;
    FText DisplayName;
    ECireBindCategory Category = ECireBindCategory::Interface;
    FCireKeyChord Default[2];
};

enum class ECireBindPolicy : uint8 { Swap, UnbindOther };

struct FCireBindResult
{
    bool bChanged = false;
    /** Another action already used the chord. */
    FName ConflictAction;
    int32 ConflictIndex = INDEX_NONE;
    /** What the conflicting action ended up with (swap) or an unbound chord (unbind). */
    FCireKeyChord ConflictNowBound;
};

struct FCireCaptureResult
{
    enum EKind : uint8 { None, Cancelled, Unbound, Bound, Rejected } Kind = None;
    FName Action;
    int32 Index = 0;
    FCireKeyChord Chord;
    FCireBindResult Bind;
};

class CIRESTEAMSURVIVAL_API FCireKeybindings
{
public:
    static constexpr int32 NumBars = 3;
    static constexpr int32 SlotsPerBar = 12;
    static constexpr int32 SchemaVersion = 1;

    FCireKeybindings();

    // --- map ----------------------------------------------------------------------------------
    const FCireKeyChord& Get(FName Action, int32 Index) const;
    FCireBindResult Bind(FName Action, int32 Index, const FCireKeyChord& Chord, ECireBindPolicy Policy = ECireBindPolicy::Swap);
    void Unbind(FName Action, int32 Index);
    /** First other binding that uses Chord (exact chord match), or NAME_None. */
    FName FindConflict(const FCireKeyChord& Chord, FName IgnoreAction = NAME_None, int32 IgnoreIndex = INDEX_NONE, int32* OutIndex = nullptr) const;
    void ResetToDefaults();
    bool IsDefault() const;
    FString Label(FName Action) const;
    FString FullLabel(FName Action) const;

    // --- input --------------------------------------------------------------------------------
    bool WasPressed(const APlayerController* PC, FName Action) const;
    bool WasReleased(const APlayerController* PC, FName Action) const;
    bool IsDown(const APlayerController* PC, FName Action) const;

    // --- capture (rebinding UI) ----------------------------------------------------------------
    void BeginCapture(FName Action, int32 Index);
    void CancelCapture();
    bool IsCapturing() const { return !CaptureActionId.IsNone(); }
    FName CaptureAction() const { return CaptureActionId; }
    int32 CaptureIndex() const { return CaptureSlot; }
    /** Returns true while capture consumed this frame's input. */
    bool TickCapture(const APlayerController* PC, FCireCaptureResult& Out, ECireBindPolicy Policy = ECireBindPolicy::Swap);
    /** Most recent finished capture (bound / unbound / cancelled), for the UI to show conflicts. */
    const FCireCaptureResult& LastCapture() const { return LastResult; }
    /** Apply a captured key as if pressed with the given modifiers (also used by tests). */
    FCireCaptureResult ApplyCapturedKey(const FKey& Key, bool bShift, bool bCtrl, bool bAlt, ECireBindPolicy Policy = ECireBindPolicy::Swap);
    static bool IsBindableKey(const FKey& Key);

    // --- action-bar placement store -----------------------------------------------------------
    void AssignSlot(const FString& ProfileId, FName Slot, const FString& AbilityId);
    void ClearSlot(const FString& ProfileId, FName Slot);
    void ResetSlot(const FString& ProfileId, FName Slot);
    /** Explicit placement, if any. OutExplicit false means the automatic default applies. */
    FString SlotAssignment(const FString& ProfileId, FName Slot, bool& bOutExplicit) const;

    // --- persistence --------------------------------------------------------------------------
    void LoadFrom(const FConfigFile& Config);
    void SaveTo(FConfigFile& Config) const;

private:
    TMap<FName, TStaticArray<FCireKeyChord, 2>> Map;
    TMap<FString, TMap<FName, FString>> Placements; // profile -> slot -> ability ("" = explicit empty)
    FName CaptureActionId;
    int32 CaptureSlot = 0;
    FKey PendingModifier;
    FCireCaptureResult LastResult;
};

namespace CireKeybindings
{
    CIRESTEAMSURVIVAL_API const TArray<FCireActionInfo>& Actions();
    CIRESTEAMSURVIVAL_API const FCireActionInfo* Find(FName Action);
    CIRESTEAMSURVIVAL_API FName SlotAction(int32 Bar, int32 Slot); // 1-based bar and slot
    CIRESTEAMSURVIVAL_API bool ParseSlotAction(FName Action, int32& OutBar, int32& OutSlot);
    CIRESTEAMSURVIVAL_API FText CategoryName(ECireBindCategory Category);
    /** Default bindings used when no HUD/profile exists (bots, dedicated server, tests). */
    CIRESTEAMSURVIVAL_API const FCireKeybindings& Defaults();
    /** Ability id placed in a slot for this hero (explicit placement, else the automatic default). */
    CIRESTEAMSURVIVAL_API FString SlotAbilityId(const FCireKeybindings& Bindings, const ACireHero& Hero, FName Slot);
    /** Index into Hero.Skills to cast for this slot, or INDEX_NONE (empty, unlearned or passive). */
    CIRESTEAMSURVIVAL_API int32 ResolveSlot(const FCireKeybindings& Bindings, const ACireHero& Hero, FName Slot);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke();
#endif
}
