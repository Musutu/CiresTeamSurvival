#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CireMusic.h"
#include "CireAmbience.h"
#include "CireFootsteps.h"
#include "CireAudio.generated.h"

class FCireUISettings;
class USoundBase;
class USoundClass;
class USoundMix;
class UAudioComponent;
class USceneComponent;
struct FCireAudioProbe;

/** Mix buses. Each maps to /Game/Audio/Mix/SCL_<Bus> and its SMX_<Bus> submix. */
enum class ECireAudioBus : uint8 { Music, SFX, Ambience, UI, Voice };

/**
 * Game audio front door (see Docs/Audio.md).
 *
 * Cues are data (Content/Data/AudioCues.json). Any system may call these from client code;
 * they are no-ops on a dedicated server, when muted, or when the cue is unknown.
 *
 *   CireAudio::PlayCue(this, "coins_buy", Location);     // positional one-shot
 *   CireAudio::PlayCue2D(this, "loot_pickup");            // 2D one-shot
 *   UAudioComponent* Hum = CireAudio::PlayAttached("teleport_channel", Mesh); // loop; caller stops it
 *
 * Bus volumes (master x bus, mute, music on/off) come from the local player's Options profile and are
 * applied through a sound-mix class override, so cue volumes never need to multiply them again.
 */
namespace CireAudio
{
    /** Effective linear gain for a bus from local preferences: master x bus; 0 when muted (and for Music when music is off). */
    CIRESTEAMSURVIVAL_API float BusGain(const FCireUISettings& Settings, ECireAudioBus Bus);
    /** The local player's preferences (the HUD profile), or defaults when there is no local HUD. */
    CIRESTEAMSURVIVAL_API const FCireUISettings& LocalSettings(const UObject* WorldContext);

    /** Where the local player hears from (the audio device listener; the camera as a fallback). */
    CIRESTEAMSURVIVAL_API FTransform ListenerTransform(const UObject* WorldContext);
    CIRESTEAMSURVIVAL_API bool HasCue(FName CueId);
    CIRESTEAMSURVIVAL_API bool PlayCue(const UObject* WorldContext, FName CueId, FVector Location, float VolumeScale = 1.f);
    CIRESTEAMSURVIVAL_API bool PlayCue2D(const UObject* WorldContext, FName CueId, float VolumeScale = 1.f);
    /** Starts a cue at a location and returns its component (loops keep playing until stopped). */
    CIRESTEAMSURVIVAL_API UAudioComponent* SpawnCueAtLocation(const UObject* WorldContext, FName CueId, FVector Location, float VolumeScale = 1.f);
    /** Starts a cue attached to a component (loops keep playing until the caller calls Stop/FadeOut). */
    CIRESTEAMSURVIVAL_API UAudioComponent* PlayAttached(FName CueId, USceneComponent* AttachTo, FName Socket = NAME_None, float VolumeScale = 1.f);
    /** "Footsteps/FS_Plate_{01..08}" -> eight names (zero-padded like the first bound). */
    CIRESTEAMSURVIVAL_API TArray<FString> ExpandSoundNames(const FString& Pattern);
    /** Re-read the Audio*.json files (developer tooling / tests). */
    CIRESTEAMSURVIVAL_API void ReloadData();
    /** Credits lines required by the CC-BY music licence (Options > Audio shows them). */
    CIRESTEAMSURVIVAL_API const TArray<FString>& Credits();

    // ---- HUD integration (called from small `// audio:` hooks in the HUD) ----
    /** ACireHUD::PlayWowSound index -> cue; false keeps the legacy synthesized sound. */
    CIRESTEAMSURVIVAL_API bool PlayHudSound(const UObject* WorldContext, int32 LegacyIndex, float Volume);
    /** Banner type (ECireBanner as uint8) -> cue; false keeps the legacy horn/chime. */
    CIRESTEAMSURVIVAL_API bool PlayBanner(const UObject* WorldContext, uint8 Banner);
    /** A hovered interactive element (one call per frame while hovered); plays ui_hover on entry. */
    CIRESTEAMSURVIVAL_API void NoteHover(const UObject* WorldContext, float X, float Y);

    /** Name of the current music state ("town", "combat", ...) for HUD/diagnostics. */
    CIRESTEAMSURVIVAL_API FString MusicStateName(const UObject* WorldContext);

#if !UE_BUILD_SHIPPING
    /** Native checks: settings persistence, bus gains, cue data, armour mapping, music transitions, footstep cadence. */
    CIRESTEAMSURVIVAL_API bool RunAudioSmoke(UWorld* World);
    /** Test hook: force the settings used by LocalSettings (nullptr restores the HUD profile). */
    CIRESTEAMSURVIVAL_API void SetSettingsOverride(const FCireUISettings* Settings);
#endif
}

/**
 * Per-world runtime: pushes bus volumes, drives the score, district ambience, positional prop emitters,
 * footsteps and event detection (pack-leader roars, purchases, teleports). Exists only in game worlds
 * that can hear (never on a dedicated server).
 */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireAudioSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override { return bStarted; }
    virtual bool IsTickableWhenPaused() const override { return true; }

    static UCireAudioSubsystem* Get(const UObject* WorldContext);

    /** Resolves "Folder/Name" to /Game/Audio/Folder/Name and caches the loaded sound. */
    USoundBase* ResolveSound(const FString& ShortPath);
    /** Keeps a runtime component alive for the lifetime of this world. */
    void Keep(UAudioComponent* Component);

    FCireMusicDirector Director;
    FCireMusicPlayer Music;
    FCireAmbiencePlayer Ambience;
    FCireFootstepPlayer Footsteps;

    /** Last pushed bus gains (Music, SFX, Ambience, UI, Voice). */
    float AppliedGain[5] = {-1, -1, -1, -1, -1};
    int32 FramesTicked = 0;

private:
    UPROPERTY(Transient) TObjectPtr<USoundMix> VolumeMix;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundClass>> BusClasses;
    UPROPERTY(Transient) TMap<FString, TObjectPtr<USoundBase>> SoundCache;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Owned;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> TeleportHum;
    bool bStarted = false;
    bool bReverbActive = false;

    // event detection state
    TSet<TWeakObjectPtr<AActor>> RoaredBosses;
    TMap<TWeakObjectPtr<AActor>, FString> BossCasting;
    int32 LastGold = INDEX_NONE;
    FVector LastHeroLocation = FVector::ZeroVector;
    bool bHasLastHeroLocation = false;
    float ShakeTime = 0.f, ShakeAmplitude = 0.f;

    void ApplyBusVolumes(const FCireUISettings& Settings);
    void DetectEvents(float DeltaTime);
public:
    /** Footstep camera shake request for the local body (cm). */
    void AddLocalShake(float Amplitude);
    void UpdateShake(float DeltaTime);
    /** -CireAudioProbe (development builds): renders music, ambience, footsteps and event cues to WAV
     *  through the bus submixes with speakers muted (see Tools/RunAudioChecks.py). */
    void TickProbe(float DeltaTime);
    FCireAudioProbe* Probe = nullptr;
    void FinishProbe();
};
