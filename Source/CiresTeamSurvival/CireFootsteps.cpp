#include "CireFootsteps.h"
#include "CireAudio.h"
#include "CireCreatureArt.h"
#include "CireEnvironmentProps.h"
#include "CireGame.h"
#include "CireNPCState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireFootsteps, Log, All);

namespace
{
CireFootsteps::FData GSteps;
bool GStepsLoaded = false;

TArray<FString> Expand(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
{
    FString Pattern;
    return O->TryGetStringField(Field, Pattern) ? CireAudio::ExpandSoundNames(Pattern) : TArray<FString>();
}

void ReadBindings(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field, TMap<FString, CireFootsteps::FBinding>& Out)
{
    const TSharedPtr<FJsonObject>* Map = nullptr;
    if(!Root->TryGetObjectField(Field, Map)) return;
    for(const auto& Pair : (*Map)->Values)
    {
        const TSharedPtr<FJsonObject> O = Pair.Value->AsObject(); if(!O) continue;
        CireFootsteps::FBinding B; FString Class; double N = 0;
        O->TryGetStringField(TEXT("class"), Class); B.Class = FName(*Class);
        if(O->TryGetNumberField(TEXT("pitch"), N)) B.Pitch = FMath::Clamp(static_cast<float>(N), .5f, 2.f);
        if(O->TryGetNumberField(TEXT("volume"), N)) B.Volume = FMath::Clamp(static_cast<float>(N), 0.f, 2.f);
        Out.Add(FString(*Pair.Key), B);
    }
}

const FName FootBones[][2] = {{TEXT("foot_l"), TEXT("foot_r")}, {TEXT("Foot_L"), TEXT("Foot_R")}, {TEXT("ball_l"), TEXT("ball_r")},
    {TEXT("LeftFoot"), TEXT("RightFoot")}, {TEXT("l_foot"), TEXT("r_foot")}};
}

const TArray<FString>& CireFootsteps::FLayer::For(ESurface Surface) const
{
    if(!Any.IsEmpty()) return Any;
    const TArray<FString>& Preferred = Surface == ESurface::Dirt ? Dirt : Stone;
    return Preferred.IsEmpty() ? (Surface == ESurface::Dirt ? Stone : Dirt) : Preferred;
}

const CireFootsteps::FData& CireFootsteps::Data(bool bReload)
{
    if(GStepsLoaded && !bReload) return GSteps;
    GStepsLoaded = true; GSteps = FData();
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/AudioFootsteps.json"))) ||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
    {
        UE_LOG(LogCireFootsteps, Warning, TEXT("AudioFootsteps.json missing or invalid"));
        return GSteps;
    }
    double N = 0;
    if(Root->TryGetNumberField(TEXT("maxDistance"), N)) GSteps.MaxDistance = FMath::Clamp(static_cast<float>(N), 300.f, 10000.f);
    if(Root->TryGetNumberField(TEXT("maxStepsPerSecond"), N)) GSteps.MaxStepsPerSecond = FMath::Clamp(static_cast<float>(N), 1.f, 200.f);
    if(Root->TryGetNumberField(TEXT("volumeJitter"), N)) GSteps.VolumeJitter = FMath::Clamp(static_cast<float>(N), 0.f, .9f);
    if(Root->TryGetNumberField(TEXT("speedWalk"), N)) GSteps.SpeedWalk = FMath::Max(1.f, static_cast<float>(N));
    if(Root->TryGetNumberField(TEXT("speedRun"), N)) GSteps.SpeedRun = FMath::Max(GSteps.SpeedWalk + 1.f, static_cast<float>(N));
    const TSharedPtr<FJsonObject>* Classes = nullptr;
    if(Root->TryGetObjectField(TEXT("classes"), Classes))
        for(const auto& Pair : (*Classes)->Values)
        {
            const TSharedPtr<FJsonObject> O = Pair.Value->AsObject(); if(!O) continue;
            FClass C; C.Id = FName(*Pair.Key);
            if(O->TryGetNumberField(TEXT("volume"), N)) C.Volume = FMath::Clamp(static_cast<float>(N), 0.f, 3.f);
            if(O->TryGetNumberField(TEXT("strideWalk"), N)) C.StrideWalk = FMath::Clamp(static_cast<float>(N), 10.f, 1000.f);
            if(O->TryGetNumberField(TEXT("strideRun"), N)) C.StrideRun = FMath::Clamp(static_cast<float>(N), 10.f, 1000.f);
            if(O->TryGetNumberField(TEXT("cameraShake"), N)) C.CameraShake = FMath::Clamp(static_cast<float>(N), 0.f, 10.f);
            const TArray<TSharedPtr<FJsonValue>>* Layers = nullptr;
            if(O->TryGetArrayField(TEXT("layers"), Layers))
                for(const auto& V : *Layers)
                {
                    const TSharedPtr<FJsonObject> L = V->AsObject(); if(!L) continue;
                    FLayer Layer;
                    Layer.Stone = Expand(L, TEXT("stone")); Layer.Dirt = Expand(L, TEXT("dirt")); Layer.Any = Expand(L, TEXT("any"));
                    if(L->TryGetNumberField(TEXT("volume"), N)) Layer.Volume = FMath::Clamp(static_cast<float>(N), 0.f, 3.f);
                    if(L->TryGetNumberField(TEXT("chance"), N)) Layer.Chance = FMath::Clamp(static_cast<float>(N), 0.f, 1.f);
                    if(L->TryGetNumberField(TEXT("delay"), N)) Layer.Delay = FMath::Clamp(static_cast<float>(N), 0.f, 1.f);
                    const TArray<TSharedPtr<FJsonValue>>* Pitch = nullptr;
                    if(L->TryGetArrayField(TEXT("pitch"), Pitch) && Pitch->Num() == 2)
                    {
                        Layer.PitchMin = (*Pitch)[0]->AsNumber(); Layer.PitchMax = (*Pitch)[1]->AsNumber();
                        if(Layer.PitchMax < Layer.PitchMin) Swap(Layer.PitchMin, Layer.PitchMax);
                    }
                    if(!Layer.Any.IsEmpty() || !Layer.Stone.IsEmpty() || !Layer.Dirt.IsEmpty()) C.Layers.Add(Layer);
                }
            GSteps.Classes.Add(C.Id, C);
        }
    ReadBindings(Root, TEXT("profiles"), GSteps.Profiles);
    ReadBindings(Root, TEXT("monsters"), GSteps.Monsters);
    TArray<FString> Archetypes;
    if(Root->TryGetStringArrayField(TEXT("runtimeArchetypes"), Archetypes)) for(const FString& A : Archetypes) GSteps.RuntimeArchetypes.Add(FName(*A));
    const TSharedPtr<FJsonObject>* Fallback = nullptr;
    if(Root->TryGetObjectField(TEXT("fallback"), Fallback))
    {
        FString S;
        if((*Fallback)->TryGetStringField(TEXT("hero"), S)) GSteps.FallbackHero = FName(*S);
        if((*Fallback)->TryGetStringField(TEXT("monster"), S)) GSteps.FallbackMonster = FName(*S);
    }
    GSteps.bValid = GSteps.Classes.Contains(GSteps.FallbackHero) && GSteps.Classes.Contains(GSteps.FallbackMonster);
    return GSteps;
}

CireFootsteps::FBinding CireFootsteps::ForProfile(const FString& ProfileId, int32 RuntimeArchetype, bool* bExplicit)
{
    const FData& D = Data();
    if(const FBinding* B = D.Profiles.Find(ProfileId.ToLower()); B && D.Classes.Contains(B->Class)) { if(bExplicit) *bExplicit = true; return *B; }
    if(bExplicit) *bExplicit = false;
    FBinding B;
    B.Class = D.RuntimeArchetypes.IsValidIndex(RuntimeArchetype) && D.Classes.Contains(D.RuntimeArchetypes[RuntimeArchetype]) ? D.RuntimeArchetypes[RuntimeArchetype] : D.FallbackHero;
    return B;
}

CireFootsteps::FBinding CireFootsteps::ForMonster(const FString& ArchetypeId, bool* bExplicit)
{
    const FData& D = Data();
    if(const FBinding* B = D.Monsters.Find(ArchetypeId.ToLower()); B && D.Classes.Contains(B->Class)) { if(bExplicit) *bExplicit = true; return *B; }
    if(bExplicit) *bExplicit = false;
    FBinding B; B.Class = D.FallbackMonster;
    return B;
}

CireFootsteps::FBinding CireFootsteps::ForCharacter(const ACharacter* Character)
{
    if(const ACireHero* Hero = Cast<ACireHero>(Character)) return ForProfile(Hero->ChampionProfileId, Hero->Archetype);
    if(const ACireMonster* Monster = Cast<ACireMonster>(Character))
        return ForMonster(Monster->NPCState ? Monster->NPCState->ArchetypeId.ToString() : FString());
    FBinding B; B.Class = Data().FallbackHero;
    return B;
}

float CireFootsteps::StepInterval(const FClass& Class, float Speed, float SpeedWalk, float SpeedRun)
{
    if(!(Speed >= 30.f)) return 0.f;
    const float Alpha = FMath::Clamp((Speed - SpeedWalk) / FMath::Max(1.f, SpeedRun - SpeedWalk), 0.f, 1.f);
    const float Stride = FMath::Lerp(Class.StrideWalk, Class.StrideRun, Alpha);
    return FMath::Clamp(Stride / Speed, .16f, 1.6f);
}

bool CireFootsteps::PhaseCrossedContact(float PreviousPhase, float Phase)
{
    float Delta = Phase - PreviousPhase;
    if(Delta > PI) Delta -= 2.f * PI;
    else if(Delta < -PI) Delta += 2.f * PI;
    if(FMath::Abs(Delta) < 1e-4f) return false;
    const float A = PreviousPhase, B = PreviousPhase + Delta;
    return FMath::FloorToInt(FMath::Max(A, B) / PI) != FMath::FloorToInt(FMath::Min(A, B) / PI);
}

// ---------------------------------------------------------------------------------------------
void FCireFootstepPlayer::Reset()
{
    Tracks.Reset(); Pending.Reset(); Nearby.Reset(); LastPick.Reset();
}

FString FCireFootstepPlayer::Pick(const TArray<FString>& Options, const FString& Key)
{
    if(Options.IsEmpty()) return FString();
    int32 Index = FMath::RandRange(0, Options.Num() - 1);
    int32& Last = LastPick.FindOrAdd(Key, -1);
    if(Options.Num() > 1 && Index == Last) Index = (Index + 1 + FMath::RandRange(0, Options.Num() - 2)) % Options.Num();
    Last = Index;
    return Options[Index];
}

CireFootsteps::ESurface FCireFootstepPlayer::SurfaceUnder(UWorld* World, const FVector& Foot, const ACharacter* Ignore) const
{
    using namespace CireFootsteps;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CireFootstepSurface), false, Ignore);
    Params.bReturnPhysicalMaterial = false;
    if(World->LineTraceSingleByObjectType(Hit, Foot + FVector(0, 0, 60), Foot - FVector(0, 0, 160), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
    {
        if(const UPrimitiveComponent* C = Hit.GetComponent())
        {
            if(C->ComponentTags.Contains(TEXT("CireTown")) || C->GetName().Contains(TEXT("Route"))) return ESurface::Stone;
            FString Material = C->GetNumMaterials() > 0 && C->GetMaterial(0) ? C->GetMaterial(0)->GetName().ToLower() : FString();
            for(const TCHAR* Word : {TEXT("cobble"), TEXT("stone"), TEXT("road"), TEXT("plaza"), TEXT("brick"), TEXT("paving"), TEXT("tile")})
                if(Material.Contains(Word)) return ESurface::Stone;
            for(const TCHAR* Word : {TEXT("grass"), TEXT("dirt"), TEXT("mud"), TEXT("soil"), TEXT("field"), TEXT("ground"), TEXT("sand")})
                if(Material.Contains(Word)) return ESurface::Dirt;
        }
    }
    const int32 Team = FMath::Clamp(static_cast<int32>(Foot.Y >= 0.0), 0, 1);
    const FName District = CireEnvironmentProps::DistrictAt(World, Team, Foot);
    return District.IsNone() || District == TEXT("breach") ? ESurface::Dirt : ESurface::Stone;
}

void FCireFootstepPlayer::Step(UCireAudioSubsystem& Audio, ACharacter* Character, const FVector& FootLocation, float Speed, bool bLocal, bool bShake)
{
    using namespace CireFootsteps;
    const FData& D = Data();
    if(Budget < 1.f) { ++StepsDropped; return; }
    Budget -= 1.f;
    const FBinding Binding = Character ? (Tracks.Contains(Character) ? Tracks[Character].Binding : ForCharacter(Character)) : FBinding{D.FallbackHero};
    const FClass* Class = D.Classes.Find(Binding.Class);
    if(!Class) { ++StepsDropped; return; }
    PlayClass(Audio, *Class, Binding, FootLocation, Speed, bLocal, Character);
    if(bLocal && bShake && Class->CameraShake > 0.f) Audio.AddLocalShake(Class->CameraShake * FMath::Lerp(.7f, 1.f, FMath::Clamp(Speed / 500.f, 0.f, 1.f)));
}

void FCireFootstepPlayer::PlayClass(UCireAudioSubsystem& Audio, const CireFootsteps::FClass& Class, const CireFootsteps::FBinding& Binding,
    const FVector& FootLocation, float Speed, bool bLocal, ACharacter* Character)
{
    using namespace CireFootsteps;
    const FData& D = Data();
    UWorld* World = Audio.GetWorld();
    const ESurface Surface = SurfaceUnder(World, FootLocation, Character);
    const float SpeedGain = FMath::Lerp(.75f, 1.1f, FMath::Clamp((Speed - 100.f) / 500.f, 0.f, 1.f));
    const float LocalGain = bLocal ? .85f : 1.f;
    for(int32 I = 0; I < Class.Layers.Num(); ++I)
    {
        const FLayer& Layer = Class.Layers[I];
        if(Layer.Chance < 1.f && FMath::FRand() > Layer.Chance) continue;
        const FString Name = Pick(Layer.For(Surface), FString::Printf(TEXT("%s/%d/%d"), *Class.Id.ToString(), I, static_cast<int32>(Surface)));
        USoundBase* Sound = Name.IsEmpty() ? nullptr : Audio.ResolveSound(Name);
        if(!Sound) continue;
        const float Volume = Layer.Volume * Class.Volume * Binding.Volume * SpeedGain * LocalGain * (1.f - FMath::FRand() * D.VolumeJitter);
        const float Pitch = FMath::FRandRange(Layer.PitchMin, Layer.PitchMax) * Binding.Pitch;
        if(Layer.Delay > 0.f) { Pending.Add({Character, FootLocation, Clock + Layer.Delay * FMath::FRandRange(.8f, 1.2f), Volume, Pitch, Sound}); continue; }
        UGameplayStatics::PlaySoundAtLocation(World, Sound, FootLocation, Volume, Pitch);
    }
    ++StepsPlayed;
    ++ClassSteps.FindOrAdd(Class.Id);
}

void FCireFootstepPlayer::Tick(UCireAudioSubsystem& Audio, const FVector& Listener, const APawn* LocalPawn, bool bShake, float DeltaSeconds)
{
    using namespace CireFootsteps;
    const FData& D = Data();
    UWorld* World = Audio.GetWorld();
    if(!D.bValid || !World) return;
    const float Dt = FMath::Clamp(DeltaSeconds, 0.f, .1f);
    Clock += Dt;
    Budget = FMath::Min(Budget + Dt * D.MaxStepsPerSecond, FMath::Max(2.f, D.MaxStepsPerSecond * .25f));

    for(int32 I = Pending.Num() - 1; I >= 0; --I)
    {
        if(Clock < Pending[I].At) continue;
        if(Pending[I].Sound.IsValid()) UGameplayStatics::PlaySoundAtLocation(World, Pending[I].Sound.Get(), Pending[I].Location, Pending[I].Volume, Pending[I].Pitch);
        Pending.RemoveAtSwap(I);
    }

    ScanTimer -= Dt;
    if(ScanTimer <= 0.f)
    {
        ScanTimer = .4f;
        Nearby.Reset();
        const float Max2 = FMath::Square(D.MaxDistance + 400.f);
        TArray<TPair<float, ACharacter*>> Found;
        for(TActorIterator<ACharacter> It(World); It; ++It)
            if(IsValid(*It)) { const float Dist2 = FVector::DistSquared(It->GetActorLocation(), Listener); if(Dist2 < Max2) Found.Add({Dist2, *It}); }
        Found.Sort([](const TPair<float, ACharacter*>& A, const TPair<float, ACharacter*>& B) { return A.Key < B.Key; });
        for(int32 I = 0; I < Found.Num() && I < 24; ++I) Nearby.Add(Found[I].Value);
        for(auto It = Tracks.CreateIterator(); It; ++It) if(!It->Key.IsValid() || !Nearby.Contains(It->Key)) It.RemoveCurrent();
    }

    for(const TWeakObjectPtr<ACharacter>& Weak : Nearby)
    {
        ACharacter* C = Weak.Get();
        if(!C || C->IsHidden() || !C->GetMesh() || !C->GetMesh()->GetVisibleFlag()) continue;
        FTrack& T = Tracks.FindOrAdd(Weak);
        if(T.Binding.Class.IsNone()) T.Binding = ForCharacter(C);
        const FClass* Class = D.Classes.Find(T.Binding.Class);
        const UCharacterMovementComponent* Move = C->GetCharacterMovement();
        const bool bDead = (Cast<ACireHero>(C) && Cast<ACireHero>(C)->bDead) || (Cast<ACireMonster>(C) && Cast<ACireMonster>(C)->Health <= 0.f);
        const float Speed = static_cast<float>(C->GetVelocity().Size2D());
        T.SinceStep += Dt;
        if(!Class || !Move || !Move->IsMovingOnGround() || bDead) { T.bLiftedL = T.bLiftedR = false; T.Cadence = 0.f; continue; }
        const bool bLocal = C == LocalPawn;
        const float Bottom = C->GetActorLocation().Z - (C->GetCapsuleComponent() ? C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f);
        const FVector Feet(C->GetActorLocation().X, C->GetActorLocation().Y, Bottom);

        // 1. creature gait phase
        const UCireCreatureArt* Creature = C->FindComponentByClass<UCireCreatureArt>();
        if(Creature && Creature->VisualMesh())
        {
            const float Phase = Creature->MotionPhase();
            if(T.LastPhase >= 0.f && Speed > 20.f && PhaseCrossedContact(T.LastPhase, Phase))
            {
                Step(Audio, C, Feet + C->GetActorForwardVector() * 30.f, Speed, bLocal, bShake); ++PhaseSteps; T.SinceStep = 0.f;
            }
            T.LastPhase = Phase;
            continue;
        }
        // 2. humanoid foot bones
        USkeletalMeshComponent* Mesh = C->GetMesh();
        if(!T.bBonesChecked && Mesh->GetSkeletalMeshAsset())
        {
            T.bBonesChecked = true;
            for(const auto& Pair : FootBones)
                if(Mesh->GetBoneIndex(Pair[0]) != INDEX_NONE && Mesh->GetBoneIndex(Pair[1]) != INDEX_NONE) { T.BoneL = Pair[0]; T.BoneR = Pair[1]; T.bBones = true; break; }
        }
        if(T.bBones && !T.bUseCadence)
        {
            const float Scale = FMath::Max(.3f, static_cast<float>(C->GetActorScale3D().Z));
            const float Lift = 4.f * Scale, Plant = 1.5f * Scale;
            const FVector FootL = Mesh->GetBoneLocation(T.BoneL), FootR = Mesh->GetBoneLocation(T.BoneR);
            const float HL = static_cast<float>(FootL.Z) - Bottom, HR = static_cast<float>(FootR.Z) - Bottom;
            // Planted height = a running minimum that relaxes upward slowly (slopes, stairs, crouch).
            T.BaselineL = FMath::Min(T.BaselineL + Dt * 3.f, HL); T.BaselineR = FMath::Min(T.BaselineR + Dt * 3.f, HR);
            bool bStepped = false;
            auto Foot = [&](float H, float Baseline, bool& bLifted, const FVector& At)
            {
                if(H > Baseline + Lift) bLifted = true;
                else if(bLifted && H < Baseline + Plant && Speed > 40.f && T.SinceStep > .12f)
                {
                    bLifted = false;
                    Step(Audio, C, FVector(At.X, At.Y, Bottom), Speed, bLocal, bShake); ++BoneSteps; T.SinceStep = 0.f; bStepped = true;
                }
            };
            Foot(HL, T.BaselineL, T.bLiftedL, FootL);
            Foot(HR, T.BaselineR, T.bLiftedR, FootR);
            T.MovingNoContact = (Speed > 150.f && !bStepped) ? T.MovingNoContact + Dt : (bStepped ? 0.f : T.MovingNoContact);
            if(T.MovingNoContact > 1.5f)
            {
                T.bUseCadence = true; // the body is not animating its feet: fall back to cadence
                UE_LOG(LogCireFootsteps, Verbose, TEXT("%s: no foot contacts while moving; using cadence"), *C->GetName());
            }
            continue;
        }
        // 3. speed cadence
        const float Interval = StepInterval(*Class, Speed, D.SpeedWalk, D.SpeedRun);
        if(Interval <= 0.f) { T.Cadence = 0.f; continue; }
        if(T.Cadence <= 0.f) T.Cadence = Interval * .6f; // first footfall soon after starting to move
        T.Cadence += Dt;
        if(T.Cadence >= Interval)
        {
            T.Cadence -= Interval;
            const FVector Side = C->GetActorRightVector() * ((CadenceSteps & 1) ? 12.f : -12.f);
            Step(Audio, C, Feet + Side, Speed, bLocal, bShake); ++CadenceSteps; T.SinceStep = 0.f;
        }
    }
}
