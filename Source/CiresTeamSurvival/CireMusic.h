#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/AudioComponent.h"

class UAudioComponent;
class UWorld;
class UCireAudioSubsystem;
class FCireUISettings;

/** Score states (Content/Data/AudioMusic.json). Silence follows the end-of-match stinger. */
enum class ECireMusicState : uint8 { None, Town, Combat, Boss, Arena, Silence };
enum class ECireMusicStinger : uint8 { None, Victory, Defeat };

/** What the director needs to know this frame; gathered client-side from replicated state. */
struct FCireMusicInputs
{
    /** ACireGameState::Phase: 0 survival, 1 prep, 2 arena, 3 finished, 4 recovery. */
    int32 Phase = 0;
    /** A lane wave is still advancing, or a challenge pack is fighting a hero near the listener. */
    bool bCombat = false;
    /** A lane boss is alive in this realm, or a Pack Leader is engaged near the listener. */
    bool bBoss = false;
    /** Only meaningful when Phase == 3. */
    bool bLocalTeamWon = true;
};

/**
 * Pure state machine for the score. Combat and Boss are held for a few seconds after the last threat
 * so short lulls between packs do not flip the music; the finish plays one stinger, then silence.
 */
struct CIRESTEAMSURVIVAL_API FCireMusicDirector
{
    float CombatHoldSeconds = 6.f;
    float BossHoldSeconds = 9.f;
    ECireMusicState State = ECireMusicState::None;
    float CombatHold = 0.f;
    float BossHold = 0.f;
    bool bStingerPlayed = false;

    /** Advances the machine. Returns true when State changed; OutStinger is set once on the finish. */
    bool Update(const FCireMusicInputs& In, float DeltaSeconds, ECireMusicStinger& OutStinger);
    static const TCHAR* Name(ECireMusicState State);
    static bool IsCombatState(ECireMusicState State)
    {
        return State == ECireMusicState::Combat || State == ECireMusicState::Boss || State == ECireMusicState::Arena;
    }
};

namespace CireMusic
{
    struct FStateDef { TArray<FString> Tracks; float Volume = .55f; };
    struct FData
    {
        bool bValid = false;
        float Crossfade = 3.f, CombatHold = 6.f, BossHold = 9.f, CombatRadius = 4200.f, BossRadius = 6500.f;
        TMap<ECireMusicState, FStateDef> States;
        FString VictoryTrack, DefeatTrack;
        float VictoryVolume = .75f, DefeatVolume = .75f;
        TArray<FString> Credits;
        TMap<FString, FString> Titles;
    };
    CIRESTEAMSURVIVAL_API const FData& Data(bool bReload = false);
}

/** Two crossfading music slots plus a stinger slot; honours the music on/off switch. */
class CIRESTEAMSURVIVAL_API FCireMusicPlayer
{
public:
    void Tick(UCireAudioSubsystem& Audio, const FCireUISettings& Settings, ECireMusicState State, ECireMusicStinger Stinger, float DeltaSeconds);
    void Stop(float FadeSeconds);
    void Reset();
    /** The track currently fading in / playing ("" when silent). */
    const FString& CurrentTrack() const { return Track; }
    /** Display title of the current track ("Five Armies"), or "" when silent. */
    FString CurrentTitle() const;
    bool IsPlaying() const;
private:
    TStrongObjectPtr<UAudioComponent> Slots[2];
    TStrongObjectPtr<UAudioComponent> StingerSlot;
    int32 Active = 0;
    ECireMusicState Playing = ECireMusicState::None;
    TMap<ECireMusicState, int32> Rotation;
    FString Track;
    bool bWasEnabled = true;
    void Start(UCireAudioSubsystem& Audio, ECireMusicState State, float Fade);
};
