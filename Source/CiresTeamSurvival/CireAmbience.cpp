#include "CireAmbience.h"
#include "CireAudio.h"
#include "CireEnvironmentProps.h"
#include "CireGame.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"

namespace
{
CireAmbience::FData GAmbience;
bool GAmbienceLoaded = false;

FVector2D ReadPair(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, FVector2D Default)
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if(!O->TryGetArrayField(Field, A) || A->Num() != 2) return Default;
    FVector2D V((*A)[0]->AsNumber(), (*A)[1]->AsNumber());
    if(V.Y < V.X) Swap(V.X, V.Y);
    return V;
}

CireAmbience::FBed ReadBed(const TSharedPtr<FJsonObject>& O)
{
    CireAmbience::FBed Bed;
    O->TryGetStringField(TEXT("sound"), Bed.Sound);
    double N = 0;
    if(O->TryGetNumberField(TEXT("volume"), N)) Bed.Volume = FMath::Clamp(static_cast<float>(N), 0.f, 2.f);
    return Bed;
}
}

const CireAmbience::FData& CireAmbience::Data(bool bReload)
{
    if(GAmbienceLoaded && !bReload) return GAmbience;
    GAmbienceLoaded = true; GAmbience = FData();
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/AudioAmbience.json"))) ||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return GAmbience;
    double N = 0;
    if(Root->TryGetNumberField(TEXT("fadeSeconds"), N)) GAmbience.FadeSeconds = FMath::Clamp(static_cast<float>(N), .1f, 20.f);
    if(Root->TryGetNumberField(TEXT("duckLevel"), N)) GAmbience.DuckLevel = FMath::Clamp(static_cast<float>(N), 0.f, 1.f);
    if(Root->TryGetNumberField(TEXT("duckSeconds"), N)) GAmbience.DuckSeconds = FMath::Clamp(static_cast<float>(N), .05f, 10.f);
    if(Root->TryGetNumberField(TEXT("emitterRadius"), N)) GAmbience.EmitterRadius = static_cast<float>(N);
    if(Root->TryGetNumberField(TEXT("maxEmitters"), N)) GAmbience.MaxEmitters = FMath::Clamp(static_cast<int32>(N), 0, 16);
    const TSharedPtr<FJsonObject>* Night = nullptr;
    if(Root->TryGetObjectField(TEXT("night"), Night)) GAmbience.Night = ReadBed(*Night);
    const TSharedPtr<FJsonObject>* Districts = nullptr;
    if(Root->TryGetObjectField(TEXT("districts"), Districts))
        for(const auto& Pair : (*Districts)->Values)
        {
            const TSharedPtr<FJsonObject> O = Pair.Value->AsObject(); if(!O) continue;
            FDistrict D;
            const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
            if(O->TryGetArrayField(TEXT("beds"), List)) for(const auto& V : *List) if(V->AsObject()) D.Beds.Add(ReadBed(V->AsObject()));
            if(O->TryGetArrayField(TEXT("oneShots"), List))
                for(const auto& V : *List)
                {
                    const TSharedPtr<FJsonObject> S = V->AsObject(); if(!S) continue;
                    FOneShot Shot; FString Cue; S->TryGetStringField(TEXT("cue"), Cue); Shot.Cue = FName(*Cue);
                    Shot.Interval = ReadPair(S, TEXT("interval"), Shot.Interval);
                    Shot.Interval.X = FMath::Max(1.0, Shot.Interval.X);
                    Shot.Distance = ReadPair(S, TEXT("distance"), Shot.Distance);
                    Shot.Height = ReadPair(S, TEXT("height"), Shot.Height);
                    D.OneShots.Add(Shot);
                }
            GAmbience.Districts.Add(FName(*Pair.Key), D);
        }
    const TSharedPtr<FJsonObject>* Emitters = nullptr;
    if(Root->TryGetObjectField(TEXT("emitters"), Emitters))
        for(const auto& Pair : (*Emitters)->Values)
        {
            const TSharedPtr<FJsonObject> O = Pair.Value->AsObject(); if(!O) continue;
            FEmitter E; FString Cue; O->TryGetStringField(TEXT("cue"), Cue); E.Cue = FName(*Cue);
            const TArray<TSharedPtr<FJsonValue>>* Offset = nullptr;
            if(O->TryGetArrayField(TEXT("offset"), Offset) && Offset->Num() == 3) E.Offset = FVector((*Offset)[0]->AsNumber(), (*Offset)[1]->AsNumber(), (*Offset)[2]->AsNumber());
            if(O->TryGetNumberField(TEXT("volume"), N)) E.Volume = FMath::Clamp(static_cast<float>(N), 0.f, 2.f);
            E.Interval = ReadPair(O, TEXT("interval"), FVector2D::ZeroVector);
            GAmbience.Emitters.Add(FName(*Pair.Key), E);
        }
    GAmbience.bValid = GAmbience.Districts.Contains(TEXT("outskirts"));
    return GAmbience;
}

FName CireAmbience::ResolveDistrict(const UWorld* World, int32 Team, const FVector& Location, bool bArena)
{
    const FData& D = Data();
    if(bArena && D.Districts.Contains(TEXT("arena"))) return TEXT("arena");
    const FName Id = World ? CireEnvironmentProps::DistrictAt(World, FMath::Clamp(Team, 0, 1), Location) : NAME_None;
    return !Id.IsNone() && D.Districts.Contains(Id) ? Id : FName(TEXT("outskirts"));
}

// ---------------------------------------------------------------------------------------------
void FCireAmbiencePlayer::Reset()
{
    for(auto& Pair : Beds) if(Pair.Value.Component.IsValid()) Pair.Value.Component->Stop();
    for(auto& Slot : EmitterSlots) if(Slot.Component.IsValid()) Slot.Component->Stop();
    Beds.Reset(); EmitterSlots.Reset(); Props.Reset(); OneShotTimers.Reset(); District = NAME_None;
}

int32 FCireAmbiencePlayer::ActiveBeds() const
{
    int32 N = 0;
    for(const auto& Pair : Beds) if(Pair.Value.Component.IsValid() && Pair.Value.Component->IsPlaying() && Pair.Value.Current > .01f) ++N;
    return N;
}

int32 FCireAmbiencePlayer::ActiveEmitters() const
{
    int32 N = 0;
    for(const auto& Slot : EmitterSlots) if(Slot.Component.IsValid() && Slot.Component->IsPlaying()) ++N;
    return N;
}

void FCireAmbiencePlayer::Tick(UCireAudioSubsystem& Audio, const FVector& Listener, int32 Team, bool bArena, bool bDuck, float DeltaSeconds)
{
    const CireAmbience::FData& Data = CireAmbience::Data();
    UWorld* World = Audio.GetWorld();
    if(!Data.bValid || !World) return;
    Clock += DeltaSeconds;
    const FName Now = ForcedDistrict.IsNone() ? CireAmbience::ResolveDistrict(World, Team, Listener, bArena) : ForcedDistrict;
    if(Now != District) { District = Now; OneShotTimers.Reset(); }
    const float DuckTarget = bDuck ? Data.DuckLevel : 1.f;
    DuckGain = FMath::FInterpConstantTo(DuckGain, DuckTarget, DeltaSeconds, (1.f - Data.DuckLevel) / Data.DuckSeconds);

    // ---- beds: weight toward this district's layers (plus the night layer), keyed by sound ----
    TMap<FString, float> Targets;
    if(!Data.Night.Sound.IsEmpty()) Targets.Add(Data.Night.Sound, Data.Night.Volume);
    const CireAmbience::FDistrict* Def = Data.Districts.Find(District);
    if(Def) for(const auto& Bed : Def->Beds) { float& T = Targets.FindOrAdd(Bed.Sound); T = FMath::Max(T, Bed.Volume); }
    for(const auto& Pair : Targets) Beds.FindOrAdd(Pair.Key);
    const float Rate = 1.f / Data.FadeSeconds;
    for(auto It = Beds.CreateIterator(); It; ++It)
    {
        const float* T = Targets.Find(It.Key());
        const float Target = T ? *T : 0.f;
        FBedState& Bed = It.Value();
        Bed.Current = FMath::FInterpConstantTo(Bed.Current, Target, DeltaSeconds, Rate);
        if(Bed.Current <= .001f && Target <= 0.f)
        {
            if(Bed.Component.IsValid()) Bed.Component->Stop();
            It.RemoveCurrent();
            continue;
        }
        if(!Bed.Component.IsValid())
        {
            USoundBase* Sound = Audio.ResolveSound(It.Key());
            if(!Sound) continue;
            UAudioComponent* C = UGameplayStatics::CreateSound2D(World, Sound, .001f, 1.f, FMath::FRandRange(0.f, 20.f), nullptr, false, false);
            if(!C) continue;
            Bed.Component.Reset(C);
            C->Play(FMath::FRandRange(0.f, 20.f));
        }
        Bed.Component->SetVolumeMultiplier(FMath::Max(.001f, Bed.Current * DuckGain));
    }

    // ---- one-shots ----
    if(Def)
        for(int32 I = 0; I < Def->OneShots.Num(); ++I)
        {
            const CireAmbience::FOneShot& Shot = Def->OneShots[I];
            float* Due = OneShotTimers.Find(I);
            if(!Due) { OneShotTimers.Add(I, Clock + FMath::FRandRange(Shot.Interval.X * .3f, Shot.Interval.Y)); continue; }
            if(Clock < *Due) continue;
            *Due = Clock + FMath::FRandRange(Shot.Interval.X, Shot.Interval.Y);
            const float Yaw = FMath::FRandRange(0.f, 2.f * PI), Distance = FMath::FRandRange(Shot.Distance.X, Shot.Distance.Y);
            const FVector At = Listener + FVector(FMath::Cos(Yaw) * Distance, FMath::Sin(Yaw) * Distance, FMath::FRandRange(Shot.Height.X, Shot.Height.Y));
            CireAudio::PlayCue(World, Shot.Cue, At, DuckGain);
            ++OneShotsPlayed;
        }

    // ---- positional prop emitters ----
    PropScanTimer -= DeltaSeconds;
    if(PropScanTimer <= 0.f) { PropScanTimer = 5.f; ScanProps(World); }
    EmitterTimer -= DeltaSeconds;
    if(EmitterTimer <= 0.f) { EmitterTimer = .5f; UpdateEmitters(Audio, Listener); }
    for(auto& Slot : EmitterSlots)
    {
        if(Slot.Component.IsValid()) Slot.Component->SetVolumeMultiplier(FMath::Max(.001f, Slot.Volume * DuckGain));
        else if(Slot.Interval.Y > 0.f && Clock >= Slot.NextPlay)
        {
            Slot.NextPlay = Clock + FMath::FRandRange(Slot.Interval.X, Slot.Interval.Y);
            CireAudio::PlayCue(World, Slot.Cue, Slot.Location, Slot.Volume * DuckGain);
        }
    }
}

void FCireAmbiencePlayer::ScanProps(UWorld* World)
{
    const CireAmbience::FData& Data = CireAmbience::Data();
    Props.Reset();
    for(TActorIterator<ACireWorld> It(World); It; ++It)
    {
        TArray<UInstancedStaticMeshComponent*> Components;
        It->GetComponents(Components);
        for(const UInstancedStaticMeshComponent* C : Components)
        {
            if(!C || !C->ComponentTags.Contains(TEXT("CireTown")) || !C->GetName().EndsWith(TEXT("_0"))) continue;
            for(const FName& Tag : C->ComponentTags)
            {
                const CireAmbience::FEmitter* E = Data.Emitters.Find(Tag);
                if(!E) continue;
                for(int32 I = 0; I < C->GetInstanceCount(); ++I)
                {
                    FTransform T;
                    if(C->GetInstanceTransform(I, T, true)) Props.Add({Tag, T.GetLocation() + E->Offset});
                }
            }
        }
    }
}

void FCireAmbiencePlayer::UpdateEmitters(UCireAudioSubsystem& Audio, const FVector& Listener)
{
    const CireAmbience::FData& Data = CireAmbience::Data();
    TArray<const FPropPoint*> Wanted;
    const float Radius2 = FMath::Square(Data.EmitterRadius);
    for(const FPropPoint& P : Props) if(FVector::DistSquared(P.Location, Listener) < Radius2) Wanted.Add(&P);
    Wanted.Sort([&](const FPropPoint& A, const FPropPoint& B) { return FVector::DistSquared(A.Location, Listener) < FVector::DistSquared(B.Location, Listener); });
    if(Wanted.Num() > Data.MaxEmitters) Wanted.SetNum(Data.MaxEmitters);
    // keep slots whose prop is still wanted, release the rest
    for(int32 I = EmitterSlots.Num() - 1; I >= 0; --I)
    {
        const bool bKeep = Wanted.ContainsByPredicate([&](const FPropPoint* P) { return P->Location.Equals(EmitterSlots[I].Location, 1.f); });
        if(bKeep) continue;
        if(EmitterSlots[I].Component.IsValid()) EmitterSlots[I].Component->FadeOut(.8f, 0.f);
        EmitterSlots.RemoveAt(I);
    }
    for(const FPropPoint* P : Wanted)
    {
        if(EmitterSlots.ContainsByPredicate([&](const FEmitterSlot& S) { return S.Location.Equals(P->Location, 1.f); })) continue;
        const CireAmbience::FEmitter* E = Data.Emitters.Find(P->Slot);
        if(!E) continue;
        FEmitterSlot Slot; Slot.Location = P->Location; Slot.Slot = P->Slot; Slot.Cue = E->Cue; Slot.Volume = E->Volume; Slot.Interval = E->Interval;
        if(E->Interval.Y > 0.f) Slot.NextPlay = Clock + FMath::FRandRange(0.f, E->Interval.Y);
        else if(UAudioComponent* C = CireAudio::SpawnCueAtLocation(Audio.GetWorld(), E->Cue, P->Location, E->Volume * DuckGain))
        {
            C->bAutoDestroy = false;
            Slot.Volume = C->VolumeMultiplier / FMath::Max(DuckGain, .01f); // cue x emitter volume, before ducking
            Slot.Component.Reset(C);
            C->FadeIn(1.f, 1.f, FMath::FRandRange(0.f, 10.f));
        }
        EmitterSlots.Add(MoveTemp(Slot));
    }
}
