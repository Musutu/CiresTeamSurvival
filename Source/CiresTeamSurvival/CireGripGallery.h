#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
// weapon-grips: -CireGripGallery renders every armed champion (-CireTripoChampions bodies, Fab plate bodies included)
// alone on a lit stage in six states (idle, run, attack windup, attack contact, cast, dodge roll), each as a
// full-body frame and a close-up of the weapon hand, and logs one CIRE_GRIP_GALLERY_METRIC line per champion and state
// (prop, hand, grip mode, deviation from the animation's intended grip, prop length against body height, clip).
// Optional -CireGripGalleryOnly=<profile,...>. -CireLegacyGrips renders the bind-pose grips (the "before").
// Run via Tools/RunGripGallery.py, which also assembles one contact sheet per champion. Docs/WeaponLoadouts.md.
namespace CireGripGallery
{
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
}
