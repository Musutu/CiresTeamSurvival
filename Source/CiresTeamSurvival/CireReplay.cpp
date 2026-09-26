#include "CireReplay.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#include "Engine/DemoNetDriver.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/NetworkVersion.h"
#include "Misc/Paths.h"
#include "NetworkReplayStreaming.h"
#include "ReplaySubsystem.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireRealm.h"
#include "CireReplaySpectator.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCireReplay, Log, All);

namespace
{
constexpr const TCHAR* LocalStreamer = TEXT("LocalFileNetworkReplayStreaming");
const TArray<FString> LocalOptions = {TEXT("ReplayStreamerOverride=LocalFileNetworkReplayStreaming")};
UDemoNetDriver* Driver(const UCireReplaySubsystem* Subsystem)
{
    const auto* Instance = Subsystem ? Subsystem->GetGameInstance() : nullptr;
    UWorld* World = Instance ? Instance->GetWorld() : nullptr;
    return World ? World->GetDemoNetDriver() : nullptr;
}
FString SafeTitle(const FString& Label)
{
    FString Result;
    for (TCHAR C : Label.Left(80)) if (FChar::IsAlnum(C) || C == TEXT(' ') || C == TEXT('-') || C == TEXT('_')) Result.AppendChar(C);
    Result.TrimStartAndEndInline();
    return Result.IsEmpty() ? TEXT("Cire Match") : Result;
}
#if !UE_BUILD_SHIPPING
TArray<IConsoleObject*> Commands;
int32 SubsystemCount = 0;
struct FReplayProbe
{
    int32 Step = 0, Checks = 0;
    double Started = FPlatformTime::Seconds(), StepStarted = 0, LastDiagnostic = 0;
    float PausedTime = 0, OriginalMaxFps = 0;
    FString Id;
    TWeakObjectPtr<ACireHero> First, Second;
    bool Check(bool Value, const TCHAR* Text)
    {
        ++Checks;
        if (Value) return true;
        UE_LOG(LogCireReplay, Error, TEXT("CIRE_REPLAY_INTEGRATION_FAIL step=%d reason=%s"), Step, Text);
        FPlatformMisc::RequestExitWithStatus(false, 1); return false;
    }
    bool Tick(UCireReplaySubsystem* Replay)
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 90) return Check(false, TEXT("native replay integration timed out"));
        UWorld* World = Replay->GetGameInstance()->GetWorld();
        if (Step >= 4 && Now - LastDiagnostic >= 3)
        {
            LastDiagnostic = Now;
            const auto* D = Driver(Replay);
            const auto Stream = D ? D->GetReplayStreamer() : nullptr;
            FArchive* Archive = Stream ? Stream->GetStreamingArchive() : nullptr;
            UE_LOG(LogCireReplay, Display, TEXT("CIRE_REPLAY_PROGRESS step=%d world=%s begun=%d worldTime=%.3f playing=%d time=%.3f duration=%.3f frame=%d paused=%d data=%d streamPos=%lld streamBytes=%lld packets=%d tasks=%d viewer=%s"),
                Step, *GetNameSafe(World), World && World->HasBegunPlay(), World ? World->GetTimeSeconds() : -1.f,
                Replay->IsPlaying(), Replay->CurrentSeconds(), Replay->DurationSeconds(), D ? D->GetDemoFrameNum() : -1,
                Replay->IsPaused(), Stream && Stream->IsDataAvailable(), Archive ? Archive->Tell() : -1LL, Archive ? Archive->TotalSize() : -1LL,
                D ? D->PlaybackPackets.Num() : -1, D && D->IsAnyTaskPending(), *GetNameSafe(World ? World->GetFirstPlayerController() : nullptr));
        }
        if (!World || !World->HasBegunPlay()) return true;
        if (Step == 0)
        {
            if (Now - Started < 2) return true;
            if (!Check(World->GetNetMode() == NM_Standalone && !Replay->IsRecording(), TEXT("probe requires standalone without automatic recording"))) return false;
            // Record slower than playback so frame-zero buffered-packet startup
            // is exercised with a real native file, independent of host speed.
            if (auto* Fps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
            { OriginalMaxFps = Fps->GetFloat(); Fps->Set(20.f, ECVF_SetByConsole); }
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            for (int32 Team = 0; Team < 2; ++Team)
            {
                auto* Hero = World->SpawnActor<ACireHero>(FVector(1800, Team == 0 ? -2100 : 2100, 5000), FRotator::ZeroRotator, P);
                if (!Check(Hero != nullptr, TEXT("recording fixture spawned"))) return false;
                Hero->TeamId = Team; Hero->Draft(0); Hero->HeroName = FString::Printf(TEXT("CIRE_REPLAY_FIXTURE_%d"), Team);
                Hero->Health = Hero->MaxHealth = 1000; Hero->SetActorTickEnabled(false); Hero->GetCharacterMovement()->DisableMovement(); Hero->ForceNetUpdate();
                if (Team == 0) First = Hero; else Second = Hero;
            }
            if (!Check(Replay->StartRecording(TEXT("Native replay validation")), TEXT("native recorder starts"))) return false;
            Id = Replay->GetActiveId(); Step = 1; StepStarted = Now;
        }
        else if (Step == 1 && Now - StepStarted >= 1)
        {
            if (!Check(First.IsValid() && Second.IsValid(), TEXT("recorded actors survive"))) return false;
            First->Health = Second->Health = 400; First->ForceNetUpdate(); Second->ForceNetUpdate(); Step = 2; StepStarted = Now;
        }
        else if (Step == 2 && Now - StepStarted >= 3)
        { Replay->StopRecording(); Step = 3; StepStarted = Now; }
        else if (Step == 3 && !Replay->IsBusy())
        {
            const auto* Entry = Replay->GetEntries().FindByPredicate([&](const FCireReplayEntry& E) { return E.Id == Id; });
            if (!Check(Entry && !Entry->bLive && Entry->Bytes > 0 && Entry->DurationSeconds > 2.f, TEXT("finished native replay has bytes and duration"))) return false;
            if (auto* Fps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) Fps->Set(120.f, ECVF_SetByConsole);
            if (!Check(Replay->Play(Id), TEXT("native playback accepted"))) return false;
            Step = 4; StepStarted = Now;
        }
        else if (Step == 4 && Replay->IsPlaying() && Replay->CurrentSeconds() > .5f)
        {
            auto* Viewer = Cast<ACireReplaySpectator>(World->GetFirstPlayerController());
            if (!Viewer || !Viewer->PlayerState || !Viewer->GetViewTarget()) return true;
            if (!Check(Replay->SetPaused(true), TEXT("native playback pauses"))) return false;
            PausedTime = Replay->CurrentSeconds(); Step = 5; StepStarted = Now;
        }
        else if (Step == 5 && Now - StepStarted > .3)
        {
            if (!Check(Replay->IsPaused() && FMath::Abs(Replay->CurrentSeconds() - PausedTime) < .05f, TEXT("paused native replay time remains fixed"))) return false;
            if (!Check(Replay->SetSpeed(2.f) && FMath::IsNearlyEqual(Replay->PlaybackSpeed(), 2.f), TEXT("native replay speed changes"))) return false;
            Replay->SetPaused(false);
            if (!Check(Replay->Seek(.25f), TEXT("native backward seek accepted"))) return false;
            Step = 6; StepStarted = Now;
        }
        else if (Step == 6 && !Replay->IsBusy())
        {
            if (!Check(Replay->CurrentSeconds() < 1.5f, TEXT("backward seek moved native replay clock"))) return false;
            if (!Check(Replay->Seek(Replay->DurationSeconds() - .2f), TEXT("native forward seek accepted"))) return false;
            Step = 7; StepStarted = Now;
        }
        else if (Step == 7 && !Replay->IsBusy())
        {
            bool Teams[2] = {false, false};
            auto* Viewer = World->GetFirstPlayerController();
            for (TCireActorIterator<ACireHero> It(World); It; ++It)
            {
                const int32 Team = It->TeamId;
                if (Team >= 0 && Team < 2 && It->HeroName == FString::Printf(TEXT("CIRE_REPLAY_FIXTURE_%d"), Team))
                    Teams[Team] = It->Health > 0 && It->Health <= 400 && CireRealm::CanObserve(Viewer, *It);
            }
            if (!Check(Teams[0] && Teams[1], TEXT("native stream restores changed health and both observable realms"))) return false;
            if (!Check(Replay->ExitPlayback(), TEXT("native replay exits"))) return false;
            Step = 8; StepStarted = Now;
        }
        else if (Step == 8 && !Replay->IsPlaying() && World->GetNetMode() == NM_Standalone && Now - StepStarted > .5)
        {
            if (auto* Fps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) Fps->Set(OriginalMaxFps, ECVF_SetByConsole);
            UE_LOG(LogCireReplay, Display, TEXT("CIRE_REPLAY_INTEGRATION_PASS checks=%d id=%s"), Checks, *Id);
            FPlatformMisc::RequestExitWithStatus(false, 0); return false;
        }
        return true;
    }
};
void ExecuteReplayCommand(const TArray<FString>& Args, UWorld* World)
{
    auto* Replay = CireReplay::Get(World);
    if (!Replay) return;
    const FString Action = Args.IsEmpty() ? TEXT("status") : Args[0].ToLower();
    if (Action == TEXT("record"))
    {
        FString Title;
        for (int32 I = 1; I < Args.Num(); ++I) { if (!Title.IsEmpty()) Title += TEXT(" "); Title += Args[I]; }
        Replay->StartRecording(Title.IsEmpty() ? TEXT("Cire Match") : Title);
    }
    else if (Action == TEXT("stop")) Replay->StopRecording();
    else if (Action == TEXT("list")) Replay->RefreshList();
    else if (Action == TEXT("play") && Args.Num() == 2) Replay->Play(Args[1]);
    else if (Action == TEXT("pause")) Replay->SetPaused(true);
    else if (Action == TEXT("resume")) Replay->SetPaused(false);
    else if (Action == TEXT("exit")) Replay->ExitPlayback();
    else if ((Action == TEXT("seek") || Action == TEXT("speed")) && Args.Num() == 2)
    {
        float Value = 0;
        if (LexTryParseString(Value, *Args[1])) { if (Action == TEXT("seek")) Replay->Seek(Value); else Replay->SetSpeed(Value); }
        else Replay->Status = TEXT("Enter a finite numeric seek time or speed.");
    }
    else if (Action != TEXT("status")) Replay->Status = TEXT("cire.Replay record [title] | stop | list | play ID | pause | resume | seek seconds | speed .25..4 | exit");
    UE_LOG(LogCireReplay, Display, TEXT("%s"), *Replay->Status);
}
#endif
}
UCireReplaySubsystem* CireReplay::Get(UWorld* World) { return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UCireReplaySubsystem>() : nullptr; }
bool CireReplay::IsPlayback(const UWorld* World) { return World && World->IsPlayingReplay(); }
bool UCireReplaySubsystem::IsValidReplayId(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > 96) return false;
    for (TCHAR C : Id) if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-')) return false;
    return true;
}
FString UCireReplaySubsystem::StorageDirectory() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Demos")); }
void UCireReplaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UReplaySubsystem>();
    Super::Initialize(Collection);
    PlaybackTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCireReplaySubsystem::TickPlaybackBootstrap), .05f);
#if !UE_BUILD_SHIPPING
    if (++SubsystemCount == 1)
        Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("cire.Replay"),
            TEXT("Native local replays: record [title], stop, list, play ID, pause, resume, seek seconds, speed .25..4, exit."),
            FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecuteReplayCommand), ECVF_Default));
    if (FParse::Param(FCommandLine::Get(), TEXT("CireReplayProbe")))
    {
        const auto Probe = MakeShared<FReplayProbe>();
        ProbeTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Probe](float) { return Probe->Tick(this); }), .05f);
    }
#endif
}
void UCireReplaySubsystem::Deinitialize()
{
    if (PlaybackTicker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(PlaybackTicker);
    // UReplaySubsystem handles driver shutdown and flushing; do not cause travel
    // from teardown by calling its generic StopReplay while viewing a replay.
    if (IsRecording()) GetGameInstance()->StopRecordingReplay();
    BrowserStreamer.Reset();
    bListing = bSeeking = false;
#if !UE_BUILD_SHIPPING
    if (ProbeTicker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(ProbeTicker);
    if (--SubsystemCount == 0) { for (auto* Command : Commands) IConsoleManager::Get().UnregisterConsoleObject(Command); Commands.Reset(); }
#endif
    Super::Deinitialize();
}
bool UCireReplaySubsystem::TickPlaybackBootstrap(float)
{
    auto* D = Driver(this);
    const auto Stream = D ? D->GetReplayStreamer() : nullptr;
    // UE 5.8 TickDemoPlayback waits for unread stream data on frame zero even
    // when it already buffered the entire replay. A shorter playback tick than
    // the first recorded frame leaves that packet in the future forever.
    // A native seek reloads the stream and advances to its real first timestamp;
    // all actor creation and packet processing remain inside DemoNetDriver.
    const bool bStalled = D && D->IsPlaying() && D->GetDemoFrameNum() == 0 && !bSeeking &&
        !IsPaused() && !D->IsAnyTaskPending() && Stream && !Stream->IsDataAvailable() &&
        !D->PlaybackPackets.IsEmpty() && DurationSeconds() > 0 &&
        FMath::IsFinite(D->PlaybackPackets[0].TimeSeconds) && D->PlaybackPackets[0].TimeSeconds > D->GetDemoCurrentTime();
    if (!bStalled)
    {
        if (!D || !D->IsPlaying() || D->GetDemoFrameNum() > 0) BootstrapRecoveredDriver.Reset();
        BootstrapObservedDriver.Reset(); BootstrapObservedAt = 0; return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (BootstrapObservedDriver.Get() != D)
    {
        BootstrapObservedDriver = D; BootstrapObservedAt = Now; return true;
    }
    if (Now - BootstrapObservedAt < .25 || BootstrapRecoveredDriver.Get() == D) return true;
    BootstrapRecoveredDriver = D;
    const float FirstPacketTime = D->PlaybackPackets[0].TimeSeconds;
    UE_LOG(LogCireReplay, Display, TEXT("CIRE_REPLAY_NATIVE_BOOTSTRAP buffered=%d current=%.6f first=%.6f"),
        D->PlaybackPackets.Num(), D->GetDemoCurrentTime(), FirstPacketTime);
    Seek(FirstPacketTime + .001f);
    return true;
}
bool UCireReplaySubsystem::IsRecording() const { const auto* D = Driver(this); return D && D->IsRecording(); }
bool UCireReplaySubsystem::IsPlaying() const { const auto* D = Driver(this); return D && D->IsPlaying(); }
bool UCireReplaySubsystem::IsPaused() const
{
    const auto* World = GetGameInstance()->GetWorld();
    return IsPlaying() && World && World->GetWorldSettings()->GetPauserPlayerState() != nullptr;
}
float UCireReplaySubsystem::CurrentSeconds() const { const auto* D = Driver(this); return D ? D->GetDemoCurrentTime() : 0.f; }
float UCireReplaySubsystem::DurationSeconds() const { const auto* D = Driver(this); return D ? D->GetDemoTotalTime() : 0.f; }
float UCireReplaySubsystem::PlaybackSpeed() const
{
    const auto* World = GetGameInstance()->GetWorld();
    return IsPlaying() && World ? World->GetWorldSettings()->DemoPlayTimeDilation : 1.f;
}
bool UCireReplaySubsystem::StartRecording(const FString& Label)
{
    UWorld* World = GetGameInstance()->GetWorld();
    if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client || IsPlaying())
    { Status = TEXT("Only the authoritative match can record; playback cannot be recorded."); return false; }
    if (IsRecording()) { Status = TEXT("A replay is already recording."); return false; }
    if (World->WorldType == EWorldType::PIE) { Status = TEXT("Record in Standalone Game or a packaged build, not Play in Editor."); return false; }
    ActiveId = TEXT("Cire_") + FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S")) + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
    GetGameInstance()->StartRecordingReplay(ActiveId, SafeTitle(Label), LocalOptions);
    if (!IsRecording()) { Status = TEXT("The native replay recorder could not start. Check the game log."); ActiveId.Reset(); return false; }
    Status = TEXT("Recording local replay: ") + ActiveId;
    UE_LOG(LogCireReplay, Display, TEXT("CIRE_REPLAY_RECORDING id=%s directory=%s"), *ActiveId, *StorageDirectory());
    return true;
}
void UCireReplaySubsystem::StopRecording()
{
    if (!IsRecording()) { Status = IsPlaying() ? TEXT("Use Exit to leave replay playback.") : TEXT("No replay is recording."); return; }
    GetGameInstance()->StopRecordingReplay();
    // Finalize the local stream before returning its file to the browser.
    FNetworkReplayStreaming::Get().GetFactory(LocalStreamer).Flush();
    Status = TEXT("Saved replay: ") + ActiveId;
    UE_LOG(LogCireReplay, Display, TEXT("CIRE_REPLAY_SAVED id=%s"), *ActiveId);
    RefreshList();
}
void UCireReplaySubsystem::RefreshList()
{
    if (bListing) return;
    if (!BrowserStreamer.IsValid()) BrowserStreamer = FNetworkReplayStreaming::Get().GetFactory(LocalStreamer).CreateReplayStreamer();
    if (!BrowserStreamer.IsValid()) { Status = TEXT("Local replay storage is unavailable."); return; }
    bListing = true; Status = TEXT("Reading local replays...");
    BrowserStreamer->EnumerateStreams(FNetworkVersion::GetReplayVersion(), INDEX_NONE, FString(), TArray<FString>(),
        FEnumerateStreamsCallback::CreateWeakLambda(this, [this](const FEnumerateStreamsResult& Result)
        {
            bListing = false;
            if (!Result.WasSuccessful()) { Status = TEXT("Could not read the local replay list."); return; }
            Entries.Reset();
            for (const auto& Stream : Result.FoundStreams)
            {
                if (!IsValidReplayId(Stream.Name)) continue;
                FCireReplayEntry Entry;
                Entry.Id = Stream.Name; Entry.Title = Stream.FriendlyName; Entry.RecordedUtc = Stream.Timestamp;
                Entry.DurationSeconds = FMath::Max(0, Stream.LengthInMS) / 1000.f; Entry.Bytes = Stream.SizeInBytes; Entry.bLive = Stream.bIsLive;
                Entries.Add(MoveTemp(Entry));
            }
            Entries.Sort([](const FCireReplayEntry& A, const FCireReplayEntry& B) { return A.RecordedUtc > B.RecordedUtc; });
            Status = FString::Printf(TEXT("%d compatible local replay(s)."), Entries.Num());
            UE_LOG(LogCireReplay, Display, TEXT("CIRE_REPLAY_LIST count=%d"), Entries.Num());
            for (const auto& Entry : Entries) UE_LOG(LogCireReplay, Display, TEXT("REPLAY id=%s seconds=%.1f live=%d title=%s"), *Entry.Id, Entry.DurationSeconds, Entry.bLive, *Entry.Title);
        }));
}
bool UCireReplaySubsystem::Play(const FString& Id)
{
    // The UI may pass a reference into Entries; stopping a recording can refresh
    // that array, so retain the selected ID across the asynchronous operation.
    const FString ReplayId = Id;
    UWorld* World = GetGameInstance()->GetWorld();
    if (!World || (World->GetNetMode() != NM_Standalone && !IsPlaying()))
    { Status = TEXT("Leave the multiplayer session before viewing a local replay."); return false; }
    if (!IsValidReplayId(ReplayId)) { Status = TEXT("Invalid local replay identifier."); return false; }
    if (IsBusy()) { Status = TEXT("Wait for the replay operation to finish."); return false; }
    const auto* Entry = Entries.FindByPredicate([&](const FCireReplayEntry& Candidate) { return Candidate.Id == ReplayId; });
    if (!Entry || Entry->bLive) { Status = TEXT("Refresh the list and select a finished compatible replay."); return false; }
    if (IsRecording()) StopRecording();
    bSeeking = false;
    if (!GetGameInstance()->PlayReplay(ReplayId, World, LocalOptions)) { Status = TEXT("The native replay player could not open that recording."); return false; }
    ActiveId = ReplayId; Status = TEXT("Opening replay: ") + ReplayId;
    UE_LOG(LogCireReplay, Display, TEXT("CIRE_REPLAY_PLAY_REQUEST id=%s"), *ReplayId);
    return true;
}
bool UCireReplaySubsystem::SetPaused(bool bPause)
{
    UWorld* World = GetGameInstance()->GetWorld();
    if (!IsPlaying() || !World || bSeeking) return false;
    auto* Controller = World->GetFirstPlayerController();
    if (bPause && (!Controller || !Controller->PlayerState)) { Status = TEXT("Replay spectator is still loading."); return false; }
    World->GetWorldSettings()->SetPauserPlayerState(bPause ? Controller->PlayerState.Get() : nullptr);
    Status = bPause ? TEXT("Replay paused.") : TEXT("Replay playing.");
    return true;
}
bool UCireReplaySubsystem::SetSpeed(float Multiplier)
{
    if (!IsPlaying() || !FMath::IsFinite(Multiplier) || Multiplier < .25f || Multiplier > 4.f) return false;
    GetGameInstance()->GetWorld()->GetWorldSettings()->DemoPlayTimeDilation = Multiplier;
    Status = FString::Printf(TEXT("Replay speed %.2fx"), Multiplier); return true;
}
bool UCireReplaySubsystem::Seek(float Seconds)
{
    auto* D = Driver(this);
    if (!D || !D->IsPlaying() || bSeeking || !FMath::IsFinite(Seconds) || DurationSeconds() <= 0) return false;
    const bool bRestorePause = IsPaused();
    if (bRestorePause) SetPaused(false);
    bSeeking = true; Status = TEXT("Seeking replay...");
    D->GotoTimeInSeconds(FMath::Clamp(Seconds, 0.f, FMath::Max(0.f, DurationSeconds() - .01f)),
        FOnGotoTimeDelegate::CreateWeakLambda(this, [this, bRestorePause](bool bSuccess)
        {
            bSeeking = false;
            if (bRestorePause && IsPlaying()) SetPaused(true);
            Status = bSuccess ? TEXT("Replay seek complete.") : TEXT("Replay seek failed.");
            UE_LOG(LogCireReplay, Display, TEXT("CIRE_REPLAY_SEEK success=%d time=%.2f"), bSuccess, CurrentSeconds());
        }));
    return true;
}
bool UCireReplaySubsystem::ExitPlayback()
{
    if (!IsPlaying()) return false;
    if (auto* World = GetGameInstance()->GetWorld()) { World->GetWorldSettings()->SetPauserPlayerState(nullptr); World->GetWorldSettings()->DemoPlayTimeDilation = 1.f; }
    bSeeking = false;
    // Native StopReplay returns to GameDefaultMap (configured by UReplaySubsystem).
    GetGameInstance()->StopRecordingReplay();
    Status = TEXT("Exited replay; returning to the battlefield.");
    return true;
}

#if !UE_BUILD_SHIPPING
bool CireReplay::RunReplaySmoke(UWorld* World)
{
    int32 Checks = 0; bool bPass = true;
    auto Check = [&](bool Value, const TCHAR* Label) { ++Checks; if (!Value) { bPass = false; UE_LOG(LogCireReplay, Error, TEXT("CIRE_REPLAY_CHECK_FAIL %s"), Label); } };
    Check(UCireReplaySubsystem::IsValidReplayId(TEXT("Cire_20260923T210000_abcdef123456")), TEXT("generated replay identifier accepted"));
    for (const TCHAR* Invalid : {TEXT(""), TEXT("../outside"), TEXT("C:\\outside"), TEXT("replay?listen"), TEXT("name.replay"), TEXT("unsafe name")})
        Check(!UCireReplaySubsystem::IsValidReplayId(Invalid), TEXT("path or URL replay identifier rejected"));
    Check(!UCireReplaySubsystem::IsValidReplayId(FString::ChrN(97, TEXT('a'))), TEXT("replay ID length bounded"));
    auto* Replay = Get(World);
    Check(Replay != nullptr, TEXT("native replay subsystem initialized"));
    if (Replay && !Replay->IsPlaying() && !Replay->IsRecording())
    {
        const FString OldStatus = Replay->Status;
        Check(!Replay->SetPaused(true) && !Replay->Seek(10) && !Replay->SetSpeed(2), TEXT("playback controls cannot mutate a live match"));
        Check(!Replay->Play(TEXT("../outside")), TEXT("playback rejects path traversal before engine dispatch"));
        Check(!Replay->Play(TEXT("missing_safe_replay")), TEXT("unlisted recordings cannot be loaded"));
        Replay->Status = OldStatus;
    }
    Check(FNetworkReplayStreaming::Get().GetFactory(LocalStreamer).CreateReplayStreamer().IsValid(), TEXT("native local file replay factory available"));
    UE_LOG(LogCireReplay, Display, TEXT("CIRE_REPLAY_%s checks=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks);
    return bPass;
}
#endif
