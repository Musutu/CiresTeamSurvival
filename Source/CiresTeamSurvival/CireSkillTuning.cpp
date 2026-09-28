#include "CireSkillTuning.h"
#include "CireAbilityShapes.h" // aoe-scale
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
        if (!Key.StartsWith(TEXT("basic_"))) S.Radius = CireAbilityShapes::AoE(S.Radius); // aoe-scale: skillshot lanes 30% wider (basic attacks unchanged)
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
            S.Radius=CireAbilityShapes::AoE(S.Radius); // aoe-scale
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

// ============================================================================ casting-rules
#include "CireAbilityDB.h"
#include "CireGame.h"
#include "HAL/IConsoleManager.h"

namespace
{
FCireCastRules GCastRules;
bool bCastRulesLoaded = false;
void LoadCastRulesOnce()
{
    if (bCastRulesLoaded) return;
    bCastRulesLoaded = true;
    FString Json, Error; FCireCastRules R;
    if (FFileHelper::LoadFileToString(Json, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/CastRules.json"))) && CireSkillTuning::ParseCastRules(Json, R, Error)) GCastRules = MoveTemp(R);
    else UE_LOG(LogCireSkillTuning, Warning, TEXT("Cast rules unavailable, using defaults: %s"), *Error);
}
float CastNumber(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float Default, float Min, float Max, bool& bOk)
{
    double V = Default;
    if (O.IsValid() && O->HasField(Key) && (!O->TryGetNumberField(Key, V) || !FMath::IsFinite(V))) { bOk = false; return Default; }
    if (V < Min || V > Max) { bOk = false; return Default; }
    return static_cast<float>(V);
}
TSet<FString> CastStrings(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
{
    TSet<FString> Out; const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (O.IsValid() && O->TryGetArrayField(Key, A)) for (const auto& V : *A) { FString S; if (V->TryGetString(S)) Out.Add(S.ToLower()); }
    return Out;
}
float CastRamp(float V, float Low, float High) { return High > Low ? FMath::Clamp((V - Low) / (High - Low), 0.f, 1.f) : (V >= High ? 1.f : 0.f); }
float CastSnap(float V, float Step) { return Step > 0 ? FMath::RoundToFloat(V / Step) * Step : V; }
bool CastLabelHas(const FCireAbilityDef& D, std::initializer_list<const TCHAR*> Words)
{
    for (const TCHAR* W : Words) if (D.EffectLabel.Contains(W)) return true;
    return false;
}
// One application at the reference PRIMARY: base + coef x primary, or % of max health; per-second / per-pulse x duration.
float CastReferenceAmount(const FCireAbilityDef& D, const FCireCastRules& R)
{
    float P = D.EffectLabel.Contains(TEXT("%")) ? D.Base.Effect / 100.f * R.ReferenceMaxHealth
        : (D.ScalePrimary > 0 || D.ScaleBase > 0 ? D.ScaleBase + D.ScalePrimary * R.ReferencePrimary : D.Base.Effect);
    if (CastLabelHas(D, {TEXT("per second"), TEXT("per tick"), TEXT("per pulse"), TEXT("per mote")})) P *= FMath::Max(1.f, D.Duration);
    return FMath::Max(0.f, P);
}
}

FCireCastVerdict CireSkillTuning::EvaluateCastRule(const FCireAbilityDef& D, const FCireCastRules& R, const FString& ForcedIn)
{
    FCireCastVerdict V;
    const FString Forced = ForcedIn.ToLower();
    const bool bHeal = D.ScaleComponent == TEXT("heal");
    if (bHeal) V.HealScale = R.HealingScale;
    if (!R.bEnabled || D.IsPassive() || Forced == TEXT("exempt") || R.Exempt.Contains(D.Id.ToLower())) return V;
    const float Radius = D.Radius / FMath::Max(.01f, CireAbilityShapes::AoERadiusScale()); // authored radius (the DB row is already aoe-scaled)
    if (Forced.IsEmpty())
    {
        // Rolls, constructs, summons and pets are placement / movement skills: no cast rule.
        if (D.EffectTags.Contains(TEXT("Roll")) || D.Section == TEXT("construct") || D.Section == TEXT("summon") || D.IsConstruct() || D.IsPet()) return V;
        if (bHeal)
        {
            if (D.Targeting == TEXT("self") && Radius <= R.AoEHealMinRadius) return V; // self-only emergency heals keep their authored cast
            V.Rule = Radius > R.AoEHealMinRadius && D.Targeting != TEXT("ally") ? ECireCastRule::AoEHeal : ECireCastRule::DirectHeal;
        }
        else if (D.ScaleComponent == TEXT("damage") && Radius >= R.AoEMinRadius && D.Section != TEXT("attack") &&
            (D.Targeting == TEXT("aim") || D.Targeting == TEXT("self") || D.Targeting == TEXT("enemy")) &&
            !(D.Targeting == TEXT("self") && D.Duration >= R.SelfAuraSeconds) && // persistent self auras / transforms
            !CastLabelHas(D, {TEXT("per bounce"), TEXT("per target"), TEXT("per hit"), TEXT("per slash"), TEXT("per tick")}))
            V.Rule = ECireCastRule::AoEDamage;
    }
    else if (Forced == TEXT("aoedamage")) V.Rule = ECireCastRule::AoEDamage;
    else if (Forced == TEXT("directheal")) V.Rule = ECireCastRule::DirectHeal;
    else if (Forced == TEXT("aoeheal")) V.Rule = ECireCastRule::AoEHeal;
    const float Amount = CastReferenceAmount(D, R);
    switch (V.Rule)
    {
    case ECireCastRule::AoEDamage:
    {
        bool bControl = false;
        for (const auto& E : D.Effects) bControl |= E.Type == TEXT("stun") || E.Type == TEXT("root") || E.Type == TEXT("silence") || E.Type == TEXT("interrupt");
        V.Metric = Amount * FMath::Sqrt(FMath::Max(Radius, 1.f) / FMath::Max(1.f, R.AoEAreaReference)) * (bControl ? 1.f + R.AoEControlBonus : 1.f);
        V.CastTime = FMath::Lerp(R.AoEDamageMinCast, R.AoEDamageMaxCast, CastRamp(V.Metric, R.AoEImpactLow, R.AoEImpactHigh));
        V.CastTime = FMath::Clamp(CastSnap(V.CastTime, R.CastStep), R.AoEDamageMinCast, R.AoEDamageMaxCast);
        break;
    }
    case ECireCastRule::DirectHeal:
        V.Metric = Amount;
        V.CastTime = FMath::Clamp(CastSnap(FMath::Lerp(R.DirectHealMinCast, R.DirectHealMaxCast, CastRamp(Amount, R.DirectHealPowerLow, R.DirectHealPowerHigh)), R.CastStep),
            R.DirectHealMinCast, R.DirectHealMaxCast);
        break;
    case ECireCastRule::AoEHeal:
    {
        V.Metric = Amount;
        V.CastTime = FMath::Clamp(CastSnap(R.AoEHealMaxCast * CastRamp(Amount, R.AoEHealPowerLow, R.AoEHealPowerHigh), R.CastStep), 0.f, R.AoEHealMaxCast);
        const float Speed = R.AoEHealMaxCast > 0 ? V.CastTime / R.AoEHealMaxCast : 1.f;
        V.HealScale = R.HealingScale * FMath::Lerp(R.AoEHealInstantEffect, R.AoEHealFullCastEffect, Speed); // faster cast -> weaker heal
        break;
    }
    default: break;
    }
    return V;
}

void CireSkillTuning::ApplyCastRules(FCireAbilityDef& D, const FString& Forced, bool bCastWhileMovingAuthored)
{
    const FCireCastVerdict V = EvaluateCastRule(D, CastRules(), Forced);
    D.AuthoredCastTime = D.CastTime;
    D.CastRule = FName(*CastRuleName(V.Rule));
    D.HealScale = V.HealScale;
    D.CastMetric = V.Metric;
    if (V.Rule == ECireCastRule::None) return;
    D.CastTime = D.Base.CastTime = V.CastTime;
    if (!bCastWhileMovingAuthored) D.bCastWhileMoving = D.CastTime <= 0; // WoW: cast-time spells stand still (bots are exempt)
}

FString CireSkillTuning::CastRuleName(ECireCastRule Rule)
{
    switch (Rule)
    {
    case ECireCastRule::AoEDamage: return TEXT("aoeDamage");
    case ECireCastRule::DirectHeal: return TEXT("directHeal");
    case ECireCastRule::AoEHeal: return TEXT("aoeHeal");
    default: return TEXT("none");
    }
}

bool CireSkillTuning::ParseCastRules(const FString& Json, FCireCastRules& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) { Error = TEXT("CastRules.json is not a JSON object"); return false; }
    double Schema = 0; FString Profile;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || Schema != 1 || !Root->TryGetStringField(TEXT("profile"), Profile) || Profile != TEXT("CireCastRules"))
    { Error = TEXT("CastRules.json needs schemaVersion 1 and profile CireCastRules"); return false; }
    FCireCastRules R; bool bOk = true;
    auto Sub = [&](const TCHAR* Key) { const TSharedPtr<FJsonObject>* O = nullptr; return Root->TryGetObjectField(Key, O) && O ? *O : TSharedPtr<FJsonObject>(); };
    Root->TryGetBoolField(TEXT("enabled"), R.bEnabled);
    const auto Ref = Sub(TEXT("reference")), Cls = Sub(TEXT("classification")), Aoe = Sub(TEXT("aoeDamage")), Dh = Sub(TEXT("directHeal")),
        Ah = Sub(TEXT("aoeHeal")), Heal = Sub(TEXT("healing")), Pierce = Sub(TEXT("piercing"));
    R.ReferencePrimary = CastNumber(Ref, TEXT("primary"), R.ReferencePrimary, 0, 1000, bOk);
    R.ReferenceMaxHealth = CastNumber(Ref, TEXT("maxHealth"), R.ReferenceMaxHealth, 1, 100000, bOk);
    R.AoEMinRadius = CastNumber(Cls, TEXT("aoeMinRadius"), R.AoEMinRadius, 0, 3000, bOk);
    R.AoEHealMinRadius = CastNumber(Cls, TEXT("aoeHealMinRadius"), R.AoEHealMinRadius, 0, 3000, bOk);
    R.SelfAuraSeconds = CastNumber(Cls, TEXT("selfAuraSeconds"), R.SelfAuraSeconds, 0, 120, bOk);
    R.Exempt = CastStrings(Cls, TEXT("exempt"));
    R.AoEDamageMinCast = CastNumber(Aoe, TEXT("minCast"), R.AoEDamageMinCast, 0, 10, bOk);
    R.AoEDamageMaxCast = CastNumber(Aoe, TEXT("maxCast"), R.AoEDamageMaxCast, 0, 10, bOk);
    R.AoEImpactLow = CastNumber(Aoe, TEXT("impactLow"), R.AoEImpactLow, 0, 100000, bOk);
    R.AoEImpactHigh = CastNumber(Aoe, TEXT("impactHigh"), R.AoEImpactHigh, 0, 100000, bOk);
    R.AoEAreaReference = CastNumber(Aoe, TEXT("areaReference"), R.AoEAreaReference, 1, 5000, bOk);
    R.AoEControlBonus = CastNumber(Aoe, TEXT("controlBonus"), R.AoEControlBonus, 0, 5, bOk);
    R.DirectHealMinCast = CastNumber(Dh, TEXT("minCast"), R.DirectHealMinCast, 0, 10, bOk);
    R.DirectHealMaxCast = CastNumber(Dh, TEXT("maxCast"), R.DirectHealMaxCast, 0, 10, bOk);
    R.DirectHealPowerLow = CastNumber(Dh, TEXT("powerLow"), R.DirectHealPowerLow, 0, 100000, bOk);
    R.DirectHealPowerHigh = CastNumber(Dh, TEXT("powerHigh"), R.DirectHealPowerHigh, 0, 100000, bOk);
    R.AoEHealMaxCast = CastNumber(Ah, TEXT("maxCast"), R.AoEHealMaxCast, 0, 10, bOk);
    R.AoEHealPowerLow = CastNumber(Ah, TEXT("powerLow"), R.AoEHealPowerLow, 0, 100000, bOk);
    R.AoEHealPowerHigh = CastNumber(Ah, TEXT("powerHigh"), R.AoEHealPowerHigh, 0, 100000, bOk);
    R.AoEHealInstantEffect = CastNumber(Ah, TEXT("instantEffect"), R.AoEHealInstantEffect, 0, 2, bOk);
    R.AoEHealFullCastEffect = CastNumber(Ah, TEXT("fullCastEffect"), R.AoEHealFullCastEffect, 0, 2, bOk);
    R.HealingScale = CastNumber(Heal, TEXT("abilityHealingScale"), R.HealingScale, 0, 2, bOk);
    R.CastStep = CastNumber(Root, TEXT("castStep"), R.CastStep, 0, 1, bOk);
    if (Pierce.IsValid())
    {
        Pierce->TryGetBoolField(TEXT("championSkillshots"), R.bPierceChampionSkillshots);
        R.PierceHitLimit = FMath::RoundToInt(CastNumber(Pierce, TEXT("hitLimit"), static_cast<float>(R.PierceHitLimit), 1, 32, bOk));
        R.PierceFalloff = CastNumber(Pierce, TEXT("falloffPerTarget"), R.PierceFalloff, 0, 1, bOk);
        R.PierceMinDamage = CastNumber(Pierce, TEXT("minDamageFraction"), R.PierceMinDamage, 0, 1, bOk);
        R.NeverPierce = CastStrings(Pierce, TEXT("never"));
    }
    if (!bOk || R.AoEDamageMinCast > R.AoEDamageMaxCast || R.DirectHealMinCast > R.DirectHealMaxCast) { Error = TEXT("CastRules.json has an out-of-range number"); return false; }
    Out = MoveTemp(R); Error.Reset(); return true;
}

const FCireCastRules& CireSkillTuning::CastRules() { LoadCastRulesOnce(); return GCastRules; }
bool CireSkillTuning::ReloadCastRules(FString* Error)
{
    FString Json, Reason; FCireCastRules R;
    const bool bOk = FFileHelper::LoadFileToString(Json, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/CastRules.json"))) && ParseCastRules(Json, R, Reason);
    bCastRulesLoaded = true;
    if (bOk) GCastRules = MoveTemp(R);
    if (Error) *Error = Reason;
    CireAbilityDB::Reload();
    UE_LOG(LogCireSkillTuning, Display, TEXT("CIRE_CAST_RULES_%s %s"), bOk ? TEXT("LOADED") : TEXT("REJECTED"), *Reason);
    return bOk;
}
#if !UE_BUILD_SHIPPING
void CireSkillTuning::DebugSetCastRules(const FCireCastRules& Rules) { bCastRulesLoaded = true; GCastRules = Rules; }
#endif

float CireSkillTuning::HealScaleFor(const FString& AbilityName)
{
    const FCireAbilityDef* D = CireAbilityDB::FindByName(AbilityName);
    if (!D) D = CireAbilityDB::Find(AbilityName);
    return D ? D->HealScale : 1.f;
}

bool CireSkillTuning::ShouldPierce(const AActor* Source, const FString& AbilityName)
{
    const FCireCastRules& R = CastRules();
    if (!R.bPierceChampionSkillshots || !Cast<ACireHero>(Source)) return false;
    const FCireAbilityDef* D = CireAbilityDB::FindByName(AbilityName);
    return D && !R.NeverPierce.Contains(D->Id.ToLower());
}

static FAutoConsoleCommand GCireReloadCastRules(TEXT("cire.ReloadCastRules"),
    TEXT("casting-rules: reload Content/Data/CastRules.json and re-derive every ability's cast time and healing."),
    FConsoleCommandDelegate::CreateLambda([] { FString Error; CireSkillTuning::ReloadCastRules(&Error); }));
