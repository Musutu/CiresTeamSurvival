#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
// Arena gallery (-CireArenaGallery[=id,id]): renders an overview and a gameplay-camera shot of every
// arena at the window resolution (the runner, Tools/RunArenaGallery.py, forces 1920x1080).
namespace CireArenaGallery
{
#if !UE_BUILD_SHIPPING
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
#endif
}
