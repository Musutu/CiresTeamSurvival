#pragma once
#include "CoreMinimal.h"

class ACharacter;
class APawn;
class UCireAudioSubsystem;
class UWorld;
class USoundBase;

/**
 * Footsteps by armour weight (Content/Data/AudioFootsteps.json).
 *
 * Contact sources, in order of preference:
 *   1. Bear / Centaur gait phase (UCireCreatureArt::MotionPhase): diagonal contacts at 0 and PI.
 *   2. Humanoid foot bones (foot_l / foot_r): a step fires when a foot drops back to its planted height.
 *   3. Speed cadence: one step every StepInterval() seconds while moving on the ground.
 */
namespace CireFootsteps
{
    enum class ESurface : uint8 { Stone, Dirt };
    struct FLayer
    {
        TArray<FString> Stone, Dirt, Any;
        float Volume = 1.f, PitchMin = 1.f, PitchMax = 1.f, Chance = 1.f, Delay = 0.f;
        const TArray<FString>& For(ESurface Surface) const;
    };
    struct FClass
    {
        FName Id;
        float Volume = 1.f, StrideWalk = 80.f, StrideRun = 150.f, CameraShake = 0.f;
        TArray<FLayer> Layers;
    };
    struct FBinding { FName Class; float Pitch = 1.f, Volume = 1.f; };
    struct FData
    {
        bool bValid = false;
        float MaxDistance = 2800.f, MaxStepsPerSecond = 36.f, VolumeJitter = .15f, SpeedWalk = 250.f, SpeedRun = 600.f;
        TMap<FName, FClass> Classes;
        TMap<FString, FBinding> Profiles;
        TMap<FString, FBinding> Monsters;
        TArray<FName> RuntimeArchetypes;
        FName FallbackHero = TEXT("leather"), FallbackMonster = TEXT("mail");
    };
    CIRESTEAMSURVIVAL_API const FData& Data(bool bReload = false);

    /** Armour binding for a champion profile id; bExplicit reports whether the profile is listed. */
    CIRESTEAMSURVIVAL_API FBinding ForProfile(const FString& ProfileId, int32 RuntimeArchetype, bool* bExplicit = nullptr);
    /** Armour binding for an NPC archetype id (NPCArchetypes.json key). */
    CIRESTEAMSURVIVAL_API FBinding ForMonster(const FString& ArchetypeId, bool* bExplicit = nullptr);
    /** Binding for any character (hero profile, monster archetype, or fallback). */
    CIRESTEAMSURVIVAL_API FBinding ForCharacter(const ACharacter* Character);

    /**
     * Seconds between footfalls at a ground speed (cm/s) for a class: the stride grows from StrideWalk
     * to StrideRun between SpeedWalk and SpeedRun. Returns 0 below 30 cm/s (standing: no steps).
     */
    CIRESTEAMSURVIVAL_API float StepInterval(const FClass& Class, float Speed, float SpeedWalk = 250.f, float SpeedRun = 600.f);
    /** True when a gait phase (radians) crossed one of the diagonal contact points (0 or PI) going forward or backward. */
    CIRESTEAMSURVIVAL_API bool PhaseCrossedContact(float PreviousPhase, float Phase);
}

class CIRESTEAMSURVIVAL_API FCireFootstepPlayer
{
public:
    void Tick(UCireAudioSubsystem& Audio, const FVector& Listener, const APawn* LocalPawn, bool bShake, float DeltaSeconds);
    void Reset();
    /** Plays one footfall for a character now (also used by tests and the offline render probe). */
    void Step(UCireAudioSubsystem& Audio, ACharacter* Character, const FVector& FootLocation, float Speed, bool bLocal, bool bShake);
    int32 StepsPlayed = 0;
    int32 StepsDropped = 0;
    int32 BoneSteps = 0, PhaseSteps = 0, CadenceSteps = 0;
    /** Plays one footfall of an armour class directly (probe / tests; no character needed). */
    void PlayClass(UCireAudioSubsystem& Audio, const CireFootsteps::FClass& Class, const CireFootsteps::FBinding& Binding,
        const FVector& FootLocation, float Speed, bool bLocal, ACharacter* Character);
    int32 TrackedCharacters() const { return Tracks.Num(); }
    TMap<FName, int32> ClassSteps;
    float Budget = 0.f;
private:
    struct FTrack
    {
        CireFootsteps::FBinding Binding;
        FName BoneL, BoneR;
        bool bBones = false, bBonesChecked = false, bUseCadence = false;
        float BaselineL = 1e9f, BaselineR = 1e9f, PrevL = 0.f, PrevR = 0.f;
        bool bLiftedL = false, bLiftedR = false;
        float Cadence = 0.f, LastPhase = -1.f, MovingNoContact = 0.f, SinceStep = 0.f, BaselineAge = 0.f;
        int32 LastPick = -1;
    };
    struct FPending { TWeakObjectPtr<ACharacter> Character; FVector Location; float At = 0.f; float Volume = 1.f; float Pitch = 1.f; TWeakObjectPtr<USoundBase> Sound; };
    TMap<TWeakObjectPtr<ACharacter>, FTrack> Tracks;
    TArray<FPending> Pending;
    TMap<FString, int32> LastPick;
    float Clock = 0.f;
    float ScanTimer = 0.f;
    TArray<TWeakObjectPtr<ACharacter>> Nearby;
    CireFootsteps::ESurface SurfaceUnder(UWorld* World, const FVector& Foot, const ACharacter* Ignore) const;
    FString Pick(const TArray<FString>& Options, const FString& Key);
};
