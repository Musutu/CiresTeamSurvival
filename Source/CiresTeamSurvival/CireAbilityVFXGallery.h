#pragma once
// ability-vfx: one-by-one ability audit harness. Casts every champion skill (actives, ultimates,
// passives, basic attacks) and every monster race ability in isolation on a clean raised stage with
// a target dummy, through the real gameplay paths, and captures key frames (aim/telegraph, cast,
// travel, impact, lingering, end). Run with Tools/RunAbilityVFXGallery.py (-CireAbilityVFXGallery).
class ACireGameMode;
namespace CireAbilityVFXGallery
{
#if !UE_BUILD_SHIPPING
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
#endif
}
