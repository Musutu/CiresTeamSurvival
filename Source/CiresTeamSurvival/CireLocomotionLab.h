#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
class ACireController;
// movement-feel: -CireLocomotionLab measures and films locomotion of real champion and monster bodies.
// Each subject (real drafted hero / configured monster) runs a scripted course on a grid floor: idle,
// start, run, stop, 90 degree turn, direction reversal, strafe, backpedal, turn in place (smooth and snapped),
// a hill (up/down slope) and a cornered path. Every frame records planted-contact slip, visual yaw, pose jerk
// and frozen poses; selected windows are captured as slow-motion frame sequences.
// Options: -CireLocoLabSubjects=hero:lancer,monster:dire_wolf  -CireLocoLabTag=before  -CireLocoLabNoCapture
// Output: Saved/LocomotionLab/<stamp>-<tag>/metrics.json, frames/<subject>/<segment>_<nn>.png.
// Run with a fixed 60 Hz step (Tools/RunLocomotionLab.py passes -UseFixedTimeStep -FPS=60).
// Network check (RunLocomotionLab.py --net): a dedicated server runs the course (-CireLocomotionLab -CireLocoLabNet)
// and a remote client (-CireLocoLabClient) samples the same subjects as simulated proxies into
// Saved/LocomotionLab/<stamp>-net-client/samples, for comparison with a standalone run.
namespace CireLocomotionLab
{
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
    /** Remote client side of the network check; false when -CireLocoLabClient is absent. */
    bool TickClient(ACireController* Controller);
}
