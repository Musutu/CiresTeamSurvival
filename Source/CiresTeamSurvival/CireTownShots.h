#pragma once
// town-trim: scripted before/after captures of the Medieval Kingdom town (Tools/RunTownShots.py).
//
// -CireTown -CireTownShots=edge,castle,water [-CireTownShotsExit]: once the town is playable (both realms streamed, the
// navmesh ready), a camera visits a list of views in both realms and screenshots each into Saved/TownShots/<stamp>/:
//   edge   : looking out across the Play Bounds border (and an overview of each realm), to show what the trim leaves;
//            the same run with -CireNoTownTrim is the "before".
//   castle : the castle interior views of CastleTown.json "castleInterior.views", shot with the castle fill lights hidden
//            ("before") and shown ("after").
//   water  : every pack water body, shot with the water hidden ("before", the empty riverbeds) and shown ("after").
// -CireCastleLightScan (with the navmesh): walkable spots under a roof or walled in inside the castle levels, spaced, as
// suggested "castleInterior.anchors" (logged and written to Saved/TownShots/castle_anchors.json).
#include "CoreMinimal.h"

class UWorld;

namespace CireTownShots
{
    void Initialize(UWorld* World);
}
