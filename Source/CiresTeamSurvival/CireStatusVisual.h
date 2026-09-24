#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CireStatusVisual.generated.h"

class UProceduralMeshComponent;
class ACharacter;
/** Local cosmetic state, driven by the same replicated expiry values as the HUD. */
UCLASS(Transient)
class CIRESTEAMSURVIVAL_API UCireStatusVisualComponent : public UActorComponent {
    GENERATED_BODY()
public:
    UCireStatusVisualComponent();
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick) override;
private:
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Shapes;
    void CreateShapes(ACharacter* Unit);
};
namespace CireStatusVisual { CIRESTEAMSURVIVAL_API void Attach(ACharacter* Unit); }
