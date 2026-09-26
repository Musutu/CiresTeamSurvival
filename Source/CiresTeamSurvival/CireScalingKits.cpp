#include "CireScalingKits.h"
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "CireClassTraits.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCrowdControl.h"
#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireMechTank.h"
#include "CireNPCCombat.h"
#include "CireSignatureSkills.h"
#include "CireKitSkills.h" // kits-complete
#include "CireSkillShop.h"
#include "CireSummon.h"
#include "CireThreat.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireKits, Log, All);

namespace K = Cires::Kits;

namespace
{
const FName ArtilleryBuff(TEXT("artillery")), EagleEyeBuff(TEXT("eagle_eye")), LongshotBuff(TEXT("longshot")), ShieldWallBuff(TEXT("shield_wall")),
    AmpBuff(TEXT("l15_amped")), VulnerableBuff(TEXT("l15_vulnerable")), MechWeakBuff(TEXT("mech_weakened"));
const FString PaviseName(TEXT("Construct: Pavise"));
int32 GProcDepth = 0;
bool GRandomProcs = true;
float Roll() { return GRandomProcs ? FMath::FRand() : 2.f; } // 2 never passes a chance check // extra hits (Headshot, double attack, DoT ticks) never proc again

struct FProcGuard { FProcGuard() { ++GProcDepth; } ~FProcGuard() { --GProcDepth; } };

ACireGameMode* ModeOf(const AActor* A) { return A && A->GetWorld() ? A->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr; }
float Now(const UWorld* W) { return W ? W->GetTimeSeconds() : 0.f; }

// Profiles whose WeaponLoadouts preset carries a shield part.
const TSet<FString>& ShieldProfiles()
{
    static TSet<FString> Out;static bool bLoaded=false;
    if(bLoaded)return Out;bLoaded=true;
    FString Json;TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/WeaponLoadouts.json")))||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root)return Out;
    const TSharedPtr<FJsonObject>* Presets=nullptr;const TSharedPtr<FJsonObject>* Profiles=nullptr;
    if(!Root->TryGetObjectField(TEXT("presets"),Presets)||!Root->TryGetObjectField(TEXT("profiles"),Profiles))return Out;
    TSet<FString> Shielded;
    for(const auto& P:(*Presets)->Values)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;const TArray<TSharedPtr<FJsonValue>>* Parts=nullptr;
        if(!P.Value->TryGetObject(O)||!(*O)->TryGetArrayField(TEXT("parts"),Parts))continue;
        for(const auto& V:*Parts){const TSharedPtr<FJsonObject>* Part=nullptr;FString Asset;
            if(V->TryGetObject(Part)&&(*Part)->TryGetStringField(TEXT("asset"),Asset)&&Asset.Contains(TEXT("shield"),ESearchCase::IgnoreCase))Shielded.Add(FString(P.Key));}
    }
    for(const auto& P:(*Profiles)->Values){FString Preset;if(P.Value->TryGetString(Preset)&&Shielded.Contains(Preset))Out.Add(FString(P.Key));}
    return Out;
}

float EffectAtLevel(const ACireHero* H,const FString& Id,float Fallback)
{
    return CireKits::ScaledEffect(H,Id,Fallback); // kits-complete: level x potency (Eric's universal primary rule)
}

template<typename F> void ForHostilesNear(ACireHero* H,FVector Center,float Radius,F&& Fn)
{
    auto* Mode=ModeOf(H);if(!Mode)return;
    TArray<AActor*> Hits;
    for(auto* M:Mode->Monsters)if(IsValid(M)&&CireCombat::AreHostile(H,M)&&FVector::DistSquared2D(M->GetActorLocation(),Center)<=FMath::Square(Radius))Hits.Add(M);
    for(TActorIterator<ACireHero> It(H->GetWorld());It;++It)if(CireCombat::AreHostile(H,*It)&&FVector::DistSquared2D(It->GetActorLocation(),Center)<=FMath::Square(Radius))Hits.Add(*It);
    for(AActor* A:Hits)Fn(A);
}

int32 GAreaDepth=0;
bool IsAreaAbility(const FString& Name)
{
    if(GAreaDepth>0)return true;
    const FCireAbilityDef* D=CireAbilityDB::FindByName(Name);
    return D&&D->Radius>50&&D->ScaleComponent!=TEXT("summon");
}
}

FCireAreaDamageScope::FCireAreaDamageScope(){++GAreaDepth;}
FCireAreaDamageScope::~FCireAreaDamageScope(){--GAreaDepth;}
bool FCireAreaDamageScope::Active(){return GAreaDepth>0;}

// ============================================================================ scaling
int32 CireKits::PrimaryOf(const ACireHero* H){return H?H->PrimaryAttribute():0;}
FString CireKits::PrimaryName(const ACireHero* H)
{
    if(!H)return TEXT("Primary");
    const auto P=H->PrimaryStat();
    return P==Cires::PrimaryStat::Strength?TEXT("STR"):P==Cires::PrimaryStat::Agility?TEXT("AGI"):TEXT("INT");
}
float CireKits::Amount(const ACireHero* H,const FString& Id,float FallbackBase,float FallbackCoef)
{
    const FCireAbilityDef* D=CireAbilityDB::Find(Id);
    const bool bDb=D&&D->ScalePrimary>0;
    return static_cast<float>(FMath::Min(10000.0,K::ScaledAmount(bDb?D->ScaleBase:FallbackBase,bDb?D->ScalePrimary:FallbackCoef,PrimaryOf(H))));
}
float CireKits::Potency(const ACireHero* H,const FString& Id)
{
    const FCireAbilityDef* D=CireAbilityDB::Find(Id);
    if(!H||!D||D->PotencyPerPoint<=0)return 1.f;
    return 1.f+FMath::Min(D->PotencyCap,D->PotencyPerPoint*static_cast<float>(PrimaryOf(H)))/100.f;
}
float CireKits::ScaledEffect(const ACireHero* H,const FString& Id,float Fallback)
{
    const int32 L=FMath::Max(1,SkillLevel(H,Id));
    const auto S=CireAbilityDB::EffectiveStats(Id,L);
    return (S.Effect>0?S.Effect:Fallback)*Potency(H,Id);
}
float CireKits::ControlScale(const AActor* Source)
{
    const ACireHero* H=OwnerOf(Source);
    if(!H)return 1.f;
    return 1.f+FMath::Min(25.f,.25f*static_cast<float>(PrimaryOf(H)))/100.f;
}
float CireKits::DotPerSecondBonus(const ACireHero* H,const FString& Id)
{
    const FCireAbilityDef* D=CireAbilityDB::Find(Id);return D?D->DotPerSecondPrimary*PrimaryOf(H):0.f;
}
FString CireKits::ScalingLine(const ACireHero* H,const FString& Id)
{
    const FCireAbilityDef* D=CireAbilityDB::Find(Id);
    if(D&&D->PotencyPerPoint>0) // kits-complete: utility potency line
    {
        if(!H)return FString::Printf(TEXT("Potency: +%s%% effect per Primary (max +%.0f%%)"),*FString::SanitizeFloat(D->PotencyPerPoint,0),D->PotencyCap);
        return FString::Printf(TEXT("Potency: x%.2f from Primary (%s %d): +%s%% per point, max +%.0f%%"),Potency(H,Id),*PrimaryName(H),PrimaryOf(H),
            *FString::SanitizeFloat(D->PotencyPerPoint,0),D->PotencyCap);
    }
    if(!D||D->ScalePrimary<=0)return FString();
    const FString Verb=D->ScaleComponent==TEXT("heal")?TEXT("Heals"):D->ScaleComponent==TEXT("shield")?TEXT("Barrier of"):TEXT("Deals");
    const FString Coef=FString::SanitizeFloat(D->ScalePrimary,0);
    if(!H)return FString::Printf(TEXT("%s %.0f + %s× Primary %s"),*Verb,D->ScaleBase,*Coef,*CireAbilityDB::ScalingWord(*D));
    const int32 Level=FMath::Max(1,SkillLevel(H,Id));
    const float LevelScale=D->Base.Effect>0?CireAbilityDB::EffectiveStats(Id,Level).Effect/D->Base.Effect:1.f;
    FString Line=FString::Printf(TEXT("%s %.0f + %s× Primary (%s %d) %s = %.0f"),*Verb,D->ScaleBase,*Coef,*PrimaryName(H),PrimaryOf(H),*CireAbilityDB::ScalingWord(*D),Amount(H,Id));
    if(Level>1&&!FMath::IsNearlyEqual(LevelScale,1.f))Line+=FString::Printf(TEXT(" (x%.2f at level %d = %.0f)"),LevelScale,Level,Amount(H,Id)*LevelScale);
    return Line;
}
FString CireKits::DescribeFor(const ACireHero* H,const FString& Id,int32 Level)
{
    FString Text=CireAbilityDB::Describe(Id,Level);if(Text.IsEmpty())return Text;
    // Replace the generic "base + coef x Primary" line with the hero's exact numbers.
    const FString Line=ScalingLine(H,Id);
    if(!Line.IsEmpty())
    {
        TArray<FString> Lines;Text.ParseIntoArray(Lines,TEXT("\n"),false);
        for(FString& L:Lines)if(L.Contains(TEXT("x Primary"))||L.StartsWith(TEXT("Potency:")))L=Line;
        Text=FString::Join(Lines,TEXT("\n"));
    }
    if(const FCireAbilityDef* D=CireAbilityDB::Find(Id))
        if(K::Level15Unlocked(Level)&&(!D->Level15Label.IsEmpty()||!D->Aura15Label.IsEmpty()))Text+=TEXT("  (active)");
    return Text;
}

// ============================================================================ inheritance
float CireKits::AttackSpeedMultiplier(const ACireHero* H)
{
    if(!H)return 1.f;
    const float Passive=H->HasSkill(TEXT("battle_rhythm"))?1.f+ScaledEffect(H,TEXT("battle_rhythm"),20.f)/100.f:1.f; // kits-complete: level x potency
    const float Speed=(1.f+H->Agility*0.01f+CireItems::AttackSpeedBonus(H)+CireClassTraits::AttackSpeedBonus(H)+CireSignatureSkills::AttackSpeedBonus(H)+AttackSpeedBonus(H))*Passive;
    return FMath::Clamp(FMath::IsFinite(Speed)?Speed:1.f,.25f,5.f);
}
ACireHero* CireKits::OwnerOf(const AActor* Unit)
{
    if(const auto* S=::Cast<ACireSummon>(Unit))return S->GetOwnerHero();
    if(const auto* C=::Cast<ACireConstruct>(Unit))return ::Cast<ACireHero>(C->GetSourceActor());
    return const_cast<ACireHero*>(::Cast<ACireHero>(Unit));
}
float CireKits::InheritedInterval(const AActor* Unit,float Base)
{
    const ACireHero* O=OwnerOf(Unit);
    if(!O||O==Unit)return Base;
    return static_cast<float>(K::InheritedAttackInterval(Base,AttackSpeedMultiplier(O)));
}
float CireKits::OwnerCooldown(const ACireHero* O,float Base)
{
    return static_cast<float>(Cires::CooldownSeconds(Base,IsValid(O)?O->CDR:0.f));
}
float CireKits::InheritedCooldown(const AActor* Unit,float Base){return OwnerCooldown(OwnerOf(Unit),Base);}

// ============================================================================ stat hooks
int32 CireKits::SkillLevel(const ACireHero* H,const FString& Id){return CireSkillShop::Level(H,Id);}
bool CireKits::IsArtilleryActive(const ACireHero* H){return H&&CireBuffs::IsActive(H,ArtilleryBuff);}
float CireKits::AttackSpeedBonus(const ACireHero* H)
{
    if(!H||H->IsA<ACireSummon>())return 0.f;
    float Bonus=static_cast<float>(PartyAuras(H).AttackSpeed);
    if(IsArtilleryActive(H))Bonus+=EffectAtLevel(H,TEXT("artillery"),100.f)/100.f;
    return Bonus;
}
float CireKits::BasicRange(const ACireHero* H,float Base)
{
    if(!H||H->IsA<ACireSummon>())return Base;
    float Bonus=0;
    if(H->HasSkill(TEXT("artillery_training")))Bonus+=EffectAtLevel(H,TEXT("artillery_training"),150.f);
    if(CireBuffs::IsActive(H,EagleEyeBuff))Bonus+=EffectAtLevel(H,TEXT("eagle_eye"),400.f);
    if(CireBuffs::IsActive(H,LongshotBuff))Bonus+=250.f;
    Bonus+=CireKitSkills::BasicRangeBonus(H); // kits-complete: Elder of the Deepwood reach
    return static_cast<float>(K::EffectiveBasicRange(Base,Bonus,IsArtilleryActive(H)));
}
float CireKits::CritBonus(const AActor* Source)
{
    const auto* H=::Cast<ACireHero>(Source);return H&&!H->IsA<ACireSummon>()?static_cast<float>(PartyAuras(H).Crit):0.f;
}
bool CireKits::BlocksCasting(ACireHero* H,const FString& Id)
{
    if(!IsArtilleryActive(H)||Id==TEXT("artillery"))return false;
    H->Notice=TEXT("Artillery: basic attacks only until it ends.");return true;
}

// ============================================================================ defence
bool CireKits::CarriesShield(const ACireHero* H)
{
    return H&&!H->IsA<ACireSummon>()&&ShieldProfiles().Contains(H->ChampionProfileId);
}
bool CireKits::IsShieldTank(const ACireHero* H)
{
    return CarriesShield(H)&&CireClassTraits::Role(H)==Cires::SkillDraftRole::Tank;
}
float CireKits::DefenseMultiplier(const AActor* Defender)
{
    const auto* H=::Cast<ACireHero>(Defender);
    const double Shield=K::ShieldDefenseMultiplier(IsShieldTank(H));
    return static_cast<float>(K::VulnerableDefense(Shield,CireBuffs::IsActive(Defender,VulnerableBuff)));
}
float CireKits::FlatDefense(const ACireHero* H,bool bPhysical)
{
    if(!H||H->IsA<ACireSummon>())return 0.f;
    const auto T=PartyAuras(H);return static_cast<float>(bPhysical?T.Armor:T.MagicResist);
}
float CireKits::ModifyIncomingDamage(ACireHero* H,AActor* Causer,const FString& Name,float Amount)
{
    if(!H||!H->HasAuthority()||!FMath::IsFinite(Amount)||Amount<=0)return Amount;
    const bool bPhysical=CireItems::IsBasicAttack(Causer,Name)||[&]{const auto* D=CireAbilityDB::FindByName(Name);return D&&D->School==TEXT("physical");}();
    // AoE-resist aura.
    if(IsAreaAbility(Name))
    {
        const double Chance=PartyAuras(H).AoeResistChance;
        if(Chance>0&&Roll()<Chance){CireCombat::BroadcastAvoidance(Causer,H,ECireHitOutcome::Resist,Name,Amount);return 0.f;}
    }
    // Pavise cover: an allied pavise within 3m that stands between the hero and the attacker.
    if(IsValid(Causer))
        for(TActorIterator<ACireConstruct> It(H->GetWorld());It;++It)
        {
            if(It->IsActorBeingDestroyed()||It->GetDisplayName()!=PaviseName||It->OriginTeam!=H->TeamId)continue;
            const FVector P=It->GetActorLocation();
            if(FVector::DistSquared2D(P,H->GetActorLocation())>FMath::Square(300.f))continue;
            const FVector ToAttacker=(Causer->GetActorLocation()-H->GetActorLocation()).GetSafeNormal2D(),ToPavise=(P-H->GetActorLocation()).GetSafeNormal2D();
            if(FVector::DotProduct(ToAttacker,ToPavise)>.2f){Amount*=.75f;break;}
        }
    // Shield Wall: frontal reduction; frontal physical hits always count as blocked.
    bool bFrontalWall=false;
    if(CireBuffs::IsActive(H,ShieldWallBuff)&&IsValid(Causer))
    {
        const FVector To=(Causer->GetActorLocation()-H->GetActorLocation()).GetSafeNormal2D();
        if(FVector::DotProduct(H->GetActorForwardVector().GetSafeNormal2D(),To)>.2f)
        {
            bFrontalWall=true;
            const float Reduction=FMath::Clamp(EffectAtLevel(H,TEXT("shield_wall"),60.f)/100.f,0.f,.8f);
            const float Before=Amount;Amount*=1.f-Reduction;
            if(bPhysical){CireCombat::BroadcastAvoidance(Causer,H,ECireHitOutcome::Block,Name,Before-Amount);return Amount;}
        }
    }
    // Shield-bearing tank: 30% chance to block 50% of a physical hit.
    if(!bFrontalWall)
    {
        const auto R=K::ResolveShieldBlock(Amount,bPhysical,IsShieldTank(H),Roll());
        if(R.Blocked){CireCombat::BroadcastAvoidance(Causer,H,ECireHitOutcome::Block,Name,static_cast<float>(R.Prevented));return static_cast<float>(R.Damage);}
    }
    return Amount;
}
bool CireKits::IgnoresStun(AActor* Target)
{
    const auto* H=::Cast<ACireHero>(Target);if(!H||H->IsA<ACireSummon>())return false;
    const double Chance=PartyAuras(H).StunIgnoreChance;
    if(Chance>0&&Roll()<Chance){CireCombat::BroadcastAvoidance(Target,Target,ECireHitOutcome::Resist,TEXT("Stun (aura)"));return true;}
    return false;
}
float CireKits::MonsterAttackRate(const ACireMonster* M)
{
    return M&&CireBuffs::IsActive(M,MechWeakBuff)?static_cast<float>(1.0-K::MechSlowAttackSpeed):1.f;
}

// ============================================================================ damage pipeline
float CireKits::ModifyOutgoingDamage(AActor* Source,AActor* Target,float Amount,const FString& Name)
{
    if(!IsValid(Source)||!IsValid(Target)||!FMath::IsFinite(Amount)||Amount<=0)return Amount;
    Amount=static_cast<float>(K::AmplifiedDamage(Amount,CireBuffs::IsActive(Target,AmpBuff)));
    const auto* H=::Cast<ACireHero>(Source);
    if(H&&!H->IsA<ACireSummon>())
    {
        const bool bBasic=CireItems::IsBasicAttack(Source,Name);
        const float Distance=static_cast<float>(FVector::Dist2D(Source->GetActorLocation(),Target->GetActorLocation()));
        if(bBasic&&CireBuffs::IsActive(H,LongshotBuff)&&Distance>800.f)Amount*=1.f+EffectAtLevel(H,TEXT("longshot"),25.f)/100.f;
        const double Ranged=PartyAuras(H).RangedDamage;
        if(Ranged>0&&((bBasic&&H->IsRangedBasicAttack())||(!bBasic&&Distance>=500.f)))Amount*=static_cast<float>(1.0+Ranged);
    }
    return Amount;
}

void CireKits::ApplyLevel15(ACireHero* H,const FCireAbilityDef& D,AActor* Target,float Hit)
{
    if(!H||!H->HasAuthority()||!IsValid(Target)||!CireCombat::IsAlive(Target))return;
    const auto& N=K::L15();const FName B=D.Level15Bonus;
    auto* Sub=UCireKitsSubsystem::Get(H->GetWorld());
    if(B==TEXT("dot"))
    {
        if(Sub&&Hit>0){UCireKitsSubsystem::FDot X;X.Source=H;X.Target=Target;X.Ticks=FMath::RoundToInt(N.DotSeconds);
            X.PerTick=static_cast<float>(K::DotTotal(Hit)/FMath::Max(1,X.Ticks));X.Name=D.Name+TEXT(" burn (Lv 15)");Sub->Dots.Add(X);}
    }
    else if(B==TEXT("healCut"))CireCrowdControl::HealCut(Target,static_cast<float>(N.HealCutFraction),static_cast<float>(N.HealCutSeconds),H);
    else if(B==TEXT("stun"))
    {
        const FString Key=FString::Printf(TEXT("%s/%s"),*Target->GetName(),*D.Id);const float T=Now(H->GetWorld());
        if(!Sub||Sub->StunLockout.FindRef(Key)<=T){CireCrowdControl::Stun(Target,static_cast<float>(N.StunSeconds),H);if(Sub)Sub->StunLockout.Add(Key,T+static_cast<float>(N.StunInternalCooldown));}
    }
    else if(B==TEXT("slow"))CireCrowdControl::Slow(Target,static_cast<float>(N.SlowSeconds),H);
    else if(B==TEXT("damageAmp"))CireBuffs::Apply(Target,AmpBuff,static_cast<float>(N.DamageAmpSeconds),H);
    else if(B==TEXT("vulnerability"))CireBuffs::Apply(Target,VulnerableBuff,static_cast<float>(N.VulnerabilitySeconds),H);
    else if(B==TEXT("purge"))CireSignatureSkills::PurgeBuffs(Target);
}

void CireKits::OnDamageDealt(AActor* Source,AActor* Target,float Original,float Applied,const FString& Name)
{
    if(!IsValid(Source)||!Source->HasAuthority()||!IsValid(Target)||Applied<=0||GProcDepth>0)return;
    FProcGuard Guard;
    auto* H=::Cast<ACireHero>(Source);
    if(!H||H->IsA<ACireSummon>())return;
    auto* Sub=UCireKitsSubsystem::Get(H->GetWorld());
    const bool bBasic=CireItems::IsBasicAttack(Source,Name);
    // Artillery: remember everything the window deals.
    if(Sub&&IsArtilleryActive(H)&&bBasic)
        if(auto* State=Sub->Artillery.Find(H)){K::RecordArtilleryDamage(*State,Applied);Sub->ArtilleryLastTarget.Add(H,Target);}
    // Level-15 hit bonus of the ability that landed.
    if(const FCireAbilityDef* D=CireAbilityDB::FindByName(Name))
        if(D->Level15Special.IsEmpty()&&D->Level15Trigger!=TEXT("pulse")&&!D->Level15Bonus.IsNone()&&K::Level15Unlocked(SkillLevel(H,D->Id)))
            ApplyLevel15(H,*D,Target,Applied);
    const auto Auras=PartyAuras(H);
    // Headshot: an extra hit for 2x (3x at level 15) of the original hit, on top of it.
    if(H->HasSkill(TEXT("headshot"))&&CireCombat::IsAlive(Target))
    {
        const double Extra=K::HeadshotExtra(Original,SkillLevel(H,TEXT("headshot")),Roll())*Potency(H,TEXT("headshot")); // kits-complete: potency
        if(Extra>0)CireCombat::ApplyDamage(H,Target,static_cast<float>(Extra),TEXT("Headshot"));
    }
    if(bBasic&&CireCombat::IsAlive(Target))
    {
        if(Auras.DoubleAttackChance>0&&Roll()<Auras.DoubleAttackChance)CireCombat::ApplyDamage(H,Target,Original,Name);
        if(Auras.StunOnHitChance>0&&Roll()<Auras.StunOnHitChance)CireCrowdControl::Stun(Target,.75f,H);
    }
    const FCireAbilityDef* D=CireAbilityDB::FindByName(Name);
    const bool bPhysical=bBasic||(D&&D->School==TEXT("physical"));
    const double Steal=bPhysical?Auras.PhysicalLifesteal:Auras.MagicLifesteal;
    if(Steal>0&&!H->bDead)CireCombat::ApplyHealing(H,H,static_cast<float>(Applied*Steal),bPhysical?TEXT("Physical lifesteal (aura)"):TEXT("Magic lifesteal (aura)"));
}

void CireKits::OnSkillCast(ACireHero* H,const FString& Id)
{
    if(!H||!H->HasAuthority())return;
    const FCireAbilityDef* D=CireAbilityDB::Find(Id);
    if(!D||D->Level15Trigger!=TEXT("pulse")||!D->Level15Special.IsEmpty()||D->Level15Bonus.IsNone()||!K::Level15Unlocked(SkillLevel(H,Id)))return;
    FVector Center=H->GetActorLocation();
    if(D->Targeting==TEXT("aim")&&H->bHasCastAim)Center=H->CastAimPoint;
    else if((D->Targeting==TEXT("ally")||D->Targeting==TEXT("enemy"))&&IsValid(H->Target))Center=H->Target->GetActorLocation();
    const float Radius=FMath::Clamp(D->Radius>50?D->Radius:450.f,300.f,900.f);
    const float Hit=Amount(H,Id);
    ForHostilesNear(H,Center,Radius,[&](AActor* U){ApplyLevel15(H,*D,U,Hit);});
    CireCombat::PlayCue(H,nullptr,FName(*Id),H->GetActorLocation(),Center,ECireSpellCue::Impact,Radius/450.f,false);
}

// ============================================================================ auras
FName CireKits::AuraBuffId(K::Aura A){return FName(*FString::Printf(TEXT("aura15_%s"),UTF8_TO_TCHAR(K::AuraId(A))));}
K::AuraTotals CireKits::PartyAuras(const ACireHero* H)
{
    std::vector<K::Aura> List;
    if(!H||!H->GetWorld())return K::SumAuras(List);
    for(TActorIterator<ACireHero> It(H->GetWorld());It;++It)
    {
        const ACireHero* P=*It;
        if(P->IsA<ACireSummon>()||!P->bDrafted||P->bDead||P->TeamId!=H->TeamId||P->TeamId<0)continue;
        for(const FString& Id:P->Skills)
        {
            const FCireAbilityDef* D=CireAbilityDB::Find(Id);
            if(D&&D->IsPassive()&&!D->Aura15.IsNone()&&K::Level15Unlocked(SkillLevel(P,Id)))List.push_back(K::ParseAura(TCHAR_TO_UTF8(*D->Aura15.ToString())));
        }
    }
    return K::SumAuras(List);
}

// ============================================================================ monsters vs constructs
void CireKits::AddConstructThreat(ACireMonster* M,ACireConstruct* C,float Amount)
{
    if(!M||!C||!M->HasAuthority()||!FMath::IsFinite(Amount)||Amount<=0||!C->CanBeDamagedBy(M))return;
    if(auto* Sub=UCireKitsSubsystem::Get(M->GetWorld())){float& T=Sub->ConstructThreat.FindOrAdd(M).FindOrAdd(C);T=FMath::Min(1.e9f,T+Amount);M->bEngaged=true;}
}
float CireKits::ConstructThreat(const ACireMonster* M,const ACireConstruct* C)
{
    const auto* Sub=M?UCireKitsSubsystem::Get(M->GetWorld()):nullptr;if(!Sub)return 0.f;
    const auto* Table=Sub->ConstructThreat.Find(const_cast<ACireMonster*>(M));return Table?Table->FindRef(const_cast<ACireConstruct*>(C)):0.f;
}
ACireConstruct* CireKits::ConstructVictim(const ACireMonster* M)
{
    auto* Sub=M?UCireKitsSubsystem::Get(M->GetWorld()):nullptr;if(!Sub)return nullptr;
    auto* Table=Sub->ConstructThreat.Find(const_cast<ACireMonster*>(M));if(!Table)return nullptr;
    ACireConstruct* Best=nullptr;float Top=0;
    for(auto It=Table->CreateIterator();It;++It)
    {
        ACireConstruct* C=It.Key().Get();
        if(!IsValid(C)||C->IsActorBeingDestroyed()||C->Health<=0||!C->CanBeDamagedBy(const_cast<ACireMonster*>(M))){It.RemoveCurrent();continue;}
        if(It.Value()>Top){Top=It.Value();Best=C;}
    }
    if(!Best)return nullptr;
    // Heroes keep aggro unless the construct out-threatens the current victim by the pull ratio.
    const bool bForced=M->ForcedVictim.IsValid()&&M->ForcedVictimUntil>Now(M->GetWorld());
    if(bForced)return nullptr;
    if(IsValid(M->Victim)){const float V=M->Threat.FindRef(M->Victim);if(Top<=V*CireThreat::PullRatio(M,M->Victim))return nullptr;}
    return Best;
}
bool CireKits::MonsterPursueConstruct(ACireMonster* M)
{
    if(!M||!M->HasAuthority()||M->Health<=0)return false;
    ACireConstruct* C=ConstructVictim(M);if(!C)return false;
    const FVector Dir=(C->GetActorLocation()-M->GetActorLocation()).GetSafeNormal2D();
    const float Reach=M->GetCapsuleComponent()->GetScaledCapsuleRadius()+FMath::Max(C->ConstructSpec.Width,C->ConstructSpec.Depth)*.5f+90.f;
    if(FVector::DistSquared2D(M->GetActorLocation(),C->GetActorLocation())>FMath::Square(Reach)){M->AddMovementInput(Dir);return true;}
    M->GetCharacterMovement()->StopMovementImmediately();
    if(!Dir.IsNearlyZero())M->SetActorRotation(Dir.Rotation());
    if(M->AttackTimer<=0)
    {
        M->AttackTimer=1.4f;
        CireCombat::ApplyDamage(M,C,CireNPCCombat::EffectiveDamage(M),TEXT("Smash construct"));
        CireCombat::PlayCue(M,C,TEXT("npc_wall_strike"),M->GetActorLocation(),C->GetActorLocation(),ECireSpellCue::Impact,.8f,true);
    }
    return true;
}

// ============================================================================ skills
bool CireKits::Handles(const FString& Id)
{
    static const TSet<FString> Ids={TEXT("shield_bash"),TEXT("shield_toss"),TEXT("shield_wall"),TEXT("pavise"),TEXT("mechanical_tank"),
        TEXT("artillery"),TEXT("eagle_eye"),TEXT("longshot")};
    return Ids.Contains(Id);
}
FString CireKits::Description(const FString& Id)
{
    const FCireAbilityDef* D=CireAbilityDB::Find(Id);if(!D)return FString();
    FString Cost=D->Base.EnergyCost>0?FString::Printf(TEXT("%.0f energy"),D->Base.EnergyCost):D->Base.ManaCost>0?FString::Printf(TEXT("%.0f mana"),D->Base.ManaCost):TEXT("Passive");
    if(D->Base.Cooldown>0)Cost+=FString::Printf(TEXT(" | %.0fs CD"),D->Base.Cooldown);
    return Cost+TEXT(". ")+D->Description.Replace(TEXT("{effect}"),*FString::SanitizeFloat(D->Base.Effect,0));
}
bool CireKits::MeetsRequirement(const ACireHero* H,const FString& Id,FString* Why)
{
    const FCireAbilityDef* D=CireAbilityDB::Find(Id);if(!D||D->Requires.IsEmpty())return true;
    if(D->Requires==TEXT("shield")&&!CarriesShield(H)){if(Why)*Why=TEXT("Requires a shield (shield-bearing champions only).");return false;}
    if(D->Requires==TEXT("ranged")&&!(H&&H->IsRangedBasicAttack())){if(Why)*Why=TEXT("Requires a ranged basic attack.");return false;}
    return true;
}
const TArray<FName>& CireKits::BuffIds()
{
    static const TArray<FName> Ids=[]{
        TArray<FName> Out={ArtilleryBuff,EagleEyeBuff,LongshotBuff,ShieldWallBuff,AmpBuff,VulnerableBuff,MechWeakBuff};
        for(int32 A=static_cast<int32>(K::Aura::AttackSpeed);A<=static_cast<int32>(K::Aura::RangedDamage);++A)Out.Add(AuraBuffId(static_cast<K::Aura>(A)));
        return Out;}();
    return Ids;
}

bool CireKits::Cast(ACireHero* H,int32 Slot,const FString& Id)
{
    auto* Mode=ModeOf(H);const FCireAbilityDef* D=CireAbilityDB::Find(Id);
    if(!H||!H->HasAuthority()||!Mode||!Mode->IsCombatPhase()||!D||!H->Cooldowns.IsValidIndex(Slot)||H->Cooldowns[Slot]>0)return false;
    auto Fail=[&](const TCHAR* Why){H->Notice=Why;return false;};
    if(!MeetsRequirement(H,Id))return Fail(D->Requires==TEXT("shield")?TEXT("You need a shield for this skill."):TEXT("You need a ranged weapon for this skill."));
    const float Mana=D->Base.ManaCost,Energy=D->Base.EnergyCost;
    if(!CireSkillShop::CanPayCast(H,Id,Mana,Energy))return Fail(TEXT("Not enough mana or energy."));
    const float Power=Mode->Power(H->TeamId),T=Now(H->GetWorld());
    const bool bHostile=H->IsHostile(H->Target);
    const FVector Aim=H->bHasCastAim?H->CastAimPoint:bHostile?H->Target->GetActorLocation():H->GetActorLocation()+H->GetActorForwardVector().GetSafeNormal2D()*300.f;
    auto NeedsEnemy=[&](float Range){return bHostile&&H->InRange(H->Target,Range);};
    AActor* Victim=H->Target;
    // Validate first; nothing is paid for a refused cast.
    if((Id==TEXT("shield_bash")||Id==TEXT("shield_toss"))&&!NeedsEnemy(D->Range+40.f))return Fail(TEXT("Select a hostile target in range."));
    if((Id==TEXT("pavise")||Id==TEXT("mechanical_tank"))&&FVector::DistSquared2D(Aim,H->GetActorLocation())>FMath::Square(D->Range+50.f))return Fail(TEXT("Aim within casting range."));
    ACireMechTank* Mech=nullptr;ACireConstruct* Barrier=nullptr;
    if(Id==TEXT("mechanical_tank"))
    {
        for(TActorIterator<ACireMechTank> It(H->GetWorld());It;++It)if(It->GetOwnerHero()==H)It->Destroy(); // one mech per owner
        FString Why;Mech=ACireMechTank::SpawnFor(H,Aim,&Why);if(!Mech)return Fail(TEXT("No room for the Mechanical Tank there."));
    }
    else if(Id==TEXT("pavise"))
    {
        for(TActorIterator<ACireConstruct> It(H->GetWorld());It;++It)if(It->GetSourceActor()==H&&It->GetDisplayName()==PaviseName)It->Destroy(); // one pavise per owner
        FCireConstructSpec S;S.Kind=ECireConstructKind::Wall;S.MaxHealth=FMath::Min(20000.f,Amount(H,Id)*Power);S.LifetimeSeconds=12.f;
        S.Width=260.f;S.Depth=40.f;S.Height=200.f;S.ManaCost=0;S.EnergyCost=Energy;S.CooldownSeconds=D->Base.Cooldown;S.CastRange=D->Range+50.f;
        S.bBlockMovement=false;S.bBlockProjectiles=true;S.bDestructible=true;S.bBlockFriendly=false;S.Color=FLinearColor(.55f,.6f,.68f,.9f);
        const FVector Dir=(Aim-H->GetActorLocation()).GetSafeNormal2D();
        Barrier=ACireConstruct::Spawn(H,S,Aim,Dir.IsNearlyZero()?H->GetActorRotation():Dir.Rotation(),PaviseName);
        if(!Barrier)return Fail(TEXT("The pavise cannot be planted there."));
    }
    H->Mana-=Mana;H->Energy-=Energy;
    H->Cooldowns[Slot]=static_cast<float>(Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(H->GetWorld(),D->Base.Cooldown),H->CDR));
    CireSkillShop::ApplyCastLevel(H,Slot,Id,Mana,Energy);
    H->GlobalCooldown=.9f;
    CireCombat::PlayCue(H,bHostile?Victim:nullptr,FName(*Id),H->GetActorLocation(),Aim,ECireSpellCue::Cast);
    const float Hit=Amount(H,Id)*Power;
    if(Id==TEXT("shield_bash"))
    {
        if(auto* M=::Cast<ACireMonster>(Victim))CireNPCCombat::InterruptCast(M,H);
        CireCombat::ApplyStrike(H,Victim,Hit,D->Name); // DB effects: interrupt (2s lockout) + 1s stun
    }
    else if(Id==TEXT("shield_toss"))
    {
        TArray<AActor*> Chain={Victim};float Scale=1.f;
        for(int32 Bounce=0;Bounce<2;++Bounce)
        {
            AActor* Next=nullptr;float Best=FMath::Square(D->Radius>0?D->Radius:500.f);
            ForHostilesNear(H,Chain.Last()->GetActorLocation(),D->Radius>0?D->Radius:500.f,[&](AActor* U){
                const float Dist=FVector::DistSquared2D(U->GetActorLocation(),Chain.Last()->GetActorLocation());
                if(!Chain.Contains(U)&&Dist<Best){Best=Dist;Next=U;}});
            if(!Next)break;Chain.Add(Next);
        }
        for(int32 I=0;I<Chain.Num();++I)
        {
            CireCombat::ApplyStrike(H,Chain[I],Hit*Scale,D->Name);Scale*=.8f;
            if(auto* M=::Cast<ACireMonster>(Chain[I]))CireThreat::AddRaw(M,H,Hit*2.f);
            if(I>0)CireCombat::PlayCue(H,Chain[I],FName(*Id),Chain[I-1]->GetActorLocation(),Chain[I]->GetActorLocation(),ECireSpellCue::Impact);
        }
    }
    else if(Id==TEXT("shield_wall"))CireBuffs::Apply(H,ShieldWallBuff,CireDeveloperTools::EffectSeconds(H->GetWorld(),D->Duration>0?D->Duration:6.f),H);
    else if(Id==TEXT("artillery"))
    {
        const float Seconds=static_cast<float>(K::ArtilleryDurationSeconds);
        CireBuffs::Apply(H,ArtilleryBuff,Seconds,H);
        if(auto* Sub=UCireKitsSubsystem::Get(H->GetWorld())){K::StartArtillery(Sub->Artillery.FindOrAdd(H));}
        H->bAutoAttack=true;
    }
    else if(Id==TEXT("eagle_eye"))CireBuffs::Apply(H,EagleEyeBuff,CireDeveloperTools::EffectSeconds(H->GetWorld(),8.f),H);
    else if(Id==TEXT("longshot"))
    {
        CireBuffs::Apply(H,LongshotBuff,CireDeveloperTools::EffectSeconds(H->GetWorld(),10.f),H);
        H->SlowUntil=FMath::Max(H->SlowUntil,T+10.f);
    }
    H->Notice=D->Name;H->ForceNetUpdate();
    return true;
}

// ============================================================================ subsystem
UCireKitsSubsystem* UCireKitsSubsystem::Get(const UWorld* World){return World?World->GetSubsystem<UCireKitsSubsystem>():nullptr;}

void UCireKitsSubsystem::Tick(float Dt)
{
    UWorld* W=GetWorld();
    if(!W||W->GetNetMode()==NM_Client||!FMath::IsFinite(Dt)||Dt<=0)return;
    // Level-15 DoTs.
    for(int32 I=Dots.Num()-1;I>=0;--I)
    {
        FDot& X=Dots[I];X.Timer-=Dt;
        if(!X.Source.IsValid()||!X.Target.IsValid()||!CireCombat::IsAlive(X.Target.Get())){Dots.RemoveAtSwap(I);continue;}
        if(X.Timer>0)continue;
        X.Timer+=1.f;--X.Ticks;
        {FProcGuard Guard;CireCombat::ApplyDamage(X.Source.Get(),X.Target.Get(),X.PerTick,X.Name);}
        if(X.Ticks<=0)Dots.RemoveAtSwap(I);
    }
    // Artillery windows and the level-15 bomb.
    for(auto It=Artillery.CreateIterator();It;++It)
    {
        ACireHero* H=It.Key().Get();
        if(!IsValid(H)||H->bDead){It.RemoveCurrent();continue;}
        if(!It.Value().Active())continue;
        const double Bomb=K::TickArtillery(It.Value(),Dt,CireKits::SkillLevel(H,TEXT("artillery")));
        if(It.Value().Active())continue;
        CireBuffs::Remove(H,ArtilleryBuff);
        if(Bomb>0)
        {
            AActor* Last=ArtilleryLastTarget.FindRef(H).Get();
            const FVector Center=IsValid(Last)?Last->GetActorLocation():IsValid(H->Target)?H->Target->GetActorLocation():H->GetActorLocation()+H->GetActorForwardVector()*600.f;
            LastBombDamage=static_cast<float>(Bomb);LastBombCenter=Center;
            const float Radius=static_cast<float>(K::ArtilleryBombRadius);
            CireCombat::PlayCue(H,nullptr,TEXT("starfall"),H->GetActorLocation(),Center,ECireSpellCue::Impact,2.2f,true);
            FProcGuard Guard;
            ForHostilesNear(H,Center,Radius,[&](AActor* U){CireCombat::ApplyDamage(H,U,static_cast<float>(FMath::Min(Bomb,100000.0)),TEXT("Artillery bomb"));});
            UE_LOG(LogCireKits,Display,TEXT("CIRE_ARTILLERY_BOMB damage=%.0f"),Bomb);
        }
        ArtilleryLastTarget.Remove(H);
    }
    AuraTimer-=Dt;
    if(AuraTimer<=0){AuraTimer=1.f;RefreshAuras();}
}

void UCireKitsSubsystem::RefreshAuras()
{
    UWorld* W=GetWorld();if(!W)return;
    for(TActorIterator<ACireHero> It(W);It;++It)
    {
        ACireHero* H=*It;if(H->IsA<ACireSummon>()||!H->bDrafted||H->bDead)continue;
        const auto T=CireKits::PartyAuras(H);
        const std::pair<K::Aura,double> Rows[]={{K::Aura::AttackSpeed,T.AttackSpeed},{K::Aura::DoubleAttack,T.DoubleAttackChance},{K::Aura::Crit,T.Crit},
            {K::Aura::MagicLifesteal,T.MagicLifesteal},{K::Aura::PhysicalLifesteal,T.PhysicalLifesteal},{K::Aura::Armor,T.Armor},{K::Aura::MagicResist,T.MagicResist},
            {K::Aura::StunIgnore,T.StunIgnoreChance},{K::Aura::AoeResist,T.AoeResistChance},{K::Aura::StunOnHit,T.StunOnHitChance},{K::Aura::RangedDamage,T.RangedDamage}};
        for(const auto& R:Rows)if(R.second>0)CireBuffs::Apply(H,CireKits::AuraBuffId(R.first),1.6f,H);
    }
}
void CireKits::SetRandomProcs(bool bEnabled){GRandomProcs=bEnabled;}
