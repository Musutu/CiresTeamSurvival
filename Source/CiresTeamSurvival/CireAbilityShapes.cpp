// ability-vfx: true hit shapes for every ability (see CireAbilityShapes.h).
#include "CireAbilityShapes.h"
#include "CireAbilityLibrary.h"
#include "CireChampionRoster.h"
#include "CireGame.h"
#include "CireNPCArchetypes.h"
#include "CireRaces.h"
#include "CireSkillTuning.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
// Ability Database entries: school override and the void zone of teleport/portal skills.
struct FDbEntry { bool bSchool=false; ECireSchool School=ECireSchool::Steel; FCireHitShape Void; };
TMap<FName,FDbEntry> GDatabase;bool bDatabaseLoaded=false;
const FDbEntry* DbFind(FName Id)
{
    if(!bDatabaseLoaded)CireAbilityShapes::ReloadDatabase();
    return GDatabase.Find(Id);
}
// Stub void zones until the Ability Database lands (champion-draft adds the gameplay): teleport /
// portal skills leave an outer slowing ring and an inner stunning circle at their destination.
void StubVoid(FName Id,FCireHitShape& S)
{
    struct FStub{const TCHAR* Id;float Outer,Inner,Seconds;bool bHeal,bOrigin;};
    static const FStub Stubs[]={{TEXT("shadow_step"),260.f,110.f,2.f,false,false},{TEXT("void_blink"),240.f,100.f,2.f,false,true},
        {TEXT("void_warp"),240.f,100.f,2.f,false,true}};
    for(const FStub& X:Stubs)if(Id==FName(X.Id)){S.VoidOuter=X.Outer;S.VoidInner=X.Inner;S.VoidSeconds=X.Seconds;S.bVoidHeal=X.bHeal;S.bVoidAtOrigin=X.bOrigin;return;}
}
void ApplyDatabase(FName Id,FCireHitShape& S)
{
    if(const FDbEntry* E=DbFind(Id))
    {
        if(E->bSchool)S.School=E->School;
        if(E->Void.HasVoidZone()){S.VoidOuter=E->Void.VoidOuter;S.VoidInner=E->Void.VoidInner;S.VoidSeconds=E->Void.VoidSeconds;
            S.bVoidHeal=E->Void.bVoidHeal;S.bVoidAtOrigin=E->Void.bVoidAtOrigin;S.bVoidFromDatabase=true;return;}
    }
    StubVoid(Id,S);
}
FString Norm(FName Id)
{
    FString S=Id.ToString().ToLower();S.ReplaceInline(TEXT(" "),TEXT("_"));S.ReplaceInline(TEXT("'"),TEXT(""));
    return S;
}
// Champion families, identical to the original CireSpellPresentation mapping (kept for continuity).
ECireSchool ChampionSchool(const FString& S)
{
    if(S.Contains(TEXT("venom"))||S.Contains(TEXT("blight"))||S.Contains(TEXT("poison")))return ECireSchool::Poison;
    if(S.Contains(TEXT("ember"))||S.Contains(TEXT("cinder"))||S.Contains(TEXT("cataclysm")))return ECireSchool::Fire;
    if(S.Contains(TEXT("frost")))return ECireSchool::Frost;
    if(S.Contains(TEXT("spark"))||S.Contains(TEXT("lightning")))return ECireSchool::Storm;
    if(S.Contains(TEXT("seismic"))||S.Contains(TEXT("last_stand"))||S.Contains(TEXT("stone"))||S.Contains(TEXT("tremor"))||S.Contains(TEXT("totem"))||S.Contains(TEXT("ore"))||S.Contains(TEXT("summoned_wall"))||S.Contains(TEXT("runic_wall")))return ECireSchool::Earth;
    if(S.Contains(TEXT("blood"))||S.Contains(TEXT("red_moon"))||S.Contains(TEXT("troll"))||S.Contains(TEXT("executioner")))return ECireSchool::Blood;
    if(S.Contains(TEXT("grove"))||S.Contains(TEXT("dryad"))||S.Contains(TEXT("thorn"))||S.Contains(TEXT("seed"))||S.Contains(TEXT("spring")))return ECireSchool::Nature;
    if(S.Contains(TEXT("spectral_hunt"))||S.Contains(TEXT("spirit"))||S.Contains(TEXT("whisp"))||S.Contains(TEXT("kindred"))||S.Contains(TEXT("spectral_pack"))||S.Contains(TEXT("oathbound")))return ECireSchool::Spirit;
    if(S.Contains(TEXT("shadow"))||S.Contains(TEXT("grave"))||S.Contains(TEXT("spectral")))return ECireSchool::Shadow;
    if(S.Contains(TEXT("restoring"))||S.Contains(TEXT("renewal"))||S.Contains(TEXT("purify")))return ECireSchool::Life;
    if(S.Contains(TEXT("bastion"))||S.Contains(TEXT("sanctuary"))||S.Contains(TEXT("guard"))||S.Contains(TEXT("ashen"))||S.Contains(TEXT("protection"))||
       S.Contains(TEXT("aegis"))||S.Contains(TEXT("second_wind"))||S.Contains(TEXT("keeper"))||S.Contains(TEXT("dawn"))||S.Contains(TEXT("shield_slam")))return ECireSchool::Holy;
    if(S.Contains(TEXT("arcane"))||S.Contains(TEXT("scholar"))||S.Contains(TEXT("starfall"))||S.Contains(TEXT("rift")))return ECireSchool::Arcane;
    return ECireSchool::Steel;
}
ECireSchool MonsterSchool(const FString& Id,const FCireNPCArchetype* A,const FCireNPCAbility* Ab)
{
    const FString Race=A?A->RaceId.ToString():FString();
    const FString Buff=Ab?Ab->Buff.ToString():FString();
    if(Buff==TEXT("npc_spores"))return ECireSchool::Poison;
    if(Buff==TEXT("npc_thorns")||(Race==TEXT("blightwood")&&Buff==TEXT("npc_rooted")))return ECireSchool::Nature;
    if(Buff==TEXT("npc_runic"))return ECireSchool::Arcane;
    if(Buff==TEXT("npc_dragonfire")||(Buff==TEXT("npc_scaleward")&&Race==TEXT("drakkari")))return ECireSchool::Fire;
    if(Buff==TEXT("npc_tide")||Buff==TEXT("npc_ink"))return ECireSchool::Tide;
    if(Buff==TEXT("npc_void")||Buff==TEXT("npc_mind"))return ECireSchool::Void;
    if(Buff==TEXT("npc_profane"))return ECireSchool::Shadow;
    if(Buff==TEXT("npc_bloodlust"))return ECireSchool::Blood;
    if(Race==TEXT("drowned_deep"))return ECireSchool::Tide;
    if(Race==TEXT("voidborn"))return ECireSchool::Void;
    if(Race==TEXT("blightwood"))return ECireSchool::Poison;
    if(Race==TEXT("drakkari"))return ECireSchool::Fire;
    if(Race==TEXT("fallen_order"))return ECireSchool::Shadow;
    if(Race==TEXT("ironhide"))return ECireSchool::Blood;
    if(Race==TEXT("stoneborn"))return ECireSchool::Earth;
    if(Race==TEXT("feral_kin"))return ECireSchool::Nature;
    if(Race==TEXT("hollow"))return Buff==TEXT("npc_sundered")?ECireSchool::Steel:ECireSchool::Shadow;
    // Legacy NPC archetypes (no race).
    if(Id.Contains(TEXT("shadow"))||Id.Contains(TEXT("bolt")))return ECireSchool::Shadow;
    if(Id.Contains(TEXT("blight"))||Id.Contains(TEXT("pool")))return ECireSchool::Poison;
    if(Id.Contains(TEXT("slam"))||Id.Contains(TEXT("charge")))return ECireSchool::Earth;
    if(Id.Contains(TEXT("mend")))return ECireSchool::Life;
    if(Id.Contains(TEXT("guard"))||Id.Contains(TEXT("provoke"))||Id.Contains(TEXT("wall")))return ECireSchool::Holy;
    return ECireSchool::Steel;
}
void FromArea(FCireHitShape& S,const FCireAreaSpec& A)
{
    S.Radius=A.Radius;S.Length=A.Length;S.Width=A.Width;S.Angle=A.ConeAngleDegrees;S.Polygon=A.CustomPolygon;
    switch(A.Shape)
    {
    case ECireAreaShape::Circle:S.Kind=ECireHitShape::Circle;break;
    case ECireAreaShape::Cone:S.Kind=ECireHitShape::Cone;S.bFromCaster=true;break;
    case ECireAreaShape::Line:S.Kind=ECireHitShape::Line;S.bFromCaster=true;break;
    case ECireAreaShape::Square:S.Kind=ECireHitShape::Square;break;
    default:S.Kind=ECireHitShape::Custom;break;
    }
    S.WarningSeconds=A.WarningSeconds;S.LingerSeconds=A.bPersistent?A.DurationSeconds:.35f;
}
void FromSkillshot(FCireHitShape& S,const FCireSkillshotSpec& P,float Warning)
{
    S.Kind=ECireHitShape::Line;S.bFromCaster=true;S.bProjectile=true;
    S.Width=P.Radius*2;S.Length=FMath::Min(P.MaxRange,P.Speed*P.LifetimeSeconds);
    S.Speed=P.Speed;S.WarningSeconds=Warning;S.LingerSeconds=.4f;
}
}

float FCireHitShape::ImpactSeconds(float Distance) const
{
    if(bProjectile&&Speed>0)return WarningSeconds+FMath::Min(Distance,Length)/Speed;
    if(Kind==ECireHitShape::Unit&&Speed>0)return .25f+Distance/Speed;
    return FMath::Max(WarningSeconds,.12f);
}

FCireAreaSpec FCireHitShape::AsArea() const
{
    FCireAreaSpec A;A.Radius=FMath::Max(1.f,Radius);A.Length=FMath::Max(1.f,Length);A.Width=FMath::Max(1.f,Width);
    A.ConeAngleDegrees=FMath::Clamp(Angle>0?Angle:75.f,1.f,179.f);A.CustomPolygon=Polygon;
    switch(Kind)
    {
    case ECireHitShape::Cone:A.Shape=ECireAreaShape::Cone;break;
    case ECireHitShape::Line:A.Shape=ECireAreaShape::Line;break;
    case ECireHitShape::Square:A.Shape=ECireAreaShape::Square;break;
    case ECireHitShape::Custom:A.Shape=ECireAreaShape::Custom;break;
    default:A.Shape=ECireAreaShape::Circle;break;
    }
    return A;
}

const FCireNPCArchetype* CireAbilityShapes::FindOwner(FName AbilityId,const FCireNPCAbility** OutAbility)
{
    // Races first (their ids are unique), then the base archetypes in a stable order.
    for(const FName Race:CireRaces::Get().Order)if(const auto* R=CireRaces::FindRace(Race))
        for(const FName Unit:R->Units)if(const auto* A=CireNPCArchetypes::Find(Unit))
            if(const auto* Ab=A->FindAbility(AbilityId)){if(OutAbility)*OutAbility=Ab;return A;}
    const auto& Db=CireNPCArchetypes::Get();
    TArray<FName> Keys;Db.Archetypes.GetKeys(Keys);Keys.Sort([](FName A,FName B){return A.LexicalLess(B);});
    for(const FName K:Keys)if(const auto* Ab=Db.Archetypes[K].FindAbility(AbilityId)){if(OutAbility)*OutAbility=Ab;return &Db.Archetypes[K];}
    return nullptr;
}

ECireSchool CireAbilityShapes::SchoolFor(FName Id,const FCireNPCArchetype* Caster)
{
    const FString S=Norm(Id);
    if(const FDbEntry* E=DbFind(FName(*S));E&&E->bSchool)return E->School; // Ability Database wins
    const FCireNPCAbility* Ab=Caster?Caster->FindAbility(Id):nullptr;
    if(!Ab&&!Caster&&!S.IsEmpty())
    {
        // Champion ids never collide with monster ids; only fall back to NPC data for unknown ids.
        bool bChampion=false;for(const FName C:ChampionAbilityIds())if(Norm(C)==S){bChampion=true;break;}
        if(!bChampion)Caster=FindOwner(Id,&Ab);
    }
    if(Ab)return MonsterSchool(S,Caster,Ab);
    if(S.StartsWith(TEXT("npc_"))||S.StartsWith(TEXT("boss_")))return MonsterSchool(S,nullptr,nullptr);
    // Skillshot visual styles (CombatTuning "visualStyle") name their school directly.
    static const TMap<FString,ECireSchool> Styles={{TEXT("fire"),ECireSchool::Fire},{TEXT("frost"),ECireSchool::Frost},{TEXT("arrow"),ECireSchool::Steel},
        {TEXT("lance"),ECireSchool::Steel},{TEXT("arcane"),ECireSchool::Arcane},{TEXT("shadow"),ECireSchool::Shadow},{TEXT("poison"),ECireSchool::Poison},
        {TEXT("holy"),ECireSchool::Holy},{TEXT("storm"),ECireSchool::Storm},{TEXT("nature"),ECireSchool::Nature},{TEXT("tide"),ECireSchool::Tide},{TEXT("void"),ECireSchool::Void}};
    if(const ECireSchool* Style=Styles.Find(S))return *Style;
    if(S==TEXT("bow")||S==TEXT("basic_bow")||S==TEXT("sword")||S==TEXT("basic_sword")||S==TEXT("lance")||S==TEXT("basic_lance"))return ECireSchool::Steel;
    if(S==TEXT("arcane")||S==TEXT("basic_arcane"))return ECireSchool::Arcane;
    return ChampionSchool(S);
}

FLinearColor CireAbilityShapes::SchoolColor(ECireSchool School)
{
    static const FLinearColor Colors[]={
        {1.1f,.64f,.24f,1},{2.6f,.32f,.045f,1},{.13f,.88f,1.65f,1},
        {.45f,.55f,2.6f,1},{.51f,.08f,.92f,1},{.4f,1.35f,.82f,1},
        {1.85f,1.1f,.27f,1},{.28f,.85f,.085f,1},{.46f,.31f,1.65f,1},
        {1.15f,.56f,.16f,1},{.16f,1.12f,.45f,1},{.12f,1.2f,1.05f,1},{1.5f,.085f,.12f,1},
        {.08f,.95f,1.05f,1},{.62f,.12f,1.35f,1}};
    static_assert(UE_ARRAY_COUNT(Colors)==static_cast<int32>(ECireSchool::Count),"school colours");
    return Colors[FMath::Clamp(static_cast<int32>(School),0,static_cast<int32>(ECireSchool::Count)-1)];
}

FString CireAbilityShapes::ShapeName(ECireHitShape Kind)
{
    static const TCHAR* Names[]={TEXT("none"),TEXT("self"),TEXT("unit"),TEXT("circle"),TEXT("cone"),TEXT("line"),TEXT("square"),TEXT("custom"),TEXT("chain")};
    const int32 I=static_cast<int32>(Kind);return I>=0&&I<UE_ARRAY_COUNT(Names)?Names[I]:TEXT("none");
}
bool CireAbilityShapes::ParseSchool(const FString& In,ECireSchool& Out)
{
    const FString T=In.ToLower().TrimStartAndEnd();
    static const TMap<FString,ECireSchool> Map={{TEXT("physical"),ECireSchool::Steel},{TEXT("steel"),ECireSchool::Steel},{TEXT("fire"),ECireSchool::Fire},
        {TEXT("cold"),ECireSchool::Frost},{TEXT("frost"),ECireSchool::Frost},{TEXT("ice"),ECireSchool::Frost},{TEXT("storm"),ECireSchool::Storm},
        {TEXT("lightning"),ECireSchool::Storm},{TEXT("shadow"),ECireSchool::Shadow},{TEXT("life"),ECireSchool::Life},{TEXT("heal"),ECireSchool::Life},
        {TEXT("healing"),ECireSchool::Life},{TEXT("holy"),ECireSchool::Holy},{TEXT("light"),ECireSchool::Holy},{TEXT("poison"),ECireSchool::Poison},
        {TEXT("arcane"),ECireSchool::Arcane},{TEXT("earth"),ECireSchool::Earth},{TEXT("stone"),ECireSchool::Earth},{TEXT("nature"),ECireSchool::Nature},
        {TEXT("spirit"),ECireSchool::Spirit},{TEXT("blood"),ECireSchool::Blood},{TEXT("water"),ECireSchool::Tide},{TEXT("tide"),ECireSchool::Tide},
        {TEXT("void"),ECireSchool::Void}};
    if(const ECireSchool* F=Map.Find(T)){Out=*F;return true;}
    return false;
}

bool CireAbilityShapes::ParseDatabase(const FString& Json,TMap<FName,TPair<ECireSchool,FCireHitShape>>& Out,FString* Error)
{
    Out.Reset();TSharedPtr<FJsonObject> Root;
    if(Json.Len()>8*1024*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid()){if(Error)*Error=TEXT("not a JSON object");return false;}
    TArray<TPair<FString,TSharedPtr<FJsonObject>>> Rows;
    const TArray<TSharedPtr<FJsonValue>>* List=nullptr;const TSharedPtr<FJsonObject>* Map=nullptr;
    if(Root->TryGetArrayField(TEXT("abilities"),List)){for(const auto& V:*List){const TSharedPtr<FJsonObject>* O=nullptr;FString Id;if(V->TryGetObject(O)&&(*O)->TryGetStringField(TEXT("id"),Id))Rows.Add({Id,*O});}}
    else if(Root->TryGetObjectField(TEXT("abilities"),Map)){for(const auto& Pair:(*Map)->Values){const TSharedPtr<FJsonObject>* O=nullptr;if(Pair.Value->TryGetObject(O))Rows.Add({FString(Pair.Key),*O});}}
    else{if(Error)*Error=TEXT("needs an abilities array or object");return false;}
    auto Num=[](const TSharedPtr<FJsonObject>& O,std::initializer_list<const TCHAR*> Keys,float Default){double V=0;for(const TCHAR* K:Keys)if(O->TryGetNumberField(K,V)&&FMath::IsFinite(V))return static_cast<float>(V);return Default;};
    for(const auto& Row:Rows)
    {
        FCireHitShape Shape;ECireSchool School=ECireSchool::Count;FString Text;
        if(Row.Value->TryGetStringField(TEXT("school"),Text))ParseSchool(Text,School);
        const TSharedPtr<FJsonObject>* V=nullptr;
        if(Row.Value->TryGetObjectField(TEXT("voidZone"),V)||Row.Value->TryGetObjectField(TEXT("void"),V))
        {
            Shape.VoidOuter=FMath::Clamp(Num(*V,{TEXT("outerRadius"),TEXT("slowRadius"),TEXT("outer")},0.f),0.f,2000.f);
            Shape.VoidInner=FMath::Clamp(Num(*V,{TEXT("innerRadius"),TEXT("stunRadius"),TEXT("inner")},0.f),0.f,2000.f);
            Shape.VoidSeconds=FMath::Clamp(Num(*V,{TEXT("duration"),TEXT("durationSeconds"),TEXT("seconds")},2.f),.2f,30.f);
            bool B=false;if((*V)->TryGetBoolField(TEXT("selfHeal"),B)||(*V)->TryGetBoolField(TEXT("heal"),B))Shape.bVoidHeal=B;
            if(Num(*V,{TEXT("selfHeal"),TEXT("heal")},0.f)>0)Shape.bVoidHeal=true;
            FString At;if((*V)->TryGetStringField(TEXT("at"),At))Shape.bVoidAtOrigin=At.Equals(TEXT("origin"),ESearchCase::IgnoreCase);
        }
        Out.Add(FName(*Row.Key.ToLower()),TPair<ECireSchool,FCireHitShape>(School,Shape));
    }
    return true;
}

bool CireAbilityShapes::ReloadDatabase(FString* Error)
{
    bDatabaseLoaded=true;GDatabase.Reset();
    FString Json;const FString Path=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/Abilities.json"));
    if(!FFileHelper::LoadFileToString(Json,*Path)){if(Error)*Error=TEXT("Content/Data/Abilities.json not present (built-in schools)");return false;}
    TMap<FName,TPair<ECireSchool,FCireHitShape>> Rows;
    if(!ParseDatabase(Json,Rows,Error))return false;
    for(const auto& Pair:Rows){FDbEntry E;E.bSchool=Pair.Value.Key!=ECireSchool::Count;E.School=E.bSchool?Pair.Value.Key:ECireSchool::Steel;E.Void=Pair.Value.Value;GDatabase.Add(Pair.Key,E);}
    return true;
}
#if !UE_BUILD_SHIPPING
bool CireAbilityShapes::DebugUseDatabase(const FString& Json)
{
    TMap<FName,TPair<ECireSchool,FCireHitShape>> Rows;if(!ParseDatabase(Json,Rows))return false;
    bDatabaseLoaded=true;GDatabase.Reset();
    for(const auto& Pair:Rows){FDbEntry E;E.bSchool=Pair.Value.Key!=ECireSchool::Count;E.School=E.bSchool?Pair.Value.Key:ECireSchool::Steel;E.Void=Pair.Value.Value;GDatabase.Add(Pair.Key,E);}
    return true;
}
#endif
int32 CireAbilityShapes::DatabaseCount(){if(!bDatabaseLoaded)ReloadDatabase();return GDatabase.Num();}

FString CireAbilityShapes::SchoolName(ECireSchool School)
{
    static const TCHAR* Names[]={TEXT("steel"),TEXT("fire"),TEXT("frost"),TEXT("storm"),TEXT("shadow"),TEXT("life"),TEXT("holy"),TEXT("poison"),
        TEXT("arcane"),TEXT("earth"),TEXT("nature"),TEXT("spirit"),TEXT("blood"),TEXT("tide"),TEXT("void")};
    const int32 I=static_cast<int32>(School);return I>=0&&I<UE_ARRAY_COUNT(Names)?Names[I]:TEXT("steel");
}

TArray<FName> CireAbilityShapes::ChampionAbilityIds()
{
    static TArray<FName> Cached;
    if(!Cached.IsEmpty())return Cached;
    for(const auto& P:CireChampionRoster::All())
    {
        for(const auto& A:P.Actives)if(A.IsImplemented())Cached.AddUnique(FName(*A.Id));
        if(P.Ultimate.IsImplemented())Cached.AddUnique(FName(*P.Ultimate.Id));
        if(P.Passive.IsImplemented())Cached.AddUnique(FName(*P.Passive.Id));
    }
    for(const TCHAR* Id:{TEXT("blight_sigil"),TEXT("second_wind"),TEXT("last_stand"),TEXT("challenge_of_iron"),TEXT("seismic_reprisal"),TEXT("starfall"),
        TEXT("spectral_hunt"),TEXT("mass_aegis"),TEXT("wellspring"),TEXT("basic_sword"),TEXT("basic_bow"),TEXT("basic_lance"),TEXT("basic_arcane")})Cached.AddUnique(FName(Id));
    return Cached;
}

namespace { FCireHitShape DescribeMonsterRaw(const FCireNPCAbility& A,const FCireNPCArchetype* Caster); FCireHitShape DescribeRaw(FName Id,const FCireNPCArchetype* Caster); }
namespace
{
void Finish(FCireHitShape& S)
{
    static const TSet<FName> Heals={TEXT("restoring_light"),TEXT("purify"),TEXT("renewal"),TEXT("sanctuary"),TEXT("wellspring"),
        TEXT("second_wind"),TEXT("last_stand"),TEXT("bastion_of_dawn")};
    if(Heals.Contains(S.Id))S.bHeal=true;
    S.bBuff=!S.bHeal&&!S.bHostileOnly&&S.Kind!=ECireHitShape::None;
    ApplyDatabase(S.Id,S);
}
}
FCireHitShape CireAbilityShapes::DescribeMonster(const FCireNPCAbility& A,const FCireNPCArchetype* Caster)
{
    FCireHitShape S=DescribeMonsterRaw(A,Caster);
    if(A.Kind==ECireNPCAbilityKind::HealAlly)S.bHeal=true;
    Finish(S);return S;
}
FCireHitShape CireAbilityShapes::Describe(FName Id,const FCireNPCArchetype* Caster)
{
    const FString Low=Norm(Id);
    if(Caster)if(const auto* Ab=Caster->FindAbility(Id))return DescribeMonster(*Ab,Caster);
    FCireHitShape S=DescribeRaw(FName(*Low),Caster);
    if(S.Kind==ECireHitShape::None&&!ACireHero::IsPassive(Low)){const FCireNPCAbility* Ab=nullptr;if(const auto* Owner=FindOwner(Id,&Ab);Owner&&Ab)return DescribeMonster(*Ab,Owner);}
    // Monster ids resolved through DescribeRaw's fallback already went through DescribeMonster.
    Finish(S);
    return S;
}
namespace {
FCireHitShape DescribeMonsterRaw(const FCireNPCAbility& A,const FCireNPCArchetype* Caster)
{
    FCireHitShape S;S.Id=A.Id;S.School=MonsterSchool(Norm(A.Id),Caster,&A);S.WarningSeconds=A.CastTime;S.LingerSeconds=.35f;
    switch(A.Kind)
    {
    case ECireNPCAbilityKind::Cone:S.Kind=ECireHitShape::Cone;S.bFromCaster=true;S.Radius=A.Radius;S.Angle=A.Angle;break;
    case ECireNPCAbilityKind::TargetCircle:S.Kind=ECireHitShape::Circle;S.bAtTarget=true;S.Radius=A.Radius;break;
    case ECireNPCAbilityKind::SelfCircle:case ECireNPCAbilityKind::Rally:case ECireNPCAbilityKind::Provoke:
        S.Kind=ECireHitShape::Circle;S.bFromCaster=true;S.Radius=A.Radius;S.bHostileOnly=A.Kind!=ECireNPCAbilityKind::Rally;break;
    case ECireNPCAbilityKind::Charge:case ECireNPCAbilityKind::Pull:
        // Gameplay shortens the lane to (distance to target + 150) at cast time; Length is the authored maximum.
        S.Kind=ECireHitShape::Line;S.bFromCaster=true;S.Length=A.Length;S.Width=A.Width;break;
    case ECireNPCAbilityKind::Projectile:
        if(const auto* P=CireSkillTuning::FindSkillshot(A.Skillshot))FromSkillshot(S,*P,A.CastTime);
        else S.Kind=ECireHitShape::Unit;
        break;
    case ECireNPCAbilityKind::Guard:case ECireNPCAbilityKind::HealAlly:S.Kind=ECireHitShape::Unit;S.bHostileOnly=false;break;
    case ECireNPCAbilityKind::Melee:S.Kind=ECireHitShape::Unit;S.WarningSeconds=0;break;
    case ECireNPCAbilityKind::Summon:S.Kind=ECireHitShape::Self;S.bHostileOnly=false;S.LingerSeconds=1.2f;break;
    default:S.Kind=ECireHitShape::Self;S.bHostileOnly=false;break; // enrage, shield wall, disengage
    }
    if(A.DamagePerSecond>0&&A.Duration>0)S.LingerSeconds=A.Duration;
    return S;
}

FCireHitShape DescribeRaw(FName Id,const FCireNPCArchetype* Caster)
{
    using namespace CireAbilityShapes;
    const FString S=Norm(Id);
    FCireHitShape R;R.Id=Id;R.School=SchoolFor(Id);
    auto Circle=[&](float Radius,bool bSelf){R.Kind=ECireHitShape::Circle;R.Radius=Radius;R.bFromCaster=bSelf;R.bAtTarget=!bSelf;};
    if(ACireHero::IsPassive(S)){R.Kind=ECireHitShape::None;return R;}
    if(const auto* A=CireAbilityLibrary::Find(S)){FromArea(R,A->Area);R.bGroundAim=true;return R;}
    if(const auto* P=CireSkillTuning::FindSkillshot(S);P&&(S==TEXT("ember_lance")||S==TEXT("frost_bind")||S==TEXT("piercing_shot")))
    {FromSkillshot(R,*P,P->WarningSeconds);R.bGroundAim=true;return R;}
    if(S.StartsWith(TEXT("basic_")))
    {
        R.Kind=ECireHitShape::Unit;
        if(S!=TEXT("basic_sword"))if(const auto* P=CireSkillTuning::FindSkillshot(S==TEXT("basic_bow")?TEXT("basic_arrow"):S))R.Speed=P->Speed;
        R.LingerSeconds=.3f;return R;
    }
    if(S==TEXT("summoned_wall")||S==TEXT("protection_dome"))
    {
        if(const auto* C=CireSkillTuning::FindConstruct(S))
        {
            const float X=C->Depth*.5f,Y=C->Width*.5f;R.Kind=ECireHitShape::Custom;R.Polygon={{-X,-Y},{X,-Y},{X,Y},{-X,Y}};
            R.bGroundAim=true;R.bHostileOnly=false;R.LingerSeconds=FMath::Min(C->LifetimeSeconds,2.f);
        }
        return R;
    }
    if(S==TEXT("oathbound_guardian")||S==TEXT("spectral_pack"))
    {
        if(const auto* M=CireSkillTuning::FindSummon(S)){Circle(M->Count==1?45.f:230.f,false);R.bGroundAim=true;R.bHostileOnly=false;R.LingerSeconds=1.6f;}
        return R;
    }
    if(const auto* Role=CireSkillTuning::FindRoleSkill(S))
    {
        if(S==TEXT("starfall")){Circle(Role->Radius,false);R.bGroundAim=true;R.WarningSeconds=Role->WarningSeconds;R.LingerSeconds=.4f;return R;}
        if(S==TEXT("seismic_reprisal")){Circle(Role->Radius,true);R.WarningSeconds=Role->WarningSeconds;R.LingerSeconds=.4f;return R;}
        if(S==TEXT("challenge_of_iron")){Circle(Role->Radius,true);R.LingerSeconds=1.f;return R;}
        if(S==TEXT("mass_aegis")){Circle(Role->Radius,true);R.bHostileOnly=false;R.LingerSeconds=1.f;return R;}
        if(S==TEXT("wellspring")){R.Kind=ECireHitShape::Unit;R.bHostileOnly=false;R.LingerSeconds=1.f;return R;}
        if(S==TEXT("spectral_hunt")){R.Kind=ECireHitShape::Unit;R.LingerSeconds=1.6f;return R;}
        R.Kind=ECireHitShape::Self;R.bHostileOnly=false;R.LingerSeconds=1.f;return R; // second_wind, last_stand
    }
    if(S==TEXT("cleaving_strike")){Circle(CleaveRadius,true);R.LingerSeconds=.6f;return R;}
    if(S==TEXT("war_cry")){Circle(WarCryRadius,true);R.LingerSeconds=1.f;return R;}
    if(S==TEXT("sanctuary")){Circle(SanctuaryRadius,true);R.bHostileOnly=false;R.LingerSeconds=1.f;return R;}
    if(S==TEXT("bastion_of_dawn")){Circle(BastionRadius,true);R.bHostileOnly=false;R.LingerSeconds=1.f;return R;}
    if(S==TEXT("renewal")){Circle(RenewalRadius,true);R.bHostileOnly=false;R.LingerSeconds=1.f;return R;}
    if(S==TEXT("cataclysm")){Circle(CataclysmRadius,false);R.LingerSeconds=1.2f;return R;}
    if(S==TEXT("chain_spark")){R.Kind=ECireHitShape::Chain;R.Radius=ChainRadius;R.bAtTarget=true;R.LingerSeconds=.6f;return R;}
    if(S==TEXT("iron_guard")){R.Kind=ECireHitShape::Self;R.bHostileOnly=false;R.LingerSeconds=1.f;return R;}
    if(S==TEXT("restoring_light")||S==TEXT("purify")){R.Kind=ECireHitShape::Unit;R.bHostileOnly=false;R.LingerSeconds=.8f;return R;}
    if(S==TEXT("shield_slam")||S==TEXT("shadow_step")||S==TEXT("executioners_verdict")){R.Kind=ECireHitShape::Unit;R.LingerSeconds=.6f;return R;}
    // Monster ability id (cue) without a known caster.
    const FCireNPCAbility* Ab=nullptr;
    if(const auto* Owner=FindOwner(Id,&Ab);Owner&&Ab)return DescribeMonster(*Ab,Owner);
    return R;
}
} // namespace
