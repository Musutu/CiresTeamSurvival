#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
// paragon-champions: -CireParagonGallery renders every installed Paragon champion on the real lane (offscreen
// 1920x1080, Saved/ParagonGallery/<stamp>): a three-quarter close-up per hero, then the hero casting each of its own
// Paragon abilities at a training dummy (Paragon clip + Paragon FX), and logs the body, clip and action state per shot
// (CIRE_PARAGON_GALLERY_POSE). Optional -CireParagonGalleryOnly=<id,...>. Run via Tools/RunParagonGallery.py.
// The captures show Epic-licensed content: they stay in Saved/ (never committed).
namespace CireParagonGallery
{
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
}
