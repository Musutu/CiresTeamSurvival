#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CireExpansionNetProbe.generated.h"
class ACireGameMode;
class ACireController;

// Exists only when explicit development probe flags spawn it. The owner-only
// channel acknowledges observations; it never authorizes a gameplay mutation.
UCLASS(NotBlueprintable, Transient)
class CIRESTEAMSURVIVAL_API ACireExpansionProbeChannel : public AActor
{
    GENERATED_BODY()
public:
    ACireExpansionProbeChannel();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated) int32 Team = INDEX_NONE;
    UPROPERTY(Replicated) int32 Stage = 0;
    UFUNCTION(Server, Reliable) void ServerAcknowledge(int32 ObservedStage);
};
namespace CireExpansionNetProbe
{
    CIRESTEAMSURVIVAL_API bool TickServer(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API bool TickClient(ACireController* Controller);
}
