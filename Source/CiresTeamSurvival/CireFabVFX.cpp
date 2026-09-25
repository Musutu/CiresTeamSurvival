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
#include "Engine/World.h"

namespace
{
TAutoConsoleVariable<int32> CVarFabVFX(TEXT("cire.FabVFX"), 1,
    TEXT("1: overlay Niagara systems from the locally installed Fab VFX packs (Content/Data/FabVFX.json). 0: procedural presentation only."));

struct FTable
{
    bool bLoaded = false;
    TMap<FString, CireFabVFX::FEntry> Schools; // "<school>.<role>"
    TMap<FString, CireFabVFX::FEntry> Buffs;
    TMap<FString, TWeakObjectPtr<UNiagaraSystem>> Resolved;
    TSet<FString> Unresolvable;
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

const CireFabVFX::FEntry* CireFabVFX::FindBuff(const FString& Key)
{
    return Loaded().Buffs.Find(Key.ToLower());
}

UNiagaraSystem* CireFabVFX::Resolve(const FEntry* Entry)
{
    if(!Entry)return nullptr;
    FTable& T=Loaded();
    for(const FString& Path:Entry->Candidates)
    {
        if(const TWeakObjectPtr<UNiagaraSystem>* Hit=T.Resolved.Find(Path);Hit&&Hit->IsValid())return Hit->Get();
        if(T.Unresolvable.Contains(Path))continue;
        UNiagaraSystem* System=PackagePresent(Path)?LoadObject<UNiagaraSystem>(nullptr,*Path,nullptr,LOAD_NoWarn|LOAD_Quiet):nullptr;
        if(System){T.Resolved.Add(Path,System);return System;}
        T.Unresolvable.Add(Path);
    }
    return nullptr;
}

UNiagaraSystem* CireFabVFX::ResolveSchool(ECireSchool School, ERole Role, float* OutScale)
{
    if(!Enabled())return nullptr;
    const FEntry* E=Find(School,Role);
    UNiagaraSystem* S=Resolve(E);
    if(OutScale)*OutScale=E?E->Scale:1.f;
    return S;
}

UNiagaraComponent* CireFabVFX::SpawnAttached(UNiagaraSystem* System, USceneComponent* Parent, FVector Offset, float Scale, bool bAutoDestroy)
{
    if(!System||!Parent||!Enabled())return nullptr;
    UNiagaraComponent* C=UNiagaraFunctionLibrary::SpawnSystemAttached(System,Parent,NAME_None,Offset,FRotator::ZeroRotator,
        FVector(Scale),EAttachLocation::KeepRelativeOffset,bAutoDestroy,ENCPoolMethod::None,true,true);
    return C;
}

UNiagaraComponent* CireFabVFX::SpawnAt(UWorld* World, UNiagaraSystem* System, FVector Location, FRotator Rotation, float Scale)
{
    if(!System||!World||!Enabled())return nullptr;
    return UNiagaraFunctionLibrary::SpawnSystemAtLocation(World,System,Location,Rotation,FVector(Scale),true,true,ENCPoolMethod::AutoRelease,true);
}

void CireFabVFX::ApplyTint(UNiagaraComponent* Component, FLinearColor Tint)
{
    if(!Component||Tint.A<=0)return;
    // Vendor packs expose colour under different user parameter names; setting an absent one is a no-op.
    static const FName Names[]={TEXT("Color"),TEXT("Colour"),TEXT("MainColor"),TEXT("Main Color"),TEXT("User.Color"),TEXT("Tint")};
    for(const FName& N:Names)Component->SetVariableLinearColor(N,Tint);
}

CireFabVFX::FCoverage CireFabVFX::Coverage()
{
    FCoverage C;FTable& T=Loaded();
    for(const auto& Pair:T.Schools){++C.Configured;if(Resolve(&Pair.Value))++C.Resolved;else C.Missing.Add(Pair.Key);}
    for(const auto& Pair:T.Buffs){++C.Configured;if(Resolve(&Pair.Value))++C.Resolved;else C.Missing.Add(TEXT("buff.")+Pair.Key);}
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
    const FCoverage Cov=Coverage();
    UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_VFX coverage %d/%d slots resolve (packs present: %s)"),Cov.Resolved,Cov.Configured,Cov.Resolved>0?TEXT("yes"):TEXT("no"));
    UE_LOG(LogTemp,Display,TEXT("%s"),bOk?TEXT("CIRE_FAB_VFX_TESTS_PASS"):TEXT("CIRE_FAB_VFX_TESTS_FAIL"));
    return bOk;
}
#endif
