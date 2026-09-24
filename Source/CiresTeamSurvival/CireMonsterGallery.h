#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
// -CireMonsterGallery: offscreen 1920x1080 review of the animated Tripo monster bodies (lineup,
// pack, every archetype mid-attack, locomotion frames, deaths) and a live wave fighting in the
// town seen through the gameplay camera framing. Optional -CireMonsterGalleryOnly=<stage,...>.
// Run via Tools/RunMonsterGallery.py. Docs/MonsterArt.md.
namespace CireMonsterGallery
{
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
}
