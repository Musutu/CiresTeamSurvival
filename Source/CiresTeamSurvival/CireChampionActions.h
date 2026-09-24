#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CireChampionActions.generated.h"

class ACireGameMode;
class ACireHero;
class UAnimSequence;
class UCireCombatAnimInstance;
class USkeletalMesh;

/**
 * Champion attack and cast animation from the Tripo motion library (Docs/MonsterArt.md, "Champions").
 * Clips: /Game/Art/Characters/ChampionAttacks02 (Tools/RetargetChampionAttacks.py), rules in
 * Content/Data/ChampionAttacks02.json. The basic attack plays the weapon class's clip with its contact
 * frame on the server's release; a cast (seen as a skill cooldown starting) plays cast_a_spell, war_cry
 * for shouts, or the weapon strike. Presentation only: no gameplay state is read back from it.
 */
namespace CireChampionActions
{
    struct FWindow { float Start = 0.f, Contact = 0.f, End = 0.f, RecoverRate = 1.f; };
    /** Drives the champion action layer this frame. False: no ChampionAttacks02 clips for this body (prototype clip stays). */
    CIRESTEAMSURVIVAL_API bool Apply(ACireHero& Hero, UCireCombatAnimInstance& Anim, float DeltaSeconds, float Speed);
    /** Weapon motion class of the champion (melee, bow, crossbow, cast, throw, none). */
    CIRESTEAMSURVIVAL_API FString MotionFor(const ACireHero& Hero);
    /** Clip for a body ("slash", "cast_a_spell", "war_cry", "attack_bow", "attack_crossbow"); null when absent. */
    CIRESTEAMSURVIVAL_API UAnimSequence* ClipFor(const USkeletalMesh* Body, const FString& Clip);
    CIRESTEAMSURVIVAL_API FWindow Window(const FString& Clip);
    /** "attack" | "ability" | "spell" | "shout": which clip a learned skill plays. */
    CIRESTEAMSURVIVAL_API FString SkillKind(const FString& SkillId);
    /** Equipped weapon preset (WeaponLoadouts.json), which selects the swing style. */
    CIRESTEAMSURVIVAL_API FString StyleName(const ACireHero& Hero);
    /** Clip name the champion plays for an action kind ("attack", "ability", "spell", "shout"). */
    CIRESTEAMSURVIVAL_API FString ClipName(const ACireHero& Hero, const FString& Kind);
#if !UE_BUILD_SHIPPING
    /** Galleries: hold a clip at a phase (0 = window start, 1 = contact/release, 2 = end). False when the body lacks it. */
    CIRESTEAMSURVIVAL_API bool Hold(ACireHero& Hero, const FString& Clip, float Phase);
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}

/** Local per-champion state for CireChampionActions (not replicated; created on demand). */
UCLASS(Transient)
class CIRESTEAMSURVIVAL_API UCireChampionAction : public UActorComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> Sequence;
    UPROPERTY(Transient) TObjectPtr<USkeletalMesh> CachedBody;
    FString Clip;
    CireChampionActions::FWindow Window;
    double StartedAt = 0.0;
    float Windup = 0.f;
    float RecoverRate = 1.f;
    /** The action is the basic attack of SeenAttackSerial: its start follows the replicated attack time. */
    bool bBasic = false;
    uint32 SeenAttackSerial = 0;
    TArray<float> LastCooldowns;
    /** Galleries/tests: hold the layer at an absolute clip time instead of following gameplay. */
    bool bHold = false;
    float HoldTime = 0.f;
};
