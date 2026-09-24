#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
class ACireController;
namespace CireInterfaceProbe {
    bool TickServer(ACireGameMode* Mode);
    bool TickClient(ACireController* Controller);
    void ObserveChat(ACireController* Controller, const FString& Message);
}
