#include "CireFabVFX.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h" // telegraphs: user parameter types
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "CireAbilityDB.h" // kits-complete: display-name lookups
#include "CireRaces.h" // pack-usage: monster-ability coverage test
#include "CireNPCArchetypes.h"

namespace
{
TAutoConsoleVariable<int32> CVarFabVFX(TEXT("cire.FabVFX"), 1,
    TEXT("1: overlay Niagara systems from the locally installed Fab VFX packs (Content/Data/FabVFX.json). 0: procedural presentation only."));

struct FTable
{
    bool bLoaded = false;
    TMap<FString, CireFabVFX::FEntry> Schools; // "<school>.<role>"
    TMap<FString, CireFabVFX::FEntry> Buffs;
    TMap<FString, CireFabVFX::FEntry> Abilities; // fab-coverage + kits-complete: "<skill id>.<role>"
    TMap<FString, TWeakObjectPtr<UFXSystemAsset>> Resolved;
    TSet<FString> Unresolvable;
    TMap<FString, FString> GroundExcluded; // telegraphs: object path -> why it never sits on a zone (square/diamond, does not scale)
    TMap<FString, float> GroundRadius; // telegraphs: measured XY footprint radius at scale 1 (FabVFX.json "groundRadius")
};
FTable& Table() { static FTable T; return T; }

CireFabVFX::FEntry ParseEntry(const TSharedPtr<FJsonValue>& Value)
{
    CireFabVFX::FEntry E;
    if(!Value.IsValid())return E;
    if(Value->Type==EJson::String){E.Candidates.Add(Value->AsString());return E;}
    if(Value->Type==EJson::Array){for(const auto& V:Value->AsArray())if(V->Type==EJson::String)E.Candidates.Add(V->AsString());return E;}
    const TSharedPtr<FJsonObject> O=Value->AsObject();
    if(!O.IsValid())return E;
    const TArray<TSharedPtr<FJsonValue>>* Paths=nullptr;
    if(O->TryGetArrayField(TEXT("paths"),Paths))for(const auto& V:*Paths)if(V->Type==EJson::String)E.Candidates.Add(V->AsString());
    double Scale=1;if(O->TryGetNumberField(TEXT("scale"),Scale))E.Scale=FMath::Clamp(static_cast<float>(Scale),.05f,20.f);
    const TArray<TSharedPtr<FJsonValue>>* Tint=nullptr;
    if(O->TryGetArrayField(TEXT("tint"),Tint)&&Tint->Num()>=3)
        E.Tint=FLinearColor((*Tint)[0]->AsNumber(),(*Tint)[1]->AsNumber(),(*Tint)[2]->AsNumber(),1);
    double Strength=1;if(O->TryGetNumberField(TEXT("tintStrength"),Strength))E.TintStrength=FMath::Clamp(static_cast<float>(Strength),0.f,1.f); // pack-usage
    return E;
}

void Load()
{
    FTable& T=Table();
    T=FTable();T.bLoaded=true;
    FString Text;
    if(!FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/FabVFX.json"))))return;
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())return;
    const TSharedPtr<FJsonObject>* Schools=nullptr;
    if(Root->TryGetObjectField(TEXT("schools"),Schools))
        for(const auto& S:(*Schools)->Values)
            if(const TSharedPtr<FJsonObject> Roles=S.Value->AsObject())
                for(const auto& R:Roles->Values)
                {
                    CireFabVFX::FEntry E=ParseEntry(R.Value);
                    if(E.Candidates.Num())T.Schools.Add(FString(S.Key.ToView()).ToLower()+TEXT(".")+FString(R.Key.ToView()).ToLower(),MoveTemp(E));
                }
    const TSharedPtr<FJsonObject>* Abilities=nullptr; // fab-coverage
    if(Root->TryGetObjectField(TEXT("abilities"),Abilities))
        for(const auto& A:(*Abilities)->Values)
            if(const TSharedPtr<FJsonObject> Roles=A.Value->AsObject())
                for(const auto& R:Roles->Values)
                {
                    CireFabVFX::FEntry E=ParseEntry(R.Value);
                    if(E.Candidates.Num())T.Abilities.Add(FString(A.Key.ToView()).ToLower()+TEXT(".")+FString(R.Key.ToView()).ToLower(),MoveTemp(E));
                }
    // ability-expansion: FabVFX.expansion.json "abilities" (same shape) signs the expansion pool; FabVFX.json wins on a clash.
    {
        FString XText;TSharedPtr<FJsonObject> XRoot;const TSharedPtr<FJsonObject>* XAbilities=nullptr;
        if(FFileHelper::LoadFileToString(XText,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/FabVFX.expansion.json")))&&
           FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(XText),XRoot)&&XRoot.IsValid()&&XRoot->TryGetObjectField(TEXT("abilities"),XAbilities))
            for(const auto& A:(*XAbilities)->Values)
                if(const TSharedPtr<FJsonObject> Roles=A.Value->AsObject())
                    for(const auto& R:Roles->Values)
                    {
                        const FString Key=FString(A.Key.ToView()).ToLower()+TEXT(".")+FString(R.Key.ToView()).ToLower();
                        CireFabVFX::FEntry E=ParseEntry(R.Value);
                        if(E.Candidates.Num()&&!T.Abilities.Contains(Key))T.Abilities.Add(Key,MoveTemp(E));
                    }
    }
    // telegraphs: curated ground overlays. "groundRadius" (path -> cm at scale 1, measured by RunSpellGallery.py --fab-ground)
    // is the allow-list; "groundExcluded" (path -> reason) documents the systems that must never sit on a zone.
    const TSharedPtr<FJsonObject>* Radii=nullptr;
    if(Root->TryGetObjectField(TEXT("groundRadius"),Radii))
        for(const auto& R:(*Radii)->Values){double V=0;if(R.Value->TryGetNumber(V)&&V>1)T.GroundRadius.Add(FString(R.Key.ToView()),static_cast<float>(V));}
    const TSharedPtr<FJsonObject>* Excluded=nullptr;
    if(Root->TryGetObjectField(TEXT("groundExcluded"),Excluded))
        for(const auto& R:(*Excluded)->Values)T.GroundExcluded.Add(FString(R.Key.ToView()),R.Value->Type==EJson::String?R.Value->AsString():FString(TEXT("excluded")));
    const TSharedPtr<FJsonObject>* Buffs=nullptr;
    if(Root->TryGetObjectField(TEXT("buffs"),Buffs))
        for(const auto& B:(*Buffs)->Values)
        {
            CireFabVFX::FEntry E=ParseEntry(B.Value);
            if(E.Candidates.Num())T.Buffs.Add(FString(B.Key.ToView()).ToLower(),MoveTemp(E));
        }
}
FTable& Loaded(){FTable& T=Table();if(!T.bLoaded)Load();return T;}

bool PackagePresent(const FString& ObjectPath)
{
    const FString Package=FPackageName::ObjectPathToPackageName(ObjectPath);
    return FPackageName::IsValidLongPackageName(Package)&&FPackageName::DoesPackageExist(Package);
}
}

FString CireFabVFX::RoleName(ERole Role)
{
    static const TCHAR* Names[]={TEXT("cast"),TEXT("projectile"),TEXT("impact"),TEXT("area"),TEXT("aura")};
    const int32 I=static_cast<int32>(Role);return I>=0&&I<UE_ARRAY_COUNT(Names)?Names[I]:TEXT("cast");
}

bool CireFabVFX::Enabled()
{
    static const bool bOffByFlag=(FParse::Param(FCommandLine::Get(),TEXT("CireNoFabVFX"))||FParse::Param(FCommandLine::Get(),TEXT("CireNoFab")));
    return !bOffByFlag&&CVarFabVFX.GetValueOnGameThread()!=0;
}

void CireFabVFX::Reload(){Table().bLoaded=false;Load();}

const CireFabVFX::FEntry* CireFabVFX::Find(ECireSchool School, ERole Role)
{
    FTable& T=Loaded();
    if(const FEntry* E=T.Schools.Find(CireAbilityShapes::SchoolName(School)+TEXT(".")+RoleName(Role)))return E;
    // A school without its own art borrows the generic "default" set for that role.
    return T.Schools.Find(FString(TEXT("default."))+RoleName(Role));
}

const CireFabVFX::FEntry* CireFabVFX::FindAbility(FName Skill, ERole Role)
{
    FTable& T=Loaded();
    if(T.Abilities.IsEmpty()||Skill.IsNone())return nullptr;
    if(const FEntry* E=T.Abilities.Find(Skill.ToString().ToLower()+TEXT(".")+RoleName(Role)))return E;
    // kits-complete: combat events carry display names ("Gravewood Maul"); map them to the ability id.
    if(const FCireAbilityDef* D=CireAbilityDB::FindByName(Skill.ToString()))return T.Abilities.Find(D->Id.ToLower()+TEXT(".")+RoleName(Role));
    return nullptr;
}

const CireFabVFX::FEntry* CireFabVFX::FindBuff(const FString& Key)
{
    return Loaded().Buffs.Find(Key.ToLower());
}


const CireFabVFX::FEntry* CireFabVFX::FindFor(FName Skill, ECireSchool School, ERole Role)
{
    // The ability's signature system when its pack is installed, else the school's shared set.
    if(const FEntry* Own=FindAbility(Skill,Role);Own&&Resolve(Own))return Own;
    return Find(School,Role);
}

UFXSystemAsset* CireFabVFX::Resolve(const FEntry* Entry)
{
    if(!Entry)return nullptr;
    FTable& T=Loaded();
    for(const FString& Path:Entry->Candidates)
    {
        if(const TWeakObjectPtr<UFXSystemAsset>* Hit=T.Resolved.Find(Path);Hit&&Hit->IsValid())return Hit->Get();
        if(T.Unresolvable.Contains(Path))continue;
        // Niagara (Lord Enot, UrtanoVFX, SoftTofu, Hivemind) or Cascade (Kakky FX Variety Pack) system.
        UFXSystemAsset* System=PackagePresent(Path)?LoadObject<UFXSystemAsset>(nullptr,*Path,nullptr,LOAD_NoWarn|LOAD_Quiet):nullptr;
        if(System&&(System->IsA<UNiagaraSystem>()||System->IsA<UParticleSystem>())){T.Resolved.Add(Path,System);return System;}
        T.Unresolvable.Add(Path);
    }
    return nullptr;
}

UFXSystemAsset* CireFabVFX::ResolveSchool(ECireSchool School, ERole Role, float* OutScale)
{
    if(!Enabled())return nullptr;
    const FEntry* E=Find(School,Role);
    UFXSystemAsset* S=Resolve(E);
    if(OutScale)*OutScale=E?E->Scale:1.f;
    return S;
}

UFXSystemComponent* CireFabVFX::SpawnAttached(UFXSystemAsset* System, USceneComponent* Parent, FVector Offset, float Scale, bool bAutoDestroy)
{
    if(!System||!Parent||!Enabled())return nullptr;
    if(UNiagaraSystem* Niagara=Cast<UNiagaraSystem>(System))
        return UNiagaraFunctionLibrary::SpawnSystemAttached(Niagara,Parent,NAME_None,Offset,FRotator::ZeroRotator,
            FVector(Scale),EAttachLocation::KeepRelativeOffset,bAutoDestroy,ENCPoolMethod::None,true,true);
    if(UParticleSystem* Cascade=Cast<UParticleSystem>(System))
        return UGameplayStatics::SpawnEmitterAttached(Cascade,Parent,NAME_None,Offset,FRotator::ZeroRotator,FVector(Scale),
            EAttachLocation::KeepRelativeOffset,bAutoDestroy,EPSCPoolMethod::None,true);
    return nullptr;
}

UFXSystemComponent* CireFabVFX::SpawnAt(UWorld* World, UFXSystemAsset* System, FVector Location, FRotator Rotation, float Scale)
{
    if(!System||!World||!Enabled())return nullptr;
    if(UNiagaraSystem* Niagara=Cast<UNiagaraSystem>(System))
        return UNiagaraFunctionLibrary::SpawnSystemAtLocation(World,Niagara,Location,Rotation,FVector(Scale),true,true,ENCPoolMethod::AutoRelease,true);
    if(UParticleSystem* Cascade=Cast<UParticleSystem>(System))
        return UGameplayStatics::SpawnEmitterAtLocation(World,Cascade,FTransform(Rotation,Location,FVector(Scale)),true,EPSCPoolMethod::AutoRelease,true);
    return nullptr;
}

void CireFabVFX::ApplyTint(UFXSystemComponent* Component, FLinearColor Tint)
{
    if(!Component||Tint.A<=0)return;
    // Vendor packs expose colour under different user parameter names; setting an absent one is a no-op.
    static const FName Names[]={TEXT("Color"),TEXT("Colour"),TEXT("MainColor"),TEXT("Main Color"),TEXT("User.Color"),TEXT("Tint")};
    for(const FName& N:Names)Component->SetColorParameter(N,Tint);
}

int32 CireFabVFX::Recolor(UFXSystemComponent* Component, FLinearColor Tint, float Strength)
{
    UNiagaraComponent* Niagara=Cast<UNiagaraComponent>(Component);
    UNiagaraSystem* System=Niagara?Niagara->GetAsset():nullptr;
    if(!System||Tint.A<=0||!FMath::IsFinite(Strength)||Strength<=0)return 0;
    Strength=FMath::Min(Strength,1.f);
    const FLinearColor Want=FLinearColor(Tint.R,Tint.G,Tint.B,1).LinearRGBToHSV(); // R=hue 0..360, G=saturation, B=value
    const FNiagaraUserRedirectionParameterStore& Store=System->GetExposedParameters();
    TArray<FNiagaraVariable> Params;Store.GetUserParameters(Params);
    int32 Changed=0;
    for(const FNiagaraVariable& P:Params)
    {
        if(P.GetType()!=FNiagaraTypeDefinition::GetColorDef())continue;
        const FLinearColor C=Store.GetParameterValue<FLinearColor>(P);
        const float Value=FMath::Max3(C.R,C.G,C.B);
        if(!FMath::IsFinite(Value)||Value<=0)continue;
        FLinearColor HSV=FLinearColor(C.R/Value,C.G/Value,C.B/Value,1).LinearRGBToHSV(); // normalised: HDR intensity kept in Value
        if(HSV.G<.12f)continue; // white / grey flashes and smoke stay neutral: the read of the burst is kept
        // Hue moves the short way round the wheel; saturation never drops below the source's own.
        float Delta=Want.R-HSV.R;if(Delta>180)Delta-=360;if(Delta<-180)Delta+=360;
        HSV.R=FMath::Fmod(HSV.R+Delta*Strength+360.f,360.f);
        HSV.G=FMath::Lerp(HSV.G,FMath::Max(HSV.G,Want.G),Strength);
        FLinearColor Out=HSV.HSVToLinearRGB();
        Out.R*=Value;Out.G*=Value;Out.B*=Value;Out.A=C.A;
        Niagara->SetVariableLinearColor(P.GetName(),Out);++Changed;
    }
    return Changed;
}

void CireFabVFX::ApplyEntryTint(UFXSystemComponent* Component, const FEntry& Entry)
{
    if(!Component||Entry.Tint.A<=0)return;
    Recolor(Component,Entry.Tint,Entry.TintStrength);
}

const CireFabVFX::FEntry* CireFabVFX::FindKey(const FString& Key, ERole Role)
{
    return Key.IsEmpty()?nullptr:Loaded().Abilities.Find(Key.ToLower()+TEXT(".")+RoleName(Role));
}

void CireFabVFX::Release(UFXSystemComponent* Component)
{
    if(!Component)return;
    // Pooled components (SpawnAt: auto-release) return to the pool on completion; auto-destroy is only for
    // unpooled ones (Niagara ensures !bAutoDestroy || PoolingMethod == None).
    if(UNiagaraComponent* Niagara=Cast<UNiagaraComponent>(Component)){if(Niagara->PoolingMethod==ENCPoolMethod::None)Niagara->SetAutoDestroy(true);}
    else if(UParticleSystemComponent* Cascade=Cast<UParticleSystemComponent>(Component)){if(Cascade->PoolingMethod==EPSCPoolMethod::None)Cascade->bAutoDestroy=true;}
    Component->Deactivate();
}

bool CireFabVFX::IsGroundOverlay(const UFXSystemAsset* System, FString* Why)
{
    auto No=[Why](const TCHAR* Reason){if(Why)*Why=Reason;return false;};
    // Cascade systems expose no colour parameters (cannot follow the brightness slider): never a ground overlay.
    if(!System||!System->IsA<UNiagaraSystem>())return No(TEXT("not a Niagara system"));
    const FString Path=System->GetPathName();
    if(const FString* Reason=Loaded().GroundExcluded.Find(Path)){if(Why)*Why=*Reason;return false;}
    if(!Loaded().GroundRadius.Contains(Path))return No(TEXT("unmeasured (no groundRadius entry)"));
    return true;
}

UFXSystemAsset* CireFabVFX::ResolveGround(const FEntry* Entry, FString* Why)
{
    if(Why)*Why=Entry?TEXT("no candidate installed"):TEXT("no area entry");
    if(!Entry||!Enabled())return nullptr;
    FString Last;
    for(const FString& Path:Entry->Candidates)
    {
        FEntry One;One.Candidates.Add(Path);
        UFXSystemAsset* System=Resolve(&One);
        if(!System)continue;
        if(IsGroundOverlay(System,&Last))return System;
        if(Why)*Why=FString::Printf(TEXT("%s: %s"),*System->GetName(),*Last);
    }
    return nullptr;
}

float CireFabVFX::NativeGroundRadius(const UFXSystemAsset* System)
{
    const float* R=System?Loaded().GroundRadius.Find(System->GetPathName()):nullptr;return R?*R:0.f;
}

float CireFabVFX::MeasureReach(const UFXSystemComponent* Component)
{
    if(!Component)return 0.f;
    const FBoxSphereBounds B=Component->Bounds;const FVector Here=Component->GetComponentLocation();
    return static_cast<float>(FVector2D(B.Origin.X-Here.X,B.Origin.Y-Here.Y).Size()+FMath::Max(B.BoxExtent.X,B.BoxExtent.Y));
}

int32 CireFabVFX::DimColors(UFXSystemComponent* Component, float Brightness)
{
    UNiagaraComponent* Niagara=Cast<UNiagaraComponent>(Component);
    UNiagaraSystem* System=Niagara?Niagara->GetAsset():nullptr;
    if(!System||!FMath::IsFinite(Brightness))return 0;
    Brightness=FMath::Clamp(Brightness,0.f,1.f);
    const FNiagaraUserRedirectionParameterStore& Store=System->GetExposedParameters();
    TArray<FNiagaraVariable> Params;Store.GetUserParameters(Params);
    int32 Scaled=0;
    for(const FNiagaraVariable& P:Params)
    {
        if(P.GetType()==FNiagaraTypeDefinition::GetColorDef())
        {
            FLinearColor C=Store.GetParameterValue<FLinearColor>(P);
            C.R*=Brightness;C.G*=Brightness;C.B*=Brightness; // alpha untouched: vendors use it for erosion/opacity masks
            Niagara->SetVariableLinearColor(P.GetName(),C);++Scaled;
        }
        else if(P.GetType()==FNiagaraTypeDefinition::GetFloatDef())
        {
            const FString Name=P.GetName().ToString();
            if(Name.Contains(TEXT("Emissive"))||Name.Contains(TEXT("Intensity"))||Name.Contains(TEXT("Brightness"))||Name.Contains(TEXT("Glow")))
            {Niagara->SetVariableFloat(P.GetName(),Store.GetParameterValue<float>(P)*Brightness);++Scaled;}
        }
    }
    return Scaled;
}

CireFabVFX::FCoverage CireFabVFX::Coverage()
{
    FCoverage C;FTable& T=Loaded();
    auto Count=[&](const FEntry& E,const FString& Key){++C.Configured;if(UFXSystemAsset* S=Resolve(&E)){++C.Resolved;C.Cascade+=S->IsA<UParticleSystem>()?1:0;}else C.Missing.Add(Key);};
    for(const auto& Pair:T.Schools)Count(Pair.Value,Pair.Key);
    for(const auto& Pair:T.Buffs)Count(Pair.Value,TEXT("buff.")+Pair.Key);
    TSet<FString> Ids;
    for(const auto& Pair:T.Abilities)
    {
        int32 Dot=INDEX_NONE;Pair.Key.FindLastChar(TEXT('.'),Dot);
        Ids.Add(Dot==INDEX_NONE?Pair.Key:Pair.Key.Left(Dot));
        ++C.AbilitySlots;if(Resolve(&Pair.Value))++C.AbilitySlotsResolved;
        Count(Pair.Value,TEXT("ability.")+Pair.Key);
    }
    C.Abilities=Ids.Num();
    return C;
}

#if !UE_BUILD_SHIPPING
bool CireFabVFX::RunTests(UWorld* World)
{
    bool bOk=true;
    auto Check=[&](bool b,const TCHAR* What){if(!b){bOk=false;UE_LOG(LogTemp,Error,TEXT("CIRE_FAB_VFX_TEST_FAIL %s"),What);}};
    Reload();
    // Missing packs must resolve quietly to nullptr: a clean clone has none of them.
    FEntry Fake;Fake.Candidates.Add(TEXT("/Game/__NoSuchFabPack__/NS_Missing.NS_Missing"));
    Check(Resolve(&Fake)==nullptr,TEXT("missing package resolves to null"));
    Check(Resolve(nullptr)==nullptr,TEXT("null entry"));
    Check(SpawnAt(World,nullptr,FVector::ZeroVector,FRotator::ZeroRotator,1)==nullptr,TEXT("null system spawns nothing"));
    // Every configured path is a well-formed /Game object path (typos would silently never resolve).
    for(const auto& Pair:Table().Schools)for(const FString& P:Pair.Value.Candidates)
        Check(P.StartsWith(TEXT("/Game/"))&&P.Contains(TEXT(".")),*FString::Printf(TEXT("bad path %s in %s"),*P,*Pair.Key));
    for(const auto& Pair:Table().Buffs)for(const FString& P:Pair.Value.Candidates)
        Check(P.StartsWith(TEXT("/Game/"))&&P.Contains(TEXT(".")),*FString::Printf(TEXT("bad path %s in buff %s"),*P,*Pair.Key));
    // fab-coverage: per-ability signatures use the same path rules, and an ability entry that does not
    // resolve must fall back to the school set rather than to nothing.
    for(const auto& Pair:Table().Abilities)for(const FString& P:Pair.Value.Candidates)
        Check(P.StartsWith(TEXT("/Game/"))&&P.Contains(TEXT(".")),*FString::Printf(TEXT("bad path %s in ability %s"),*P,*Pair.Key));
    {
        FTable& T=Table();
        FEntry Missing;Missing.Candidates.Add(TEXT("/Game/__NoSuchFabPack__/NS_Missing.NS_Missing"));
        T.Abilities.Add(TEXT("__fab_probe__.cast"),Missing);
        Check(FindAbility(TEXT("__fab_probe__"),ERole::Cast)!=nullptr,TEXT("ability entry found"));
        Check(FindFor(TEXT("__fab_probe__"),ECireSchool::Fire,ERole::Cast)==Find(ECireSchool::Fire,ERole::Cast),TEXT("unresolved ability entry falls back to the school"));
        Check(FindAbility(NAME_None,ERole::Cast)==nullptr,TEXT("no skill, no ability entry"));
        T.Abilities.Remove(TEXT("__fab_probe__.cast"));
    }
    Release(nullptr);
    // pack-usage: recolour variants, hit / kill / level-up signatures and full monster-ability coverage are data.
    {
        FEntry Tinted;Tinted.Tint=FLinearColor(1,0,0,1);
        Check(Recolor(nullptr,Tinted.Tint,1.f)==0,TEXT("recolour of no component changes nothing"));
        ApplyEntryTint(nullptr,Tinted);
        Check(FindKey(TEXT(""),ERole::Impact)==nullptr,TEXT("empty key finds nothing"));
        for(const auto& Pair:Table().Abilities)
        {
            const FEntry& E=Pair.Value;
            Check(FMath::IsFinite(E.Tint.R)&&FMath::IsFinite(E.Tint.G)&&FMath::IsFinite(E.Tint.B)&&E.Tint.GetMin()>=0,*FString::Printf(TEXT("tint of %s is a colour"),*Pair.Key));
            Check(E.TintStrength>=0&&E.TintStrength<=1,*FString::Printf(TEXT("tint strength of %s in 0..1"),*Pair.Key));
        }
        for(const TCHAR* Key:{TEXT("hit.flesh"),TEXT("hit.armor"),TEXT("hit.stone"),TEXT("hit.wood"),TEXT("hit.none"),TEXT("hit.flesh.crit"),
            TEXT("kill.humanoid"),TEXT("kill.creature"),TEXT("kill.golem"),TEXT("kill.ethereal"),TEXT("kill.boss")})
            Check(FindKey(Key,ERole::Impact)!=nullptr,*FString::Printf(TEXT("hit / kill signature %s in FabVFX.json"),Key));
        Check(FindKey(TEXT("level_up"),ERole::Cast)!=nullptr,TEXT("level_up flourish in FabVFX.json"));
        // pack-usage-3 (playtest 6): the circular green / teal swirls never carry the common physical hits (flesh / armour),
        // and plate / mail hits vary with the attacker's weapon instead of one system on every armoured target.
        {
            static const TCHAR* Swirls[]={TEXT("NS_Air_Magic_Hit3"),TEXT("NS_Air_Magic_Splash"),TEXT("NS_Shadow_Magic_Hit2")};
            TSet<FString> ArmorLooks;
            for(const TCHAR* Layer:{TEXT("hit.flesh"),TEXT("hit.armor")})
                for(const TCHAR* Weapon:{TEXT(""),TEXT(".sword"),TEXT(".axe"),TEXT(".mace"),TEXT(".dagger"),TEXT(".spear"),TEXT(".bow"),TEXT(".pistol")})
                    for(const TCHAR* Crit:{TEXT(""),TEXT(".crit")})
                    {
                        const FString Key=FString(Layer)+Weapon+Crit;
                        const FEntry* E=FindKey(Key,ERole::Impact);
                        if(!E||E->Candidates.IsEmpty())continue;
                        for(const TCHAR* S:Swirls)Check(!E->Candidates[0].Contains(S),*FString::Printf(TEXT("%s does not use the %s swirl"),*Key,S));
                        if(FString(Layer)==TEXT("hit.armor")&&!*Crit)ArmorLooks.Add(E->Candidates[0]);
                    }
            Check(ArmorLooks.Num()>=4,*FString::Printf(TEXT("armour hits vary with the weapon (%d distinct systems)"),ArmorLooks.Num()));
        }
        // Every monster race ability owns a signature (at least one role), and within one unit no two abilities share the same
        // cast look (system + tint): "no two nearby spells look alike".
        int32 Monsters=0,Covered=0;TArray<FString> Missing,Twins;
        for(const FName Race:CireRaces::Get().Order)if(const auto* R=CireRaces::FindRace(Race))
            for(const FName Unit:R->Units)if(const auto* A=CireNPCArchetypes::Find(Unit))
            {
                TSet<FString> Looks;
                for(const FCireNPCAbility& Ab:A->Abilities)
                {
                    if(Ab.bBasic)continue; // basic melee / bolts take the hit signatures (weapon x body), not a per-ability look
                    ++Monsters;bool bAny=false;
                    for(int32 I=0;I<static_cast<int32>(ERole::Count);++I)if(FindAbility(Ab.Id,static_cast<ERole>(I)))bAny=true;
                    if(bAny)++Covered;else if(Missing.Num()<12)Missing.Add(Ab.Id.ToString());
                    if(const FEntry* C=FindAbility(Ab.Id,ERole::Cast);C&&C->Candidates.Num())
                    {
                        const FString Look=C->Candidates[0]+C->Tint.ToString();
                        if(Looks.Contains(Look)){if(Twins.Num()<12)Twins.Add(Unit.ToString()+TEXT("/")+Ab.Id.ToString());}
                        else Looks.Add(Look);
                    }
                }
            }
        Check(Monsters>0&&Covered==Monsters,*FString::Printf(TEXT("every monster ability has a Fab signature (%d/%d; missing: %s)"),Covered,Monsters,*FString::Join(Missing,TEXT(", "))));
        Check(Twins.IsEmpty(),*FString::Printf(TEXT("no two abilities of one unit share a cast look (%s)"),*FString::Join(Twins,TEXT(", "))));
        UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_VFX monster abilities with a signature: %d/%d"),Covered,Monsters);
    }
    const FCoverage Cov=Coverage();
    UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_VFX coverage %d/%d slots resolve (packs present: %s); abilities %d with %d/%d own slots resolving; %d Cascade"),
        Cov.Resolved,Cov.Configured,Cov.Resolved>0?TEXT("yes"):TEXT("no"),Cov.Abilities,Cov.AbilitySlotsResolved,Cov.AbilitySlots,Cov.Cascade);
    UE_LOG(LogTemp,Display,TEXT("%s"),bOk?TEXT("CIRE_FAB_VFX_TESTS_PASS"):TEXT("CIRE_FAB_VFX_TESTS_FAIL"));
    return bOk;
}
#endif
