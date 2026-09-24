#pragma once
#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "CireChat.generated.h"

USTRUCT()
struct FCireChatMessage {
    GENERATED_BODY()
    UPROPERTY() FString Sender;
    UPROPERTY() FString Text;
    UPROPERTY() bool bTeamOnly = true;
    UPROPERTY() int32 SenderTeam = 0;
    UPROPERTY() float TimeSeconds = 0;
};

// Uses native character events so chat supports Unicode and the user's keyboard layout.
UCLASS()
class CIRESTEAMSURVIVAL_API UCireViewportClient : public UGameViewportClient {
    GENERATED_BODY()
public:
    virtual bool InputChar(FViewport* Viewport, int32 ControllerId, TCHAR Character) override;
};
