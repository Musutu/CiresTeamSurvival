#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
// champion-hq: -CireChampionHQGallery renders the champions (and their summons) on the real lane, offscreen 1920x1080, into
// Saved/ChampionHQ/Gallery/<stamp>: a front lineup, then per champion idle / run / attack (contact frame) / cast (release
// frame) / heavy (war cry peak) close-ups in 3/4 front view, and per summon idle / attack. -CireChampionHQProfiles=a,b,...
// limits the champions; -CireChampionHQSummons=1 adds the summon stages. Logs each body's mesh, material and bounds so a
// fallback body is visible in the log. Run via Tools/RunChampionHQGallery.py.
namespace CireChampionHQGallery
{
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
}
