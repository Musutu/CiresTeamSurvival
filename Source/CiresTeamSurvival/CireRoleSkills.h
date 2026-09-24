#pragma once
#include "CoreMinimal.h"
class ACireHero;
class ACireGameMode;

namespace CireRoleSkills
{
    bool Handles(const FString& Id);
    bool IsUltimate(const FString& Id);
    FString Name(const FString& Id);
    FString Targeting(const FString& Id);
    FString Description(const FString& Id);
    bool Cast(ACireHero* Hero,int32 Slot,const FString& Id);
#if !UE_BUILD_SHIPPING
    bool RunSmoke(ACireGameMode* Mode);
#endif
}
