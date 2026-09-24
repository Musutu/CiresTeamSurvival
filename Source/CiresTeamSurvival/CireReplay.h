#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "CireReplay.generated.h"

class INetworkReplayStreamer;
class UDemoNetDriver;

USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireReplayEntry
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() FString Title;
    UPROPERTY() FDateTime RecordedUtc;
    UPROPERTY() float DurationSeconds = 0;
    UPROPERTY() int64 Bytes = 0;
    UPROPERTY() bool bLive = false;
};

// The actual recording and playback are Unreal network replays, stored by
// LocalFileNetworkReplayStreaming under Saved/Demos. This object survives travel.
UCLASS()
class CIRESTEAMSURVIVAL_API UCireReplaySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    bool StartRecording(const FString& Label = TEXT("Cire Match"));
    void StopRecording();
    void RefreshList();
    bool Play(const FString& Id);
    bool SetPaused(bool bPause);
    bool SetSpeed(float Multiplier);
    bool Seek(float Seconds);
    bool ExitPlayback();
    bool IsRecording() const;
    bool IsPlaying() const;
    bool IsPaused() const;
    bool IsBusy() const { return bListing || bSeeking; }
    float CurrentSeconds() const;
    float DurationSeconds() const;
    float PlaybackSpeed() const;
    const TArray<FCireReplayEntry>& GetEntries() const { return Entries; }
    const FString& GetActiveId() const { return ActiveId; }
    static bool IsValidReplayId(const FString& Id);
    static FString StorageDirectory();
    UPROPERTY() FString Status = TEXT("Local replays ready.");
private:
    UPROPERTY() TArray<FCireReplayEntry> Entries;
    TSharedPtr<INetworkReplayStreamer> BrowserStreamer;
    FString ActiveId;
    bool bListing = false;
    bool bSeeking = false;
    bool TickPlaybackBootstrap(float DeltaSeconds);
    FTSTicker::FDelegateHandle PlaybackTicker;
    TWeakObjectPtr<UDemoNetDriver> BootstrapObservedDriver;
    TWeakObjectPtr<UDemoNetDriver> BootstrapRecoveredDriver;
    double BootstrapObservedAt = 0;
#if !UE_BUILD_SHIPPING
    FTSTicker::FDelegateHandle ProbeTicker;
#endif
};

namespace CireReplay
{
    CIRESTEAMSURVIVAL_API UCireReplaySubsystem* Get(UWorld* World);
    CIRESTEAMSURVIVAL_API bool IsPlayback(const UWorld* World);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunReplaySmoke(UWorld* World);
#endif
}
