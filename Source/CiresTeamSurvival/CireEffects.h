#pragma once
// wow-ui: buff/debuff presentation layer.
//  - Modifier summary registry (Content/Data/BuffModifiers.json, plus optional rows in
//    Content/Data/Abilities.json "buffModifiers"): per effect id, a list of stat deltas,
//    a dispel type, a crowd-control kind and a one-line description, formatted into
//    concise symbol strings ("DEF +40%", "Healing −50% (10s)", "Stunned 1.5s").
//  - Gather(): every buff/debuff currently on a unit (CireBuffs records + effects derived
//    from replicated state: guard, taunt, slow, poison, NPC status flags, item buffs).
//  - Cast bars: CireCasts::Get() for monsters (UCireNPCState) and heroes (a registered
//    provider), with client-side INTERRUPTED / SILENCED detection.
//  - FCireCalloutQueue: throttled queue for "you gained X" callouts.
// Presentation only; nothing here changes gameplay. See Docs/BuffModifiers.md.
#include "CoreMinimal.h"

class AActor;
class ACireHero;
class UWorld;

enum class ECireControl : uint8 { None, Slow, Taunt, Root, Silence, HealCut, Disarm, Fear, Stun };
enum class ECireDispel : uint8 { None, Magic, Poison, Curse, Disease, Physical };
enum class ECireEffectKind : uint8 { Buff, Debuff, Stance, Aura, Passive };

struct FCireStatMod
{
    FString Stat;           // "DEF", "ATK", "Healing", "Move", ...
    float Value = 0.f;      // signed; 0 = label only ("Mana Regen")
    FString Unit;           // "%", "", "/s"
    float Duration = 0.f;   // > 0: this part lasts less than the effect ("(3s)")
};

struct FCireEffectInfo
{
    FName Id;
    FString Name;
    ECireEffectKind Kind = ECireEffectKind::Buff;
    ECireDispel Dispel = ECireDispel::None;
    ECireControl Control = ECireControl::None;
    FString School, Line;
    TArray<FCireStatMod> Mods;
    bool bCallout = true;
    bool bFromRegistry = false; // false: fallback built from BuffVisuals only
    bool IsHarmful() const { return Kind == ECireEffectKind::Debuff; }
};

/** One effect currently on a unit. End <= 0: no timer (until removed / passive). */
struct FCireActiveEffect
{
    FName Id;
    float Start = 0.f, End = 0.f;
    int32 Stacks = 1;
    TWeakObjectPtr<AActor> Source;
    bool bFromLocalPlayer = false;
};

namespace CireEffects
{
    CIRESTEAMSURVIVAL_API bool Reload(FString& Error);
    CIRESTEAMSURVIVAL_API bool Parse(const FString& Json, TMap<FName, FCireEffectInfo>& Out, FString& Error);
    /** Registry row merged with the BuffVisuals name/kind/school; never null for known ids. */
    CIRESTEAMSURVIVAL_API const FCireEffectInfo* Find(FName Id);
    CIRESTEAMSURVIVAL_API bool HasRegistryRow(FName Id);
    /** "DEF +40%", "Healing −50% (10s)", "Mana Regen". */
    CIRESTEAMSURVIVAL_API FString FormatMod(const FCireStatMod& Mod);
    /** Control word + remaining: "Stunned 1.5s", "Silenced 3s", "Rooted". */
    CIRESTEAMSURVIVAL_API FString FormatControl(ECireControl Control, float Remaining);
    CIRESTEAMSURVIVAL_API FString ControlWord(ECireControl Control);   // "STUNNED"
    CIRESTEAMSURVIVAL_API FString ControlBadge(ECireControl Control);  // "STUN"
    /** All symbols of an effect joined: "DEF +40%  ·  Move −35%"; control first. */
    CIRESTEAMSURVIVAL_API FString Symbols(const FCireEffectInfo& Info, float Remaining = -1.f);
    CIRESTEAMSURVIVAL_API FString DurationText(float Seconds); // "8s", "1.5s", "2m"
    CIRESTEAMSURVIVAL_API FLinearColor BorderColor(const FCireEffectInfo& Info);
    /** Every effect on a unit now (records + derived state), sorted: CC, debuffs, buffs. */
    CIRESTEAMSURVIVAL_API void Gather(const AActor* Unit, float ServerNow, TArray<FCireActiveEffect>& Out, const AActor* LocalHero = nullptr);
    /** Strongest crowd control currently on the unit (Slow/Taunt are not "hard"). */
    CIRESTEAMSURVIVAL_API ECireControl HardControl(const AActor* Unit, float ServerNow, float* OutRemaining = nullptr);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke();
#endif
}

// ---------------------------------------------------------------------------
// Cast bars
// ---------------------------------------------------------------------------
enum class ECireCastResult : uint8 { None, Completed, Interrupted, Silenced };

struct FCireCastView
{
    bool bCasting = false;
    FName AbilityId;
    FString Name;
    float Progress = 0.f, Remaining = 0.f, Duration = 0.f;
    bool bInterruptible = true, bChannel = false, bHeal = false;
    /** Result of the most recent cast (flash "INTERRUPTED" / "SILENCED" for ~1s). */
    ECireCastResult Result = ECireCastResult::None;
    float ResultAge = 99.f;
    FString ResultName;
};

/** Pure cast-state tracker (unit tests drive it directly). */
struct CIRESTEAMSURVIVAL_API FCireCastTracker
{
    bool bWasCasting = false;
    float LastEnd = 0.f;
    FName LastAbility;
    FString LastName;
    ECireCastResult Result = ECireCastResult::None;
    float ResultTime = -100.f;
    /** Feed one sample; Now and End on the same clock. bSilenced: unit is silenced now. */
    void Sample(bool bCasting, FName Ability, const FString& Name, float End, float Now, bool bSilenced);
};

namespace CireCasts
{
    /** Heroes have no cast fields yet: gameplay registers how to read a hero's cast. */
    using FHeroProvider = TFunction<bool(const ACireHero& Hero, float ServerNow, FCireCastView& Out)>;
    CIRESTEAMSURVIVAL_API void RegisterHeroProvider(FHeroProvider Provider);
    /** Current cast of any unit plus the interrupt/silence flash (client-side detection). */
    CIRESTEAMSURVIVAL_API FCireCastView Get(const AActor* Unit, float ServerNow);
#if !UE_BUILD_SHIPPING
    /** Gallery: show a synthetic cast (Duration>0) or a result flash on a unit. */
    CIRESTEAMSURVIVAL_API void DebugSet(const AActor* Unit, const FCireCastView& View);
    CIRESTEAMSURVIVAL_API void DebugClear();
#endif
}

// ---------------------------------------------------------------------------
// Callouts ("you gained X")
// ---------------------------------------------------------------------------
struct FCireCallout
{
    FName Id;
    float Duration = 0.f;   // effect duration (for the text)
    double Shown = -1.0;    // start time on screen
    bool bControl = false;  // hard CC: prominent centre callout, jumps the queue
};

/** Throttled callout queue: min spacing, per-id cooldown, bounded length, CC first. */
struct CIRESTEAMSURVIVAL_API FCireCalloutQueue
{
    float MinSpacing = .9f, SameIdCooldown = 6.f, Life = 2.2f;
    int32 MaxQueued = 3;
    TArray<FCireCallout> Queue;
    TOptional<FCireCallout> Active;
    TMap<FName, double> LastShown;
    double LastStart = -100.0;
    /** Returns false when dropped (cooldown / queue full). */
    bool Push(FName Id, float Duration, bool bControl, double Now);
    /** Advances; returns the callout on screen (if any). */
    const FCireCallout* Tick(double Now);
    void Reset() { Queue.Reset(); Active.Reset(); LastShown.Reset(); LastStart = -100.0; }
};
