#include "CireSkillTuning.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireSkillTuning, Log, All);

namespace
{
FCireTuningData Current;
bool bLoaded = false;
bool Object(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Key, TSharedPtr<FJsonObject>& Out)
{
    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (!Parent.IsValid() || !Parent->TryGetObjectField(Key, Value) || !Value || !Value->IsValid()) return false;
    Out = *Value; return true;
}
bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, float& Out, double Min, double Max)
{
    double Value = 0;
    if (!Object->TryGetNumberField(Key, Value) || !FMath::IsFinite(Value) || Value < Min || Value > Max) return false;
    Out = static_cast<float>(Value); return true;
}
bool Integer(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32& Out, int32 Min, int32 Max)
{
    double Value=0;
    if(!Object->TryGetNumberField(Key,Value)||!FMath::IsFinite(Value)||Value<Min||Value>Max||Value!=FMath::FloorToDouble(Value))return false;
    Out=static_cast<int32>(Value);return true;
}
bool Id(const TSharedPtr<FJsonObject>& Object, FString& Out)
{
    if (!Object->TryGetStringField(TEXT("id"), Out) || Out.IsEmpty() || Out.Len() > 64) return false;
    for (TCHAR C : Out) if (!(C >= 'a' && C <= 'z') && !(C >= 'A' && C <= 'Z') && !(C >= '0' && C <= '9') && C != '_' && C != '-') return false;
    return true;
}
bool Color(const TSharedPtr<FJsonObject>& Object, FLinearColor& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(TEXT("color"), Values) || Values->Num() != 4) return false;
    double C[4] = {};
    for (int32 I = 0; I < 4; ++I)
        if (!(*Values)[I]->TryGetNumber(C[I]) || !FMath::IsFinite(C[I]) || C[I] < (I == 3 ? .03 : 0) || C[I] > (I == 3 ? 1 : 8)) return false;
    Out = FLinearColor(C[0], C[1], C[2], C[3]); return true;
}
bool Policy(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, ECireProjectileCollision& Out)
{
    FString Value;
    if (!Object->TryGetStringField(Key, Value)) return false;
    if (Value == TEXT("ignore")) Out = ECireProjectileCollision::Ignore;
    else if (Value == TEXT("stop")) Out = ECireProjectileCollision::Stop;
    else if (Value == TEXT("pierce")) Out = ECireProjectileCollision::Pierce;
    else if (Value == TEXT("reflect")) Out = ECireProjectileCollision::Reflect;
    else return false;
    return true;
}
bool Keys(const TSharedPtr<FJsonObject>& Object, const TSet<FString>& Allowed)
{
    for (const auto& Pair : Object->Values) if (!Allowed.Contains(FString(Pair.Key.ToView()))) return false;
    return true;
}
void LoadOnce() { if (!bLoaded) CireSkillTuning::Reload(); }
TMap<FString,FCireRoleSkillSpec> RoleDefaults()
{
    return {
        {TEXT("second_wind"),{0,30,20,0,0,0,0,0,0,.18f}},
        {TEXT("last_stand"),{0,50,85,0,0,0,0,0,0,.40f}},
        {TEXT("challenge_of_iron"),{0,65,90,0,850,12,0,0,0,0}},
        {TEXT("seismic_reprisal"),{0,60,65,0,450,0,.8f,160,3,0}},
        {TEXT("starfall"),{130,0,80,1500,500,0,1.25f,180,3.5f,0}},
        {TEXT("spectral_hunt"),{125,0,90,1200,0,18,0,58,.4f,0}},
        {TEXT("mass_aegis"),{120,0,85,0,900,12,0,0,0,0}},
        {TEXT("wellspring"),{130,0,75,1200,0,5,0,220,4,0}}
    };
}
}

bool CireSkillTuning::ParseJson(const FString& Json, FCireTuningData& Out, FString& Error)
{
    auto Fail = [&Error](const TCHAR* Reason) { Error = Reason; return false; };
    if (Json.Len() > 256 * 1024) return Fail(TEXT("Combat tuning exceeds 256 KB"));
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) return Fail(TEXT("Invalid combat tuning JSON"));
    FString Engine, Version, Profile, Units; double Schema = 0;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || Schema != 1 ||
        !Root->TryGetStringField(TEXT("engine"), Engine) || Engine != TEXT("Unreal") ||
        !Root->TryGetStringField(TEXT("engineVersion"), Version) || Version != TEXT("5.8.3") ||
        !Root->TryGetStringField(TEXT("profile"), Profile) || Profile != TEXT("CireCombatTuning") ||
        !Root->TryGetStringField(TEXT("units"), Units) || Units != TEXT("centimeters") ||
        !Keys(Root, {TEXT("schemaVersion"), TEXT("engine"), TEXT("engineVersion"), TEXT("profile"), TEXT("units"), TEXT("globals"), TEXT("skillshots"), TEXT("constructs"), TEXT("summons"), TEXT("roleSkills")}))
        return Fail(TEXT("Expected CireCombatTuning schema 1 for Unreal 5.8.3 in centimeters"));
    FCireTuningData Candidate;
    Candidate.RoleSkills=RoleDefaults();
    TSharedPtr<FJsonObject> G;
    if (!Object(Root, TEXT("globals"), G)) return Fail(TEXT("Missing global combat tuning"));
    auto& V = Candidate.Globals;
    if (!Keys(G, {TEXT("critChance"),TEXT("critMultiplier"),TEXT("tankDamageThreatMultiplier"),TEXT("dpsDamageThreatMultiplier"),TEXT("healingThreatMultiplier"),TEXT("normalMonsterDamage"),TEXT("bossMonsterDamage"),TEXT("challengeMonsterDamage"),TEXT("bruiserMonsterDamage"),TEXT("casterMonsterDamage"),TEXT("rangedMonsterDamage"),TEXT("waveHealthBase"),TEXT("waveHealthPerWave"),TEXT("bossHealthMultiplier"),TEXT("challengeHealthBase"),TEXT("basicHealthMultiplier"),TEXT("bruiserHealthMultiplier"),TEXT("casterHealthMultiplier"),TEXT("rangedHealthMultiplier")}) ||
        !Number(G,TEXT("critChance"),V.CritChance,0,1) || !Number(G,TEXT("critMultiplier"),V.CritMultiplier,1,5) ||
        !Number(G,TEXT("tankDamageThreatMultiplier"),V.TankDamageThreatMultiplier,0,100) ||
        !Number(G,TEXT("dpsDamageThreatMultiplier"),V.DpsDamageThreatMultiplier,0,100) ||
        !Number(G,TEXT("healingThreatMultiplier"),V.HealingThreatMultiplier,0,100) ||
        !Number(G,TEXT("normalMonsterDamage"),V.NormalMonsterDamage,0,10000) ||
        !Number(G,TEXT("bossMonsterDamage"),V.BossMonsterDamage,0,10000) ||
        !Number(G,TEXT("challengeMonsterDamage"),V.ChallengeMonsterDamage,0,10000) ||
        !Number(G,TEXT("bruiserMonsterDamage"),V.BruiserMonsterDamage,0,10000) ||
        !Number(G,TEXT("casterMonsterDamage"),V.CasterMonsterDamage,0,10000) ||
        !Number(G,TEXT("rangedMonsterDamage"),V.RangedMonsterDamage,0,10000) ||
        !Number(G,TEXT("waveHealthBase"),V.WaveHealthBase,1,100000) ||
        !Number(G,TEXT("waveHealthPerWave"),V.WaveHealthPerWave,0,10000) ||
        !Number(G,TEXT("bossHealthMultiplier"),V.BossHealthMultiplier,1,100) ||
        !Number(G,TEXT("challengeHealthBase"),V.ChallengeHealthBase,1,100000) ||
        !Number(G,TEXT("basicHealthMultiplier"),V.BasicHealthMultiplier,.1,20) ||
        !Number(G,TEXT("bruiserHealthMultiplier"),V.BruiserHealthMultiplier,.1,20) ||
        !Number(G,TEXT("casterHealthMultiplier"),V.CasterHealthMultiplier,.1,20) ||
        !Number(G,TEXT("rangedHealthMultiplier"),V.RangedHealthMultiplier,.1,20)) return Fail(TEXT("Invalid global combat tuning field"));
    const TArray<TSharedPtr<FJsonValue>>* Shots = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Constructs = nullptr;
    if (!Root->TryGetArrayField(TEXT("skillshots"), Shots) || Shots->Num() > 64 ||
        !Root->TryGetArrayField(TEXT("constructs"), Constructs) || Constructs->Num() > 32) return Fail(TEXT("Missing or excessive recipe arrays"));
    for (const auto& Entry : *Shots)
    {
        const TSharedPtr<FJsonObject>* ObjectPointer = nullptr;
        if (!Entry->TryGetObject(ObjectPointer) || !ObjectPointer || !ObjectPointer->IsValid()) return Fail(TEXT("Skillshot must be an object"));
        const auto& J = *ObjectPointer; FCireSkillshotSpec S; FString Key;
        if (!Id(J, Key) || Candidate.Skillshots.Contains(Key) ||
            !Keys(J,{TEXT("id"),TEXT("speed"),TEXT("radius"),TEXT("maxRange"),TEXT("lifetimeSeconds"),TEXT("damage"),TEXT("warningSeconds"),TEXT("manaCost"),TEXT("energyCost"),TEXT("cooldownSeconds"),TEXT("castRange"),TEXT("canCrit"),TEXT("hitLimit"),TEXT("reflectionLimit"),TEXT("hitSameTargetAgain"),TEXT("abilityName"),TEXT("worldCollision"),TEXT("playerCollision"),TEXT("monsterCollision"),TEXT("protectionCollision"),TEXT("wallCollision"),TEXT("visualStyle"),TEXT("color")}) ||
            !Number(J,TEXT("speed"),S.Speed,50,10000) || !Number(J,TEXT("radius"),S.Radius,1,200) ||
            !Number(J,TEXT("maxRange"),S.MaxRange,50,5000) || !Number(J,TEXT("lifetimeSeconds"),S.LifetimeSeconds,.05,30) ||
            !Number(J,TEXT("damage"),S.Damage,0,10000) || !Number(J,TEXT("warningSeconds"),S.WarningSeconds,0,10) ||
            !Number(J,TEXT("manaCost"),S.ManaCost,0,10000) || !Number(J,TEXT("energyCost"),S.EnergyCost,0,100) ||
            !Number(J,TEXT("cooldownSeconds"),S.CooldownSeconds,0,300) || !Number(J,TEXT("castRange"),S.CastRange,0,5000) ||
            !J->TryGetBoolField(TEXT("canCrit"),S.bCanCrit) || !J->TryGetStringField(TEXT("visualStyle"),S.VisualStyle) || S.VisualStyle.IsEmpty() || S.VisualStyle.Len()>32 ||
            !Integer(J,TEXT("hitLimit"),S.HitLimit,1,32) || !Integer(J,TEXT("reflectionLimit"),S.ReflectionLimit,0,8) ||
            !J->TryGetBoolField(TEXT("hitSameTargetAgain"),S.bHitSameTargetAgain) || !J->TryGetStringField(TEXT("abilityName"),S.AbilityName) || S.AbilityName.Len()>80 ||
            !Policy(J,TEXT("worldCollision"),S.WorldCollision) || !Policy(J,TEXT("playerCollision"),S.PlayerCollision) ||
            !Policy(J,TEXT("monsterCollision"),S.MonsterCollision) || !Policy(J,TEXT("protectionCollision"),S.ProtectionCollision) ||
            !Policy(J,TEXT("wallCollision"),S.WallCollision) || !Color(J,S.Color)) return Fail(TEXT("Invalid skillshot ID, bounds, collision policy or appearance"));
        Candidate.Skillshots.Add(Key, MoveTemp(S));
    }
    for (const auto& Entry : *Constructs)
    {
        const TSharedPtr<FJsonObject>* ObjectPointer = nullptr;
        if (!Entry->TryGetObject(ObjectPointer) || !ObjectPointer || !ObjectPointer->IsValid()) return Fail(TEXT("Construct must be an object"));
        const auto& J = *ObjectPointer; FCireConstructSpec S; FString Key,Kind;
        if (!Id(J, Key) || Candidate.Constructs.Contains(Key) ||
            !Keys(J,{TEXT("id"),TEXT("kind"),TEXT("maxHealth"),TEXT("lifetimeSeconds"),TEXT("width"),TEXT("depth"),TEXT("height"),TEXT("manaCost"),TEXT("energyCost"),TEXT("cooldownSeconds"),TEXT("castRange"),TEXT("blockMovement"),TEXT("blockProjectiles"),TEXT("destructible"),TEXT("blockFriendly"),TEXT("protectionResponse"),TEXT("color")}) ||
            !J->TryGetStringField(TEXT("kind"),Kind) || (Kind!=TEXT("wall") && Kind!=TEXT("protection")) ||
            !Number(J,TEXT("maxHealth"),S.MaxHealth,1,100000) || !Number(J,TEXT("lifetimeSeconds"),S.LifetimeSeconds,.1,120) ||
            !Number(J,TEXT("width"),S.Width,20,2000) || !Number(J,TEXT("depth"),S.Depth,10,1000) || !Number(J,TEXT("height"),S.Height,20,2000) ||
            !Number(J,TEXT("manaCost"),S.ManaCost,0,10000) || !Number(J,TEXT("energyCost"),S.EnergyCost,0,100) ||
            !Number(J,TEXT("cooldownSeconds"),S.CooldownSeconds,0,300) || !Number(J,TEXT("castRange"),S.CastRange,0,5000) ||
            !J->TryGetBoolField(TEXT("blockMovement"),S.bBlockMovement) || !J->TryGetBoolField(TEXT("blockProjectiles"),S.bBlockProjectiles) ||
            !J->TryGetBoolField(TEXT("destructible"),S.bDestructible) || !J->TryGetBoolField(TEXT("blockFriendly"),S.bBlockFriendly) ||
            !Policy(J,TEXT("protectionResponse"),S.ProtectionResponse) || !Color(J,S.Color))
            return Fail(TEXT("Invalid construct ID, dimensions, lifetime or flags"));
        S.Kind=Kind==TEXT("wall")?ECireConstructKind::Wall:ECireConstructKind::Protection;
        Candidate.Constructs.Add(Key, MoveTemp(S));
    }
    const TArray<TSharedPtr<FJsonValue>>* Summons = nullptr;
    if (Root->HasField(TEXT("summons")))
    {
        if (!Root->TryGetArrayField(TEXT("summons"),Summons) || Summons->Num()>32) return Fail(TEXT("Invalid summons array"));
        for(const auto& Entry:*Summons)
        {
            const TSharedPtr<FJsonObject>* ObjectPointer=nullptr;
            if(!Entry->TryGetObject(ObjectPointer)||!ObjectPointer||!ObjectPointer->IsValid())return Fail(TEXT("Summon must be an object"));
            const auto& J=*ObjectPointer;FCireSummonSpec S;FString Key;
            if(!Id(J,Key)||Candidate.Summons.Contains(Key)||
                !Keys(J,{TEXT("id"),TEXT("count"),TEXT("health"),TEXT("damage"),TEXT("durationSeconds"),TEXT("manaCost"),TEXT("energyCost"),TEXT("cooldownSeconds"),TEXT("castRange"),TEXT("commandable"),TEXT("leashRange"),TEXT("moveSpeed"),TEXT("attackRange"),TEXT("archetypeVisual")})||
                !Integer(J,TEXT("count"),S.Count,1,3)||!Number(J,TEXT("health"),S.Health,1,100000)||
                !Number(J,TEXT("damage"),S.Damage,0,10000)||!Number(J,TEXT("durationSeconds"),S.DurationSeconds,.1,300)||
                !Number(J,TEXT("manaCost"),S.ManaCost,0,10000)||!Number(J,TEXT("energyCost"),S.EnergyCost,0,100)||
                !Number(J,TEXT("cooldownSeconds"),S.CooldownSeconds,0,300)||!Number(J,TEXT("castRange"),S.CastRange,0,3000)||
                !J->TryGetBoolField(TEXT("commandable"),S.bCommandable)||!Number(J,TEXT("leashRange"),S.LeashRange,100,5000)||
                !Number(J,TEXT("moveSpeed"),S.MoveSpeed,50,1500)||!Number(J,TEXT("attackRange"),S.AttackRange,50,2000)||
                !Integer(J,TEXT("archetypeVisual"),S.ArchetypeVisual,0,3))return Fail(TEXT("Invalid summon ID, count, resources or dimensions"));
            Candidate.Summons.Add(Key,MoveTemp(S));
        }
    }
    if(Root->HasField(TEXT("roleSkills")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Recipes=nullptr;
        if(!Root->TryGetArrayField(TEXT("roleSkills"),Recipes)||Recipes->Num()>32)return Fail(TEXT("Invalid roleSkills array"));
        TSet<FString> Seen;
        for(const auto& Entry:*Recipes)
        {
            const TSharedPtr<FJsonObject>* P=nullptr;
            if(!Entry->TryGetObject(P)||!P||!P->IsValid())return Fail(TEXT("Role skill must be an object"));
            const auto& J=*P;FString Key;FCireRoleSkillSpec S;
            if(!Id(J,Key)||Seen.Contains(Key)||!Candidate.RoleSkills.Contains(Key)||
                !Keys(J,{TEXT("id"),TEXT("manaCost"),TEXT("energyCost"),TEXT("cooldownSeconds"),TEXT("castRange"),TEXT("radius"),TEXT("durationSeconds"),TEXT("warningSeconds"),TEXT("flatPower"),TEXT("primaryScaling"),TEXT("maxHealthFraction")})||
                !Number(J,TEXT("manaCost"),S.ManaCost,0,10000)||!Number(J,TEXT("energyCost"),S.EnergyCost,0,100)||
                !Number(J,TEXT("cooldownSeconds"),S.CooldownSeconds,1,300)||!Number(J,TEXT("castRange"),S.CastRange,0,3000)||
                !Number(J,TEXT("radius"),S.Radius,0,1200)||!Number(J,TEXT("durationSeconds"),S.DurationSeconds,0,30)||
                !Number(J,TEXT("warningSeconds"),S.WarningSeconds,0,5)||!Number(J,TEXT("flatPower"),S.FlatPower,0,10000)||
                !Number(J,TEXT("primaryScaling"),S.PrimaryScaling,0,20)||!Number(J,TEXT("maxHealthFraction"),S.MaxHealthFraction,0,1))
                return Fail(TEXT("Invalid role skill ID, costs, duration or effect strength"));
            if((Key==TEXT("seismic_reprisal")||Key==TEXT("starfall"))&&(S.Radius<20||S.WarningSeconds<.2f))return Fail(TEXT("Area ultimates require radius >=20 and warning >=0.2 seconds"));
            if(Key==TEXT("spectral_hunt")&&(S.DurationSeconds<.1f||S.CastRange<50))return Fail(TEXT("Spectral Hunt requires a positive lifetime and cast range"));
            Seen.Add(Key);Candidate.RoleSkills.Add(Key,S);
        }
    }
    Out = MoveTemp(Candidate); Error.Reset(); return true;
}

bool CireSkillTuning::Reload(FString* Error)
{
    bLoaded = true;
    FString Json, Reason;
    FCireTuningData Candidate;
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/CombatTuning.json"));
    if (!FFileHelper::LoadFileToString(Json,*Path)) Reason=TEXT("Cannot read Content/Data/CombatTuning.json");
    else if (ParseJson(Json,Candidate,Reason))
    {
        Current=MoveTemp(Candidate);
        UE_LOG(LogCireSkillTuning,Display,TEXT("CIRE_COMBAT_TUNING_LOADED skillshots=%d constructs=%d summons=%d"),Current.Skillshots.Num(),Current.Constructs.Num(),Current.Summons.Num());
        if(Error)Error->Reset();return true;
    }
    if(Error)*Error=Reason;
    UE_LOG(LogCireSkillTuning,Warning,TEXT("Combat tuning rejected; retained previous/default values: %s"),*Reason);
    return false;
}
const FCireGlobalCombatTuning& CireSkillTuning::Get(){LoadOnce();return Current.Globals;}
const FCireSkillshotSpec* CireSkillTuning::FindSkillshot(const FString& Id){LoadOnce();return Current.Skillshots.Find(Id);}
const FCireConstructSpec* CireSkillTuning::FindConstruct(const FString& Id){LoadOnce();return Current.Constructs.Find(Id);}
const FCireSummonSpec* CireSkillTuning::FindSummon(const FString& Id){LoadOnce();return Current.Summons.Find(Id);}
const FCireRoleSkillSpec* CireSkillTuning::FindRoleSkill(const FString& Id){LoadOnce();return Current.RoleSkills.Find(Id);}
int32 CireSkillTuning::SkillshotCount(){LoadOnce();return Current.Skillshots.Num();}
int32 CireSkillTuning::ConstructCount(){LoadOnce();return Current.Constructs.Num();}
int32 CireSkillTuning::SummonCount(){LoadOnce();return Current.Summons.Num();}

#if !UE_BUILD_SHIPPING
bool CireSkillTuning::RunValidationSmoke()
{
    FString Json,Error;
    if(!FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/CombatTuning.json"))))return false;
    FCireTuningData Valid;
    if(!ParseJson(Json,Valid,Error)||Valid.Skillshots.IsEmpty()||Valid.Constructs.IsEmpty())return false;
    const int32 ShotCount=Valid.Skillshots.Num();const float Chance=Valid.Globals.CritChance;
    TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root);
    const TSharedPtr<FJsonObject>* G=nullptr;Root->TryGetObjectField(TEXT("globals"),G);
    (*G)->SetNumberField(TEXT("critChance"),1.1);
    FString Bad;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Bad));
    bool bPassed=!ParseJson(Bad,Valid,Error)&&Valid.Skillshots.Num()==ShotCount&&FMath::IsNearlyEqual(Valid.Globals.CritChance,Chance)&&!ParseJson(TEXT("{}"),Valid,Error);
    (*G)->SetNumberField(TEXT("critChance"),Chance);
    const int32 RoleCount=Valid.RoleSkills.Num();bPassed&=RoleCount==8;
    const TArray<TSharedPtr<FJsonValue>>* Roles=nullptr;
    if(Root->TryGetArrayField(TEXT("roleSkills"),Roles)&&!Roles->IsEmpty())
    {
        (*Roles)[0]->AsObject()->SetNumberField(TEXT("maxHealthFraction"),1.1);
        Bad.Reset();FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Bad));
        bPassed&=!ParseJson(Bad,Valid,Error)&&Valid.RoleSkills.Num()==RoleCount;
    }
    Root->RemoveField(TEXT("roleSkills"));Bad.Reset();FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Bad));
    FCireTuningData Legacy;bPassed&=ParseJson(Bad,Legacy,Error)&&Legacy.RoleSkills.Num()==8&&Legacy.Skillshots.Num()==ShotCount;
    UE_LOG(LogCireSkillTuning,Display,TEXT("CIRE_COMBAT_TUNING_VALIDATION_%s"),bPassed?TEXT("PASS"):TEXT("FAIL"));
    return bPassed;
}
#endif
