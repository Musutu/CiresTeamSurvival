#pragma once
class ACireGameMode;
// aura-vfx: offscreen gallery of every buff/aura signature on real champion and
// monster bodies, plus attack frames with the empowered swipe and on-hit burst.
namespace CireAuraGallery
{
#if !UE_BUILD_SHIPPING
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
#endif
}
