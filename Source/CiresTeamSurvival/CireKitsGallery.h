#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
// scaling-kits: -CireKitsGallery renders the Mechanical Tank taunting, a shield BLOCK, the level-15 Artillery
// bomb and the Skill Shop tooltip with its "Lv 15: +..." line (offscreen 1920x1080, Saved/KitsGallery/<stamp>).
// Run via Tools/RunKitsGallery.py.
namespace CireKitsGallery
{
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
}
