#include "CireNPCArchetypes.h"
#include "CireSkillTuning.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireNPCData,Log,All);

namespace
{
FCireNPCDatabase Database;
bool bLoaded=false;

bool ParseEnum(const FString& Text,ECireNPCRole& Out)
{
    if(Text==TEXT("bruiser"))Out=ECireNPCRole::Bruiser;else if(Text==TEXT("tank"))Out=ECireNPCRole::Tank;
    else if(Text==TEXT("caster"))Out=ECireNPCRole::Caster;else if(Text==TEXT("ranged"))Out=ECireNPCRole::Ranged;else return false;
    return true;
}
bool ParseEnum(const FString& Text,ECireNPCClass& Out)
{
    if(Text==TEXT("normal"))Out=ECireNPCClass::Normal;else if(Text==TEXT("elite"))Out=ECireNPCClass::Elite;
    else if(Text==TEXT("boss"))Out=ECireNPCClass::Boss;else return false;
    return true;
}
bool ParseEnum(const FString& Text,ECireNPCAbilityKind& Out)
{
    static const TMap<FString,ECireNPCAbilityKind> Kinds={
        {TEXT("melee"),ECireNPCAbilityKind::Melee},{TEXT("projectile"),ECireNPCAbilityKind::Projectile},
        {TEXT("cone"),ECireNPCAbilityKind::Cone},{TEXT("targetCircle"),ECireNPCAbilityKind::TargetCircle},
        {TEXT("selfCircle"),ECireNPCAbilityKind::SelfCircle},{TEXT("charge"),ECireNPCAbilityKind::Charge},
        {TEXT("guard"),ECireNPCAbilityKind::Guard},{TEXT("provoke"),ECireNPCAbilityKind::Provoke},
        {TEXT("rally"),ECireNPCAbilityKind::Rally},{TEXT("enrage"),ECireNPCAbilityKind::Enrage},
        {TEXT("healAlly"),ECireNPCAbilityKind::HealAlly},{TEXT("shieldWall"),ECireNPCAbilityKind::ShieldWall},
        {TEXT("disengage"),ECireNPCAbilityKind::Disengage}};
    if(const auto* Found=Kinds.Find(Text)){Out=*Found;return true;}
    return false;
}
// Bounded numeric read: missing keys keep the default, present keys must be finite and in range.
bool Number(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,float& Value,float Min,float Max,FString& Error,const FString& Where)
{
    if(!O->HasField(Key))return true;
    double V=0;
    if(!O->TryGetNumberField(Key,V)||!FMath::IsFinite(V)||V<Min||V>Max)
    {Error=FString::Printf(TEXT("%s.%s must be a number in [%g, %g]"),*Where,Key,Min,Max);return false;}
    Value=static_cast<float>(V);return true;
}
bool Color(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,FLinearColor& Value,FString& Error,const FString& Where)
{
    if(!O->HasField(Key))return true;
    const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
    if(!O->TryGetArrayField(Key,Items)||(Items->Num()!=3&&Items->Num()!=4))
    {Error=FString::Printf(TEXT("%s.%s must be [r,g,b(,a)]"),*Where,Key);return false;}
    float C[4]={0,0,0,1};
    for(int32 I=0;I<Items->Num();++I)
    {
        double V=0;if(!(*Items)[I]->TryGetNumber(V)||V<0||V>1){Error=FString::Printf(TEXT("%s.%s channels must be in [0,1]"),*Where,Key);return false;}
        C[I]=static_cast<float>(V);
    }
    Value=FLinearColor(C[0],C[1],C[2],C[3]);return true;
}
bool SafeAsset(const FString& Path)
{
    return Path.IsEmpty()||((Path.StartsWith(TEXT("/Game/"))||Path.StartsWith(TEXT("/Engine/")))&&!Path.Contains(TEXT(".."))&&!Path.Contains(TEXT("\\")));
}
bool Names(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,TArray<FName>& Out,FString& Error)
{
    Out.Reset();if(!O->HasField(Key))return true;
    const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
    if(!O->TryGetArrayField(Key,Items)){Error=FString::Printf(TEXT("%s must be an array of archetype ids"),Key);return false;}
    for(const auto& Item:*Items){FString S;if(!Item->TryGetString(S)||S.IsEmpty()){Error=FString::Printf(TEXT("%s contains a non-string id"),Key);return false;}Out.Add(FName(*S));}
    return true;
}
bool ParseAbility(const TSharedPtr<FJsonObject>& O,FCireNPCAbility& A,FString& Error,const FString& Where)
{
    FString Id,Kind;
    if(!O->TryGetStringField(TEXT("id"),Id)||Id.IsEmpty()||!O->TryGetStringField(TEXT("name"),A.Name)||A.Name.IsEmpty()||
        !O->TryGetStringField(TEXT("description"),A.Description)||!O->TryGetStringField(TEXT("type"),Kind)||!ParseEnum(Kind,A.Kind))
    {Error=Where+TEXT(" needs id, name, description and a known type");return false;}
    A.Id=FName(*Id);
    O->TryGetBoolField(TEXT("basic"),A.bBasic);O->TryGetBoolField(TEXT("interruptible"),A.bInterruptible);
    O->TryGetStringField(TEXT("targeting"),A.Targeting);O->TryGetStringField(TEXT("skillshot"),A.Skillshot);
    if(A.Targeting!=TEXT("victim")&&A.Targeting!=TEXT("farthest")){Error=Where+TEXT(".targeting must be victim or farthest");return false;}
    const bool bNumbers=Number(O,TEXT("cooldown"),A.Cooldown,0,600,Error,Where)&&Number(O,TEXT("castTime"),A.CastTime,0,10,Error,Where)&&
        Number(O,TEXT("range"),A.Range,0,5000,Error,Where)&&Number(O,TEXT("minRange"),A.MinRange,0,5000,Error,Where)&&
        Number(O,TEXT("radius"),A.Radius,10,3000,Error,Where)&&Number(O,TEXT("angle"),A.Angle,5,360,Error,Where)&&
        Number(O,TEXT("length"),A.Length,50,5000,Error,Where)&&Number(O,TEXT("width"),A.Width,20,2000,Error,Where)&&
        Number(O,TEXT("damageMultiplier"),A.DamageMultiplier,0,50,Error,Where)&&Number(O,TEXT("damagePerSecond"),A.DamagePerSecond,0,10000,Error,Where)&&
        Number(O,TEXT("duration"),A.Duration,0,120,Error,Where)&&Number(O,TEXT("magnitude"),A.Magnitude,0,5,Error,Where)&&
        Number(O,TEXT("healthThreshold"),A.HealthThreshold,0,1,Error,Where)&&Number(O,TEXT("initialCooldown"),A.InitialCooldown,0,600,Error,Where)&&
        Color(O,TEXT("color"),A.Color,Error,Where);
    if(!bNumbers)return false;
    if(A.MinRange>A.Range){Error=Where+TEXT(".minRange exceeds range");return false;}
    if(A.Kind==ECireNPCAbilityKind::Projectile&&A.Skillshot.IsEmpty()){Error=Where+TEXT(" projectile needs a skillshot id");return false;}
    if((A.Kind==ECireNPCAbilityKind::Cone||A.Kind==ECireNPCAbilityKind::TargetCircle||A.Kind==ECireNPCAbilityKind::SelfCircle||
        A.Kind==ECireNPCAbilityKind::Charge)&&A.CastTime<.2f){Error=Where+TEXT(" telegraphed abilities need castTime >= 0.2s");return false;}
    if((A.Kind==ECireNPCAbilityKind::Enrage||A.Kind==ECireNPCAbilityKind::ShieldWall)&&A.HealthThreshold<=0)
    {Error=Where+TEXT(" needs healthThreshold");return false;}
    if((A.Kind==ECireNPCAbilityKind::Guard||A.Kind==ECireNPCAbilityKind::Provoke||A.Kind==ECireNPCAbilityKind::Rally||
        A.Kind==ECireNPCAbilityKind::ShieldWall)&&(A.Duration<=0||A.Magnitude<=0||A.Magnitude>(A.Kind==ECireNPCAbilityKind::Rally?2.f:.9f)))
    {Error=Where+TEXT(" needs duration > 0 and a bounded magnitude");return false;}
    return true;
}
bool ParseArchetype(const FString& Key,const TSharedPtr<FJsonObject>& O,FCireNPCArchetype& A,FString& Error)
{
    const FString Where=TEXT("archetypes.")+Key;FString Role,Class;
    if(!O->TryGetStringField(TEXT("displayName"),A.DisplayName)||A.DisplayName.IsEmpty()||
        !O->TryGetStringField(TEXT("role"),Role)||!ParseEnum(Role,A.Role)||
        !O->TryGetStringField(TEXT("classification"),Class)||!ParseEnum(Class,A.Classification))
    {Error=Where+TEXT(" needs displayName, role (bruiser|tank|caster|ranged) and classification (normal|elite|boss)");return false;}
    A.Id=FName(*Key);
    FString Tuning;
    if(O->TryGetStringField(TEXT("tuningKind"),Tuning))
    {
        const int32 Index=TArray<FString>{TEXT("basic"),TEXT("bruiser"),TEXT("caster"),TEXT("ranged")}.IndexOfByKey(Tuning);
        if(Index==INDEX_NONE){Error=Where+TEXT(".tuningKind must be basic|bruiser|caster|ranged");return false;}
        A.TuningKind=Index;
    }
    double Leak=1;
    if(O->TryGetNumberField(TEXT("leakCost"),Leak)){if(Leak<1||Leak>100){Error=Where+TEXT(".leakCost out of range");return false;}A.LeakCost=static_cast<int32>(Leak);}
    if(!(Number(O,TEXT("healthMultiplier"),A.HealthMultiplier,.05f,100,Error,Where)&&Number(O,TEXT("damage"),A.Damage,0,10000,Error,Where)&&
        Number(O,TEXT("eliteDamageMultiplier"),A.EliteDamageMultiplier,0,20,Error,Where)&&Number(O,TEXT("moveSpeed"),A.MoveSpeed,50,1200,Error,Where)&&
        Number(O,TEXT("armor"),A.Armor,0,.8f,Error,Where)&&Number(O,TEXT("attackRange"),A.AttackRange,80,2500,Error,Where)&&
        Number(O,TEXT("attackInterval"),A.AttackInterval,.3f,10,Error,Where)&&Number(O,TEXT("preferredRange"),A.PreferredRange,0,2500,Error,Where)&&
        Number(O,TEXT("kiteRange"),A.KiteRange,0,2000,Error,Where)&&Number(O,TEXT("scale"),A.Scale,.4f,3,Error,Where)))return false;
    if(const TSharedPtr<FJsonObject>* Mesh=nullptr;O->TryGetObjectField(TEXT("mesh"),Mesh))
    {
        (*Mesh)->TryGetStringField(TEXT("slot"),A.MeshSlot);(*Mesh)->TryGetStringField(TEXT("path"),A.MeshPath);
        (*Mesh)->TryGetStringField(TEXT("material"),A.MaterialPath);
        if(!Number(*Mesh,TEXT("scale"),A.MeshScale,.01f,100,Error,Where+TEXT(".mesh"))||!Number(*Mesh,TEXT("yaw"),A.MeshYaw,-360,360,Error,Where+TEXT(".mesh"))||
            !Color(*Mesh,TEXT("tint"),A.Tint,Error,Where+TEXT(".mesh")))return false;
        if(!SafeAsset(A.MeshPath)||!SafeAsset(A.MaterialPath)){Error=Where+TEXT(".mesh paths must be /Game/ or /Engine/ object paths");return false;}
    }
    if(const TArray<TSharedPtr<FJsonValue>>* Props=nullptr;O->TryGetArrayField(TEXT("props"),Props))
        for(const auto& Value:*Props)
        {
            const TSharedPtr<FJsonObject>* P=nullptr;FCireNPCProp Prop;FString Bone;
            if(!Value->TryGetObject(P)||!(*P)->TryGetStringField(TEXT("asset"),Prop.Asset)||!SafeAsset(Prop.Asset)||Prop.Asset.IsEmpty())
            {Error=Where+TEXT(".props entries need a safe asset path");return false;}
            if((*P)->TryGetStringField(TEXT("bone"),Bone))Prop.Bone=FName(*Bone);
            float X=0,Y=0,Z=0,Pitch=0,Yaw=0,Roll=0;
            if(!Number(*P,TEXT("x"),X,-500,500,Error,Where)||!Number(*P,TEXT("y"),Y,-500,500,Error,Where)||!Number(*P,TEXT("z"),Z,-500,500,Error,Where)||
                !Number(*P,TEXT("pitch"),Pitch,-360,360,Error,Where)||!Number(*P,TEXT("yaw"),Yaw,-360,360,Error,Where)||
                !Number(*P,TEXT("roll"),Roll,-360,360,Error,Where)||!Number(*P,TEXT("scale"),Prop.Scale,.01f,20,Error,Where))return false;
            Prop.Offset=FVector(X,Y,Z);Prop.Rotation=FRotator(Pitch,Yaw,Roll);A.Props.Add(Prop);
        }
    const TArray<TSharedPtr<FJsonValue>>* Abilities=nullptr;
    if(!O->TryGetArrayField(TEXT("abilities"),Abilities)||Abilities->IsEmpty()){Error=Where+TEXT(" needs a non-empty abilities array");return false;}
    int32 Basics=0;
    for(int32 I=0;I<Abilities->Num();++I)
    {
        const TSharedPtr<FJsonObject>* AO=nullptr;FCireNPCAbility Ability;
        if(!(*Abilities)[I]->TryGetObject(AO)||!ParseAbility(*AO,Ability,Error,FString::Printf(TEXT("%s.abilities[%d]"),*Where,I)))
        {if(Error.IsEmpty())Error=Where+TEXT(" ability must be an object");return false;}
        if(A.FindAbility(Ability.Id)){Error=Where+TEXT(" has duplicate ability id ")+Ability.Id.ToString();return false;}
        Basics+=Ability.bBasic?1:0;A.Abilities.Add(Ability);
    }
    if(Basics!=1){Error=Where+TEXT(" needs exactly one basic attack");return false;}
    const auto* Basic=A.BasicAttack();
    if(Basic->Kind!=ECireNPCAbilityKind::Melee&&Basic->Kind!=ECireNPCAbilityKind::Projectile){Error=Where+TEXT(" basic attack must be melee or projectile");return false;}
    const bool bRangedRole=A.Role==ECireNPCRole::Caster||A.Role==ECireNPCRole::Ranged;
    if(bRangedRole!=(Basic->Kind==ECireNPCAbilityKind::Projectile)){Error=Where+TEXT(" casters/ranged use projectile basics; tanks/bruisers use melee");return false;}
    if(bRangedRole&&(A.PreferredRange<=A.KiteRange||A.PreferredRange>A.AttackRange)){Error=Where+TEXT(" needs kiteRange < preferredRange <= attackRange");return false;}
    return true;
}
FCireNPCDatabase Fallback()
{
    // Only used if the authored document is missing or invalid; logs an error
    // so automated checks fail loudly while the game keeps running.
    FCireNPCDatabase D;FCireNPCArchetype A;A.Id=TEXT("fallback_bruiser");A.DisplayName=TEXT("Hollow Infantry");
    FCireNPCAbility Swing;Swing.Id=TEXT("npc_melee");Swing.Name=TEXT("Strike");Swing.Description=TEXT("A weapon swing.");Swing.bBasic=true;
    A.Abilities.Add(Swing);D.Archetypes.Add(A.Id,A);D.LegacyKinds={A.Id,A.Id,A.Id,A.Id};D.WaveComposition={A.Id};
    D.WaveBoss=A.Id;D.PackMembers={A.Id};D.PackLeader=A.Id;return D;
}
}

const FCireNPCAbility* FCireNPCArchetype::FindAbility(FName AbilityId) const
{
    return Abilities.FindByPredicate([AbilityId](const FCireNPCAbility& A){return A.Id==AbilityId;});
}
const FCireNPCAbility* FCireNPCArchetype::BasicAttack() const
{
    return Abilities.FindByPredicate([](const FCireNPCAbility& A){return A.bBasic;});
}

bool CireNPCArchetypes::ParseJson(const FString& Json,FCireNPCDatabase& Out,FString& Error)
{
    Error.Reset();FCireNPCDatabase D;TSharedPtr<FJsonObject> Root;
    if(Json.Len()>512*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid())
    {Error=TEXT("NPCArchetypes.json is not a bounded JSON object");return false;}
    double Version=0;
    if(!Root->TryGetNumberField(TEXT("schemaVersion"),Version)||Version!=1){Error=TEXT("schemaVersion must be 1");return false;}
    const TSharedPtr<FJsonObject>* Archetypes=nullptr;
    if(!Root->TryGetObjectField(TEXT("archetypes"),Archetypes)||(*Archetypes)->Values.IsEmpty()){Error=TEXT("archetypes object is required");return false;}
    for(const auto& Pair:(*Archetypes)->Values)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FCireNPCArchetype A;const FString Key(Pair.Key);
        if(!Pair.Value->TryGetObject(O)){Error=FString(TEXT("archetype "))+Key+TEXT(" must be an object");return false;}
        if(!ParseArchetype(Key,*O,A,Error))return false;
        D.Archetypes.Add(A.Id,A);
    }
    FString Boss,Leader;
    if(!Names(Root,TEXT("legacyKinds"),D.LegacyKinds,Error)||!Names(Root,TEXT("waveComposition"),D.WaveComposition,Error))return false;
    if(D.LegacyKinds.Num()!=4){Error=TEXT("legacyKinds must list exactly four ids (infantry, bruiser, caster, ranged)");return false;}
    if(D.WaveComposition.IsEmpty()){Error=TEXT("waveComposition must not be empty");return false;}
    if(!Root->TryGetStringField(TEXT("waveBoss"),Boss)){Error=TEXT("waveBoss is required");return false;}
    D.WaveBoss=FName(*Boss);
    const TSharedPtr<FJsonObject>* Packs=nullptr;
    if(!Root->TryGetObjectField(TEXT("challengePacks"),Packs)||!Names(*Packs,TEXT("members"),D.PackMembers,Error)||D.PackMembers.IsEmpty()||
        D.PackMembers.Num()>6||!(*Packs)->TryGetStringField(TEXT("leader"),Leader))
    {if(Error.IsEmpty())Error=TEXT("challengePacks needs 1-6 members and a leader");return false;}
    D.PackLeader=FName(*Leader);
    double FromTier=1;(*Packs)->TryGetNumberField(TEXT("leaderFromTier"),FromTier);
    if(FromTier<1||FromTier>10){Error=TEXT("challengePacks.leaderFromTier must be 1..10");return false;}
    D.PackLeaderFromTier=static_cast<int32>(FromTier);
    auto Known=[&](FName Id){return D.Archetypes.Contains(Id);};
    for(const TArray<FName>* List:{&D.LegacyKinds,&D.WaveComposition,&D.PackMembers})
        for(FName Id:*List)if(!Known(Id)){Error=TEXT("unknown archetype id ")+Id.ToString();return false;}
    if(!Known(D.WaveBoss)||!Known(D.PackLeader)){Error=TEXT("waveBoss/challengePacks.leader must name archetypes");return false;}
    if(D.Archetypes[D.WaveBoss].Classification!=ECireNPCClass::Boss||D.Archetypes[D.PackLeader].Classification!=ECireNPCClass::Boss)
    {Error=TEXT("waveBoss and pack leader must be boss classification");return false;}
    if(const TSharedPtr<FJsonObject>* T=nullptr;Root->TryGetObjectField(TEXT("threat"),T))
    {
        auto& R=D.Threat;const FString W=TEXT("threat");
        if(!(Number(*T,TEXT("meleePullRatio"),R.MeleePullRatio,1,3,Error,W)&&Number(*T,TEXT("rangedPullRatio"),R.RangedPullRatio,1,3,Error,W)&&
            Number(*T,TEXT("meleeRangeCm"),R.MeleeRangeCm,50,1500,Error,W)&&Number(*T,TEXT("decayDelaySeconds"),R.DecayDelaySeconds,0,120,Error,W)&&
            Number(*T,TEXT("decayPerSecond"),R.DecayPerSecond,0,1,Error,W)&&Number(*T,TEXT("publishInterval"),R.PublishInterval,.05f,5,Error,W)&&
            Number(*T,TEXT("tauntMaxSeconds"),R.TauntMaxSeconds,.5f,30,Error,W)))return false;
    }
    Out=MoveTemp(D);return true;
}

bool CireNPCArchetypes::Reload(FString* OutError)
{
    FString Json,Error;const FString Path=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/NPCArchetypes.json"));
    FCireNPCDatabase Candidate;
    if(!FFileHelper::LoadFileToString(Json,*Path))Error=TEXT("Cannot read Content/Data/NPCArchetypes.json");
    else ParseJson(Json,Candidate,Error);
    if(!Error.IsEmpty())
    {
        UE_LOG(LogCireNPCData,Error,TEXT("CIRE_NPC_DATA_ERROR %s"),*Error);
        if(!bLoaded){Database=Fallback();bLoaded=true;}
        if(OutError)*OutError=Error;return false;
    }
    // Projectile basics must reference authored skillshots; warn instead of failing so a
    // tuning edit elsewhere cannot silently disable all monsters.
    for(const auto& Pair:Candidate.Archetypes)for(const auto& A:Pair.Value.Abilities)
        if(A.Kind==ECireNPCAbilityKind::Projectile&&!CireSkillTuning::FindSkillshot(A.Skillshot))
            UE_LOG(LogCireNPCData,Warning,TEXT("NPC ability %s references missing skillshot %s"),*A.Id.ToString(),*A.Skillshot);
    Database=MoveTemp(Candidate);bLoaded=true;
    UE_LOG(LogCireNPCData,Display,TEXT("CIRE_NPC_DATA_LOADED archetypes=%d"),Database.Archetypes.Num());
    return true;
}

const FCireNPCDatabase& CireNPCArchetypes::Get(){if(!bLoaded)Reload();return Database;}
const FCireNPCArchetype* CireNPCArchetypes::Find(FName Id){return Get().Archetypes.Find(Id);}
FName CireNPCArchetypes::LegacyKind(int32 Kind)
{
    const auto& D=Get();return D.LegacyKinds.IsValidIndex(Kind)?D.LegacyKinds[Kind]:D.LegacyKinds.IsEmpty()?NAME_None:D.LegacyKinds[0];
}
FString CireNPCArchetypes::RoleLabel(ECireNPCRole Role)
{
    switch(Role){case ECireNPCRole::Tank:return TEXT("Tank");case ECireNPCRole::Caster:return TEXT("Caster");
    case ECireNPCRole::Ranged:return TEXT("Ranged");default:return TEXT("Bruiser");}
}
FString CireNPCArchetypes::ClassLabel(ECireNPCClass Class)
{
    return Class==ECireNPCClass::Boss?TEXT("Boss"):Class==ECireNPCClass::Elite?TEXT("Elite"):TEXT("Normal");
}
FString CireNPCArchetypes::KindLabel(const FCireNPCAbility& A)
{
    if(A.bBasic)return TEXT("Attack");
    switch(A.Kind)
    {
    case ECireNPCAbilityKind::Projectile:case ECireNPCAbilityKind::HealAlly:return A.bInterruptible?TEXT("Cast (interruptible)"):TEXT("Cast");
    case ECireNPCAbilityKind::Cone:case ECireNPCAbilityKind::TargetCircle:case ECireNPCAbilityKind::SelfCircle:case ECireNPCAbilityKind::Charge:return TEXT("Telegraph");
    case ECireNPCAbilityKind::Guard:case ECireNPCAbilityKind::Rally:case ECireNPCAbilityKind::ShieldWall:return TEXT("Buff");
    case ECireNPCAbilityKind::Provoke:return TEXT("Taunt");
    case ECireNPCAbilityKind::Enrage:return TEXT("Enrage");
    case ECireNPCAbilityKind::Disengage:return TEXT("Movement");
    default:return TEXT("Attack");
    }
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommand NPCReloadCommand(TEXT("cire.NPCs.Reload"),TEXT("Reload Content/Data/NPCArchetypes.json (affects newly configured monsters)."),
    FConsoleCommandDelegate::CreateLambda([](){CireNPCArchetypes::Reload();}));

bool CireNPCArchetypes::RunValidationSmoke()
{
    bool bPass=true;int32 Checks=0;
    auto Check=[&](bool V,const TCHAR* Why){++Checks;if(!V){bPass=false;UE_LOG(LogCireNPCData,Error,TEXT("CIRE_NPC_DATA_CHECK_FAIL %s"),Why);}};
    FString Json,Error;FCireNPCDatabase D;
    Check(FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/NPCArchetypes.json")))&&ParseJson(Json,D,Error),TEXT("authored NPCArchetypes.json parses"));
    if(!Error.IsEmpty())UE_LOG(LogCireNPCData,Error,TEXT("CIRE_NPC_DATA_CHECK_FAIL parse: %s"),*Error);
    bool Roles[4]={false,false,false,false};int32 Bosses=0;
    for(const auto& Pair:D.Archetypes)
    {
        const auto& A=Pair.Value;Roles[static_cast<int32>(A.Role)]=true;Bosses+=A.Classification==ECireNPCClass::Boss;
        for(const auto& Ab:A.Abilities)Check(!Ab.Name.IsEmpty()&&!Ab.Description.IsEmpty(),TEXT("every ability has a UI name and description"));
        if(A.Classification==ECireNPCClass::Boss)
        {
            int32 Telegraphed=0;
            for(const auto& Ab:A.Abilities)Telegraphed+=!Ab.bBasic&&Ab.CastTime>0;
            Check(Telegraphed>=3,TEXT("bosses have at least three telegraphed abilities"));
        }
    }
    Check(Roles[0]&&Roles[1]&&Roles[2]&&Roles[3],TEXT("all four roles are represented"));
    Check(Bosses>=2&&D.Archetypes.Contains(D.PackLeader)&&D.Archetypes[D.PackLeader].Abilities.Num()>=4,TEXT("pack leader and wave boss exist with a full kit"));
    for(const FName Id:D.WaveComposition)if(const auto* A=D.Archetypes.Find(Id))Check(A->Classification==ECireNPCClass::Normal,TEXT("wave composition uses normal units"));
    FCireNPCDatabase Rejected;
    Check(!ParseJson(TEXT("{\"schemaVersion\":2}"),Rejected,Error),TEXT("rejects unknown schema"));
    const FString Bad=Json.Replace(TEXT("\"role\": \"tank\""),TEXT("\"role\": \"healer\""));
    Check(Bad==Json||!ParseJson(Bad,Rejected,Error),TEXT("rejects unknown role"));
    const FString Traversal=Json.Replace(TEXT("\"asset\": \"/Game/"),TEXT("\"asset\": \"/Game/../"));
    Check(Traversal==Json||!ParseJson(Traversal,Rejected,Error),TEXT("rejects asset traversal"));
    UE_LOG(LogCireNPCData,Display,TEXT("CIRE_NPC_DATA_SMOKE_%s checks=%d archetypes=%d"),bPass?TEXT("PASS"):TEXT("FAIL"),Checks,D.Archetypes.Num());
    return bPass;
}
#endif
