#include "CireAbilityLibrary.h"
#include "CireScalingKits.h" // scaling-kits
#include "CireSkillShop.h" // progression-shop: per-level cast scaling
#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireAttackSystem.h"
#include "CireCombatEvents.h"
#include "CireSkillRuntime.h"
#include "CireConstruct.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireAbilityLibrary,Log,All);
namespace {
TMap<FString,FCireAuthoredAbility> Abilities;
bool bLoaded=false;
bool Number(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,float& V,float Max=10000.f){
    double N=0;if(!J->TryGetNumberField(Key,N)||!FMath::IsFinite(N)||N<0||N>Max)return false;V=static_cast<float>(N);return true;
}
void Load(){
    if(bLoaded)return;bLoaded=true;
    FString Text;const FString Path=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/AstraAbilities.json"));
    if(!FFileHelper::LoadFileToString(Text,*Path)||Text.Len()>4*1024*1024){UE_LOG(LogCireAbilityLibrary,Warning,TEXT("Cannot load bounded Astra definitions: %s"),*Path);return;}
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())return;
    FString Engine,Profile,Units;double Version=0;
    if(!Root->TryGetNumberField(TEXT("schemaVersion"),Version)||Version!=1||!Root->TryGetStringField(TEXT("engine"),Engine)||Engine!=TEXT("Unreal")||
       !Root->TryGetStringField(TEXT("profile"),Profile)||Profile!=TEXT("CireGroundAreas")||!Root->TryGetStringField(TEXT("units"),Units)||Units!=TEXT("centimeters"))return;
    const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;if(!Root->TryGetArrayField(TEXT("abilities"),Entries)||Entries->Num()>64)return;
    for(const auto& Entry:*Entries){
        const auto J=Entry->AsObject();if(!J)continue;
        FCireAuthoredAbility A;FString Targeting;
        if(!J->TryGetStringField(TEXT("id"),A.Id)||A.Id.IsEmpty()||A.Id.Len()>64||Abilities.Contains(A.Id)||!J->TryGetStringField(TEXT("name"),A.Name)||A.Name.Len()>80||
           !J->TryGetStringField(TEXT("targeting"),Targeting)||Targeting!=TEXT("ground")||
           !Number(J,TEXT("manaCost"),A.ManaCost)||!Number(J,TEXT("energyCost"),A.EnergyCost,100)||!Number(J,TEXT("cooldownSeconds"),A.CooldownSeconds,300)||
           !Number(J,TEXT("castRange"),A.CastRange,2000)||A.CastRange<100)continue;
        const TSharedPtr<FJsonObject>* Area=nullptr;if(!J->TryGetObjectField(TEXT("area"),Area))continue;
        FString Shape;auto& S=A.Area;if(!(*Area)->TryGetStringField(TEXT("shape"),Shape))continue;
        if(Shape==TEXT("circle"))S.Shape=ECireAreaShape::Circle;else if(Shape==TEXT("cone"))S.Shape=ECireAreaShape::Cone;
        else if(Shape==TEXT("line"))S.Shape=ECireAreaShape::Line;else if(Shape==TEXT("square"))S.Shape=ECireAreaShape::Square;
        else if(Shape==TEXT("custom"))S.Shape=ECireAreaShape::Custom;else continue;
        if(!Number(*Area,TEXT("radius"),S.Radius)||!Number(*Area,TEXT("length"),S.Length)||!Number(*Area,TEXT("width"),S.Width)||
           !Number(*Area,TEXT("coneAngleDegrees"),S.ConeAngleDegrees)||!Number(*Area,TEXT("warningSeconds"),S.WarningSeconds)||
           !Number(*Area,TEXT("durationSeconds"),S.DurationSeconds)||!Number(*Area,TEXT("tickInterval"),S.TickInterval)||
           !Number(*Area,TEXT("damagePerSecond"),S.DamagePerSecond)||!Number(*Area,TEXT("burstDamage"),S.BurstDamage)||
           !Number(*Area,TEXT("verticalTolerance"),S.VerticalTolerance)||!(*Area)->TryGetBoolField(TEXT("persistent"),S.bPersistent)||!(*Area)->TryGetBoolField(TEXT("poison"),S.bPoison))continue;
        const TArray<TSharedPtr<FJsonValue>>* Color=nullptr;const TArray<TSharedPtr<FJsonValue>>* Polygon=nullptr;
        if(!(*Area)->TryGetArrayField(TEXT("color"),Color)||Color->Num()!=4||!(*Area)->TryGetArrayField(TEXT("customPolygon"),Polygon)||Polygon->Num()>32)continue;
        bool Valid=true;double C[4]={};for(int32 I=0;I<4;++I)Valid&=(*Color)[I]->TryGetNumber(C[I])&&FMath::IsFinite(C[I])&&C[I]>=0&&C[I]<=4;
        S.Color=FLinearColor(C[0],C[1],C[2],C[3]);
        for(const auto& P:*Polygon){const TArray<TSharedPtr<FJsonValue>>* XY=nullptr;double X=0,Y=0;
            if(!P->TryGetArray(XY)||XY->Num()!=2||!(*XY)[0]->TryGetNumber(X)||!(*XY)[1]->TryGetNumber(Y)||!FMath::IsFinite(X)||!FMath::IsFinite(Y)){Valid=false;break;}
            S.CustomPolygon.Add(FVector2D(X,Y));}
        S.AbilityName=A.Name;
        if(Valid&&ACireAreaEffect::ValidateSpec(S))Abilities.Add(A.Id,A);
    }
    UE_LOG(LogCireAbilityLibrary,Display,TEXT("CIRE_ASTRA_ABILITIES_LOADED count=%d path=%s"),Abilities.Num(),*Path);
}
}
const FCireAuthoredAbility* CireAbilityLibrary::Find(const FString& Id){Load();return Abilities.Find(Id);}
int32 CireAbilityLibrary::Count(){Load();return Abilities.Num();}
bool CireAbilityLibrary::Cast(ACireHero* Hero,int32 Slot,const FCireAuthoredAbility& A){
    if(!IsValid(Hero)||!Hero->HasAuthority()||!Hero->Cooldowns.IsValidIndex(Slot))return false;
    if(!CireSkillShop::CanPayCast(Hero,A.Id,A.ManaCost,A.EnergyCost)){Hero->Notice=TEXT("Not enough mana or energy.");return false;} // progression-shop: Skill Shop level (Ability DB curve)
    const bool Directional=A.Area.Shape==ECireAreaShape::Cone||A.Area.Shape==ECireAreaShape::Line;
    FVector Aim=Hero->bHasCastAim?Hero->CastAimPoint:(Hero->IsHostile(Hero->Target)?Hero->Target->GetActorLocation():Hero->GetActorLocation()+Hero->GetActorForwardVector()*FMath::Min(500.f,A.CastRange));
    if(!CireSkillRuntime::InRealmBounds(Hero->GetWorld()->GetAuthGameMode<ACireGameMode>(),Hero->TeamId,Aim)){Hero->Notice=TEXT("Choose ground inside your battlefield.");return false;}
    if(FVector::DistSquared2D(Aim,Hero->GetActorLocation())>FMath::Square(A.CastRange)){Hero->Notice=TEXT("Ground target is out of range.");return false;}
    FRotator Heading=(Aim-Hero->GetActorLocation()).GetSafeNormal2D().Rotation();
    FVector Center=Directional?Hero->GetActorLocation():Aim;
    FHitResult Floor;FCollisionQueryParams Q(SCENE_QUERY_STAT(CireAuthoredGround),false,Hero);
    if(!Hero->GetWorld()->LineTraceSingleByObjectType(Floor,Center+FVector(0,0,300),Center-FVector(0,0,700),FCollisionObjectQueryParams(ECC_WorldStatic),Q)){
        Hero->Notice=TEXT("No ground at that location.");return false;}
    Center=Floor.ImpactPoint;
    FHitResult Wall;
    if(Hero->GetWorld()->LineTraceSingleByObjectType(Wall,Hero->GetActorLocation()+FVector(0,0,35),Center+FVector(0,0,60),FCollisionObjectQueryParams(ECC_WorldStatic),Q)||ACireConstruct::FindBlockingConstruct(Hero,Center)){
        Hero->Notice=TEXT("Ground target is blocked.");return false;}
    FCireAreaSpec Spec=A.Area;const auto* Mode=Hero->GetWorld()->GetAuthGameMode<ACireGameMode>();
    const float Power=Mode?Mode->Power(Hero->TeamId):1.f;Spec.BurstDamage=CireKits::Amount(Hero,A.Id,Spec.BurstDamage)*Power;Spec.DamagePerSecond=(Spec.DamagePerSecond+CireKits::DotPerSecondBonus(Hero,A.Id))*Power; // scaling-kits: base + coef x PRIMARY
    if(!ACireAreaEffect::Spawn(Hero,Spec,Center,Heading)){Hero->Notice=TEXT("Area could not be created.");return false;}
    Hero->Mana-=A.ManaCost;Hero->Energy-=A.EnergyCost;Hero->Cooldowns[Slot]=Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(Hero->GetWorld(),A.CooldownSeconds),Hero->CDR);Hero->GlobalCooldown=.9f;Hero->Notice=A.Name;
    CireSkillShop::ApplyCastLevel(Hero,Slot,A.Id,A.ManaCost,A.EnergyCost); // progression-shop: Skill Shop level (Ability DB curve)
    CireCombat::PlayCue(Hero,nullptr,FName(*A.Id),Hero->GetActorLocation(),Center,ECireSpellCue::Cast);
    return true;
}
