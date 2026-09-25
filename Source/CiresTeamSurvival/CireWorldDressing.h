#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CireWorldDressing.generated.h"

class UPointLightComponent;

/**
 * world-dressing: presentation-only life for the town (Docs/EnvironmentProps.md, "World dressing").
 * Torch and brazier lights whose slot declares "light": {"flicker": 0..1} are modulated here. The
 * component lives on the ACireWorld actor, never ticks on a dedicated server and never replicates.
 */
UCLASS(Transient)
class CIRESTEAMSURVIVAL_API UCireLightFlicker : public UActorComponent
{
    GENERATED_BODY()
public:
    UCireLightFlicker();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    struct FEntry { TWeakObjectPtr<UPointLightComponent> Light; float Base = 0.f, Amount = 0.f, Seed = 0.f; };
    TArray<FEntry> Entries;
    float Clock = 0.f;
};

namespace CireWorldDressing
{
    /** Registers a slot light for flicker (Amount 0..1 of its intensity). */
    CIRESTEAMSURVIVAL_API void AddFlicker(AActor* WorldActor, UPointLightComponent* Light, float Amount);
    /** Forgets every registered light (the town is being re-placed). */
    CIRESTEAMSURVIVAL_API void ResetFlicker(AActor* WorldActor);
    CIRESTEAMSURVIVAL_API int32 FlickerCount(const AActor* WorldActor);
}
