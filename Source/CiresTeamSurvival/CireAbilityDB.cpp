#include "CireAbilityDB.h"
#include "CireChampionProfiles.h"
#include "CireChampionRoster.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireAbilityDB,Log,All);

namespace
{
TArray<FCireAbilityDef> GAbilities;
TMap<FString,int32> GIndex,GNameIndex;
TMap<FString,FCireChampionKit> GKits;
TMap<FName,TArray<FCireModifier>> GModifiers;
bool GLoaded=false;

void LoadOnce(){if(!GLoaded)CireAbilityDB::Reload();}
float Num(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,float Default=0){double V=Default;return J&&J->TryGetNumberField(Key,V)&&FMath::IsFinite(V)?static_cast<float>(V):Default;}
FString Str(const TSharedPtr<FJsonObject>& J,const TCHAR* Key){FString V;if(J)J->TryGetStringField(Key,V);return V;}
TArray<FString> Strings(const TSharedPtr<FJsonObject>& J,const TCHAR* Key)
{
    TArray<FString> Out;const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
    if(J&&J->TryGetArrayField(Key,A))for(const auto& V:*A){FString S;if(V->TryGetString(S))Out.Add(S);}
    return Out;
}
FString Trim(float V){return FMath::IsNearlyEqual(V,FMath::RoundToFloat(V),.05f)?FString::Printf(TEXT("%.0f"),V):FString::Printf(TEXT("%.1f"),V);}
}

bool CireAbilityDB::ParseJson(const FString& Json,TArray<FCireAbilityDef>& OutAbilities,TMap<FString,FCireChampionKit>& OutKits,
    TMap<FName,TArray<FCireModifier>>& OutModifiers,FString& Error)
{
    auto Fail=[&](const FString& Why){Error=Why;return false;};
    TSharedPtr<FJsonObject> Root;
    if(Json.Len()>4*1024*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid())return Fail(TEXT("Invalid Abilities.json"));
    double Schema=0;if(!Root->TryGetNumberField(TEXT("schemaVersion"),Schema)||Schema!=1)return Fail(TEXT("Expected schemaVersion 1"));
    const TArray<FString> Schools=Strings(Root,TEXT("schools"));
    const TSharedPtr<FJsonObject>* Abilities=nullptr;const TSharedPtr<FJsonObject>* Champions=nullptr;
    if(!Root->TryGetObjectField(TEXT("abilities"),Abilities)||!Root->TryGetObjectField(TEXT("champions"),Champions))return Fail(TEXT("abilities/champions required"));
    TArray<FCireAbilityDef> Parsed;TSet<FString> Seen;
    static const TSet<FString> Kinds={TEXT("active"),TEXT("passive"),TEXT("ultimate")};
    static const TSet<FString> Targets={TEXT("self"),TEXT("ally"),TEXT("enemy"),TEXT("aim"),TEXT("passive")};
    static const TSet<FString> Types={TEXT("DPS"),TEXT("TANK"),TEXT("HEAL")};
    for(const auto& Pair:(*Abilities)->Values)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;if(!Pair.Value->TryGetObject(O)||!O)return Fail(TEXT("ability must be an object"));
        const TSharedPtr<FJsonObject>& J=*O;FCireAbilityDef D;
        D.Id=Str(J,TEXT("id"));D.Name=Str(J,TEXT("name"));D.Icon=Str(J,TEXT("icon"));D.Kind=Str(J,TEXT("kind"));D.School=Str(J,TEXT("school"));
        D.Targeting=Str(J,TEXT("targeting"));D.Status=Str(J,TEXT("status"));D.Description=Str(J,TEXT("description"));D.EffectLabel=Str(J,TEXT("effectLabel"));D.Category=Str(J,TEXT("category"));
        D.Types=Strings(J,TEXT("types"));D.Champions=Strings(J,TEXT("champions"));D.SignatureOf=Strings(J,TEXT("signatureOf"));
        D.CastTime=Num(J,TEXT("castTime"));
        // feat/camera-movement: optional; missing means WoW behaviour (instants move, cast-time spells stand still).
        if(!J->TryGetBoolField(TEXT("castWhileMoving"),D.bCastWhileMoving))D.bCastWhileMoving=D.CastTime<=0;
        if(D.Id!=FString(Pair.Key)||D.Id.IsEmpty()||Seen.Contains(D.Id)||D.Name.IsEmpty()||!Kinds.Contains(D.Kind)||!Schools.Contains(D.School)||
            !Targets.Contains(D.Targeting)||D.Types.IsEmpty()||D.CastTime<0||D.CastTime>10)return Fail(TEXT("Invalid ability identity: ")+D.Id);
        for(const FString& T:D.Types)if(!Types.Contains(T))return Fail(TEXT("Invalid type: ")+D.Id);
        const TSharedPtr<FJsonObject>* B=nullptr;const TSharedPtr<FJsonObject>* C=nullptr;
        if(!J->TryGetObjectField(TEXT("base"),B)||!J->TryGetObjectField(TEXT("curve"),C))return Fail(TEXT("base/curve required: ")+D.Id);
        D.Base.Effect=Num(*B,TEXT("effect"));D.Base.ManaCost=Num(*B,TEXT("manaCost"));D.Base.EnergyCost=Num(*B,TEXT("energyCost"));
        D.Base.Cooldown=Num(*B,TEXT("cooldown"));D.Base.CastTime=D.CastTime;D.Range=Num(*B,TEXT("range"));D.Radius=Num(*B,TEXT("radius"));D.Duration=Num(*B,TEXT("duration"));
        auto& Cv=D.Curve;
        Cv.EffectGrowth=Num(*C,TEXT("effectGrowth"),Cv.EffectGrowth);Cv.EffectHalfLevels=Num(*C,TEXT("effectHalfLevels"),Cv.EffectHalfLevels);
        Cv.EffectCap=Num(*C,TEXT("effectCap"),Cv.EffectCap);Cv.CostCapMultiplier=Num(*C,TEXT("costCapMultiplier"),Cv.CostCapMultiplier);
        Cv.CostRampLevels=Num(*C,TEXT("costRampLevels"),Cv.CostRampLevels);Cv.CooldownFloorFraction=Num(*C,TEXT("cooldownFloorFraction"),Cv.CooldownFloorFraction);
        Cv.CooldownDecayLevels=Num(*C,TEXT("cooldownDecayLevels"),Cv.CooldownDecayLevels);Cv.MinCooldownSeconds=Num(*C,TEXT("minCooldownSeconds"),Cv.MinCooldownSeconds);
        if(!Cires::Abilities::ValidBase(D.Base)||!Cires::Abilities::ValidCurve(D.Curve))return Fail(TEXT("Invalid base numbers or curve: ")+D.Id);
        const TArray<TSharedPtr<FJsonValue>>* Effects=nullptr;
        if(J->TryGetArrayField(TEXT("effects"),Effects))for(const auto& V:*Effects)
        {
            const TSharedPtr<FJsonObject>* E=nullptr;if(!V->TryGetObject(E)||!E)return Fail(TEXT("effect must be an object: ")+D.Id);
            FCireAbilityEffect X;X.Type=FName(*Str(*E,TEXT("type")));X.Zone=FName(*Str(*E,TEXT("zone")));X.Duration=Num(*E,TEXT("duration"));
            X.Magnitude=Num(*E,TEXT("magnitude"));X.Radius=Num(*E,TEXT("radius"));X.LockoutSeconds=Num(*E,TEXT("lockoutSeconds"));X.Label=Str(*E,TEXT("label"));
            if(X.Type.IsNone()||X.Duration<0||X.Duration>60||X.Magnitude<0||X.Magnitude>1||X.Radius<0)return Fail(TEXT("Invalid effect: ")+D.Id);
            D.Effects.Add(X);
        }
        const TSharedPtr<FJsonObject>* Void=nullptr;
        if(J->TryGetObjectField(TEXT("void"),Void))
        {
            auto& Z=D.Void;Z.InnerRadius=Num(*Void,TEXT("innerRadius"));Z.OuterRadius=Num(*Void,TEXT("outerRadius"));Z.InnerDuration=Num(*Void,TEXT("innerDuration"));
            Z.OuterDuration=Num(*Void,TEXT("outerDuration"));Z.OuterMagnitude=Num(*Void,TEXT("outerMagnitude"));Z.Damage=Num(*Void,TEXT("damage"));
            Z.SelfHealMaxHealthFraction=Num(*Void,TEXT("selfHealMaxHealthFraction"));
            Z.bValid=Z.InnerRadius>0&&Z.OuterRadius>Z.InnerRadius&&Z.OuterRadius<=3000&&Z.InnerDuration>=0&&Z.OuterMagnitude>=0&&Z.OuterMagnitude<=1;
            if(!Z.bValid)return Fail(TEXT("Invalid void zone: ")+D.Id);
        }
        Seen.Add(D.Id);Parsed.Add(MoveTemp(D));
    }
    TMap<FString,FCireChampionKit> Kits;
    for(const auto& Pair:(*Champions)->Values)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;if(!Pair.Value->TryGetObject(O)||!O)return Fail(TEXT("champion must be an object"));
        FCireChampionKit K;K.Name=Str(*O,TEXT("name"));K.PrimaryRole=Str(*O,TEXT("primaryRole"));K.Roles=Strings(*O,TEXT("roles"));
        K.Signature=Strings(*O,TEXT("signature"));K.Purchasable=Strings(*O,TEXT("purchasable"));K.PurchasableImplemented=Strings(*O,TEXT("purchasableImplemented"));
        for(const FString& S:K.Purchasable)if(!Seen.Contains(S))return Fail(TEXT("Unknown purchasable skill ")+S+TEXT(" for ")+FString(Pair.Key));
        Kits.Add(FString(Pair.Key),MoveTemp(K));
    }
    TMap<FName,TArray<FCireModifier>> Modifiers;
    const TSharedPtr<FJsonObject>* Mods=nullptr;
    if(Root->TryGetObjectField(TEXT("buffModifiers"),Mods))for(const auto& Pair:(*Mods)->Values)
    {
        // Same row format as Content/Data/BuffModifiers.json (Docs/BuffModifiers.md).
        const TSharedPtr<FJsonObject>* Row=nullptr;if(!Pair.Value->TryGetObject(Row)||!Row)return Fail(TEXT("buffModifiers rows must be objects"));
        auto& List=Modifiers.Add(FName(*FString(Pair.Key)));
        const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
        if((*Row)->TryGetArrayField(TEXT("mods"),A))for(const auto& V:*A)
        {
            const TSharedPtr<FJsonObject>* M=nullptr;if(!V->TryGetObject(M)||!M)continue;
            FCireModifier X;X.Stat=Str(*M,TEXT("stat"));X.Value=Num(*M,TEXT("value"));const FString Unit=Str(*M,TEXT("unit"));
            X.Label=X.Value==0?Str(*Row,TEXT("name")):FString::Printf(TEXT("%s %s%s%s"),*X.Stat,X.Value>0?TEXT("+"):TEXT("-"),*Trim(FMath::Abs(X.Value)),*Unit);
            List.Add(X);
        }
    }
    OutAbilities=MoveTemp(Parsed);OutKits=MoveTemp(Kits);OutModifiers=MoveTemp(Modifiers);Error.Reset();return true;
}

bool CireAbilityDB::Reload()
{
    GLoaded=true;FString Json,Error;TArray<FCireAbilityDef> A;TMap<FString,FCireChampionKit> K;TMap<FName,TArray<FCireModifier>> M;
    const FString Path=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/Abilities.json"));
    if(!FFileHelper::LoadFileToString(Json,*Path)||!ParseJson(Json,A,K,M,Error))
    {UE_LOG(LogCireAbilityDB,Error,TEXT("Ability database rejected; keeping previous: %s"),*Error);return false;}
    GAbilities=MoveTemp(A);GKits=MoveTemp(K);GModifiers=MoveTemp(M);GIndex.Reset();GNameIndex.Reset();
    for(int32 I=0;I<GAbilities.Num();++I){GIndex.Add(GAbilities[I].Id,I);GNameIndex.Add(GAbilities[I].Name,I);}
    UE_LOG(LogCireAbilityDB,Display,TEXT("CIRE_ABILITY_DB_LOADED abilities=%d champions=%d"),GAbilities.Num(),GKits.Num());
    return true;
}

const TArray<FCireAbilityDef>& CireAbilityDB::All(){LoadOnce();return GAbilities;}
const FCireAbilityDef* CireAbilityDB::Find(const FString& Id){LoadOnce();const int32* I=GIndex.Find(Id);return I?&GAbilities[*I]:nullptr;}
const FCireAbilityDef* CireAbilityDB::FindByName(const FString& Name){LoadOnce();const int32* I=GNameIndex.Find(Name);return I?&GAbilities[*I]:nullptr;}
const FCireChampionKit* CireAbilityDB::Kit(const FString& ProfileId){LoadOnce();return GKits.Find(ProfileId);}

FCireAbilityStats CireAbilityDB::EffectiveStats(const FString& Id,int32 Level)
{
    FCireAbilityStats Out;const FCireAbilityDef* D=Find(Id);Out.Level=FMath::Max(1,Level);if(!D)return Out;
    const auto S=Cires::Abilities::Scale(D->Base,D->Curve,Out.Level);
    Out.Effect=static_cast<float>(S.Effect);Out.ManaCost=static_cast<float>(S.ManaCost);Out.EnergyCost=static_cast<float>(S.EnergyCost);
    Out.Cooldown=static_cast<float>(S.Cooldown);Out.CastTime=static_cast<float>(S.CastTime);Out.Range=D->Range;Out.Radius=D->Radius;Out.Duration=D->Duration;
    return Out;
}

FString CireAbilityDB::Describe(const FString& Id,int32 Level)
{
    const FCireAbilityDef* D=Find(Id);if(!D)return FString();
    const FCireAbilityStats Now=EffectiveStats(Id,Level),Next=EffectiveStats(Id,Level+1);
    FString Text=D->Description.Replace(TEXT("{effect}"),*Trim(Now.Effect));
    TArray<FString> Parts;
    Parts.Add(FString::Printf(TEXT("Level %d"),Now.Level));
    Parts.Add(FString::Printf(TEXT("%s %s (+%s next)"),*Trim(Now.Effect),*D->EffectLabel,*Trim(Next.Effect-Now.Effect)));
    if(Now.ManaCost>0)Parts.Add(FString::Printf(TEXT("%s mana (+%s)"),*Trim(Now.ManaCost),*Trim(Next.ManaCost-Now.ManaCost)));
    if(Now.EnergyCost>0)Parts.Add(FString::Printf(TEXT("%s energy (+%s)"),*Trim(Now.EnergyCost),*Trim(Next.EnergyCost-Now.EnergyCost)));
    if(Now.Cooldown>0)Parts.Add(FString::Printf(TEXT("%.1fs cooldown (-%.1fs)"),Now.Cooldown,Now.Cooldown-Next.Cooldown));
    if(Now.CastTime>0)Parts.Add(FString::Printf(TEXT("%.1fs cast"),Now.CastTime));
    return Text+TEXT("\n")+FString::Join(Parts,TEXT("  |  "));
}

TArray<FString> CireAbilityDB::PurchasableSkills(const FString& ProfileId,bool bImplementedOnly)
{
    const FCireChampionKit* K=Kit(ProfileId);if(!K)return {};
    return bImplementedOnly?K->PurchasableImplemented:K->Purchasable;
}
bool CireAbilityDB::CanLearn(const FString& ProfileId,const FString& AbilityId)
{
    const FCireChampionKit* K=Kit(ProfileId);return K&&K->Purchasable.Contains(AbilityId);
}
TArray<FString> CireAbilityDB::OpeningSkills(const FString& ProfileId)
{
    TArray<FString> Out;const FCireChampionProfile* P=CireChampionRoster::Find(ProfileId);if(!P)return Out;
    for(const auto& S:Cires::OpeningSkillPool(CireChampionProfiles::PrimaryRole(*P)))Out.Add(UTF8_TO_TCHAR(S.Id.c_str()));
    return Out;
}
const TArray<FCireModifier>* CireAbilityDB::BuffModifiers(FName BuffId){LoadOnce();return GModifiers.Find(BuffId);}
FString CireAbilityDB::ModifierSummary(FName BuffId)
{
    const auto* M=BuffModifiers(BuffId);if(!M)return FString();
    TArray<FString> Labels;for(const auto& X:*M)Labels.Add(X.Label);return FString::Join(Labels,TEXT(", "));
}

#if !UE_BUILD_SHIPPING
bool CireAbilityDB::RunSmoke()
{
    int32 Checks=0;bool bPass=true;
    const auto Check=[&](bool b,const TCHAR* Why){++Checks;if(!b){bPass=false;UE_LOG(LogCireAbilityDB,Error,TEXT("CIRE_ABILITY_DB_FAIL %s"),Why);}};
    Check(Reload()&&All().Num()>=100,TEXT("database loads 100+ abilities"));
    for(const auto& S:Cires::StarterSkillPool())Check(Find(UTF8_TO_TCHAR(S.Id.c_str()))&&Find(UTF8_TO_TCHAR(S.Id.c_str()))->IsImplemented(),TEXT("every pool skill has an implemented row"));
    for(const auto& P:CireChampionRoster::All())
    {
        Check(Kit(P.Id)&&PurchasableSkills(P.Id).Num()>=8,TEXT("every champion has a purchasable kit"));
        for(const FString& Id:OpeningSkills(P.Id))Check(CanLearn(P.Id,Id),TEXT("opening skills are purchasable"));
    }
    const auto L1=EffectiveStats(TEXT("ember_lance"),1),L10=EffectiveStats(TEXT("ember_lance"),10),L200=EffectiveStats(TEXT("ember_lance"),200);
    Check(L10.Effect>L1.Effect&&L200.Effect>L10.Effect&&L10.ManaCost>=L1.ManaCost&&L10.Cooldown<L1.Cooldown&&L200.Cooldown>=L1.Cooldown*.6f-1e-3f,TEXT("scaling monotone with floor"));
    Check(Find(TEXT("shadow_step"))&&Find(TEXT("shadow_step"))->Void.bValid,TEXT("shadow step carries void zones"));
    Check(Describe(TEXT("restoring_light"),3).Contains(TEXT("next")),TEXT("describe shows next level"));
    Check(ModifierSummary(TEXT("armor_broken"))==TEXT("Armor -50%"),TEXT("modifier summaries"));
    Check(FindByName(TEXT("Blight Sigil"))==Find(TEXT("blight_sigil")),TEXT("lookup by display name"));
    UE_LOG(LogCireAbilityDB,Display,TEXT("CIRE_ABILITY_DB_%s checks=%d abilities=%d"),bPass?TEXT("PASS"):TEXT("FAIL"),Checks,All().Num());
    return bPass;
}
#endif
