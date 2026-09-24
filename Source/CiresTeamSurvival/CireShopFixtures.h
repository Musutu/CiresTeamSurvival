#pragma once
// progression-shop: development fixtures (shop gallery, shop network probe, progression probe).
class ACireGameMode;
class ACireController;
namespace CireShopFixtures
{
#if !UE_BUILD_SHIPPING
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
    bool TickClient(ACireController* Controller);
#endif
}
