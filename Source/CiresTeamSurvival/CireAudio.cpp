#include "CireAudio.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireUISettings.h"
#include "CireNPCState.h"
#include "CireItems.h"
#include "AudioDevice.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/ReverbEffect.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundAttenuation.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireAudio, Log, All);

namespace
{
struct FCue
{
    TArray<FString> Sounds;      // shipped fallback (CC0 / CC BY, under /Game/Audio)
    TArray<FString> Pack;        // Fab pack members (/Game/<Pack>/...), preferred when installed
    TArray<FString> Active;      // what plays now: installed pack members, else Sounds
    TArray<FName> With;          // layered cues fired together with this one
    float Volume = 1.f, PitchMin = 1.f, PitchMax = 1.f, Jitter = .08f, Cooldown = 0.f;
    float PackVolume = 1.f, PackPitchMin = -1.f, PackPitchMax = -1.f;
    float Priority = 1.f, Duck = 0.f, DuckSeconds = 0.f;
    int32 MaxVoices = 0;         // simultaneous components of this cue (0 = unlimited)
    int32 ResolvedEpoch = -1;
    bool bLoop = false, bCombat = false, bPackActive = false, bHasBus = false;
    ECireAudioBus Bus = ECireAudioBus::SFX;
    FName Attenuation;           // "combat", "close", "large", ... (AudioCues.json "attenuation") or none (sound's own)
    FName Concurrency;           // SC_<name> under /Game/Audio/Mix, optional
};
struct FAttenuationPreset { float Inner = 150.f, Falloff = 3000.f, LowPassAtMax = 3000.f, ReverbMin = .12f, ReverbMax = .5f; };
struct FCueData
{
    bool bValid = false;
    TMap<FName, FCue> Cues;
    TMap<int32, FName> HudLegacy;
    TMap<FString, FName> Banners;
    TMap<FString, FName> Shop;
    TMap<FString, FName> UiLegacy;
    TMap<FName, FAttenuationPreset> Attenuations;
    int32 MaxCombatVoices = 28;
};
FCueData GCueData;
bool GCueLoaded = false;
bool GPacksEnabled = true;
int32 GPackEpoch = 0;
CireAudio::FStats GStats;
TMap<FName, double> GLastCuePlay;
TMap<FName, int32> GLastCuePick;
TMap<FName, TArray<TWeakObjectPtr<UAudioComponent>>> GVoices;
struct FCombatVoice { TWeakObjectPtr<UAudioComponent> Component; float Priority = 0.f; };
TArray<FCombatVoice> GCombatVoices;
TWeakObjectPtr<UWorld> GUIWorld;
#if !UE_BUILD_SHIPPING
const FCireUISettings* GSettingsOverride = nullptr;
#endif

TSharedPtr<FJsonObject> ReadJson(const TCHAR* File)
{
    FString Text;
    if(!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), File))) return nullptr;
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    return FJsonSerializer::Deserialize(Reader, Root) ? Root : nullptr;
}

void ReadRange(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float& Min, float& Max)
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if(O->TryGetArrayField(Field, A) && A->Num() == 2) { Min = (*A)[0]->AsNumber(); Max = (*A)[1]->AsNumber(); if(Max < Min) Swap(Min, Max); }
}

bool ParseBus(const FString& S, ECireAudioBus& Out)
{
    if(S.Equals(TEXT("music"), ESearchCase::IgnoreCase)) Out = ECireAudioBus::Music;
    else if(S.Equals(TEXT("sfx"), ESearchCase::IgnoreCase) || S.Equals(TEXT("combat"), ESearchCase::IgnoreCase)) Out = ECireAudioBus::SFX;
    else if(S.Equals(TEXT("ambience"), ESearchCase::IgnoreCase)) Out = ECireAudioBus::Ambience;
    else if(S.Equals(TEXT("ui"), ESearchCase::IgnoreCase)) Out = ECireAudioBus::UI;
    else if(S.Equals(TEXT("voice"), ESearchCase::IgnoreCase)) Out = ECireAudioBus::Voice;
    else return false;
    return true;
}

FCueData& MutableCues()
{
    if(GCueLoaded) return GCueData;
    GCueLoaded = true; GCueData = FCueData(); ++GPackEpoch;
    const TSharedPtr<FJsonObject> Root = ReadJson(TEXT("AudioCues.json"));
    if(!Root) { UE_LOG(LogCireAudio, Warning, TEXT("AudioCues.json missing or invalid")); return GCueData; }
    double Budget = 0;
    if(Root->TryGetNumberField(TEXT("maxCombatVoices"), Budget)) GCueData.MaxCombatVoices = FMath::Clamp(static_cast<int32>(Budget), 4, 128);
    const TSharedPtr<FJsonObject>* Presets = nullptr;
    if(Root->TryGetObjectField(TEXT("attenuation"), Presets))
        for(const auto& Pair : (*Presets)->Values)
            if(const TSharedPtr<FJsonObject> O = Pair.Value->AsObject())
            {
                FAttenuationPreset P; double N = 0;
                if(O->TryGetNumberField(TEXT("inner"), N)) P.Inner = FMath::Max(0.f, static_cast<float>(N));
                if(O->TryGetNumberField(TEXT("falloff"), N)) P.Falloff = FMath::Max(100.f, static_cast<float>(N));
                if(O->TryGetNumberField(TEXT("lowPassAtMax"), N)) P.LowPassAtMax = FMath::Clamp(static_cast<float>(N), 200.f, 20000.f);
                ReadRange(O, TEXT("reverb"), P.ReverbMin, P.ReverbMax);
                GCueData.Attenuations.Add(FName(*Pair.Key), P);
            }
    const TSharedPtr<FJsonObject>* CueMap = nullptr;
    if(Root->TryGetObjectField(TEXT("cues"), CueMap))
        for(const auto& Pair : (*CueMap)->Values)
        {
            const TSharedPtr<FJsonObject> O = Pair.Value->AsObject(); if(!O) continue;
            FCue Cue;
            const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
            if(O->TryGetArrayField(TEXT("sounds"), List)) for(const auto& S : *List) Cue.Sounds.Append(CireAudio::ExpandSoundNames(S->AsString()));
            if(O->TryGetArrayField(TEXT("pack"), List)) for(const auto& S : *List) Cue.Pack.Append(CireAudio::ExpandSoundNames(S->AsString()));
            if(O->TryGetArrayField(TEXT("with"), List)) for(const auto& S : *List) Cue.With.Add(FName(*S->AsString()));
            double N = 0;
            if(O->TryGetNumberField(TEXT("volume"), N)) Cue.Volume = FMath::Clamp(static_cast<float>(N), 0.f, 4.f);
            if(O->TryGetNumberField(TEXT("volumeJitter"), N)) Cue.Jitter = FMath::Clamp(static_cast<float>(N), 0.f, .9f);
            if(O->TryGetNumberField(TEXT("cooldown"), N)) Cue.Cooldown = FMath::Max(0.f, static_cast<float>(N));
            if(O->TryGetNumberField(TEXT("packVolume"), N)) Cue.PackVolume = FMath::Clamp(static_cast<float>(N), 0.f, 4.f);
            if(O->TryGetNumberField(TEXT("priority"), N)) Cue.Priority = FMath::Clamp(static_cast<float>(N), 0.f, 100.f);
            if(O->TryGetNumberField(TEXT("maxVoices"), N)) Cue.MaxVoices = FMath::Clamp(static_cast<int32>(N), 0, 64);
            if(O->TryGetNumberField(TEXT("duck"), N)) Cue.Duck = FMath::Clamp(static_cast<float>(N), 0.f, 1.f);
            if(O->TryGetNumberField(TEXT("duckSeconds"), N)) Cue.DuckSeconds = FMath::Clamp(static_cast<float>(N), 0.f, 10.f);
            ReadRange(O, TEXT("pitch"), Cue.PitchMin, Cue.PitchMax);
            ReadRange(O, TEXT("packPitch"), Cue.PackPitchMin, Cue.PackPitchMax);
            O->TryGetBoolField(TEXT("loop"), Cue.bLoop);
            O->TryGetBoolField(TEXT("combat"), Cue.bCombat);
            FString Text;
            if(O->TryGetStringField(TEXT("bus"), Text)) Cue.bHasBus = ParseBus(Text, Cue.Bus);
            if(O->TryGetStringField(TEXT("attenuation"), Text)) Cue.Attenuation = FName(*Text);
            if(O->TryGetStringField(TEXT("concurrency"), Text)) Cue.Concurrency = FName(*Text);
            if(!Cue.Sounds.IsEmpty() || !Cue.Pack.IsEmpty()) GCueData.Cues.Add(FName(*Pair.Key), Cue);
        }
    const TSharedPtr<FJsonObject>* Hud = nullptr;
    if(Root->TryGetObjectField(TEXT("hudLegacy"), Hud))
        for(const auto& Pair : (*Hud)->Values) if(FString(*Pair.Key).IsNumeric()) GCueData.HudLegacy.Add(FCString::Atoi(*Pair.Key), FName(*Pair.Value->AsString()));
    const TSharedPtr<FJsonObject>* Banners = nullptr;
    if(Root->TryGetObjectField(TEXT("banners"), Banners))
        for(const auto& Pair : (*Banners)->Values) if(FString(*Pair.Key) != TEXT("notes")) GCueData.Banners.Add(FString(*Pair.Key), FName(*Pair.Value->AsString()));
    const TSharedPtr<FJsonObject>* Shop = nullptr;
    if(Root->TryGetObjectField(TEXT("shopLegacy"), Shop))
        for(const auto& Pair : (*Shop)->Values) if(FString(*Pair.Key) != TEXT("notes")) GCueData.Shop.Add(FString(*Pair.Key), FName(*Pair.Value->AsString()));
    const TSharedPtr<FJsonObject>* Ui = nullptr;
    if(Root->TryGetObjectField(TEXT("uiLegacy"), Ui))
        for(const auto& Pair : (*Ui)->Values) if(FString(*Pair.Key) != TEXT("notes")) GCueData.UiLegacy.Add(FString(*Pair.Key), FName(*Pair.Value->AsString()));
    GCueData.bValid = !GCueData.Cues.IsEmpty();
    return GCueData;
}
const FCueData& Cues() { return MutableCues(); }

/** Installed pack members, else the shipped fallback. Re-resolved when packs are toggled or data reloads. */
const TArray<FString>& Members(FCue& Cue)
{
    if(Cue.ResolvedEpoch == GPackEpoch) return Cue.Active;
    Cue.ResolvedEpoch = GPackEpoch; Cue.Active.Reset(); Cue.bPackActive = false;
    if(GPacksEnabled)
        for(const FString& P : Cue.Pack) if(!CireAudio::SoundObjectPath(P).IsEmpty()) Cue.Active.Add(P);
    Cue.bPackActive = !Cue.Active.IsEmpty();
    if(!Cue.bPackActive) Cue.Active = Cue.Sounds;
    return Cue.Active;
}

UWorld* WorldOf(const UObject* Context)
{
    return GEngine && Context ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
}

/** Picks a cue member (never the previous one when there is a choice) and computes volume/pitch. */
USoundBase* Prepare(UCireAudioSubsystem& Audio, FName Id, FCue& Cue, float Scale, float& OutVolume, float& OutPitch)
{
    const TArray<FString>& List = Members(Cue);
    if(List.IsEmpty()) return nullptr;
    int32 Index = List.Num() == 1 ? 0 : FMath::RandRange(0, List.Num() - 1);
    if(List.Num() > 1) if(const int32* Last = GLastCuePick.Find(Id); Last && *Last == Index) Index = (Index + 1) % List.Num();
    GLastCuePick.Add(Id, Index);
    OutVolume = Cue.Volume * Scale * (1.f - FMath::FRand() * Cue.Jitter) * (Cue.bPackActive ? Cue.PackVolume : 1.f);
    const bool bPackPitch = Cue.bPackActive && Cue.PackPitchMin > 0.f;
    OutPitch = bPackPitch ? FMath::FRandRange(Cue.PackPitchMin, Cue.PackPitchMax) : FMath::FRandRange(Cue.PitchMin, Cue.PitchMax);
    USoundBase* Sound = Audio.ResolveSound(List[Index]);
    if(Sound) { ++(Cue.bPackActive ? GStats.PackPlays : GStats.FallbackPlays); }
    return Sound;
}

bool CooldownReady(FName Key, float Cooldown)
{
    const double Now = FPlatformTime::Seconds();
    if(const double* Last = GLastCuePlay.Find(Key); Last && Now - *Last < Cooldown) return false;
    GLastCuePlay.Add(Key, Now);
    return true;
}

FSoundAttenuationSettings MakeAttenuation(const FAttenuationPreset& P)
{
    FSoundAttenuationSettings A;
    A.bAttenuate = true; A.bSpatialize = true;
    A.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
    A.AttenuationShape = EAttenuationShape::Sphere;
    A.AttenuationShapeExtents = FVector(P.Inner, 0, 0);
    A.FalloffDistance = P.Falloff;
    A.bAttenuateWithLPF = true; A.LPFRadiusMin = P.Inner; A.LPFRadiusMax = P.Inner + P.Falloff; A.LPFFrequencyAtMax = P.LowPassAtMax;
    A.bEnableReverbSend = true; A.ReverbWetLevelMin = P.ReverbMin; A.ReverbWetLevelMax = P.ReverbMax;
    A.ReverbDistanceMin = P.Inner; A.ReverbDistanceMax = P.Inner + P.Falloff;
    return A;
}

/** Bus class, attenuation override, priority and concurrency from the cue (pack sounds carry none of ours). */
void ApplyCueSettings(UCireAudioSubsystem& Audio, UAudioComponent* C, const FCue& Cue, bool bPositional)
{
    if(Cue.bHasBus || Cue.bPackActive)
        if(USoundClass* Class = Audio.BusClass(Cue.bHasBus ? Cue.Bus : ECireAudioBus::SFX)) C->SoundClassOverride = Class;
    if(bPositional && !Cue.Attenuation.IsNone())
        if(const FAttenuationPreset* P = Cues().Attenuations.Find(Cue.Attenuation))
        { C->bOverrideAttenuation = true; C->AttenuationOverrides = MakeAttenuation(*P); C->bAllowSpatialization = true; }
    if(!Cue.Concurrency.IsNone())
        if(USoundConcurrency* Conc = Audio.Concurrency(Cue.Concurrency)) C->ConcurrencySet.Add(Conc);
    C->bOverridePriority = true; C->Priority = FMath::Max(.1f, Cue.Priority);
}

void Prune(TArray<TWeakObjectPtr<UAudioComponent>>& List)
{
    List.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& C) { return !C.IsValid() || !C->IsPlaying(); });
}

UAudioComponent* Spawn(const UObject* Context, FName Id, const FCirePlayParams& Params, int32 Depth = 0)
{
    UWorld* World = WorldOf(Context ? Context : Params.Attach);
    UCireAudioSubsystem* Audio = UCireAudioSubsystem::Get(World);
    FCue* Cue = MutableCues().Cues.Find(Id);
    if(!Audio || !Cue || CireAudio::LocalSettings(World).bMuteAudio) return nullptr;
    // The local player's own events keep a separate cooldown so other units' spam never starves them.
    const FName Key = Params.bLocal ? FName(*(Id.ToString() + TEXT("#local"))) : Id;
    if(!CooldownReady(Key, Cue->Cooldown)) { ++GStats.DroppedCooldown; return nullptr; }
    const float Priority = Cue->Priority + Params.PriorityBoost;
    // Per-cue voice limit: other units' copies are dropped; the local player's steal the oldest.
    if(Cue->MaxVoices > 0)
    {
        TArray<TWeakObjectPtr<UAudioComponent>>& Voices = GVoices.FindOrAdd(Id);
        Prune(Voices);
        if(Voices.Num() >= Cue->MaxVoices)
        {
            if(!Params.bLocal) { ++GStats.DroppedVoices; return nullptr; }
            if(Voices[0].IsValid()) Voices[0]->FadeOut(.06f, 0.f);
            Voices.RemoveAt(0); ++GStats.Stolen;
        }
    }
    // Combat voice budget: when full, a new sound only plays if it outranks the lowest-priority voice (which fades).
    if(Cue->bCombat)
    {
        GCombatVoices.RemoveAll([](const FCombatVoice& V) { return !V.Component.IsValid() || !V.Component->IsPlaying(); });
        if(GCombatVoices.Num() >= Cues().MaxCombatVoices)
        {
            int32 Lowest = 0;
            for(int32 I = 1; I < GCombatVoices.Num(); ++I) if(GCombatVoices[I].Priority < GCombatVoices[Lowest].Priority) Lowest = I;
            if(GCombatVoices[Lowest].Priority >= Priority) { ++GStats.DroppedBudget; return nullptr; }
            GCombatVoices[Lowest].Component->FadeOut(.05f, 0.f);
            GCombatVoices.RemoveAt(Lowest); ++GStats.Stolen;
        }
    }
    float Volume = 1.f, Pitch = 1.f;
    USoundBase* Sound = Prepare(*Audio, Id, *Cue, Params.Volume, Volume, Pitch);
    Pitch *= Params.Pitch;
    // Legacy sounds without a sound class (e.g. /Game/Audio/CireCombat) bypass the bus mix unless the cue names a bus.
    if(Sound && !Sound->GetSoundClass() && !Cue->bHasBus && !Cue->bPackActive) Volume *= CireAudio::BusGain(CireAudio::LocalSettings(World), ECireAudioBus::SFX);
    if(!Sound || Volume <= 0.f) return nullptr;
    if(Cue->bLoop) if(USoundWave* Wave = Cast<USoundWave>(Sound); Wave && !Wave->bLooping) Wave->bLooping = true; // pack loops are not always flagged
    UAudioComponent* Component = nullptr;
    const bool bPositional = Params.Attach || Params.Location;
    if(!bPositional)
    {
        Component = UGameplayStatics::CreateSound2D(World, Sound, Volume, Pitch, 0.f, nullptr, false, !Cue->bLoop);
    }
    else if(FAudioDeviceHandle Device = World->GetAudioDevice(); Device.IsValid())
    {
        AActor* Owner = Params.Attach ? Params.Attach->GetOwner() : nullptr;
        FAudioDevice::FCreateComponentParams CP = Owner ? FAudioDevice::FCreateComponentParams(World, Owner) : FAudioDevice::FCreateComponentParams(World);
        if(!Params.Attach) CP.SetLocation(*Params.Location);
        CP.bStopWhenOwnerDestroyed = true;
        Component = FAudioDevice::CreateComponent(Sound, CP);
        if(Component)
        {
            if(Params.Attach) Component->AttachToComponent(Params.Attach, FAttachmentTransformRules::KeepRelativeTransform, Params.Socket);
            else Component->SetWorldLocation(*Params.Location);
            Component->SetVolumeMultiplier(Volume); Component->SetPitchMultiplier(Pitch);
            Component->bAllowSpatialization = true;
            Component->bAutoDestroy = !Cue->bLoop || !Params.Attach;
        }
    }
    if(!Component) return nullptr;
    ApplyCueSettings(*Audio, Component, *Cue, bPositional);
    Component->Play();
    ++GStats.Played;
    if(Cue->MaxVoices > 0) GVoices.FindOrAdd(Id).Add(Component);
    if(Cue->bCombat) GCombatVoices.Add({Component, Priority});
    if(Cue->Duck > 0.f) Audio->RequestDuck(Cue->Duck, FMath::Max(.3f, Cue->DuckSeconds));
    if(Depth < 2) for(const FName& Layer : Cue->With) if(Layer != Id) Spawn(Context, Layer, Params, Depth + 1);
    return Component;
}
}

// ---------------------------------------------------------------------------------------------
TArray<FString> CireAudio::ExpandSoundNames(const FString& Pattern)
{
    TArray<FString> Out;
    int32 Open = INDEX_NONE, Close = INDEX_NONE;
    if(Pattern.FindChar(TEXT('{'), Open) && Pattern.FindChar(TEXT('}'), Close) && Close > Open)
    {
        FString Range = Pattern.Mid(Open + 1, Close - Open - 1), A, B;
        if(Range.Split(TEXT(".."), &A, &B) && A.IsNumeric() && B.IsNumeric())
        {
            const int32 From = FCString::Atoi(*A), To = FCString::Atoi(*B), Width = A.Len();
            for(int32 I = From; I <= To && I - From < 64; ++I)
            {
                FString Number = FString::FromInt(I);
                while(Number.Len() < Width) Number = TEXT("0") + Number;
                Out.Add(Pattern.Left(Open) + Number + Pattern.Mid(Close + 1));
            }
            return Out;
        }
    }
    Out.Add(Pattern);
    return Out;
}

float CireAudio::BusGain(const FCireUISettings& S, ECireAudioBus Bus)
{
    if(S.bMuteAudio) return 0.f;
    const float Master = FMath::Clamp(S.MasterVolume, 0.f, 1.f);
    switch(Bus)
    {
    case ECireAudioBus::Music: return S.bMusicEnabled ? Master * FMath::Clamp(S.MusicVolume, 0.f, 1.f) : 0.f;
    case ECireAudioBus::SFX: return Master * FMath::Clamp(S.SFXVolume, 0.f, 1.f);
    case ECireAudioBus::Ambience: return Master * FMath::Clamp(S.AmbienceVolume, 0.f, 1.f);
    case ECireAudioBus::UI: return Master * FMath::Clamp(S.UIVolume, 0.f, 1.f);
    case ECireAudioBus::Voice: return Master * FMath::Clamp(S.SFXVolume, 0.f, 1.f);
    }
    return Master;
}

const FCireUISettings& CireAudio::LocalSettings(const UObject* WorldContext)
{
    static const FCireUISettings Defaults;
#if !UE_BUILD_SHIPPING
    if(GSettingsOverride) return *GSettingsOverride;
#endif
    if(UWorld* World = WorldOf(WorldContext))
        if(APlayerController* PC = World->GetFirstPlayerController())
            if(const ACireHUD* HUD = Cast<ACireHUD>(PC->GetHUD())) return HUD->UISettings;
    return Defaults;
}

bool CireAudio::HasCue(FName CueId) { return Cues().Cues.Contains(CueId); }

FTransform CireAudio::ListenerTransform(const UObject* WorldContext)
{
    UWorld* World = WorldOf(WorldContext);
    if(!World) return FTransform::Identity;
    FTransform T;
    if(FAudioDeviceHandle Device = World->GetAudioDevice(); Device.IsValid() && Device->GetListenerTransform(0, T)) return T;
    APlayerController* PC = World->GetFirstPlayerController();
    if(PC && PC->PlayerCameraManager) return FTransform(PC->PlayerCameraManager->GetCameraRotation(), PC->PlayerCameraManager->GetCameraLocation());
    if(const APawn* Pawn = PC ? PC->GetPawn() : nullptr) return Pawn->GetActorTransform();
    return FTransform::Identity;
}

bool CireAudio::PlayCue(const UObject* WorldContext, FName CueId, FVector Location, float VolumeScale)
{
    const FCue* Cue = Cues().Cues.Find(CueId);
    if(!Cue) return false;
    FCirePlayParams P; P.Location = &Location; P.Volume = VolumeScale;
    UAudioComponent* C = Spawn(WorldContext, CueId, P);
    if(C && Cue->bLoop) { UE_LOG(LogCireAudio, Warning, TEXT("Loop cue %s played as one-shot; use PlayAttached"), *CueId.ToString()); C->Stop(); return false; }
    return C != nullptr;
}

bool CireAudio::PlayCue2D(const UObject* WorldContext, FName CueId, float VolumeScale)
{
    const FCue* Cue = Cues().Cues.Find(CueId);
    if(!Cue) return false;
    FCirePlayParams P; P.Volume = VolumeScale;
    UAudioComponent* C = Spawn(WorldContext, CueId, P);
    if(C && Cue->bLoop) { C->Stop(); return false; }
    return C != nullptr;
}

UAudioComponent* CireAudio::SpawnCueAtLocation(const UObject* WorldContext, FName CueId, FVector Location, float VolumeScale)
{
    FCirePlayParams P; P.Location = &Location; P.Volume = VolumeScale;
    return Spawn(WorldContext, CueId, P);
}

UAudioComponent* CireAudio::PlayAttached(FName CueId, USceneComponent* AttachTo, FName Socket, float VolumeScale)
{
    if(!AttachTo) return nullptr;
    FCirePlayParams P; P.Attach = AttachTo; P.Socket = Socket; P.Volume = VolumeScale;
    return Spawn(AttachTo, CueId, P);
}

UAudioComponent* CireAudio::PlayCueEx(const UObject* WorldContext, FName CueId, const FCirePlayParams& Params)
{
    return Spawn(WorldContext, CueId, Params);
}

bool CireAudio::PlayLegacyPath(const UObject* WorldContext, const TCHAR* Path, float Volume)
{
    FString Key(Path);
    int32 Dot = INDEX_NONE;
    if(Key.FindLastChar(TEXT('.'), Dot)) Key.LeftInline(Dot);
    const FName* Cue = Cues().UiLegacy.Find(Key);
    if(!Cue || !HasCue(*Cue)) return false;
    PlayCue2D(WorldContext, *Cue, FMath::Clamp(Volume, 0.f, 2.f));
    return true;
}

void CireAudio::Duck(const UObject* WorldContext, float Depth, float Seconds)
{
    if(UCireAudioSubsystem* Audio = UCireAudioSubsystem::Get(WorldContext)) Audio->RequestDuck(Depth, Seconds);
}

float CireAudio::DuckGain(const UObject* WorldContext)
{
    const UCireAudioSubsystem* Audio = UCireAudioSubsystem::Get(WorldContext);
    return Audio ? Audio->DuckGainNow : 1.f;
}

void CireAudio::SetPacksEnabled(bool bEnabled) { if(GPacksEnabled != bEnabled) { GPacksEnabled = bEnabled; ++GPackEpoch; } }
bool CireAudio::PacksEnabled() { return GPacksEnabled; }
bool CireAudio::CueUsesPack(FName CueId) { FCue* Cue = MutableCues().Cues.Find(CueId); if(!Cue) return false; Members(*Cue); return Cue->bPackActive; }
TArray<FString> CireAudio::CueMembers(FName CueId) { FCue* Cue = MutableCues().Cues.Find(CueId); return Cue ? Members(*Cue) : TArray<FString>(); }
bool CireAudio::CueIsLoop(FName CueId) { const FCue* Cue = Cues().Cues.Find(CueId); return Cue && Cue->bLoop; }
TArray<FName> CireAudio::CueIds() { TArray<FName> Out; Cues().Cues.GetKeys(Out); return Out; }
CireAudio::FStats& CireAudio::Stats() { return GStats; }

bool CireAudio::ConfigureComponent(UAudioComponent* Component, FName CueId, float VolumeScale)
{
    FCue* Cue = MutableCues().Cues.Find(CueId);
    UCireAudioSubsystem* Audio = Component ? UCireAudioSubsystem::Get(Component) : nullptr;
    if(!Cue || !Audio || LocalSettings(Component).bMuteAudio) return false;
    float Volume = 1.f, Pitch = 1.f;
    USoundBase* Sound = Prepare(*Audio, CueId, *Cue, VolumeScale, Volume, Pitch);
    if(!Sound) return false;
    if(Cue->bLoop) if(USoundWave* Wave = Cast<USoundWave>(Sound); Wave && !Wave->bLooping) Wave->bLooping = true;
    if(!Sound->GetSoundClass() && !Cue->bHasBus && !Cue->bPackActive) Volume *= BusGain(LocalSettings(Component), ECireAudioBus::SFX);
    Component->SetSound(Sound);
    Component->SetVolumeMultiplier(Volume); Component->SetPitchMultiplier(Pitch);
    ApplyCueSettings(*Audio, Component, *Cue, true);
    return true;
}

FString CireAudio::SoundObjectPath(const FString& Name, bool bCheckExists)
{
    FString Package, Object;
    if(Name.StartsWith(TEXT("/")))
    {
        if(!Name.Split(TEXT("."), &Package, &Object)) { Package = Name; Object = FPackageName::GetShortName(Name); }
    }
    else
    {
        FString Folder, Short;
        if(!Name.Split(TEXT("/"), &Folder, &Short, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) { Short = Name; Folder.Reset(); }
        Package = FString::Printf(TEXT("/Game/Audio/%s%s%s"), *Folder, Folder.IsEmpty() ? TEXT("") : TEXT("/"), *Short);
        Object = Short;
    }
    if(bCheckExists && !FPackageName::DoesPackageExist(Package)) return FString();
    return Package + TEXT(".") + Object;
}

void CireAudio::ReloadData()
{
    GCueLoaded = false; Cues();
    CireMusic::Data(true); CireAmbience::Data(true); CireFootsteps::Data(true);
}

const TArray<FString>& CireAudio::Credits() { return CireMusic::Data().Credits; }

bool CireAudio::PlayHudSound(const UObject* WorldContext, int32 LegacyIndex, float Volume)
{
    const FName* Cue = Cues().HudLegacy.Find(LegacyIndex);
    if(!Cue || !HasCue(*Cue)) return false;
    PlayCue2D(WorldContext, *Cue, FMath::Clamp(Volume, 0.f, 2.f));
    return true; // handled even when rate-limited, so the legacy tone never doubles it
}

bool CireAudio::PlayShopSound(const UObject* WorldContext, const TCHAR* LegacyName, float Volume)
{
    const FName* Cue = Cues().Shop.Find(LegacyName);
    if(!Cue || (!Cue->IsNone() && !HasCue(*Cue))) return false;
    if(!Cue->IsNone()) PlayCue2D(WorldContext, *Cue, FMath::Clamp(Volume, 0.f, 2.f));
    return true;
}

bool CireAudio::PlayBanner(const UObject* WorldContext, uint8 Banner)
{
    static const TCHAR* Names[] = {TEXT("LevelUp"), TEXT("WaveIncoming"), TEXT("WaveCleared"), TEXT("PrepPhase"), TEXT("Arena"),
        TEXT("Recovery"), TEXT("ChallengeUnlocked"), TEXT("BossSpawned"), TEXT("Victory"), TEXT("Defeat"), TEXT("Custom")};
    if(Banner >= UE_ARRAY_COUNT(Names)) return false;
    const FName* Cue = Cues().Banners.Find(Names[Banner]);
    if(!Cue) return false;
    if(!Cue->IsNone()) PlayCue2D(WorldContext, *Cue);
    return true;
}

void CireAudio::NoteHover(const UObject* WorldContext, float X, float Y)
{
    static uint64 LastFrame = 0;
    static FVector2D LastKey(-1e6, -1e6);
    const FVector2D Key(X, Y);
    const bool bNew = GFrameCounter > LastFrame + 2 || !Key.Equals(LastKey, 1.f);
    if(GFrameCounter != LastFrame || bNew) { LastFrame = GFrameCounter; }
    if(bNew) { LastKey = Key; PlayCue2D(WorldContext, TEXT("ui_hover")); }
}

void CireAudio::NoteUIHover(float X, float Y)
{
    // Every hovered button reports each frame; a tick plays only when an element starts being hovered.
    // Per-element memory, so screens that draw several hover-state buttons at once never retrigger.
    static TMap<uint32, uint64> Seen;
    const uint32 Key = HashCombine(GetTypeHash(FMath::RoundToInt(X)), GetTypeHash(FMath::RoundToInt(Y)));
    const uint64* Last = Seen.Find(Key);
    const bool bNew = !Last || GFrameCounter > *Last + 2;
    Seen.Add(Key, GFrameCounter);
    if(Seen.Num() > 64) for(auto It = Seen.CreateIterator(); It; ++It) if(GFrameCounter > It->Value + 30) It.RemoveCurrent();
    if(bNew) if(UWorld* World = GUIWorld.Get()) PlayCue2D(World, TEXT("ui_hover"));
}

FString CireAudio::MusicStateName(const UObject* WorldContext)
{
    const UCireAudioSubsystem* Audio = UCireAudioSubsystem::Get(WorldContext);
    return Audio ? FCireMusicDirector::Name(Audio->Director.State) : TEXT("none");
}

#if !UE_BUILD_SHIPPING
void CireAudio::SetSettingsOverride(const FCireUISettings* Settings) { GSettingsOverride = Settings; }
#endif

// ---------------------------------------------------------------------------------------------
bool UCireAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UCireAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection) { Super::Initialize(Collection); }

void UCireAudioSubsystem::Deinitialize()
{
    if(bStarted)
        UE_LOG(LogCireAudio, Display, TEXT("CIRE_AUDIO_STATS frames=%d music=%s footsteps=%d bone=%d phase=%d cadence=%d dropped=%d ambience_oneshots=%d district=%s"),
            FramesTicked, FCireMusicDirector::Name(Director.State), Footsteps.StepsPlayed, Footsteps.BoneSteps, Footsteps.PhaseSteps,
            Footsteps.CadenceSteps, Footsteps.StepsDropped, Ambience.OneShotsPlayed, *Ambience.CurrentDistrict().ToString());
    if(bStarted)
        UE_LOG(LogCireAudio, Display, TEXT("CIRE_AUDIO_EVENT_STATS played=%d pack=%d fallback=%d dropped_cooldown=%d dropped_voices=%d dropped_budget=%d stolen=%d %s"),
            GStats.Played, GStats.PackPlays, GStats.FallbackPlays, GStats.DroppedCooldown, GStats.DroppedVoices, GStats.DroppedBudget, GStats.Stolen, *Events.StatsLine());
    Music.Reset(); Ambience.Reset(); Footsteps.Reset(); Events.Reset();
    GVoices.Reset(); GCombatVoices.Reset();
    if(TeleportHum) TeleportHum->Stop();
    for(UAudioComponent* C : Owned) if(IsValid(C)) C->Stop();
    Owned.Reset(); SoundCache.Reset(); TeleportHum = nullptr; bStarted = false;
    FinishProbe();
    Super::Deinitialize();
}

UCireAudioSubsystem* UCireAudioSubsystem::Get(const UObject* WorldContext)
{
    UWorld* World = WorldOf(WorldContext);
    return World ? World->GetSubsystem<UCireAudioSubsystem>() : nullptr;
}

TStatId UCireAudioSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UCireAudioSubsystem, STATGROUP_Tickables); }

USoundBase* UCireAudioSubsystem::ResolveSound(const FString& ShortPath)
{
    if(TObjectPtr<USoundBase>* Found = SoundCache.Find(ShortPath)) return *Found;
    const FString Path = CireAudio::SoundObjectPath(ShortPath, ShortPath.StartsWith(TEXT("/"))); // pack paths are checked first (no load warnings)
    USoundBase* Sound = Path.IsEmpty() ? nullptr : LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if(!Sound) UE_LOG(LogCireAudio, Warning, TEXT("Missing sound %s"), *ShortPath);
    SoundCache.Add(ShortPath, Sound);
    return Sound;
}

USoundClass* UCireAudioSubsystem::BusClass(ECireAudioBus Bus) const
{
    const int32 I = static_cast<int32>(Bus);
    return BusClasses.IsValidIndex(I) ? BusClasses[I].Get() : nullptr;
}

USoundConcurrency* UCireAudioSubsystem::Concurrency(FName Name)
{
    if(TObjectPtr<USoundConcurrency>* Found = ConcurrencyCache.Find(Name)) return *Found;
    const FString Short = TEXT("SC_") + Name.ToString();
    USoundConcurrency* C = LoadObject<USoundConcurrency>(nullptr, *FString::Printf(TEXT("/Game/Audio/Mix/%s.%s"), *Short, *Short), nullptr, LOAD_NoWarn | LOAD_Quiet);
    ConcurrencyCache.Add(Name, C);
    return C;
}

void UCireAudioSubsystem::RequestDuck(float Depth, float Seconds)
{
    Depth = FMath::Clamp(Depth, 0.f, 1.f);
    if(Depth >= DuckDepth - .01f) { DuckDepth = Depth; DuckHold = FMath::Max(DuckHold, Seconds); }
    else DuckHold = FMath::Max(DuckHold, FMath::Min(Seconds, .5f));
}

void UCireAudioSubsystem::Keep(UAudioComponent* Component)
{
    if(!Component) return;
    Owned.RemoveAll([](const TObjectPtr<UAudioComponent>& C) { return !IsValid(C); });
    Owned.AddUnique(Component);
}

void UCireAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if(InWorld.GetNetMode() == NM_DedicatedServer || FParse::Param(FCommandLine::Get(), TEXT("CireNoAudio"))) return;
    bStarted = true;
    GUIWorld = &InWorld;
    Director.CombatHoldSeconds = CireMusic::Data().CombatHold;
    Director.BossHoldSeconds = CireMusic::Data().BossHold;
    static const TCHAR* ClassNames[] = {TEXT("SCL_Music"), TEXT("SCL_SFX"), TEXT("SCL_Ambience"), TEXT("SCL_UI"), TEXT("SCL_Voice")};
    BusClasses.Reset();
    for(const TCHAR* Name : ClassNames)
        BusClasses.Add(LoadObject<USoundClass>(nullptr, *FString::Printf(TEXT("/Game/Audio/Mix/%s.%s"), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet));
    VolumeMix = NewObject<USoundMix>(this, TEXT("CireUserVolumes"));
    UGameplayStatics::PushSoundMixModifier(&InWorld, VolumeMix);
    if(UReverbEffect* Reverb = LoadObject<UReverbEffect>(nullptr, TEXT("/Game/Audio/Mix/REV_StoneStreets.REV_StoneStreets"), nullptr, LOAD_NoWarn | LOAD_Quiet))
    {
        UGameplayStatics::ActivateReverbEffect(&InWorld, Reverb, TEXT("CireStoneStreets"), 0.f, .55f, 2.f);
        bReverbActive = true;
    }
    for(float& G : AppliedGain) G = -1.f;
    if(FParse::Param(FCommandLine::Get(), TEXT("CireAudioProbe"))) TickProbe(0.f);
    UE_LOG(LogCireAudio, Display, TEXT("CIRE_AUDIO_READY classes=%d cues=%d reverb=%d device=%d"),
        BusClasses.FilterByPredicate([](const TObjectPtr<USoundClass>& C) { return C != nullptr; }).Num(),
        Cues().Cues.Num(), bReverbActive ? 1 : 0, InWorld.GetAudioDevice().IsValid() ? 1 : 0);
}

void UCireAudioSubsystem::ApplyBusVolumes(const FCireUISettings& Settings)
{
    UWorld* World = GetWorld();
    for(int32 Bus = 0; Bus < 5 && Bus < BusClasses.Num(); ++Bus)
    {
        float Gain = CireAudio::BusGain(Settings, static_cast<ECireAudioBus>(Bus));
        if(Bus == static_cast<int32>(ECireAudioBus::Music)) Gain *= DuckGainNow; // big moments duck the score
        if(FMath::Abs(Gain - AppliedGain[Bus]) < .002f || !BusClasses[Bus]) continue;
        AppliedGain[Bus] = Gain;
        UGameplayStatics::SetSoundMixClassOverride(World, VolumeMix, BusClasses[Bus], Gain, 1.f, Bus == static_cast<int32>(ECireAudioBus::Music) ? .25f : .12f, true);
    }
}

void UCireAudioSubsystem::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    if(!bStarted || !World) return;
    const float Dt = FMath::Clamp(DeltaTime, 0.f, .25f);
    ++FramesTicked;
    // Music duck: hold, then release over ~0.8 s.
    DuckHold = FMath::Max(0.f, DuckHold - Dt);
    if(DuckHold <= 0.f) DuckDepth = FMath::Max(0.f, DuckDepth - Dt * 1.25f);
    DuckGainNow = 1.f - DuckDepth;
    const FCireUISettings& Settings = CireAudio::LocalSettings(World);
    ApplyBusVolumes(Settings);
    if(Probe) { TickProbe(Dt); return; }
    APlayerController* PC = World->GetFirstPlayerController();
    const ACireHero* Hero = PC ? Cast<ACireHero>(PC->GetPawn()) : nullptr;
    const ACireGameState* State = World->GetGameState<ACireGameState>();
    const FVector Listener = CireAudio::ListenerTransform(World).GetLocation();
    const FVector Body = Hero ? Hero->GetActorLocation() : Listener;
    const int32 Team = Hero ? Hero->TeamId : 0;

    // ---- music ----
    const CireMusic::FData& MusicData = CireMusic::Data();
    FCireMusicInputs In;
    In.Phase = State ? State->Phase : 0;
    for(TActorIterator<ACireMonster> It(World); It; ++It)
    {
        const ACireMonster* M = *It;
        if(!IsValid(M) || M->Health <= 0.f || (Team >= 0 && M->Lane >= 0 && M->Lane != Team)) continue;
        const float Distance = FVector::Dist2D(M->GetActorLocation(), Body);
        const bool bEngaged = M->Victim != nullptr;
        if(M->IsLaneBoss() || (M->GetNPCClassification() == ECireNPCClass::Boss && bEngaged && Distance < MusicData.BossRadius)) In.bBoss = true;
        if(M->PackId < 0 || (bEngaged && Distance < MusicData.CombatRadius)) In.bCombat = true;
    }
    if(State && In.Phase == 3)
    {
        const int32 Mine = Team == 1 ? State->DuskLives : State->EmberLives, Theirs = Team == 1 ? State->EmberLives : State->DuskLives;
        const int32 MyWins = Team == 1 ? State->DuskWins : State->EmberWins, TheirWins = Team == 1 ? State->EmberWins : State->DuskWins;
        In.bLocalTeamWon = Mine > 0 && (Theirs <= 0 || MyWins >= TheirWins);
    }
    ECireMusicStinger Stinger = ECireMusicStinger::None;
    Director.Update(In, Dt, Stinger);
    Music.Tick(*this, Settings, Director.State, Stinger, Dt);

    // ---- ambience, footsteps, events ----
    Ambience.Tick(*this, Listener, Team, In.Phase == 2, FCireMusicDirector::IsCombatState(Director.State), Dt);
    Footsteps.Tick(*this, Listener, Hero, Settings.bFootstepCameraShake, Dt);
    DetectEvents(Dt);
    Events.Tick(*this, Dt); // audio-overhaul: combat outcomes, hit reactions, deaths, casts, UI state
    UpdateShake(Dt);
#if !UE_BUILD_SHIPPING
    // -CireAudioSoak=<seconds>: play a normal match, log CIRE_AUDIO_STATS and quit (Docs/Audio.md).
    static float SoakSeconds = -1.f;
    if(SoakSeconds < 0.f && !FParse::Value(FCommandLine::Get(), TEXT("CireAudioSoak="), SoakSeconds)) SoakSeconds = 0.f;
    SoakClock += Dt;
    if(SoakSeconds > 0.f && SoakClock >= SoakSeconds)
    {
        SoakSeconds = 0.f;
        UE_LOG(LogCireAudio, Display, TEXT("CIRE_AUDIO_SOAK_DONE seconds=%.0f tracked=%d music=%s track=%s beds=%d emitters=%d"), SoakClock,
            Footsteps.TrackedCharacters(), FCireMusicDirector::Name(Director.State), *Music.CurrentTitle(), Ambience.ActiveBeds(), Ambience.ActiveEmitters());
        FPlatformMisc::RequestExit(false);
    }
#endif
}

void UCireAudioSubsystem::DetectEvents(float Dt)
{
    UWorld* World = GetWorld();
    APlayerController* PC = World->GetFirstPlayerController();
    ACireHero* Hero = PC ? Cast<ACireHero>(PC->GetPawn()) : nullptr;
    const ACireGameState* State = World->GetGameState<ACireGameState>();
    const FVector Body = Hero ? Hero->GetActorLocation() : FVector::ZeroVector;

    // Pack Leader / lane boss: roar when it first engages (or first appears, for lane bosses); growl on each cast.
    for(TActorIterator<ACireMonster> It(World); It; ++It)
    {
        ACireMonster* M = *It;
        if(!IsValid(M) || M->Health <= 0.f || M->GetNPCClassification() != ECireNPCClass::Boss) continue;
        if(Hero && FVector::Dist(M->GetActorLocation(), Body) > 6000.f) continue;
        const TWeakObjectPtr<AActor> Key(M);
        if(!RoaredBosses.Contains(Key) && (M->Victim != nullptr || M->IsLaneBoss()))
        {
            RoaredBosses.Add(Key);
            CireAudio::PlayCue(this, TEXT("pack_leader_roar"), M->GetActorLocation() + FVector(0, 0, 120));
        }
        FString& Last = BossCasting.FindOrAdd(Key);
        if(!M->CastingAbility.IsEmpty() && Last != M->CastingAbility)
            CireAudio::PlayCue(this, TEXT("pack_leader_growl"), M->GetActorLocation() + FVector(0, 0, 120));
        Last = M->CastingAbility;
    }
    for(auto It = RoaredBosses.CreateIterator(); It; ++It) if(!It->IsValid()) It.RemoveCurrent();
    for(auto It = BossCasting.CreateIterator(); It; ++It) if(!It->Key.IsValid()) It.RemoveCurrent();

    if(!Hero) { bHasLastHeroLocation = false; return; }
    // Purchases/sales/loot play from the shop UI through PlayShopSound (AudioCues.json shopLegacy).
    // Teleports (arena transfer, recall, recovery): a discontinuous jump of the local body.
    const FVector Location = Hero->GetActorLocation();
    if(bHasLastHeroLocation && FVector::Dist(Location, LastHeroLocation) > 1500.f) CireAudio::PlayCue2D(this, TEXT("teleport_arrive"));
    LastHeroLocation = Location; bHasLastHeroLocation = true;
    // Teleport channel: the shop's Teleport to Base channel (replicated on UCireInventory), and the last
    // seconds of prep before the arena transfer.
    const UCireInventory* Inventory = Hero->FindComponentByClass<UCireInventory>();
    const bool bChannel = !Hero->bDead && ((Inventory && Inventory->IsChanneling())
        || (State && State->Phase == 1 && State->SecondsLeft > 0.f && State->SecondsLeft <= 3.5f));
    if(bChannel && !TeleportHum)
    {
        TeleportHum = CireAudio::PlayAttached(TEXT("teleport_channel"), Hero->GetRootComponent());
        if(TeleportHum) TeleportHum->FadeIn(.6f, 1.f);
    }
    else if(!bChannel && TeleportHum)
    {
        if(IsValid(TeleportHum)) TeleportHum->FadeOut(.4f, 0.f);
        TeleportHum = nullptr;
    }
}

void UCireAudioSubsystem::AddLocalShake(float Amplitude)
{
    ShakeAmplitude = FMath::Max(ShakeAmplitude * FMath::Exp(-ShakeTime * 10.f), Amplitude);
    ShakeTime = 0.f;
}

void UCireAudioSubsystem::UpdateShake(float Dt)
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    ACireHero* Hero = PC ? Cast<ACireHero>(PC->GetPawn()) : nullptr;
    if(!Hero || !Hero->Camera) return;
    if(ShakeAmplitude <= 0.f) return;
    ShakeTime += Dt;
    const float Envelope = ShakeAmplitude * FMath::Exp(-ShakeTime * 10.f);
    Hero->Camera->ClearAdditiveOffset();
    if(Envelope < .02f) { ShakeAmplitude = 0.f; return; }
    const FVector Offset(0.f, FMath::Sin(ShakeTime * 47.f) * Envelope * .35f, -FMath::Abs(FMath::Sin(ShakeTime * 38.f)) * Envelope);
    Hero->Camera->AddAdditiveOffset(FTransform(Offset), 0.f);
}
