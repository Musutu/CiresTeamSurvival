#include "CireAudio.h"
#include "CireEnvironmentProps.h"
#include "CireUISettings.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundSubmix.h"
#include "Sound/AudioSettings.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

DEFINE_LOG_CATEGORY_STATIC(LogCireAudioTest, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
TArray<FString> JsonKeys(const TCHAR* File, const TCHAR* Field, bool bArrayOfObjectsWithId)
{
    TArray<FString> Out;
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), File)) ||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return Out;
    if(bArrayOfObjectsWithId)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if(Root->TryGetArrayField(Field, A)) for(const auto& V : *A) { FString Id; if(V->AsObject() && V->AsObject()->TryGetStringField(TEXT("id"), Id)) Out.Add(Id); }
    }
    else
    {
        const TSharedPtr<FJsonObject>* O = nullptr;
        if(Root->TryGetObjectField(Field, O)) for(const auto& Pair : (*O)->Values) Out.Add(FString(*Pair.Key));
    }
    return Out;
}

bool SoundExists(const FString& Short)
{
    FString Folder, Name;
    if(!Short.Split(TEXT("/"), &Folder, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) return false;
    return LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/%s/%s.%s"), *Folder, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet) != nullptr;
}
}

bool CireAudio::RunAudioSmoke(UWorld* World)
{
    bool Pass = true; int32 Count = 0;
    auto Check = [&](bool bValue, const FString& Name) { ++Count; Pass &= bValue; if(!bValue) UE_LOG(LogCireAudioTest, Error, TEXT("CIRE_AUDIO_ASSERT %s"), *Name); };

    // ---- 1. settings persistence (volumes / toggles) ----
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AudioTests"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString File = FPaths::Combine(Directory, TEXT("profile-") + FGuid::NewGuid().ToString() + TEXT(".ini"));
    FCireUISettings A; A.Load(File);
    Check(FMath::IsNearlyEqual(A.MusicVolume, .6f) && FMath::IsNearlyEqual(A.AmbienceVolume, .8f) && A.bMusicEnabled && !A.bFootstepCameraShake, TEXT("audio defaults (music .6, ambience .8, music on, shake off)"));
    A.MasterVolume = .5f; A.MusicVolume = .33f; A.AmbienceVolume = .44f; A.SFXVolume = .66f; A.UIVolume = .22f;
    A.bMusicEnabled = false; A.bFootstepCameraShake = true; A.bMuteAudio = true;
    Check(A.Save(), TEXT("save audio profile"));
    FCireUISettings B; B.Load(File);
    Check(FMath::IsNearlyEqual(B.MusicVolume, .33f) && FMath::IsNearlyEqual(B.AmbienceVolume, .44f) && FMath::IsNearlyEqual(B.MasterVolume, .5f)
        && FMath::IsNearlyEqual(B.SFXVolume, .66f) && FMath::IsNearlyEqual(B.UIVolume, .22f), TEXT("volume roundtrip"));
    Check(!B.bMusicEnabled && B.bFootstepCameraShake && B.bMuteAudio, TEXT("music/shake/mute toggles roundtrip"));
    B.MusicVolume = 7.f; B.AmbienceVolume = std::numeric_limits<float>::quiet_NaN(); Check(B.Save(), TEXT("sanitize save"));
    FCireUISettings C; C.Load(File);
    Check(C.MusicVolume == 1.f && FMath::IsNearlyEqual(C.AmbienceVolume, .8f), TEXT("music clamps to 1, NaN ambience restores default"));
    const FString Legacy = FPaths::Combine(Directory, TEXT("legacy-") + FGuid::NewGuid().ToString() + TEXT(".ini"));
    FFileHelper::SaveStringToFile(TEXT("[CireUI.Preferences]\nVersion=4\nMasterVolume=0.4\n"), *Legacy);
    FCireUISettings D; D.Load(Legacy);
    Check(FMath::IsNearlyEqual(D.MasterVolume, .4f) && FMath::IsNearlyEqual(D.MusicVolume, .6f) && D.bMusicEnabled && !D.bFootstepCameraShake, TEXT("older profile keeps audio defaults"));
    D.Reset();
    Check(FMath::IsNearlyEqual(D.MusicVolume, .6f) && D.bMusicEnabled, TEXT("reset restores audio defaults"));
    IFileManager::Get().Delete(*File); IFileManager::Get().Delete(*Legacy);

    // ---- 2. bus gains ----
    FCireUISettings G; G.MasterVolume = .5f; G.MusicVolume = .5f; G.SFXVolume = 1.f; G.AmbienceVolume = .2f; G.UIVolume = .4f;
    Check(FMath::IsNearlyEqual(BusGain(G, ECireAudioBus::Music), .25f) && FMath::IsNearlyEqual(BusGain(G, ECireAudioBus::SFX), .5f)
        && FMath::IsNearlyEqual(BusGain(G, ECireAudioBus::Ambience), .1f) && FMath::IsNearlyEqual(BusGain(G, ECireAudioBus::UI), .2f), TEXT("bus gain = master x bus"));
    G.bMusicEnabled = false;
    Check(BusGain(G, ECireAudioBus::Music) == 0.f && BusGain(G, ECireAudioBus::SFX) > 0.f, TEXT("music off silences only music"));
    G.bMuteAudio = true;
    Check(BusGain(G, ECireAudioBus::SFX) == 0.f && BusGain(G, ECireAudioBus::Ambience) == 0.f && BusGain(G, ECireAudioBus::UI) == 0.f, TEXT("mute silences every bus"));

    // ---- 3. data: every referenced sound exists ----
    ReloadData();
    Check(ExpandSoundNames(TEXT("A/B_{01..03}")).Num() == 3 && ExpandSoundNames(TEXT("A/B_{01..03}"))[2] == TEXT("A/B_03") && ExpandSoundNames(TEXT("A/C")).Num() == 1, TEXT("brace expansion"));
    for(const TCHAR* Cue : {TEXT("ui_click"), TEXT("ui_hover"), TEXT("level_up"), TEXT("aggro_taken"), TEXT("banner_wave"), TEXT("banner_prep"), TEXT("banner_arena"),
        TEXT("coins_buy"), TEXT("coins_sell"), TEXT("loot_pickup"), TEXT("teleport_channel"), TEXT("teleport_arrive"), TEXT("pack_leader_roar")})
        Check(HasCue(Cue), FString::Printf(TEXT("required cue %s"), Cue));
    {
        FString Text; TSharedPtr<FJsonObject> Root; const TSharedPtr<FJsonObject>* Cues = nullptr;
        FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/AudioCues.json")));
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root && Root->TryGetObjectField(TEXT("cues"), Cues))
            for(const auto& Pair : (*Cues)->Values)
            {
                const TArray<TSharedPtr<FJsonValue>>* Sounds = nullptr;
                if(Pair.Value->AsObject() && Pair.Value->AsObject()->TryGetArrayField(TEXT("sounds"), Sounds))
                    for(const auto& S : *Sounds) for(const FString& Name : ExpandSoundNames(S->AsString())) Check(SoundExists(Name), FString::Printf(TEXT("cue %s sound %s"), *Pair.Key, *Name));
            }
        else Check(false, TEXT("AudioCues.json parses"));
    }
    const CireMusic::FData& Music = CireMusic::Data(true);
    Check(Music.bValid && Music.Credits.Num() > 0, TEXT("music data: four states + CC-BY credits"));
    for(const auto& Pair : Music.States) for(const FString& Track : Pair.Value.Tracks) Check(SoundExists(TEXT("Music/") + Track), TEXT("music track ") + Track);
    Check(SoundExists(TEXT("Music/") + Music.VictoryTrack) && SoundExists(TEXT("Music/") + Music.DefeatTrack), TEXT("victory/defeat stingers exist"));
    const CireAmbience::FData& Amb = CireAmbience::Data(true);
    Check(Amb.bValid && SoundExists(Amb.Night.Sound), TEXT("ambience data + night bed"));
    for(const auto& District : CireEnvironmentProps::Districts()) Check(Amb.Districts.Contains(District.Id), TEXT("ambience for district ") + District.Id.ToString());
    for(const auto& Pair : Amb.Districts)
    {
        for(const auto& Bed : Pair.Value.Beds) Check(SoundExists(Bed.Sound), FString::Printf(TEXT("%s bed %s"), *Pair.Key.ToString(), *Bed.Sound));
        for(const auto& Shot : Pair.Value.OneShots) Check(HasCue(Shot.Cue), FString::Printf(TEXT("%s one-shot cue %s"), *Pair.Key.ToString(), *Shot.Cue.ToString()));
    }
    for(const auto& Pair : Amb.Emitters) Check(HasCue(Pair.Value.Cue), TEXT("emitter cue for ") + Pair.Key.ToString());

    // ---- 4. armour-class mapping for every roster profile and NPC archetype ----
    const CireFootsteps::FData& Steps = CireFootsteps::Data(true);
    Check(Steps.bValid, TEXT("footstep data valid"));
    const TArray<FString> Profiles = JsonKeys(TEXT("ChampionRoster.json"), TEXT("champions"), true);
    Check(Profiles.Num() >= 22, FString::Printf(TEXT("roster has profiles (%d)"), Profiles.Num()));
    for(const FString& Id : Profiles)
    {
        bool bExplicit = false; const CireFootsteps::FBinding Bind = CireFootsteps::ForProfile(Id, 0, &bExplicit);
        Check(bExplicit && Steps.Classes.Contains(Bind.Class), TEXT("armour class for profile ") + Id);
    }
    const TArray<FString> Monsters = JsonKeys(TEXT("NPCArchetypes.json"), TEXT("archetypes"), false);
    Check(Monsters.Num() >= 7, TEXT("NPC archetypes present"));
    for(const FString& Id : Monsters) { bool bExplicit = false; CireFootsteps::ForMonster(Id, &bExplicit); Check(bExplicit, TEXT("armour class for monster ") + Id); }
    Check(CireFootsteps::ForProfile(TEXT("knight"), 0).Class == TEXT("plate") && CireFootsteps::ForProfile(TEXT("ranger"), 1).Class == TEXT("leather")
        && CireFootsteps::ForProfile(TEXT("scholar"), 2).Class == TEXT("cloth") && CireFootsteps::ForProfile(TEXT("bear"), 0).Class == TEXT("bear")
        && CireFootsteps::ForProfile(TEXT("evergrove_centaur"), 2).Class == TEXT("hooves") && CireFootsteps::ForProfile(TEXT("whisp"), 2).Class == TEXT("whisp"), TEXT("key profiles map to plate/leather/cloth/bear/hooves/whisp"));
    Check(CireFootsteps::ForProfile(TEXT("unknown_new_hero"), 1).Class == TEXT("leather"), TEXT("unlisted profile falls back by runtime archetype"));
    for(const auto& Pair : Steps.Classes)
        for(const auto& Layer : Pair.Value.Layers)
            for(const TArray<FString>* Set : {&Layer.Stone, &Layer.Dirt, &Layer.Any})
                for(const FString& Name : *Set) Check(SoundExists(Name), FString::Printf(TEXT("class %s sound %s"), *Pair.Key.ToString(), *Name));
    Check(Steps.Classes[TEXT("bear")].CameraShake > 0.f && Steps.Classes[TEXT("plate")].CameraShake == 0.f, TEXT("only heavy bodies request camera shake"));

    // ---- 5. music state transitions ----
    {
        FCireMusicDirector M; M.CombatHoldSeconds = 6.f; M.BossHoldSeconds = 9.f;
        ECireMusicStinger S = ECireMusicStinger::None;
        auto Run = [&](FCireMusicInputs In, float Seconds) { ECireMusicStinger Last = ECireMusicStinger::None; int32 Stingers = 0;
            for(float T = 0; T < Seconds; T += .1f) { M.Update(In, .1f, S); if(S != ECireMusicStinger::None) { Last = S; ++Stingers; } } S = Last; return Stingers; };
        FCireMusicInputs In; In.Phase = 0;
        Run(In, .5f); Check(M.State == ECireMusicState::Town, TEXT("warm-up plays town"));
        In.bCombat = true; Run(In, .2f); Check(M.State == ECireMusicState::Combat, TEXT("lane wave -> combat"));
        In.bCombat = false; Run(In, 3.f); Check(M.State == ECireMusicState::Combat, TEXT("combat held through a 3 s lull"));
        Run(In, 4.f); Check(M.State == ECireMusicState::Town, TEXT("combat released after the hold"));
        In.bBoss = true; In.bCombat = true; Run(In, .2f); Check(M.State == ECireMusicState::Boss, TEXT("boss beats combat"));
        In.bBoss = false; Run(In, 5.f); Check(M.State == ECireMusicState::Boss, TEXT("boss held 5 s after the kill"));
        Run(In, 5.f); Check(M.State == ECireMusicState::Combat, TEXT("boss hold ends into combat while the wave continues"));
        In.Phase = 1; In.bCombat = false; Run(In, .2f); Check(M.State == ECireMusicState::Town && M.CombatHold == 0.f, TEXT("prep -> town, holds cleared"));
        In.Phase = 2; Run(In, .2f); Check(M.State == ECireMusicState::Arena, TEXT("arena -> arena music"));
        In.Phase = 4; Run(In, .2f); Check(M.State == ECireMusicState::Town, TEXT("recovery -> town"));
        In.Phase = 3; In.bLocalTeamWon = true; const int32 Wins = Run(In, 2.f);
        Check(M.State == ECireMusicState::Silence && Wins == 1 && S == ECireMusicStinger::Victory, TEXT("finish: one victory stinger then silence"));
        In.Phase = 0; Run(In, .2f); In.Phase = 3; In.bLocalTeamWon = false; const int32 Losses = Run(In, 2.f);
        Check(Losses == 1 && S == ECireMusicStinger::Defeat, TEXT("new finish re-arms the stinger (defeat)"));
        Check(FString(FCireMusicDirector::Name(ECireMusicState::Boss)) == TEXT("boss") && FCireMusicDirector::IsCombatState(ECireMusicState::Arena)
            && !FCireMusicDirector::IsCombatState(ECireMusicState::Town), TEXT("state names and ambience ducking states"));
    }

    // ---- 6. footstep cadence vs speed ----
    for(const auto& Pair : Steps.Classes)
    {
        const CireFootsteps::FClass& Class = Pair.Value;
        Check(CireFootsteps::StepInterval(Class, 0.f) == 0.f && CireFootsteps::StepInterval(Class, 20.f) == 0.f, Pair.Key.ToString() + TEXT(": no steps while standing"));
        float Previous = 1e9f; bool bMonotonic = true;
        for(float Speed = 60.f; Speed <= 800.f; Speed += 20.f)
        {
            const float I = CireFootsteps::StepInterval(Class, Speed, Steps.SpeedWalk, Steps.SpeedRun);
            bMonotonic &= I > 0.f && I <= Previous + 1e-4f; Previous = I;
        }
        Check(bMonotonic, Pair.Key.ToString() + TEXT(": faster movement never slows the cadence"));
        if(Pair.Key != TEXT("whisp"))
        {
            const float Walk = 1.f / CireFootsteps::StepInterval(Class, 250.f, Steps.SpeedWalk, Steps.SpeedRun);
            const float Run = 1.f / CireFootsteps::StepInterval(Class, 600.f, Steps.SpeedWalk, Steps.SpeedRun);
            Check(Walk >= 1.5f && Walk <= 3.6f && Run >= 3.f && Run <= 6.5f && Run > Walk,
                FString::Printf(TEXT("%s: plausible cadence walk %.2f/s run %.2f/s"), *Pair.Key.ToString(), Walk, Run));
        }
        // integrate a 10 s run at 60 Hz the way the player does
        const float Interval = CireFootsteps::StepInterval(Class, 450.f, Steps.SpeedWalk, Steps.SpeedRun);
        float Acc = Interval * .6f; int32 Fired = 0;
        for(int32 Frame = 0; Frame < 600; ++Frame) { Acc += 1.f / 60.f; if(Acc >= Interval) { Acc -= Interval; ++Fired; } }
        Check(FMath::Abs(Fired - 10.f / Interval) <= 1.5f, FString::Printf(TEXT("%s: %d steps in 10 s at 450 cm/s"), *Pair.Key.ToString(), Fired));
    }
    Check(!CireFootsteps::PhaseCrossedContact(.1f, .2f) && CireFootsteps::PhaseCrossedContact(3.f, 3.3f) && CireFootsteps::PhaseCrossedContact(6.2f, .1f)
        && CireFootsteps::PhaseCrossedContact(.1f, 6.2f) && CireFootsteps::PhaseCrossedContact(3.3f, 3.f) && !CireFootsteps::PhaseCrossedContact(1.f, 2.f), TEXT("gait contacts at 0 and PI in both directions"));
    {
        int32 Contacts = 0; float Phase = 0.f;
        for(int32 Frame = 0; Frame < 600; ++Frame) { const float Next = FMath::Fmod(Phase + 2.f * PI * 1.5f / 60.f, 2.f * PI); Contacts += CireFootsteps::PhaseCrossedContact(Phase, Next) ? 1 : 0; Phase = Next; }
        Check(Contacts >= 29 && Contacts <= 31, FString::Printf(TEXT("1.5 gait cycles/s for 10 s -> 30 diagonal contacts (%d)"), Contacts));
    }

    // ---- 7. runtime: the subsystem exists on a hearing client and respects the step budget ----
    if(World && World->GetNetMode() != NM_DedicatedServer)
    {
        UCireAudioSubsystem* Audio = UCireAudioSubsystem::Get(World);
        Check(Audio != nullptr, TEXT("audio subsystem created for a game world"));
        if(Audio)
        {
            const int32 Before = Audio->Footsteps.StepsDropped;
            Audio->Footsteps.Budget = 0.f;
            Audio->Footsteps.Step(*Audio, nullptr, FVector::ZeroVector, 300.f, false, false);
            Check(Audio->Footsteps.StepsDropped == Before + 1, TEXT("step budget caps concurrent footsteps"));
            Check(Audio->ResolveSound(TEXT("Footsteps/FS_Plate_Stone_01")) != nullptr && Audio->ResolveSound(TEXT("Music/MUS_ThePyre")) != nullptr, TEXT("runtime sound resolution"));
        }
    }
    UE_LOG(LogCireAudioTest, Display, TEXT("CIRE_AUDIO_SMOKE_%s checks=%d"), Pass ? TEXT("PASS") : TEXT("FAIL"), Count);
    return Pass;
}
#endif

// ---------------------------------------------------------------------------------------------
// Offline render probe (-CireAudioProbe). Records the Music / Ambience / SFX submixes to WAV while their
// outputs are muted (recording taps the submix before its output volume), so nothing reaches the speakers.
struct FCireAudioProbe
{
    float T = 0.f;
    float RecordStart = -1.f;
    bool bStopped = false;
    float ExitAt = -1.f;
    FString OutDir;
    TArray<TStrongObjectPtr<USoundSubmix>> Record;
    TArray<TStrongObjectPtr<USoundSubmix>> Muted;
    TArray<TSharedPtr<FJsonValue>> Segments;
    int32 StepIndex = 0;
    float NextStep = 0.f, NextCue = 0.f;
    int32 CueIndex = 0;
    ECireMusicState LastState = ECireMusicState::None;
};

void UCireAudioSubsystem::FinishProbe()
{
    if(Probe) { delete Probe; Probe = nullptr; }
}

void UCireAudioSubsystem::TickProbe(float DeltaTime)
{
#if UE_BUILD_SHIPPING
    return;
#else
    UWorld* World = GetWorld();
    if(!Probe)
    {
        Probe = new FCireAudioProbe();
        Probe->OutDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AudioChecks"), FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"))));
        FString Override;
        if(FParse::Value(FCommandLine::Get(), TEXT("CireAudioProbeOut="), Override)) Probe->OutDir = Override;
        IFileManager::Get().MakeDirectory(*Probe->OutDir, true);
        return;
    }
    FCireAudioProbe& P = *Probe;
    P.T += DeltaTime;
    auto Segment = [&](const TCHAR* Bus, const FString& Name, float Start, float End)
    {
        TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("bus"), Bus); O->SetStringField(TEXT("name"), Name);
        O->SetNumberField(TEXT("start"), Start); O->SetNumberField(TEXT("end"), End);
        P.Segments.Add(MakeShared<FJsonValueObject>(O));
    };
    if(P.RecordStart < 0.f)
    {
        if(P.T < 2.f) return; // let the map and audio device settle
        if(!World->GetAudioDevice().IsValid())
        {
            UE_LOG(LogCireAudioTest, Error, TEXT("CIRE_AUDIO_PROBE_FAIL no audio device (do not pass -nosound)"));
            FPlatformMisc::RequestExit(false);
            return;
        }
        for(const TCHAR* Name : {TEXT("SMX_Music"), TEXT("SMX_SFX"), TEXT("SMX_Ambience"), TEXT("SMX_UI"), TEXT("SMX_Voice")})
            if(USoundSubmix* S = LoadObject<USoundSubmix>(nullptr, *FString::Printf(TEXT("/Game/Audio/Mix/%s.%s"), Name, Name)))
            {
                S->SetSubmixOutputVolume(World, 0.f); // silent speakers; the recording taps pre-output
                P.Muted.Emplace(S);
                if(FString(Name) != TEXT("SMX_UI") && FString(Name) != TEXT("SMX_Voice")) { S->StartRecordingOutput(World, 60.f); P.Record.Emplace(S); }
            }
        // The engine's main submix too, so gameplay sounds outside the Cire buses stay silent as well.
        if(USoundSubmix* Main = Cast<USoundSubmix>(GetDefault<UAudioSettings>()->MasterSubmix.TryLoad()))
        {
            Main->SetSubmixOutputVolume(World, 0.f);
            P.Muted.Emplace(Main);
        }
        // An unattended run has no focused window, and BaseEngine.ini silences unfocused apps
        // (UnfocusedVolumeMultiplier=0). Lift it for the probe; the speakers stay muted above.
        FApp::SetUnfocusedVolumeMultiplier(1.f);
        // Loud, known settings regardless of the local profile.
        static FCireUISettings Loud; Loud.MasterVolume = 1.f; Loud.MusicVolume = 1.f; Loud.SFXVolume = 1.f; Loud.AmbienceVolume = 1.f; Loud.UIVolume = 1.f;
        Loud.bMuteAudio = false; Loud.bMusicEnabled = true;
        CireAudio::SetSettingsOverride(&Loud);
        for(float& G : AppliedGain) G = -1.f;
        ApplyBusVolumes(Loud);
        P.RecordStart = P.T;
        UE_LOG(LogCireAudioTest, Display, TEXT("CIRE_AUDIO_PROBE_RECORDING submixes=%d out=%s"), P.Record.Num(), *P.OutDir);
        return;
    }
    const float R = P.T - P.RecordStart;
    const FCireUISettings& Settings = CireAudio::LocalSettings(World);
    APlayerController* PC = World->GetFirstPlayerController();
    const FTransform Ear = CireAudio::ListenerTransform(World);
    const FVector Listener = Ear.GetLocation(), Forward = Ear.GetRotation().GetForwardVector();

    // Music: town 0-8, combat 8-16, boss 16-24, arena 24-32, victory stinger 32-40.
    static const ECireMusicState States[] = {ECireMusicState::Town, ECireMusicState::Combat, ECireMusicState::Boss, ECireMusicState::Arena};
    const int32 MusicIndex = FMath::Clamp(FMath::FloorToInt(R / 8.f), 0, 4);
    ECireMusicState State = MusicIndex < 4 ? States[MusicIndex] : ECireMusicState::Silence;
    ECireMusicStinger Stinger = ECireMusicStinger::None;
    if(State != P.LastState)
    {
        if(State == ECireMusicState::Silence) Stinger = ECireMusicStinger::Victory;
        else Segment(TEXT("Music"), FCireMusicDirector::Name(State), MusicIndex * 8.f + 3.5f, MusicIndex * 8.f + 8.f);
        if(State == ECireMusicState::Silence) Segment(TEXT("Music"), TEXT("victory_stinger"), 32.2f, 38.f);
        P.LastState = State;
        Director.State = State;
    }
    Music.Tick(*this, Settings, State, Stinger, DeltaTime);

    // Ambience: six districts, 6.5 s each (3 s bed crossfade), from 0 to 39.
    static const TCHAR* Districts[] = {TEXT("gate"), TEXT("market"), TEXT("residential"), TEXT("square"), TEXT("castle"), TEXT("arena")};
    const int32 DistrictIndex = FMath::Clamp(FMath::FloorToInt(R / 6.5f), 0, 5);
    if(Ambience.ForcedDistrict != Districts[DistrictIndex])
    {
        Ambience.ForcedDistrict = Districts[DistrictIndex];
        Segment(TEXT("Ambience"), Districts[DistrictIndex], DistrictIndex * 6.5f + 3.2f, DistrictIndex * 6.5f + 6.5f);
    }
    Ambience.Tick(*this, Listener, 0, false, false, DeltaTime);

    // Footsteps: every class, five steps each at 0.45 s, 2.6 s per class, from 0.5 s. Then event cues.
    const CireFootsteps::FData& Steps = CireFootsteps::Data();
    TArray<FName> Classes; Steps.Classes.GetKeys(Classes); Classes.Sort(FNameLexicalLess());
    const int32 ClassIndex = P.StepIndex / 5;
    if(ClassIndex < Classes.Num() && R >= .5f + ClassIndex * 2.6f + (P.StepIndex % 5) * .45f)
    {
        if(P.StepIndex % 5 == 0) Segment(TEXT("SFX"), TEXT("step_") + Classes[ClassIndex].ToString(), .5f + ClassIndex * 2.6f, .5f + ClassIndex * 2.6f + 2.4f);
        const FVector Foot = Listener + Forward * 160.f - FVector(0, 0, 150);
        Footsteps.PlayClass(*this, Steps.Classes[Classes[ClassIndex]], CireFootsteps::FBinding{Classes[ClassIndex]}, Foot, 320.f, false, nullptr);
        ++P.StepIndex;
    }
    static const TCHAR* EventCues[] = {TEXT("level_up"), TEXT("aggro_taken"), TEXT("banner_wave"), TEXT("banner_prep"), TEXT("banner_arena"), TEXT("coins_buy"), TEXT("loot_pickup"), TEXT("teleport_arrive"), TEXT("pack_leader_roar")};
    const float EventStart = .5f + Classes.Num() * 2.6f + .5f;
    if(ClassIndex >= Classes.Num() && P.CueIndex < UE_ARRAY_COUNT(EventCues) && R >= EventStart + P.CueIndex * 1.6f)
    {
        const FString Cue = EventCues[P.CueIndex];
        Segment(TEXT("SFX"), TEXT("cue_") + Cue, EventStart + P.CueIndex * 1.6f, EventStart + P.CueIndex * 1.6f + 1.4f);
        if(Cue == TEXT("pack_leader_roar")) CireAudio::PlayCue(World, *Cue, Listener + Forward * 400.f);
        else CireAudio::PlayCue2D(World, *Cue);
        ++P.CueIndex;
    }

    const float End = FMath::Max(40.f, EventStart + UE_ARRAY_COUNT(EventCues) * 1.6f + 1.f);
    if(!P.bStopped && R >= End)
    {
        P.bStopped = true;
        for(auto& S : P.Record) S->StopRecordingOutput(World, EAudioRecordingExportType::WavFile, S->GetName(), P.OutDir + TEXT("/"));
        TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetArrayField(TEXT("segments"), P.Segments);
        Root->SetNumberField(TEXT("seconds"), R);
        Root->SetNumberField(TEXT("footsteps"), Footsteps.StepsPlayed);
        Root->SetNumberField(TEXT("ambienceOneShots"), Ambience.OneShotsPlayed);
        Root->SetNumberField(TEXT("emitters"), Ambience.ActiveEmitters());
        FString Json;
        FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(Json, *(P.OutDir / TEXT("timeline.json")));
        P.ExitAt = P.T + 4.f; // WAV writing finishes asynchronously
        UE_LOG(LogCireAudioTest, Display, TEXT("CIRE_AUDIO_PROBE_STOPPED seconds=%.1f footsteps=%d out=%s"), R, Footsteps.StepsPlayed, *P.OutDir);
    }
    if(P.bStopped && P.T >= P.ExitAt)
    {
        CireAudio::SetSettingsOverride(nullptr);
        UE_LOG(LogCireAudioTest, Display, TEXT("CIRE_AUDIO_PROBE_DONE out=%s"), *P.OutDir);
        FPlatformMisc::RequestExit(false);
        P.ExitAt = 1e9f;
    }
#endif
}
