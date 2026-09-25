#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
// new-champions: -CireNewChampionsGallery renders the new champions and the Aetheri constructs on the real lane
// (offscreen 1920x1080, Saved/NewChampionsGallery/<stamp>): lineup, mount and gunblade close-ups, each champion
// in combat casting a signature skill, the Constructs being placed (turret, traps, pylons, skitter bombs) and an
// Aetheri monster wave deploying its own constructs. Optional -CireNewChampionsGalleryOnly=<stage,...>.
// Run via Tools/RunNewChampionsGallery.py. Docs/NewChampions.md.
namespace CireNewChampionsGallery
{
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
}
