#pragma once
// vendors: review gallery (-CireVendorGallery, Tools/RunVendorGallery.py). Offscreen 1920x1080 captures:
//  * the mesh check for every merchant body (or -CireVendorGalleryMeshes=<asset>,<asset>): a 4-view turnaround
//    (front / right / back / left, reference pose), then for rigged bodies idle and gesture frames with hand
//    close-ups (both hands from the front, each hand from its side) to catch Janus faces, backwards hands and
//    wrist flips before and after rigging;
//  * each team-0 stall from the customer's side and a wide shot of all three;
//  * the nameplate + interact prompt at a counter;
//  * the shop on every merchant tab.
class ACireGameMode;
namespace CireVendorGallery
{
#if !UE_BUILD_SHIPPING
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
#endif
}
