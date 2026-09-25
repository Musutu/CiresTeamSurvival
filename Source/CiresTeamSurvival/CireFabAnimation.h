#pragma once
// fab-integration: optional champion clips retargeted from the purchased Fab animation packs
// (GDH All Animation Bundle, Male Locomotion Set, Gun & Sword, Crossbow; Docs/FAB-PURCHASED.md).
//
// Tools/RetargetFabAnimations.py writes one copy per champion body under
//   /Game/FabDerived/Anim/<BodyFolder>/A_<BodyFolder>_<clip>        (clips)
//   /Game/FabDerived/Anim/<BodyFolder>/BS_Fab_Locomotion_<BodyFolder> (idle/walk/run/strafe/backpedal)
// where <BodyFolder> is the body's ChampionAttacks02 folder. /Game/FabDerived is derived from licensed
// content, so it is gitignored and junctioned like the packs. Content/Data/FabAnimations.json (committed,
// paths and timing only) says which Fab clip replaces which action per weapon style / motion class.
//
// Every lookup checks the package exists and the skeleton matches the body; anything missing falls back
// to the ChampionAttacks02 / Preview02 clips and the procedural roll, jump and death pose, so a clean clone
// without Fab content runs exactly as before. cire.FabAnim 0 (or -CireNoFabAnim) forces the fallback.
#include "CoreMinimal.h"
#include "CireChampionActions.h"

class UAnimSequence;
class UBlendSpace;
class USkeletalMesh;

namespace CireFabAnimation
{
    CIRESTEAMSURVIVAL_API bool Enabled();
    CIRESTEAMSURVIVAL_API void Reload();
    /** A retargeted clip for this body, or null (absent, skeleton mismatch, overlay off). */
    CIRESTEAMSURVIVAL_API UAnimSequence* Find(const USkeletalMesh* Body, const FString& Folder, const FString& Clip);
    /** Timing of a Fab clip (seconds, like ChampionAttacks02 windows); false when FabAnimations.json has none. */
    CIRESTEAMSURVIVAL_API bool Window(const FString& Clip, CireChampionActions::FWindow& Out);
    /** Replacement for an action: "style:<preset>" then "motion:<class>" rows of FabAnimations.json, kind =
     *  attack | ability | spell | shout | hit | death | roll | jump. Seed rotates through alternatives (combos). */
    CIRESTEAMSURVIVAL_API bool Pick(const USkeletalMesh* Body, const FString& Folder, const FString& Style, const FString& Motion,
        const FString& Kind, uint32 Seed, UAnimSequence*& OutClip, FString& OutName);
    /** Retargeted 2D locomotion BlendSpace (Direction, Speed axes like Preview02) for the body, or null. */
    CIRESTEAMSURVIVAL_API UBlendSpace* Locomotion(const USkeletalMesh* Body, const FString& Folder);
    /** ChampionAttacks02 body folder of a mesh (mesh object path -> folder), empty when unknown. */
    CIRESTEAMSURVIVAL_API FString FolderFor(const USkeletalMesh* Body);

    struct FCoverage { int32 Bodies = 0, BodiesWithClips = 0, Clips = 0, Locomotion = 0; };
    CIRESTEAMSURVIVAL_API FCoverage Coverage();
#if !UE_BUILD_SHIPPING
    /** Data validity, fallback on a clean clone, and (when installed) skeleton/timing sanity of every Fab clip.
     *  Logs CIRE_FAB_ANIM_TESTS_PASS / CIRE_FAB_ANIM_TESTS_FAIL. */
    CIRESTEAMSURVIVAL_API bool RunTests();
#endif
}
