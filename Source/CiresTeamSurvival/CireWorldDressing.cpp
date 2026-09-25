#include "CireWorldDressing.h"

#include "Components/PointLightComponent.h"
#include "GameFramework/Actor.h"

// world-dressing: see CireWorldDressing.h.

UCireLightFlicker::UCireLightFlicker()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PrimaryComponentTick.TickInterval = 1.f / 30.f; // lights need not update faster than 30 Hz
    SetIsReplicatedByDefault(false);
}

void UCireLightFlicker::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    Clock += FMath::Clamp(DeltaTime, 0.f, .25f);
    for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
    {
        FEntry& E = Entries[Index];
        UPointLightComponent* Light = E.Light.Get();
        if (!Light) { Entries.RemoveAtSwap(Index); continue; }
        // Two incommensurate waves plus a slow breath: a flame, not a strobe.
        const float T = Clock + E.Seed;
        const float Wave = .55f * FMath::Sin(T * 9.3f) + .3f * FMath::Sin(T * 23.7f + 1.3f) + .15f * FMath::Sin(T * 2.1f);
        Light->SetIntensity(E.Base * FMath::Max(.2f, 1.f + E.Amount * Wave));
    }
}

namespace
{
UCireLightFlicker* FindFlicker(const AActor* Actor)
{
    return Actor ? Actor->FindComponentByClass<UCireLightFlicker>() : nullptr;
}
}

void CireWorldDressing::AddFlicker(AActor* WorldActor, UPointLightComponent* Light, float Amount)
{
    if (!WorldActor || !Light || Amount <= 0.f || WorldActor->GetNetMode() == NM_DedicatedServer) return;
    UCireLightFlicker* Flicker = FindFlicker(WorldActor);
    if (!Flicker)
    {
        Flicker = NewObject<UCireLightFlicker>(WorldActor, TEXT("CireLightFlicker"));
        WorldActor->AddInstanceComponent(Flicker);
        Flicker->RegisterComponent();
    }
    const FVector P = Light->GetComponentLocation();
    Flicker->Entries.Add({Light, Light->Intensity, FMath::Clamp(Amount, 0.f, 1.f), static_cast<float>(FMath::Frac((P.X * .0137 + P.Y * .0071)) * 100.0)});
}

void CireWorldDressing::ResetFlicker(AActor* WorldActor)
{
    if (UCireLightFlicker* Flicker = FindFlicker(WorldActor)) Flicker->Entries.Reset();
}

int32 CireWorldDressing::FlickerCount(const AActor* WorldActor)
{
    const UCireLightFlicker* Flicker = FindFlicker(WorldActor);
    return Flicker ? Flicker->Entries.Num() : 0;
}
