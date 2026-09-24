#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
namespace CireBatchArtGallery
{
#if !UE_BUILD_SHIPPING
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
#endif
}
