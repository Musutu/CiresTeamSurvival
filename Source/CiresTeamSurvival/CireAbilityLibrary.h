#pragma once
#include "CoreMinimal.h"
#include "CireAreaEffects.h"
struct FCireAuthoredAbility {
    FString Id,Name;
    FCireAreaSpec Area;
    float ManaCost=35,EnergyCost=0,CooldownSeconds=8,CastRange=900;
};
namespace CireAbilityLibrary {
    const FCireAuthoredAbility* Find(const FString& Id);
    int32 Count();
    bool Cast(ACireHero* Hero,int32 Slot,const FCireAuthoredAbility& Ability);
}
