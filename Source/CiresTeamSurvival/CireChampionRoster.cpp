#include "CireChampionRoster.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireChampionRoster,Log,All);
namespace
{
TArray<FCireChampionProfile> Current;
bool bLoaded=false;
bool Keys(const TSharedPtr<FJsonObject>& J,const TSet<FString>& Allowed)
{
    if(!J.IsValid())return false;
    for(const auto& P:J->Values)if(!Allowed.Contains(FString(P.Key.ToView())))return false;
    return true;
}
bool Text(const TSharedPtr<FJsonObject>& J,const TCHAR* K,FString& V,int32 Max=160)
{
    if(!J->TryGetStringField(K,V)||V.IsEmpty()||V.Len()>Max)return false;
    for(TCHAR C:V)if(C<32)return false;
    return true;
}
bool Id(const TSharedPtr<FJsonObject>& J,const TCHAR* K,FString& V)
{
    if(!Text(J,K,V,64)||V[0]<'a'||V[0]>'z')return false;
    for(TCHAR C:V)if(!(C>='a'&&C<='z')&&!(C>='0'&&C<='9')&&C!='_')return false;
    return true;
}
bool Integer(const TSharedPtr<FJsonObject>& J,const TCHAR* K,int32& V,int32 Min,int32 Max)
{
    double N=0;if(!J->TryGetNumberField(K,N)||!FMath::IsFinite(N)||N<Min||N>Max||N!=FMath::FloorToDouble(N))return false;
    V=static_cast<int32>(N);return true;
}
bool Number(const TSharedPtr<FJsonObject>& J,const TCHAR* K,float& V,float Min,float Max)
{
    double N=0;if(!J->TryGetNumberField(K,N)||!FMath::IsFinite(N)||N<Min||N>Max)return false;
    V=static_cast<float>(N);return true;
}
bool Object(const TSharedPtr<FJsonObject>& J,const TCHAR* K,TSharedPtr<FJsonObject>& Out)
{
    const TSharedPtr<FJsonObject>* P=nullptr;if(!J->TryGetObjectField(K,P)||!P||!P->IsValid())return false;
    Out=*P;return true;
}
bool Skill(const TSharedPtr<FJsonObject>& J,FCireChampionSkill& S)
{
    static const TSet<FString> Deliveries={TEXT("targeted"),TEXT("ground_circle"),TEXT("ground_cone"),TEXT("ground_line"),TEXT("ground_square"),TEXT("ground_polygon"),TEXT("self"),TEXT("ally"),TEXT("chain"),TEXT("projectile"),TEXT("summon"),TEXT("construct"),TEXT("transformation"),TEXT("passive")};
    return Keys(J,{TEXT("id"),TEXT("displayName"),TEXT("status"),TEXT("mechanic"),TEXT("vfxFamily"),TEXT("delivery")})&&
        Id(J,TEXT("id"),S.Id)&&Text(J,TEXT("displayName"),S.DisplayName,80)&&
        Text(J,TEXT("status"),S.Status,16)&&(S.Status==TEXT("implemented")||S.Status==TEXT("planned"))&&
        Text(J,TEXT("mechanic"),S.Mechanic,800)&&Id(J,TEXT("vfxFamily"),S.VfxFamily)&&
        Text(J,TEXT("delivery"),S.Delivery,32)&&Deliveries.Contains(S.Delivery);
}
void LoadOnce(){if(!bLoaded)CireChampionRoster::Reload();}
}
bool CireChampionRoster::ParseJson(const FString& Json,TArray<FCireChampionProfile>& Out,FString& Error)
{
    auto Fail=[&](const FString& Why){Error=Why;return false;};
    if(Json.Len()>512*1024)return Fail(TEXT("Champion roster exceeds 512 KB"));
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid())return Fail(TEXT("Invalid champion roster JSON"));
    int32 Schema=0;FString Profile,Engine,Version,StatPolicy;TSharedPtr<FJsonObject> Source;
    if(!Keys(Root,{TEXT("schemaVersion"),TEXT("profile"),TEXT("engine"),TEXT("engineVersion"),TEXT("statPolicy"),TEXT("source"),TEXT("champions")})||
        !Integer(Root,TEXT("schemaVersion"),Schema,1,1)||!Text(Root,TEXT("profile"),Profile)||Profile!=TEXT("CireChampionRoster")||
        !Text(Root,TEXT("engine"),Engine)||Engine!=TEXT("Unreal")||!Text(Root,TEXT("engineVersion"),Version)||Version!=TEXT("5.8.3")||
        !Text(Root,TEXT("statPolicy"),StatPolicy)||StatPolicy!=TEXT("existing_stat_per_point")||!Object(Root,TEXT("source"),Source))return Fail(TEXT("Expected CireChampionRoster schema 1 for UE5.8.3 with existing stat formulas"));
    FString SourcePath,Encoding,Hash;
    if(!Keys(Source,{TEXT("path"),TEXT("encoding"),TEXT("sha256")})||!Text(Source,TEXT("path"),SourcePath,512)||
        !Text(Source,TEXT("encoding"),Encoding,32)||Encoding!=TEXT("windows-1252")||!Text(Source,TEXT("sha256"),Hash,64)||Hash.Len()!=64)return Fail(TEXT("Invalid source provenance"));
    for(TCHAR C:Hash)if(!(C>='0'&&C<='9')&&!(C>='a'&&C<='f'))return Fail(TEXT("Invalid source SHA256"));
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!Root->TryGetArrayField(TEXT("champions"),Values)||Values->Num()<1||Values->Num()>64)return Fail(TEXT("Roster requires 1..64 champions"));
    TArray<FCireChampionProfile> Candidate;TSet<FString> Seen;
    const TSet<FString> Styles={TEXT("sword"),TEXT("bow"),TEXT("arcane"),TEXT("lance"),TEXT("claws"),TEXT("axes"),TEXT("flail"),TEXT("staff"),TEXT("totem")};
    const TSet<FString> Roles={TEXT("tank"),TEXT("damage"),TEXT("healer"),TEXT("support")};
    for(const auto& V:*Values)
    {
        const TSharedPtr<FJsonObject>* P=nullptr;if(!V->TryGetObject(P)||!P||!P->IsValid())return Fail(TEXT("Champion must be an object"));
        auto J=*P;FCireChampionProfile C;
        if(!Keys(J,{TEXT("id"),TEXT("displayName"),TEXT("familyId"),TEXT("variant"),TEXT("description"),TEXT("runtimeArchetype"),TEXT("primaryStat"),TEXT("strength"),TEXT("agility"),TEXT("intelligence"),TEXT("basicAttackRange"),TEXT("attackSeconds"),TEXT("attackStyle"),TEXT("threatRole"),TEXT("roles"),TEXT("artFamily"),TEXT("artStatus"),TEXT("artProvenance"),TEXT("startsWithSkills"),TEXT("actives"),TEXT("passive"),TEXT("ultimate")})||
            !Id(J,TEXT("id"),C.Id)||Seen.Contains(C.Id)||!Id(J,TEXT("familyId"),C.FamilyId)||
            !Text(J,TEXT("displayName"),C.DisplayName,80)||!Text(J,TEXT("variant"),C.Variant,64)||!Text(J,TEXT("description"),C.Description,800)||
            !Integer(J,TEXT("runtimeArchetype"),C.RuntimeArchetype,0,4)||!Text(J,TEXT("primaryStat"),C.PrimaryStat,16)||
            !(C.PrimaryStat==TEXT("strength")||C.PrimaryStat==TEXT("agility")||C.PrimaryStat==TEXT("intelligence"))||
            !Integer(J,TEXT("strength"),C.Strength,1,100)||!Integer(J,TEXT("agility"),C.Agility,1,100)||!Integer(J,TEXT("intelligence"),C.Intelligence,1,100)||
            !Number(J,TEXT("basicAttackRange"),C.BasicAttackRange,80,1500)||!Number(J,TEXT("attackSeconds"),C.AttackSeconds,.5f,4)||
            !Text(J,TEXT("attackStyle"),C.AttackStyle,16)||!Styles.Contains(C.AttackStyle)||
            !Text(J,TEXT("threatRole"),C.ThreatRole,16)||!(C.ThreatRole==TEXT("tank")||C.ThreatRole==TEXT("damage")||C.ThreatRole==TEXT("healer"))||
            !Text(J,TEXT("artFamily"),C.ArtFamily,160)||!Text(J,TEXT("artStatus"),C.ArtStatus,32)||
            !(C.ArtStatus==TEXT("prototype_fallback")||C.ArtStatus==TEXT("generated_draft")||C.ArtStatus==TEXT("approved_asset"))||
            !Text(J,TEXT("artProvenance"),C.ArtProvenance,800))return Fail(TEXT("Invalid champion identity, stats or appearance: ")+C.Id);
        const TArray<TSharedPtr<FJsonValue>>* RoleValues=nullptr;const TArray<TSharedPtr<FJsonValue>>* StartSkills=nullptr;const TArray<TSharedPtr<FJsonValue>>* Actives=nullptr;
        if(!J->TryGetArrayField(TEXT("roles"),RoleValues)||RoleValues->Num()<1||RoleValues->Num()>4||
            !J->TryGetArrayField(TEXT("startsWithSkills"),StartSkills)||StartSkills->Num()!=0||
            !J->TryGetArrayField(TEXT("actives"),Actives)||Actives->Num()!=6)return Fail(TEXT("Roles, empty starting skills and six active draft examples required: ")+C.Id);
        for(const auto& R:*RoleValues){FString Role;if(!R->TryGetString(Role)||!Roles.Contains(Role)||C.Roles.Contains(Role))return Fail(TEXT("Invalid/duplicate role: ")+C.Id);C.Roles.Add(Role);}
        if(!C.Roles.Contains(C.ThreatRole)&&!(C.ThreatRole==TEXT("healer")&&C.Roles.Contains(TEXT("support"))))return Fail(TEXT("Threat role must agree with champion role: ")+C.Id);
        TSet<FString> SkillIds;
        for(const auto& A:*Actives)
        {
            const TSharedPtr<FJsonObject>* SkillObject=nullptr;FCireChampionSkill S;
            if(!A->TryGetObject(SkillObject)||!SkillObject||!SkillObject->IsValid()||!Skill(*SkillObject,S)||SkillIds.Contains(S.Id))return Fail(TEXT("Invalid/duplicate active draft skill: ")+C.Id);
            SkillIds.Add(S.Id);C.Actives.Add(MoveTemp(S));
        }
        TSharedPtr<FJsonObject> Passive,Ultimate;
        if(!Object(J,TEXT("passive"),Passive)||!Object(J,TEXT("ultimate"),Ultimate)||!Skill(Passive,C.Passive)||!Skill(Ultimate,C.Ultimate)||
            SkillIds.Contains(C.Passive.Id)||SkillIds.Contains(C.Ultimate.Id)||C.Passive.Id==C.Ultimate.Id)return Fail(TEXT("Invalid/duplicate passive or ultimate draft skill: ")+C.Id);
        Seen.Add(C.Id);Candidate.Add(MoveTemp(C));
    }
    Out=MoveTemp(Candidate);Error.Reset();return true;
}
bool CireChampionRoster::Reload()
{
    bLoaded=true;FString Json,Error;TArray<FCireChampionProfile> Candidate;
    const FString Path=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/ChampionRoster.json"));
    if(!FFileHelper::LoadFileToString(Json,*Path)||!ParseJson(Json,Candidate,Error))
    {UE_LOG(LogCireChampionRoster,Error,TEXT("Roster rejected; retaining last valid profiles: %s (%s)"),*Error,*Path);return false;}
    Current=MoveTemp(Candidate);UE_LOG(LogCireChampionRoster,Display,TEXT("CIRE_CHAMPION_ROSTER_LOADED count=%d"),Current.Num());return true;
}
const TArray<FCireChampionProfile>& CireChampionRoster::All(){LoadOnce();return Current;}
const FCireChampionProfile* CireChampionRoster::Find(const FString& Id){for(const auto& C:All())if(C.Id==Id)return &C;return nullptr;}
const FCireChampionProfile* CireChampionRoster::FindByIndex(int32 Index){const auto& Profiles=All();return Profiles.IsValidIndex(Index)?&Profiles[Index]:nullptr;}
int32 CireChampionRoster::Count(){return All().Num();}
bool CireChampionRoster::RunValidationSmoke()
{
    FString Json,Error;TArray<FCireChampionProfile> Parsed;
    if(!FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/ChampionRoster.json")))||!ParseJson(Json,Parsed,Error))return false;
    const int32 Expected=Parsed.Num();if(Expected<5)return false;
    // Failed parse must never replace a valid roster.
    if(ParseJson(TEXT("{}"),Parsed,Error)||Parsed.Num()!=Expected)return false;
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root))return false;
    const TArray<TSharedPtr<FJsonValue>>* Champions=nullptr;if(!Root->TryGetArrayField(TEXT("champions"),Champions))return false;
    auto First=(*Champions)[0]->AsObject();First->SetArrayField(TEXT("startsWithSkills"),{MakeShared<FJsonValueString>(TEXT("shield_slam"))});
    FString Invalid;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Invalid));
    if(ParseJson(Invalid,Parsed,Error)||Parsed.Num()!=Expected)return false;
    UE_LOG(LogCireChampionRoster,Display,TEXT("CIRE_CHAMPION_ROSTER_VALIDATION PASS profiles=%d transactional=1 starts_empty=1"),Expected);return true;
}
