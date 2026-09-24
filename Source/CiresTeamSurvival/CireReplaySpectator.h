#pragma once
#include "CoreMinimal.h"
#include "CireGame.h"
#include "CireReplaySpectator.generated.h"

class ACameraActor;

UCLASS()
class CIRESTEAMSURVIVAL_API ACireReplaySpectator : public ACireController
{
    GENERATED_BODY()
public:
    ACireReplaySpectator();
    virtual void PlayerTick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TObjectPtr<ACameraActor> ReplayCamera;
    bool bReplayViewReady = false;
};
