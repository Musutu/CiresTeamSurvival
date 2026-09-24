#include "CireMusic.h"
#include "CireAudio.h"
#include "CireUISettings.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"

namespace
{
CireMusic::FData GMusic;
bool GMusicLoaded = false;
}

const CireMusic::FData& CireMusic::Data(bool bReload)
{
    if(GMusicLoaded && !bReload) return GMusic;
    GMusicLoaded = true; GMusic = FData();
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/AudioMusic.json"))) ||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return GMusic;
    double N = 0;
    if(Root->TryGetNumberField(TEXT("crossfadeSeconds"), N)) GMusic.Crossfade = FMath::Clamp(static_cast<float>(N), .1f, 15.f);
    if(Root->TryGetNumberField(TEXT("combatHoldSeconds"), N)) GMusic.CombatHold = FMath::Clamp(static_cast<float>(N), 0.f, 60.f);
    if(Root->TryGetNumberField(TEXT("bossHoldSeconds"), N)) GMusic.BossHold = FMath::Clamp(static_cast<float>(N), 0.f, 60.f);
    if(Root->TryGetNumberField(TEXT("combatRadius"), N)) GMusic.CombatRadius = static_cast<float>(N);
    if(Root->TryGetNumberField(TEXT("bossRadius"), N)) GMusic.BossRadius = static_cast<float>(N);
    const TSharedPtr<FJsonObject>* States = nullptr;
    if(Root->TryGetObjectField(TEXT("states"), States))
    {
        const TPair<const TCHAR*, ECireMusicState> Keys[] = {{TEXT("town"), ECireMusicState::Town}, {TEXT("combat"), ECireMusicState::Combat},
            {TEXT("boss"), ECireMusicState::Boss}, {TEXT("arena"), ECireMusicState::Arena}};
        for(const auto& Key : Keys)
        {
            const TSharedPtr<FJsonObject>* O = nullptr;
            if(!(*States)->TryGetObjectField(Key.Key, O)) continue;
            FStateDef Def;
            (*O)->TryGetStringArrayField(TEXT("tracks"), Def.Tracks);
            if((*O)->TryGetNumberField(TEXT("volume"), N)) Def.Volume = FMath::Clamp(static_cast<float>(N), 0.f, 2.f);
            if(!Def.Tracks.IsEmpty()) GMusic.States.Add(Key.Value, Def);
        }
    }
    const TSharedPtr<FJsonObject>* Stingers = nullptr;
    if(Root->TryGetObjectField(TEXT("stingers"), Stingers))
    {
        const TSharedPtr<FJsonObject>* O = nullptr;
        if((*Stingers)->TryGetObjectField(TEXT("victory"), O)) { (*O)->TryGetStringField(TEXT("track"), GMusic.VictoryTrack); if((*O)->TryGetNumberField(TEXT("volume"), N)) GMusic.VictoryVolume = static_cast<float>(N); }
        if((*Stingers)->TryGetObjectField(TEXT("defeat"), O)) { (*O)->TryGetStringField(TEXT("track"), GMusic.DefeatTrack); if((*O)->TryGetNumberField(TEXT("volume"), N)) GMusic.DefeatVolume = static_cast<float>(N); }
    }
    Root->TryGetStringArrayField(TEXT("credits"), GMusic.Credits);
    GMusic.bValid = GMusic.States.Num() == 4;
    return GMusic;
}

const TCHAR* FCireMusicDirector::Name(ECireMusicState State)
{
    switch(State)
    {
    case ECireMusicState::Town: return TEXT("town");
    case ECireMusicState::Combat: return TEXT("combat");
    case ECireMusicState::Boss: return TEXT("boss");
    case ECireMusicState::Arena: return TEXT("arena");
    case ECireMusicState::Silence: return TEXT("silence");
    default: return TEXT("none");
    }
}

bool FCireMusicDirector::Update(const FCireMusicInputs& In, float DeltaSeconds, ECireMusicStinger& OutStinger)
{
    OutStinger = ECireMusicStinger::None;
    const float Dt = FMath::Max(0.f, DeltaSeconds);
    ECireMusicState Target = ECireMusicState::Town;
    if(In.Phase != 3) bStingerPlayed = false;
    if(In.Phase != 0) { CombatHold = 0.f; BossHold = 0.f; }
    switch(In.Phase)
    {
    case 3:
        if(!bStingerPlayed) { OutStinger = In.bLocalTeamWon ? ECireMusicStinger::Victory : ECireMusicStinger::Defeat; bStingerPlayed = true; }
        Target = ECireMusicState::Silence;
        break;
    case 2: Target = ECireMusicState::Arena; break;
    case 1: case 4: Target = ECireMusicState::Town; break;
    default:
        BossHold = In.bBoss ? BossHoldSeconds : FMath::Max(0.f, BossHold - Dt);
        CombatHold = In.bCombat ? CombatHoldSeconds : FMath::Max(0.f, CombatHold - Dt);
        Target = (In.bBoss || BossHold > 0.f) ? ECireMusicState::Boss
            : (In.bCombat || CombatHold > 0.f) ? ECireMusicState::Combat : ECireMusicState::Town;
        break;
    }
    const bool bChanged = Target != State;
    State = Target;
    return bChanged;
}

// ---------------------------------------------------------------------------------------------
bool FCireMusicPlayer::IsPlaying() const
{
    for(const auto& Slot : Slots) if(Slot.IsValid() && Slot->IsPlaying()) return true;
    return false;
}

void FCireMusicPlayer::Reset()
{
    for(auto& Slot : Slots) { if(Slot.IsValid()) Slot->Stop(); Slot.Reset(); }
    if(StingerSlot.IsValid()) StingerSlot->Stop();
    StingerSlot.Reset(); Playing = ECireMusicState::None; Track.Reset();
}

void FCireMusicPlayer::Stop(float FadeSeconds)
{
    for(auto& Slot : Slots) if(Slot.IsValid() && Slot->IsPlaying()) Slot->FadeOut(FadeSeconds, 0.f);
    Playing = ECireMusicState::None; Track.Reset();
}

void FCireMusicPlayer::Start(UCireAudioSubsystem& Audio, ECireMusicState State, float Fade)
{
    const CireMusic::FData& Data = CireMusic::Data();
    const CireMusic::FStateDef* Def = Data.States.Find(State);
    Playing = State;
    if(Slots[Active].IsValid() && Slots[Active]->IsPlaying()) Slots[Active]->FadeOut(Fade, 0.f);
    if(!Def || Def->Tracks.IsEmpty()) { Track.Reset(); return; }
    int32& Next = Rotation.FindOrAdd(State);
    const FString Name = Def->Tracks[Next++ % Def->Tracks.Num()];
    USoundBase* Sound = Audio.ResolveSound(TEXT("Music/") + Name);
    if(!Sound) { Track.Reset(); return; }
    Active = 1 - Active;
    if(Slots[Active].IsValid()) Slots[Active]->Stop();
    UAudioComponent* C = UGameplayStatics::CreateSound2D(Audio.GetWorld(), Sound, Def->Volume, 1.f, 0.f, nullptr, false, false);
    Slots[Active].Reset(C);
    if(C) { C->bIsMusic = true; C->FadeIn(Fade, 1.f); }
    Track = Name;
}

void FCireMusicPlayer::Tick(UCireAudioSubsystem& Audio, const FCireUISettings& Settings, ECireMusicState State, ECireMusicStinger Stinger, float DeltaSeconds)
{
    const CireMusic::FData& Data = CireMusic::Data();
    const bool bEnabled = Settings.bMusicEnabled && !Settings.bMuteAudio;
    if(!bEnabled)
    {
        if(bWasEnabled) Stop(1.f);
        bWasEnabled = false;
        return;
    }
    if(!bWasEnabled) { bWasEnabled = true; Playing = ECireMusicState::None; }
    if(Stinger != ECireMusicStinger::None)
    {
        const bool bWin = Stinger == ECireMusicStinger::Victory;
        const FString& Name = bWin ? Data.VictoryTrack : Data.DefeatTrack;
        if(USoundBase* Sound = Name.IsEmpty() ? nullptr : Audio.ResolveSound(TEXT("Music/") + Name))
        {
            if(StingerSlot.IsValid()) StingerSlot->Stop();
            UAudioComponent* C = UGameplayStatics::CreateSound2D(Audio.GetWorld(), Sound, bWin ? Data.VictoryVolume : Data.DefeatVolume, 1.f, 0.f, nullptr, false, false);
            StingerSlot.Reset(C);
            if(C) { C->bIsMusic = true; C->Play(); }
        }
        Stop(1.f);
    }
    if(State == Playing) return;
    if(State == ECireMusicState::None || State == ECireMusicState::Silence) { Stop(Data.Crossfade); Playing = State; return; }
    Start(Audio, State, Data.Crossfade);
}
