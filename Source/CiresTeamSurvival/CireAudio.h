#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CireMusic.h"
#include "CireAmbience.h"
#include "CireFootsteps.h"
#include "CireSoundEvents.h"
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
/** Options for CireAudio::PlayCueEx (the sound-event path used by combat, spells, deaths and UI). */
struct FCirePlayParams
{
    /** World position (nullptr and no Attach = 2D). */
    const FVector* Location = nullptr;
    /** Follow this component instead of a fixed location (loops keep playing until the caller stops them). */
    USceneComponent* Attach = nullptr;
    FName Socket = NAME_None;
    float Volume = 1.f;
    float Pitch = 1.f;
    /** Added to the cue priority for the combat voice budget (the local player's own actions and incoming hits). */
    float PriorityBoost = 0.f;
    /** The local player caused or receives it: its own cooldown key, never starved by other units' spam. */
    bool bLocal = false;
};

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
    /** CireShopUI sound name -> cue (AudioCues.json shopLegacy); false keeps the shop's own sound. */
    CIRESTEAMSURVIVAL_API bool PlayShopSound(const UObject* WorldContext, const TCHAR* LegacyName, float Volume);
    /** A hovered interactive element (one call per frame while hovered); plays ui_hover on entry. */
    CIRESTEAMSURVIVAL_API void NoteHover(const UObject* WorldContext, float X, float Y);

    // ---- sound events (Docs/Audio.md "Sound events") ----
    /** Full-control play: bus/attenuation/voice limits/priority come from the cue; returns the component (nullptr when dropped). */
    CIRESTEAMSURVIVAL_API UAudioComponent* PlayCueEx(const UObject* WorldContext, FName CueId, const FCirePlayParams& Params);
    /** A hard-coded UI sound path (e.g. /Game/UI/Draft/Sounds/S_SkillOffer...) -> cue (AudioCues.json uiLegacy); false keeps the caller's sound. */
    CIRESTEAMSURVIVAL_API bool PlayLegacyPath(const UObject* WorldContext, const TCHAR* Path, float Volume);
    /** Music ducking for big moments: Depth 0..1 of the music bus for Seconds (the deepest active request wins). */
    CIRESTEAMSURVIVAL_API void Duck(const UObject* WorldContext, float Depth, float Seconds);
    /** Current music duck gain (1 = none). */
    CIRESTEAMSURVIVAL_API float DuckGain(const UObject* WorldContext);
    /** Hover tick for a hovered button with no world context at hand (CireUIStyle::Button); uses the local game world. */
    CIRESTEAMSURVIVAL_API void NoteUIHover(float X, float Y);
    /** Fab pack members (cue "pack") are preferred when installed; false forces the shipped fallback (tests, A/B). */
    CIRESTEAMSURVIVAL_API void SetPacksEnabled(bool bEnabled);
    CIRESTEAMSURVIVAL_API bool PacksEnabled();
    /** True when the cue currently resolves to installed Fab pack members. */
    CIRESTEAMSURVIVAL_API bool CueUsesPack(FName CueId);
    /** The members the cue plays right now (pack or fallback), as asset paths. */
    CIRESTEAMSURVIVAL_API TArray<FString> CueMembers(FName CueId);
    CIRESTEAMSURVIVAL_API bool CueIsLoop(FName CueId);
    CIRESTEAMSURVIVAL_API TArray<FName> CueIds();
    /** Picks the next member of a cue and applies bus, attenuation and priority to an existing component (looping spell/area audio). */
    CIRESTEAMSURVIVAL_API bool ConfigureComponent(UAudioComponent* Component, FName CueId, float VolumeScale = 1.f);
    /** Short "Folder/Name" (under /Game/Audio) or a full /Game/... path -> object path; empty when it does not exist. */
    CIRESTEAMSURVIVAL_API FString SoundObjectPath(const FString& Name, bool bCheckExists = true);
    /** Play/drop counters for the soak and the probe. */
    struct FStats { int32 Played = 0, PackPlays = 0, FallbackPlays = 0, DroppedCooldown = 0, DroppedVoices = 0, DroppedBudget = 0, Stolen = 0; };
    CIRESTEAMSURVIVAL_API FStats& Stats();

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
    /** The Options bus class (SCL_<Bus>), used as the class override for pack sounds. */
    USoundClass* BusClass(ECireAudioBus Bus) const;
    /** /Game/Audio/Mix/SC_<Name>, cached. */
    class USoundConcurrency* Concurrency(FName Name);
    /** Keeps a runtime component alive for the lifetime of this world. */
    void Keep(UAudioComponent* Component);

    FCireMusicDirector Director;
    FCireMusicPlayer Music;
    FCireAmbiencePlayer Ambience;
    FCireFootstepPlayer Footsteps;
    /** Combat/spell/UI sound events: outcomes, hit reactions, deaths, casts, UI state (CireSoundEvents.h). */
    FCireSoundEventTracker Events;

    /** Music duck (CireAudio::Duck): the deepest active request and how long it holds. */
    float DuckDepth = 0.f, DuckHold = 0.f, DuckGainNow = 1.f;
    void RequestDuck(float Depth, float Seconds);

    /** Last pushed bus gains (Music, SFX, Ambience, UI, Voice). */
    float AppliedGain[5] = {-1, -1, -1, -1, -1};
    int32 FramesTicked = 0;
    float SoakClock = 0.f;

private:
    UPROPERTY(Transient) TObjectPtr<USoundMix> VolumeMix;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundClass>> BusClasses;
    UPROPERTY(Transient) TMap<FString, TObjectPtr<USoundBase>> SoundCache;
    UPROPERTY(Transient) TMap<FName, TObjectPtr<class USoundConcurrency>> ConcurrencyCache;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Owned;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> TeleportHum;
    bool bStarted = false;
    bool bReverbActive = false;

    // event detection state
    TSet<TWeakObjectPtr<AActor>> RoaredBosses;
    TMap<TWeakObjectPtr<AActor>, FString> BossCasting;
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
