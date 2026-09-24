#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/AudioComponent.h"

class UAudioComponent;
class UCireAudioSubsystem;
class UWorld;

namespace CireAmbience
{
    struct FBed { FString Sound; float Volume = .5f; };
    struct FOneShot { FName Cue; FVector2D Interval = FVector2D(8, 16), Distance = FVector2D(800, 2400), Height = FVector2D(0, 300); };
    struct FDistrict { TArray<FBed> Beds; TArray<FOneShot> OneShots; bool bNoNight = false; /* arenas: e.g. underwater */ };
    struct FEmitter { FName Cue; FVector Offset = FVector::ZeroVector; float Volume = 1.f; FVector2D Interval = FVector2D::ZeroVector; };
    struct FData
    {
        bool bValid = false;
        float FadeSeconds = 3.f, DuckLevel = .45f, DuckSeconds = 1.5f, EmitterRadius = 2600.f;
        int32 MaxEmitters = 8;
        FBed Night;
        TMap<FName, FDistrict> Districts;
        TMap<FName, FEmitter> Emitters;
    };
    CIRESTEAMSURVIVAL_API const FData& Data(bool bReload = false);
    /** District id used for ambience: the town district, "arena" in the PvP arena, otherwise "outskirts". */
    CIRESTEAMSURVIVAL_API FName ResolveDistrict(const UWorld* World, int32 Team, const FVector& Location, bool bArena);
}

/** District beds (2D loops, cross-faded by weight), random one-shots and the nearest prop emitters. */
class CIRESTEAMSURVIVAL_API FCireAmbiencePlayer
{
public:
    void Tick(UCireAudioSubsystem& Audio, const FVector& Listener, int32 Team, bool bArena, bool bDuck, float DeltaSeconds);
    void Reset();
    FName CurrentDistrict() const { return District; }
    int32 ActiveBeds() const;
    int32 ActiveEmitters() const;
    float Duck() const { return DuckGain; }
    /** Test/probe override: play this district regardless of the listener position (NAME_None = off). */
    FName ForcedDistrict;
    int32 OneShotsPlayed = 0;
private:
    struct FBedState { TStrongObjectPtr<UAudioComponent> Component; float Current = 0.f; };
    struct FEmitterSlot
    {
        TStrongObjectPtr<UAudioComponent> Component;
        FVector Location = FVector::ZeroVector;
        FName Slot, Cue;
        float Volume = 1.f, NextPlay = 0.f;
        FVector2D Interval = FVector2D::ZeroVector;
    };
    struct FPropPoint { FName Slot; FVector Location; };
    TMap<FString, FBedState> Beds;
    TArray<FEmitterSlot> EmitterSlots;
    TArray<FPropPoint> Props;
    TMap<int32, float> OneShotTimers;
    FName District;
    float DuckGain = 1.f;
    float PropScanTimer = 0.f;
    float EmitterTimer = 0.f;
    float Clock = 0.f;
    void ScanProps(UWorld* World);
    void UpdateEmitters(UCireAudioSubsystem& Audio, const FVector& Listener);
};
