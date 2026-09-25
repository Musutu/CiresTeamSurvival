#include "CireAuraVisuals.h"
#include "CireFabVFX.h" // fab-integration
#include "NiagaraComponent.h"
#include "CireAuraShapes.h"
#include "CireBuffs.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireNPCState.h"
#include "CireRealm.h"
#include "CireSkillRuntime.h"
#include "CireAttackSystem.h"
#include "CireItems.h"
#include "CireAudio.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireAura,Log,All);

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------
namespace
{
TMap<FName,FCireAuraDef> GDefs;
TMap<FName,FName> GItemBuffs;
FCireAuraLimits GLimits;
bool GLoaded=false;

void EnsureLoaded()
{
    if(GLoaded)return;GLoaded=true;FString Error;
    if(!CireAuraData::Reload(Error))UE_LOG(LogCireAura,Warning,TEXT("CIRE_AURA_DATA_ERROR %s"),*Error);
}
bool ReadColor(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,FLinearColor& Out,bool bRequired)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!Object->TryGetArrayField(Key,Values))return !bRequired;
    if(Values->Num()!=3)return false;double C[3];
    for(int32 I=0;I<3;++I)if(!(*Values)[I]->TryGetNumber(C[I])||!FMath::IsFinite(C[I])||C[I]<0||C[I]>1)return false;
    Out=FLinearColor(C[0],C[1],C[2],1);return true;
}
bool ReadNumber(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,float& Out,float Min,float Max)
{
    if(!Object->HasField(Key))return true;double V=0;
    if(!Object->TryGetNumberField(Key,V)||!FMath::IsFinite(V)||V<Min||V>Max)return false;Out=static_cast<float>(V);return true;
}
bool ValidCue(const FString& Cue)
{
    if(Cue.Len()>96)return false;
    for(const TCHAR C:Cue)if(!(FChar::IsLower(C)||FChar::IsDigit(C)||C==TEXT('_')||C==TEXT('.')))return false;
    return true;
}
const TSet<FString>& Swipes(){static const TSet<FString> S={TEXT("blood"),TEXT("frost"),TEXT("holy"),TEXT("ember"),TEXT("gold"),TEXT("steel"),TEXT("rhythm"),TEXT("venom"),TEXT("arcane")};return S;}
const TSet<FString>& Hits(){static const TSet<FString> S={TEXT("splash"),TEXT("shatter"),TEXT("glint"),TEXT("sparks"),TEXT("ripple"),TEXT("none")};return S;}
const TSet<FString>& Attaches(){static const TSet<FString> S={TEXT("ground"),TEXT("body"),TEXT("hands"),TEXT("weapon"),TEXT("overhead"),TEXT("link"),TEXT("front")};return S;}
const TSet<FString>& Kinds(){static const TSet<FString> S={TEXT("buff"),TEXT("debuff"),TEXT("aura"),TEXT("stance"),TEXT("passive")};return S;}
}

bool CireAuraData::Parse(const FString& Text,TMap<FName,FCireAuraDef>& Out,FCireAuraLimits& OutLimits,FString& Error,TMap<FName,FName>* OutItemBuffs)
{
    auto Bad=[&](const FString& Why){Error=Why;return false;};
    TSharedPtr<FJsonObject> Root;const auto Reader=TJsonReaderFactory<>::Create(Text);
    if(!FJsonSerializer::Deserialize(Reader,Root)||!Root)return Bad(TEXT("Invalid BuffVisuals JSON"));
    double Version=0;const TSharedPtr<FJsonObject>* Buffs=nullptr;
    if(!Root->TryGetNumberField(TEXT("schemaVersion"),Version)||Version!=1||!Root->TryGetObjectField(TEXT("buffs"),Buffs)||
        (*Buffs)->Values.IsEmpty()||(*Buffs)->Values.Num()>128)return Bad(TEXT("Expected schemaVersion 1 and 1..128 buffs"));
    FCireAuraLimits Limits;
    const TSharedPtr<FJsonObject>* LimitRow=nullptr;
    if(Root->TryGetObjectField(TEXT("limits"),LimitRow))
    {
        float Units=Limits.MaxUnits,Layers=Limits.MaxLayersPerUnit,Lights=Limits.MaxLights,Strikes=Limits.MaxStrikes,Verts=Limits.MaxVerticesPerUnit;
        if(!ReadNumber(*LimitRow,TEXT("maxUnits"),Units,1,96)||!ReadNumber(*LimitRow,TEXT("maxLayersPerUnit"),Layers,1,12)||
           !ReadNumber(*LimitRow,TEXT("maxLights"),Lights,0,16)||!ReadNumber(*LimitRow,TEXT("maxStrikes"),Strikes,1,64)||
           !ReadNumber(*LimitRow,TEXT("maxVerticesPerUnit"),Verts,1000,16000)||
           !ReadNumber(*LimitRow,TEXT("fullDetailCm"),Limits.FullDetailCm,200,20000)||!ReadNumber(*LimitRow,TEXT("reducedDetailCm"),Limits.ReducedDetailCm,200,30000)||
           !ReadNumber(*LimitRow,TEXT("cullCm"),Limits.CullCm,200,40000)||Limits.FullDetailCm>Limits.ReducedDetailCm||Limits.ReducedDetailCm>Limits.CullCm)
            return Bad(TEXT("Invalid limits"));
        Limits.MaxUnits=FMath::RoundToInt(Units);Limits.MaxLayersPerUnit=FMath::RoundToInt(Layers);Limits.MaxLights=FMath::RoundToInt(Lights);
        Limits.MaxStrikes=FMath::RoundToInt(Strikes);Limits.MaxVerticesPerUnit=FMath::RoundToInt(Verts);
    }
    const TSharedPtr<FJsonObject>* Allegiance=nullptr;
    if(Root->TryGetObjectField(TEXT("allegiance"),Allegiance)&&(!ReadColor(*Allegiance,TEXT("friendly"),Limits.Friendly,true)||!ReadColor(*Allegiance,TEXT("hostile"),Limits.Hostile,true)))
        return Bad(TEXT("Invalid allegiance colours"));
    TMap<FName,FCireAuraDef> Candidate;
    for(const auto& Pair:(*Buffs)->Values)
    {
        const FString Key(Pair.Key.ToView());const TSharedPtr<FJsonObject>* Row=nullptr;
        if(Key.IsEmpty()||Key.Len()>64||!Pair.Value->TryGetObject(Row))return Bad(TEXT("Invalid buff row: ")+Key);
        FCireAuraDef D;D.Id=FName(*Key);
        if(!(*Row)->TryGetStringField(TEXT("name"),D.Name)||D.Name.IsEmpty()||!(*Row)->TryGetStringField(TEXT("kind"),D.Kind)||!Kinds().Contains(D.Kind))
            return Bad(TEXT("Buff requires name and kind buff|debuff|aura|stance|passive: ")+Key);
        (*Row)->TryGetStringField(TEXT("school"),D.School);(*Row)->TryGetStringField(TEXT("source"),D.Source);
        const TSharedPtr<FJsonObject>* Palette=nullptr;
        if(!(*Row)->TryGetObjectField(TEXT("palette"),Palette)||!ReadColor(*Palette,TEXT("primary"),D.Primary,true)||
           !ReadColor(*Palette,TEXT("secondary"),D.Secondary,true)||!ReadColor(*Palette,TEXT("core"),D.Core,true))return Bad(TEXT("Invalid palette: ")+Key);
        float Priority=50;if(!ReadNumber(*Row,TEXT("priority"),Priority,0,100))return Bad(TEXT("Invalid priority: ")+Key);D.Priority=FMath::RoundToInt(Priority);
        if((*Row)->HasField(TEXT("allegianceRim"))&&!(*Row)->TryGetBoolField(TEXT("allegianceRim"),D.bAllegianceRim))return Bad(TEXT("Invalid allegianceRim: ")+Key);
        if(!ReadNumber(*Row,TEXT("light"),D.Light,0,1))return Bad(TEXT("Invalid light: ")+Key);
        const TArray<TSharedPtr<FJsonValue>>* Layers=nullptr;
        if(!(*Row)->TryGetArrayField(TEXT("layers"),Layers)||Layers->IsEmpty()||Layers->Num()>6)return Bad(TEXT("Buff needs 1..6 layers: ")+Key);
        for(const auto& Value:*Layers)
        {
            const TSharedPtr<FJsonObject>* L=nullptr;FString Shape,Attach,Style;FCireAuraLayer Layer;float Count=6;
            if(!Value->TryGetObject(L)||!(*L)->TryGetStringField(TEXT("shape"),Shape)||!CireAuraShapes::ParseShape(Shape,Layer.Shape)||
               !(*L)->TryGetStringField(TEXT("attach"),Attach)||!Attaches().Contains(Attach))return Bad(TEXT("Layer needs a known shape and attach: ")+Key);
            if((*L)->TryGetStringField(TEXT("style"),Style))Layer.Style=FName(*Style);
            if(!ReadNumber(*L,TEXT("size"),Layer.Size,.2f,4)||!ReadNumber(*L,TEXT("speed"),Layer.Speed,0,8)||!ReadNumber(*L,TEXT("height"),Layer.Height,.2f,3)||
               !ReadNumber(*L,TEXT("alpha"),Layer.Alpha,0,1.5f)||!ReadNumber(*L,TEXT("count"),Count,0,48))return Bad(TEXT("Layer value out of range: ")+Key);
            Layer.Count=FMath::RoundToInt(Count);
            if((*L)->HasField(TEXT("burstOnly"))&&!(*L)->TryGetBoolField(TEXT("burstOnly"),Layer.bBurstOnly))return Bad(TEXT("Invalid burstOnly: ")+Key);
            if((*L)->HasField(TEXT("secondary"))&&!(*L)->TryGetBoolField(TEXT("secondary"),Layer.bSecondary))return Bad(TEXT("Invalid secondary: ")+Key);
            D.Layers.Add(Layer);
        }
        const TSharedPtr<FJsonObject>* Intensity=nullptr;
        if((*Row)->TryGetObjectField(TEXT("intensity"),Intensity))
        {
            float MaxStacks=1;
            if(!ReadNumber(*Intensity,TEXT("base"),D.Base,.2f,2)||!ReadNumber(*Intensity,TEXT("perStack"),D.PerStack,0,1)||
               !ReadNumber(*Intensity,TEXT("maxStacks"),MaxStacks,1,20)||!ReadNumber(*Intensity,TEXT("expiringSeconds"),D.Expiring,0,10))return Bad(TEXT("Invalid intensity: ")+Key);
            D.MaxStacks=FMath::RoundToInt(MaxStacks);
        }
        const TSharedPtr<FJsonObject>* Life=nullptr;
        if((*Row)->TryGetObjectField(TEXT("lifecycle"),Life)&&(!ReadNumber(*Life,TEXT("burstSeconds"),D.Burst,0,2)||!ReadNumber(*Life,TEXT("fadeSeconds"),D.Fade,.05f,3)))
            return Bad(TEXT("Invalid lifecycle: ")+Key);
        const TSharedPtr<FJsonObject>* Attack=nullptr;
        if((*Row)->TryGetObjectField(TEXT("attack"),Attack))
        {
            FString Swipe,Hit;
            if(!(*Attack)->TryGetStringField(TEXT("swipe"),Swipe)||!Swipes().Contains(Swipe)||!(*Attack)->TryGetStringField(TEXT("onHit"),Hit)||!Hits().Contains(Hit))
                return Bad(TEXT("Attack needs a known swipe and onHit: ")+Key);
            D.Attack.bValid=true;D.Attack.Swipe=FName(*Swipe);D.Attack.OnHit=FName(*Hit);D.Attack.Primary=D.Primary;D.Attack.Core=D.Core;
            if(!ReadColor(*Attack,TEXT("primary"),D.Attack.Primary,false)||!ReadColor(*Attack,TEXT("core"),D.Attack.Core,false))return Bad(TEXT("Invalid attack colours: ")+Key);
        }
        const TSharedPtr<FJsonObject>* Sound=nullptr;
        if((*Row)->TryGetObjectField(TEXT("sound"),Sound))
        {
            (*Sound)->TryGetStringField(TEXT("start"),D.SoundStart);(*Sound)->TryGetStringField(TEXT("loop"),D.SoundLoop);
            (*Sound)->TryGetStringField(TEXT("end"),D.SoundEnd);(*Sound)->TryGetStringField(TEXT("hit"),D.SoundHit);
            if(!ValidCue(D.SoundStart)||!ValidCue(D.SoundLoop)||!ValidCue(D.SoundEnd)||!ValidCue(D.SoundHit))return Bad(TEXT("Sound cue ids use a-z, 0-9, '_' and '.': ")+Key);
        }
        Candidate.Add(D.Id,MoveTemp(D));
    }
    TMap<FName,FName> Items;const TSharedPtr<FJsonObject>* ItemRows=nullptr;
    if(Root->TryGetObjectField(TEXT("itemBuffs"),ItemRows))for(const auto& Pair:(*ItemRows)->Values)
    {
        FString Target;const FString Key(Pair.Key.ToView());
        if(Key.IsEmpty()||!Pair.Value->TryGetString(Target)||!Candidate.Contains(FName(*Target)))return Bad(TEXT("itemBuffs entry must name a known visual: ")+Key);
        Items.Add(FName(*Key),FName(*Target));
    }
    Out=MoveTemp(Candidate);OutLimits=Limits;if(OutItemBuffs)*OutItemBuffs=MoveTemp(Items);Error.Reset();return true;
}
bool CireAuraData::Reload(FString& Error)
{
    FString Text;const FString Path=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/BuffVisuals.json"));
    if(!FFileHelper::LoadFileToString(Text,*Path)||Text.Len()>400000){Error=TEXT("Missing or oversized BuffVisuals.json");return false;}
    TMap<FName,FCireAuraDef> Candidate;FCireAuraLimits Limits;TMap<FName,FName> Items;
    if(!Parse(Text,Candidate,Limits,Error,&Items))return false;
    GDefs=MoveTemp(Candidate);GLimits=Limits;GItemBuffs=MoveTemp(Items);GLoaded=true;return true;
}
const FCireAuraDef* CireAuraData::Find(FName Id){EnsureLoaded();return GDefs.Find(Id);}
const FCireAuraLimits& CireAuraData::Limits(){EnsureLoaded();return GLimits;}
const TMap<FName,FCireAuraDef>& CireAuraData::All(){EnsureLoaded();return GDefs;}
const TMap<FName,FName>& CireAuraData::ItemBuffs(){EnsureLoaded();return GItemBuffs;}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------
namespace
{
UMaterialInterface* CoreMaterial()
{
    static TWeakObjectPtr<UMaterialInterface> Cached;
    if(!Cached.IsValid())
    {
        UMaterialInterface* M=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/FX/Auras/M_AuraCore.M_AuraCore"),nullptr,LOAD_Quiet|LOAD_NoWarn);
        if(!M)M=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpellPolish01/M_SpellCore.M_SpellCore"));
        if(!M)M=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpell/M_SpellGlow.M_SpellGlow"));
        Cached=M;
    }
    return Cached.Get();
}
UMaterialInterface* SoftMaterial()
{
    static TWeakObjectPtr<UMaterialInterface> Cached;
    if(!Cached.IsValid())
    {
        UMaterialInterface* M=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/FX/Auras/M_AuraSoft.M_AuraSoft"),nullptr,LOAD_Quiet|LOAD_NoWarn);
        if(!M)M=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpellPolish01/M_SpellSoft.M_SpellSoft"));
        Cached=M;
    }
    return Cached.Get();
}
UProceduralMeshComponent* MakeMesh(AActor* Owner,USceneComponent* Parent,const TCHAR* Name)
{
    auto* Mesh=NewObject<UProceduralMeshComponent>(Owner,MakeUniqueObjectName(Owner,UProceduralMeshComponent::StaticClass(),Name));
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetCastShadow(false);Mesh->bUseAsyncCooking=false;Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetReceivesDecals(false);Mesh->bSelectable=false;
    if(Parent){Mesh->SetupAttachment(Parent);Mesh->SetUsingAbsoluteRotation(true);Mesh->SetUsingAbsoluteScale(true);}
    Mesh->RegisterComponent();
    return Mesh;
}
void Upload(UProceduralMeshComponent* Mesh,const TArray<FVector>& V,const TArray<int32>& I,const TArray<FLinearColor>& C,const TArray<FVector2D>* UV)
{
    if(!Mesh)return;
    if(V.IsEmpty()){if(Mesh->GetNumSections()>0)Mesh->ClearMeshSection(0);Mesh->SetVisibility(false);return;}
    const auto* Section=Mesh->GetProcMeshSection(0);
    static const TArray<FVector> NoNormals;static const TArray<FProcMeshTangent> NoTangents;static const TArray<FVector2D> NoUV;
    if(Section&&Section->ProcVertexBuffer.Num()==V.Num()&&Section->ProcIndexBuffer.Num()==I.Num())
        Mesh->UpdateMeshSection_LinearColor(0,V,NoNormals,UV?*UV:NoUV,C,NoTangents,false);
    else Mesh->CreateMeshSection_LinearColor(0,V,I,NoNormals,UV?*UV:NoUV,C,NoTangents,false);
    Mesh->SetVisibility(true);
}
bool UnitAlive(const AActor* Unit)
{
    if(const auto* Hero=Cast<ACireHero>(Unit))return Hero->bDrafted&&!Hero->bDead&&Hero->Health>0;
    if(const auto* Monster=Cast<ACireMonster>(Unit))return Monster->Health>0;
    return false;
}
FString UnitName(const AActor* Unit)
{
    if(const auto* Hero=Cast<ACireHero>(Unit))return Hero->HeroName;
    if(const auto* Monster=Cast<ACireMonster>(Unit))return Monster->MonsterName;
    return FString();
}
float UnitScale(const AActor* Unit)
{
    const auto* Character=Cast<ACharacter>(Unit);
    return Character?FMath::Clamp(Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()/92.f,.6f,2.6f):1.f;
}
FName EnrageId(const ACireMonster* Monster)
{
    if(const auto* Archetype=Monster&&Monster->NPCState?Monster->NPCState->Archetype():nullptr)
        for(const auto& Ability:Archetype->Abilities)if(Ability.Kind==ECireNPCAbilityKind::Enrage&&CireAuraData::Find(Ability.Id))return Ability.Id;
    return TEXT("enraged");
}
FName ProvokeId(const ACireMonster* Monster)
{
    if(const auto* Archetype=Monster&&Monster->NPCState?Monster->NPCState->Archetype():nullptr)
        for(const auto& Ability:Archetype->Abilities)if(Ability.Kind==ECireNPCAbilityKind::Provoke&&CireAuraData::Find(Ability.Id))return Ability.Id;
    return TEXT("npc_tank_provoke");
}
float OtherIntensity(const UWorld* World)
{
    const auto* PC=World?World->GetFirstPlayerController():nullptr;const auto* HUD=PC?Cast<ACireHUD>(PC->GetHUD()):nullptr;
    return HUD?FMath::Clamp(HUD->UISettings.OtherEffectsIntensity,0.f,1.f):1.f;
}
void CameraBasis(const FRotator& Rotation,const FVector& Location,const FVector& Origin,CireAuraShapes::FContext& C)
{
    const FRotationMatrix M(Rotation);C.CamLook=M.GetUnitAxis(EAxis::X);C.CamRight=M.GetUnitAxis(EAxis::Y);C.CamUp=M.GetUnitAxis(EAxis::Z);
    C.CamLocal=Location-Origin;
}
}

// ---------------------------------------------------------------------------
// Per-unit component
// ---------------------------------------------------------------------------
UCireAuraComponent::UCireAuraComponent(){PrimaryComponentTick.bCanEverTick=false;bAutoActivate=true;}
void UCireAuraComponent::OnRegister()
{
    Super::OnRegister();
    if(auto* Subsystem=CireAuraVisuals::Get(GetWorld()))Subsystem->Register(this);
}
void UCireAuraComponent::OnUnregister()
{
    StopLoops();
    if(auto* Subsystem=CireAuraVisuals::Get(GetWorld()))Subsystem->Unregister(this);
    ReleaseMeshes();
    Super::OnUnregister();
}
void UCireAuraComponent::EnsureMeshes()
{
    AActor* Owner=GetOwner();if(!Owner||!Owner->GetRootComponent())return;
    if(!Core){Core=MakeMesh(Owner,Owner->GetRootComponent(),TEXT("AuraCore"));Core->SetMaterial(0,CoreMaterial());Core->SetTranslucentSortPriority(2);}
    if(!Soft){Soft=MakeMesh(Owner,Owner->GetRootComponent(),TEXT("AuraSoft"));Soft->SetMaterial(0,SoftMaterial());Soft->SetTranslucentSortPriority(1);}
}
void UCireAuraComponent::ReleaseMeshes()
{
    if(Core){Core->DestroyComponent();Core=nullptr;}
    if(Soft){Soft->DestroyComponent();Soft=nullptr;}
    if(Light){Light->DestroyComponent();Light=nullptr;}
    LastLayers=LastVertices=0;
}
void UCireAuraComponent::HideAll()
{
    if(Core&&Core->IsVisible()){Core->SetVisibility(false);}
    if(Soft&&Soft->IsVisible()){Soft->SetVisibility(false);}
    if(Light&&Light->IsVisible())Light->SetVisibility(false);
    LastLayers=LastVertices=0;bRenderedThisFrame=false;
}
bool UCireAuraComponent::IsLightOn() const{return Light&&Light->IsVisible()&&Light->Intensity>0;}
bool UCireAuraComponent::AreMeshesCollisionFree() const
{
    return (!Core||Core->GetCollisionEnabled()==ECollisionEnabled::NoCollision)&&(!Soft||Soft->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
}
void UCireAuraComponent::StopLoops()
{
    for(auto& Pair:LoopAudio)if(UAudioComponent* Audio=Pair.Value.Get())Audio->FadeOut(.35f,0.f);
    LoopAudio.Reset();LoopIds.Reset();
    for(auto& Pair:FabAuras)CireFabVFX::Release(Pair.Value.Get()); // fab-integration
    FabAuras.Reset();
}
// fab-integration: exact effect id first, then "<kind>.<school>" (e.g. "buff.holy"), then "<kind>".
static const CireFabVFX::FEntry* FabAuraEntry(const FCireAuraDef& Def)
{
    if(const auto* E=CireFabVFX::FindBuff(Def.Id.ToString()))return E;
    if(const auto* E=CireFabVFX::FindBuff(Def.Kind+TEXT(".")+Def.School))return E;
    return CireFabVFX::FindBuff(Def.Kind);
}
void UCireAuraComponent::UpdateFabAuras(bool bAllowed,int32& Budget,float Intensity)
{
    TMap<FName,UFXSystemAsset*> Wanted;
    // Other units' auras follow the "Other units' aura effects" slider: faint below .35, full at 1.
    if(bAllowed&&CireFabVFX::Enabled()&&Intensity>=.35f)for(const auto& I:Instances)
    {
        if(I.FadeLocal>=0||Budget<=0)continue;
        const auto* Def=CireAuraData::Find(I.Id);const auto* Entry=Def?FabAuraEntry(*Def):nullptr;
        if(UFXSystemAsset* System=CireFabVFX::Resolve(Entry)){Wanted.Add(I.Id,System);--Budget;}
    }
    for(auto It=FabAuras.CreateIterator();It;++It)
        if(!Wanted.Contains(It.Key())||!It.Value().IsValid()){CireFabVFX::Release(It.Value().Get());It.RemoveCurrent();}
    AActor* Unit=GetOwner();
    for(const auto& Pair:Wanted)
    {
        if(FabAuras.Contains(Pair.Key)||!Unit||!Unit->GetRootComponent())continue;
        const auto* Def=CireAuraData::Find(Pair.Key);const auto* Entry=FabAuraEntry(*Def);
        const float Scale=Entry->Scale*FMath::Lerp(.75f,1.f,FMath::Clamp(Intensity,0.f,1.f));
        UFXSystemComponent* FX=CireFabVFX::SpawnAttached(Pair.Value,Unit->GetRootComponent(),FVector(0,0,-88.f),Scale,false);
        CireFabVFX::ApplyTint(FX,Entry->Tint);
        if(FX)FabAuras.Add(Pair.Key,FX);
    }
}
void UCireAuraComponent::UpdateLoops(bool bAllowed,int32& Budget,float Volume)
{
    TSet<FName> Wanted;
    if(bAllowed)for(const auto& I:Instances)
    {
        if(I.FadeLocal>=0)continue;const auto* Def=CireAuraData::Find(I.Id);
        if(!Def||Def->SoundLoop.IsEmpty()||Budget<=0)continue;
        Wanted.Add(I.Id);--Budget;
    }
    for(auto It=LoopAudio.CreateIterator();It;++It)
        if(!Wanted.Contains(It.Key())||!It.Value().IsValid()){if(UAudioComponent* Audio=It.Value().Get())Audio->FadeOut(.35f,0.f);It.RemoveCurrent();}
    for(const FName Id:Wanted)
    {
        if(LoopIds.Contains(Id)&&(LoopAudio.Contains(Id)||!GetWorld()->GetAudioDeviceRaw()))continue;
        const auto* Def=CireAuraData::Find(Id);AActor* Unit=GetOwner();
        if(UAudioComponent* Audio=CireAudio::PlayAttached(FName(*Def->SoundLoop),Unit->GetRootComponent(),NAME_None,Volume))LoopAudio.Add(Id,Audio);
    }
    LoopIds=Wanted;
}
bool UCireAuraComponent::HasVisibleWork() const{return !Instances.IsEmpty();}
void UCireAuraComponent::AgeForPreview(float Seconds){for(auto& I:Instances){I.BornLocal-=Seconds;}}
void UCireAuraComponent::Want(TArray<FCireAuraInstance>& Desired,FName Id,float Start,float End,int32 Stacks,AActor* Link) const
{
    if(Desired.ContainsByPredicate([Id](const FCireAuraInstance& I){return I.Id==Id;}))return;
    FCireAuraInstance I;I.Id=Id;I.StartServer=Start;I.EndServer=End;I.Stacks=FMath::Max(1,Stacks);I.Link=Link;Desired.Add(I);
}
void UCireAuraComponent::Synchronize(float ServerNow,float LocalNow)
{
    AActor* Unit=GetOwner();
    TArray<FCireAuraInstance> Desired;
    if(UnitAlive(Unit))
    {
        const auto* Hero=Cast<ACireHero>(Unit);const auto* Monster=Cast<ACireMonster>(Unit);
        const float Shield=Hero?Hero->ShieldUntil:0,Taunt=Hero?Hero->TauntUntil:0,Slow=Hero?Hero->SlowUntil:Monster?Monster->SlowUntil:0;
        const int32 Poison=Hero?Hero->PoisonAreaCount:Monster?Monster->PoisonAreaCount:0;
        static const TSet<FName> GuardIds={TEXT("iron_guard"),TEXT("challenge_of_iron"),TEXT("sanctuary"),TEXT("bastion_of_dawn"),TEXT("mass_aegis"),TEXT("wellspring"),TEXT("oathshield")};
        static const TSet<FName> TauntIds={TEXT("war_cry"),TEXT("challenge_of_iron"),TEXT("toll_of_the_grave")};
        static const TSet<FName> SlowIds={TEXT("frost_bind"),TEXT("shield_slam")};
        bool bGuardNamed=false,bTauntNamed=false,bSlowNamed=false;
        if(const auto* State=CireBuffs::Get(Unit))for(const auto& E:State->Buffs)
        {
            const bool bOpen=E.EndTime<=E.StartTime;if(!bOpen&&E.EndTime<=ServerNow)continue;
            // Named records stay honest with the gameplay state they decorate (cleanse, death, reset).
            if(Hero&&GuardIds.Contains(E.Id)&&Shield<=ServerNow)continue;
            if(Hero&&TauntIds.Contains(E.Id)&&Taunt<=ServerNow&&Shield<=ServerNow)continue;
            if(SlowIds.Contains(E.Id)&&Slow<=ServerNow)continue;
            bGuardNamed|=GuardIds.Contains(E.Id)||E.Id==TEXT("war_cry");bTauntNamed|=TauntIds.Contains(E.Id);bSlowNamed|=SlowIds.Contains(E.Id);
            Want(Desired,E.Id,E.StartTime,bOpen?0.f:E.EndTime,E.Stacks,E.Source.Get());
        }
        if(Shield>ServerNow&&!bGuardNamed)Want(Desired,TEXT("guarded"),ServerNow,Shield,1,nullptr);
        if(Taunt>ServerNow&&!bTauntNamed)Want(Desired,TEXT("taunting"),ServerNow,Taunt,1,nullptr);
        if(Slow>ServerNow&&!bSlowNamed)Want(Desired,TEXT("slowed"),ServerNow,Slow,1,nullptr);
        if(Poison>0)Want(Desired,TEXT("poisoned"),ServerNow,0,Poison,nullptr);
        if(Hero)
        {
            if(Hero->HasSkill(TEXT("battle_rhythm")))Want(Desired,TEXT("battle_rhythm"),0,0,1,nullptr);
            if(Hero->HasSkill(TEXT("soul_conduit")))Want(Desired,TEXT("soul_conduit"),0,0,1,nullptr);
            // Item actives and consumables: replicated inventory timed buffs, mapped by "itemBuffs".
            if(const auto* Bag=Hero->Inventory.Get())for(const auto& Timed:Bag->Buffs)
                if(Timed.EndsAt>ServerNow)if(const FName* Visual=CireAuraData::ItemBuffs().Find(Timed.Id))
                    Want(Desired,*Visual,Timed.EndsAt-Timed.Duration,Timed.EndsAt,1,nullptr);
        }
        if(Monster&&Monster->NPCState)
        {
            const auto* S=Monster->NPCState.Get();
            if(S->HasStatus(CireNPCStatus::Enraged))Want(Desired,EnrageId(Monster),ServerNow,0,1,nullptr);
            if(S->HasStatus(CireNPCStatus::Rallied))Want(Desired,TEXT("rallied"),ServerNow,0,1,nullptr);
            if(S->HasStatus(CireNPCStatus::ShieldWall))Want(Desired,TEXT("npc_tank_wall"),ServerNow,0,1,nullptr);
            if(S->HasStatus(CireNPCStatus::Guarded))Want(Desired,TEXT("npc_tank_guard"),ServerNow,0,1,nullptr);
            if(S->HasStatus(CireNPCStatus::Provoking))Want(Desired,ProvokeId(Monster),ServerNow,0,1,nullptr);
        }
    }
    // Reconcile: start new, refresh existing, fade what ended.
    for(const auto& D:Desired)
    {
        FCireAuraInstance* Existing=Instances.FindByPredicate([&](const FCireAuraInstance& I){return I.Id==D.Id;});
        if(Existing)
        {
            if(Existing->FadeLocal>=0){Existing->FadeLocal=-1;Existing->BornLocal=LocalNow;}
            Existing->EndServer=D.EndServer;Existing->Stacks=D.Stacks;Existing->Link=D.Link;
            continue;
        }
        FCireAuraInstance New=D;New.BornLocal=LocalNow;New.FadeLocal=-1;Instances.Add(New);
        if(const auto* Def=CireAuraData::Find(D.Id))CireAuraVisuals::PlaySoundCue(Def->SoundStart,Unit);
    }
    for(int32 I=Instances.Num()-1;I>=0;--I)
    {
        auto& Inst=Instances[I];
        const bool bWanted=Desired.ContainsByPredicate([&](const FCireAuraInstance& D){return D.Id==Inst.Id;});
        if(!bWanted&&Inst.FadeLocal<0)
        {
            Inst.FadeLocal=LocalNow;
            if(const auto* Def=CireAuraData::Find(Inst.Id);Def&&UnitAlive(Unit))CireAuraVisuals::PlaySoundCue(Def->SoundEnd,Unit);
        }
        const auto* Def=CireAuraData::Find(Inst.Id);const float Fade=Def?Def->Fade:.3f;
        if(Inst.FadeLocal>=0&&LocalNow-Inst.FadeLocal>=Fade)Instances.RemoveAt(I);
    }
}
const FCireAuraDef* UCireAuraComponent::AttackModifier(float ServerNow) const
{
    const FCireAuraDef* Best=nullptr;
    for(const auto& I:Instances)
    {
        if(I.FadeLocal>=0)continue;const auto* Def=CireAuraData::Find(I.Id);
        if(Def&&Def->Attack.bValid&&(!Best||Def->Priority>Best->Priority))Best=Def;
    }
    return Best;
}
int32 UCireAuraComponent::Render(float LocalNow,float Anim,float ServerNow,int32 Detail,int32 MaxLayers,bool bLight,float IntensityScale,const FVector& CameraLocation,const FRotator& CameraRotation)
{
    auto* Unit=Cast<ACharacter>(GetOwner());
    if(!Unit||MaxLayers<=0||Instances.IsEmpty()){HideAll();return 0;}
    EnsureMeshes();if(!Core||!Soft){HideAll();return 0;}
    const FCireAuraLimits& Limits=CireAuraData::Limits();
    CireAuraShapes::FBuffers B{CV,CI,CC,SV,SI,SC,SUV};B.MaxCore=Limits.MaxVerticesPerUnit;B.MaxSoft=FMath::Max(400,Limits.MaxVerticesPerUnit/5);B.Reset();
    CireAuraShapes::FContext C;
    const FVector Origin=Unit->GetActorLocation();
    const float Half=Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),Radius=Unit->GetCapsuleComponent()->GetScaledCapsuleRadius();
    C.Radius=FMath::Max(34.f,Radius);C.Height=Half*2;C.Feet=-Half;C.Top=Half;C.Unit=FMath::Clamp(Half/92.f,.6f,2.6f);
    C.Forward=Unit->GetActorForwardVector();C.Detail=Detail;
    CameraBasis(CameraRotation,CameraLocation,Origin,C);
    if(auto* Body=Unit->GetMesh();Body&&Body->GetSkeletalMeshAsset()&&Body->IsVisible())
        for(const TCHAR* Bone:{TEXT("hand_l"),TEXT("hand_r")})
            if(Body->GetBoneIndex(Bone)!=INDEX_NONE)C.Hands.Add(Body->GetSocketLocation(Bone)-Origin);
    {
        // Weapon line: the largest visible hand-held prop (hero loadout or NPC prop).
        UStaticMeshComponent* Best=nullptr;float BestSize=0;
        TInlineComponentArray<UStaticMeshComponent*> Props(Unit);
        for(auto* Prop:Props)
        {
            if(!Prop||!Prop->GetStaticMesh()||!Prop->IsVisible()||Prop->bHiddenInGame)continue;
            const FName Socket=Prop->GetAttachSocketName();
            if(!Prop->ComponentHasTag(TEXT("CireWeaponProp"))&&Socket!=TEXT("hand_r")&&Socket!=TEXT("hand_l"))continue;
            const float Size=Prop->Bounds.SphereRadius;if(Size>BestSize){BestSize=Size;Best=Prop;}
        }
        if(Best)
        {
            const FBox Box=Best->GetStaticMesh()->GetBoundingBox();const FVector Ext=Box.GetExtent(),Ctr=Box.GetCenter();
            const int32 Axis=Ext.X>=Ext.Y&&Ext.X>=Ext.Z?0:Ext.Y>=Ext.Z?1:2;FVector D=FVector::ZeroVector;D[Axis]=Ext[Axis]*.92f;
            const FTransform T=Best->GetComponentTransform();C.Weapon.Add(T.TransformPosition(Ctr-D)-Origin);C.Weapon.Add(T.TransformPosition(Ctr+D)-Origin);
        }
    }

    // Highest priority first so the layer budget keeps the most important signature.
    TArray<const FCireAuraInstance*> Order;for(const auto& I:Instances)Order.Add(&I);
    Order.Sort([](const FCireAuraInstance& A,const FCireAuraInstance& B){const auto* X=CireAuraData::Find(A.Id);const auto* Y=CireAuraData::Find(B.Id);return (X?X->Priority:0)>(Y?Y->Priority:0);});
    int32 Layers=0;float RimAlpha=0;const FCireAuraDef* LightDef=nullptr;float LightAlpha=0;
    const uint32 UnitSeed=GetTypeHash(Unit->GetFName());
    const bool bHostile=[&]{
        const auto* Local=GetWorld()->GetFirstPlayerController();const auto* Viewer=Local?Cast<ACireHero>(Local->GetPawn()):nullptr;
        if(const auto* Hero=Cast<ACireHero>(Unit))return Viewer&&Hero->TeamId!=Viewer->TeamId;
        return Cast<ACireMonster>(Unit)!=nullptr;}();
    for(const FCireAuraInstance* Inst:Order)
    {
        const FCireAuraDef* Def=CireAuraData::Find(Inst->Id);if(!Def)continue;
        const float Age=FMath::Max(0.f,LocalNow-Inst->BornLocal);
        float Alpha=IntensityScale,Pop=1;
        if(Def->Burst>0&&Age<Def->Burst){const float B0=1-Age/Def->Burst;Pop=1+.35f*B0*B0;Alpha*=1+.6f*B0;}
        if(Inst->FadeLocal>=0){const float F=FMath::Clamp(1-(LocalNow-Inst->FadeLocal)/Def->Fade,0.f,1.f);Alpha*=F;Pop*=.8f+.2f*F;}
        if(Inst->EndServer>Inst->StartServer&&Def->Expiring>0)
        {
            const float Remaining=Inst->EndServer-ServerNow;
            if(Remaining<Def->Expiring)Alpha*=.5f+.5f*FMath::Abs(FMath::Cos(LocalNow*7.5f));
        }
        const float Intensity=FMath::Clamp(Def->Base+Def->PerStack*(FMath::Min(Inst->Stacks,Def->MaxStacks)-1),.2f,2.f);
        Alpha*=FMath::Clamp(.7f+.3f*Intensity,.3f,1.3f);
        if(Alpha<=.004f)continue;
        C.Time=Anim+(GetTypeHash(Inst->Id)%1000)*.013f;C.Age=Age;C.Alpha=Alpha;C.Intensity=Intensity;C.Pop=Pop;
        C.Primary=Def->Primary;C.Secondary=Def->Secondary;C.Core=Def->Core;C.Seed=UnitSeed^GetTypeHash(Inst->Id);
        C.bHasLink=false;
        if(AActor* Link=Inst->Link.Get();IsValid(Link)&&!Link->IsHidden())
        {
            const auto* LinkChar=Cast<ACharacter>(Link);
            C.bHasLink=true;C.Link=Link->GetActorLocation()+FVector(0,0,LinkChar?LinkChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*.25f:40.f)-Origin;
        }
        int32 Drawn=0;
        for(int32 LayerIndex=0;LayerIndex<Def->Layers.Num();++LayerIndex)
        {
            const auto& Layer=Def->Layers[LayerIndex];
            if(Layers>=MaxLayers)break;
            // Reduced detail keeps the two leading layers; minimal keeps only overhead marks.
            if(Detail==1&&Drawn>=2)break;
            if(Detail==0&&Layer.Shape!=ECireAuraShape::Glyph)continue;
            if(bLocalView&&Layer.Shape==ECireAuraShape::Glyph)continue;
            float LayerAlpha=1;
            if(Layer.bBurstOnly){const float Window=FMath::Max(Def->Burst,.2f)+.5f;if(Age>Window)continue;LayerAlpha=1-Age/Window;}
            C.Alpha=Alpha*LayerAlpha;
            CireAuraShapes::DrawLayer(Layer,C,B);++Layers;++Drawn;
        }
        if(Def->bAllegianceRim)RimAlpha=FMath::Max(RimAlpha,FMath::Min(Alpha,1.f));
        if(Def->Light>0&&(!LightDef||Def->Priority>LightDef->Priority)){LightDef=Def;LightAlpha=FMath::Min(Alpha,1.f);}
    }
    if(RimAlpha>0&&Detail>=1){C.Time=Anim;CireAuraShapes::DrawAllegianceRim(C,bHostile?Limits.Hostile:Limits.Friendly,RimAlpha*.6f,B);}
    Upload(Core,CV,CI,CC,nullptr);Upload(Soft,SV,SI,SC,&SUV);
    if(bLight&&LightDef&&Detail>=2)
    {
        if(!Light)
        {
            Light=NewObject<UPointLightComponent>(Unit,MakeUniqueObjectName(Unit,UPointLightComponent::StaticClass(),TEXT("AuraLight")));
            Light->SetupAttachment(Unit->GetRootComponent());Light->SetMobility(EComponentMobility::Movable);Light->SetCastShadows(false);
            Light->SetIntensityUnits(ELightUnits::Lumens);Light->bAffectsWorld=true;Light->RegisterComponent();
        }
        Light->SetRelativeLocation(FVector(0,0,C.Feet*.2f));
        Light->SetLightColor(LightDef->Primary);Light->SetAttenuationRadius(260*C.Unit);
        Light->SetIntensity(70.f*LightDef->Light*LightAlpha*(.9f+.1f*FMath::Sin(LocalNow*6)));Light->SetVisibility(true);
    }
    else if(Light&&Light->IsVisible())Light->SetVisibility(false);
    LastLayers=Layers;LastVertices=CV.Num()+SV.Num();bRenderedThisFrame=LastVertices>0;
    return LastVertices;
}

// ---------------------------------------------------------------------------
// Attack-modifier strike actor
// ---------------------------------------------------------------------------
ACireAuraStrike::ACireAuraStrike()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostPhysics;bReplicates=false;
    Core=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("StrikeCore"));SetRootComponent(Core);
    Soft=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("StrikeSoft"));Soft->SetupAttachment(Core);
    for(auto* Mesh:{Core.Get(),Soft.Get()})
    {
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);Mesh->bUseAsyncCooking=false;
    }
}
bool ACireAuraStrike::IsCollisionFree() const
{
    return Core->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&Soft->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&!GetActorEnableCollision();
}
void ACireAuraStrike::Configure(EMode InMode,const FCireAuraAttack& Attack,FVector InFrom,FVector InTo,float InScale,float InMirror)
{
    Mode=InMode;Style=Attack;From=InFrom;To=InTo;Scale=FMath::Clamp(InScale,.4f,3.f);Mirror=InMirror>=0?1.f:-1.f;Age=0;
    Duration=Mode==EMode::Swipe?.45f:Mode==EMode::Hit?.6f:Mode==EMode::Muzzle?.3f:30.f;
    OriginPhase=CireSkillRuntime::Phase(GetWorld());
    Core->SetMaterial(0,CoreMaterial());Soft->SetMaterial(0,SoftMaterial());
    SetActorEnableCollision(false);
    SetActorLocation(Mode==EMode::Hit?To:From);
    Rebuild();
}
void ACireAuraStrike::FollowProjectile(AActor* Projectile,const FCireAuraAttack& Attack,float InScale)
{
    Followed=Projectile;Trail.Reset();
    Configure(EMode::Trail,Attack,Projectile->GetActorLocation(),Projectile->GetActorLocation(),InScale);
}
void ACireAuraStrike::SetPreviewAge(float Seconds){bPreview=true;Age=FMath::Max(0.f,Seconds);Rebuild();}
void ACireAuraStrike::SetPreviewTrail(const TArray<FVector>& WorldPoints)
{
    bPreview=true;Age=0;Trail=WorldPoints;if(Trail.Num()>10)Trail.RemoveAt(0,Trail.Num()-10);
    if(!Trail.IsEmpty())SetActorLocation(Trail.Last());Rebuild();
}
void ACireAuraStrike::Tick(float Delta)
{
    Super::Tick(Delta);
    if(!bPreview&&OriginPhase!=CireSkillRuntime::Phase(GetWorld())){Destroy();return;}
    if(!bPreview)Age+=FMath::Clamp(Delta,0.f,.25f);
    if(Mode==EMode::Trail)
    {
        AActor* Actor=Followed.Get();
        if(IsValid(Actor)&&!Actor->IsActorBeingDestroyed())
        {
            SetActorHiddenInGame(Actor->IsHidden());
            if(Trail.IsEmpty()||FVector::DistSquared(Trail.Last(),Actor->GetActorLocation())>36)
            {Trail.Add(Actor->GetActorLocation());if(Trail.Num()>10)Trail.RemoveAt(0);}
            Duration=Age+.25f;
        }
        else if(Age>=Duration){Destroy();return;}
    }
    else if(!bPreview&&Age>=Duration){Destroy();return;}
    Rebuild();
}
void ACireAuraStrike::Rebuild()
{
    CireAuraShapes::FBuffers B{CV,CI,CC,SV,SI,SC,SUV};B.MaxCore=4000;B.MaxSoft=400;B.Reset();
    CireAuraShapes::FContext C;FVector CamLoc(0,0,1000);FRotator CamRot(-45,0,0);
    if(auto* Subsystem=CireAuraVisuals::Get(GetWorld());Subsystem&&Subsystem->bCameraOverride){CamLoc=Subsystem->CameraOverrideLocation;CamRot=Subsystem->CameraOverrideRotation;}
    else if(auto* PC=GetWorld()->GetFirstPlayerController())PC->GetPlayerViewPoint(CamLoc,CamRot);
    const FVector Origin=GetActorLocation();
    {const FRotationMatrix M(CamRot);C.CamLook=M.GetUnitAxis(EAxis::X);C.CamRight=M.GetUnitAxis(EAxis::Y);C.CamUp=M.GetUnitAxis(EAxis::Z);C.CamLocal=CamLoc-Origin;}
    switch(Mode)
    {
    case EMode::Swipe: CireAuraShapes::DrawSwipe(Style,Age,Scale,Mirror,To-From,C,B); break;
    case EMode::Hit: CireAuraShapes::DrawHit(Style,Age,Scale,C,B); break;
    case EMode::Muzzle: CireAuraShapes::DrawMuzzle(Style,Age,Scale,C,B); break;
    case EMode::Trail:
    {
        TArray<FVector> Local;for(const FVector& P:Trail)Local.Add(P-Origin);
        const float Fade=Followed.IsValid()?1.f:FMath::Clamp((Duration-Age)/.25f,0.f,1.f);
        CireAuraShapes::DrawTrail(Style,Local,Age,Scale,Fade,C,B);break;
    }
    }
    Upload(Core,CV,CI,CC,nullptr);Upload(Soft,SV,SI,SC,&SUV);
    LastVertices=CV.Num()+SV.Num();
}

// ---------------------------------------------------------------------------
// Subsystem: budgets, LOD, realm filtering, attack detection
// ---------------------------------------------------------------------------
bool UCireAuraSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World=Cast<UWorld>(Outer);
    return World&&(World->WorldType==EWorldType::Game||World->WorldType==EWorldType::PIE);
}
TStatId UCireAuraSubsystem::GetStatId() const{RETURN_QUICK_DECLARE_CYCLE_STAT(UCireAuraSubsystem,STATGROUP_Tickables);}
void UCireAuraSubsystem::Register(UCireAuraComponent* Component){Components.AddUnique(Component);}
void UCireAuraSubsystem::Unregister(UCireAuraComponent* Component){Components.Remove(Component);}
void UCireAuraSubsystem::Tick(float Delta){UpdateNow();}
ACireAuraStrike* UCireAuraSubsystem::SpawnStrike(ACireAuraStrike::EMode Mode,const FCireAuraAttack& Attack,FVector From,FVector To,float Scale,float Mirror)
{
    UWorld* World=GetWorld();if(!World||World->GetNetMode()==NM_DedicatedServer||!Attack.bValid||From.ContainsNaN()||To.ContainsNaN())return nullptr;
    Strikes.RemoveAll([](const TWeakObjectPtr<ACireAuraStrike>& S){return !S.IsValid()||S->IsActorBeingDestroyed();});
    if(Strikes.Num()>=CireAuraData::Limits().MaxStrikes)return nullptr;
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;P.ObjectFlags|=RF_Transient;
    auto* Strike=World->SpawnActor<ACireAuraStrike>(Mode==ACireAuraStrike::EMode::Hit?To:From,FRotator::ZeroRotator,P);
    if(!Strike)return nullptr;
    Strike->Configure(Mode,Attack,From,To,Scale,Mirror);Strikes.Add(Strike);++SpawnedStrikes;
    return Strike;
}
bool UCireAuraSubsystem::MatchHit(const FString& SourceName,const FVector& Location,float LocalNow)
{
    for(int32 I=0;I<PendingHits.Num();++I)
    {
        const auto& P=PendingHits[I];
        if(P.SourceName!=SourceName||LocalNow>P.Until||FVector::DistSquared2D(P.Near,Location)>FMath::Square(450.f))continue;
        const float Drop=FMath::Clamp(P.Scale*45.f,30.f,110.f);
        SpawnStrike(ACireAuraStrike::EMode::Hit,P.Attack,Location,Location-FVector(0,0,Drop),P.Scale);
        CireAuraVisuals::PlaySoundCue(P.SoundHit,P.Unit.Get(),&Location);
        PendingHits.RemoveAt(I);return true;
    }
    return false;
}
void UCireAuraSubsystem::QueueHit(FPendingHit&& Pending,float LocalNow)
{
    // The confirmed-hit event can arrive before the swing presentation; check recent hits first.
    for(int32 I=RecentHits.Num()-1;I>=0;--I)
    {
        const auto& R=RecentHits[I];
        if(R.SourceName==Pending.SourceName&&LocalNow-R.Time<.9f&&FVector::DistSquared2D(R.Location,Pending.Near)<FMath::Square(450.f))
        {
            const float Drop=FMath::Clamp(Pending.Scale*45.f,30.f,110.f);
            SpawnStrike(ACireAuraStrike::EMode::Hit,Pending.Attack,R.Location,R.Location-FVector(0,0,Drop),Pending.Scale);
            CireAuraVisuals::PlaySoundCue(Pending.SoundHit,Pending.Unit.Get(),&R.Location);
            RecentHits.RemoveAt(I);return;
        }
    }
    PendingHits.Add(MoveTemp(Pending));
    if(PendingHits.Num()>32)PendingHits.RemoveAt(0);
}
void UCireAuraSubsystem::HandleCombatEvents(float LocalNow)
{
    PendingHits.RemoveAll([LocalNow](const FPendingHit& P){return LocalNow>P.Until;});
    RecentHits.RemoveAll([LocalNow](const FRecentHit& R){return LocalNow-R.Time>1.f;});
    auto* PC=Cast<ACireController>(GetWorld()->GetFirstPlayerController());if(!PC)return;
    uint32 Newest=LastEventSequence;
    for(const auto& E:PC->CombatEvents)
    {
        if(E.Sequence<=LastEventSequence)continue;Newest=FMath::Max(Newest,E.Sequence);
        // Only confirmed damage: a miss or dodge never shows an empowered impact.
        if(E.bHealing||E.Outcome!=ECireHitOutcome::Hit||E.Amount<=0)continue;
        if(!MatchHit(E.SourceName,E.Location,LocalNow))
        {RecentHits.Add({E.SourceName,E.Location,LocalNow});if(RecentHits.Num()>32)RecentHits.RemoveAt(0);}
    }
    LastEventSequence=Newest;
}
void UCireAuraSubsystem::HandleAttacks(UCireAuraComponent* Aura,float ServerNow,float LocalNow)
{
    auto* Hero=Cast<ACireHero>(Aura->GetOwner());if(!Hero)return;
    if(!Aura->bAttackPrimed){Aura->bAttackPrimed=true;Aura->LastAttackSerial=Hero->AttackSerial;return;}
    if(Hero->AttackSerial!=Aura->LastAttackSerial)
    {
        Aura->LastAttackSerial=Hero->AttackSerial;
        if(Hero->AttackSerial!=0&&Aura->AttackModifier(ServerNow))
        {
            // Release matches ACireHero::BasicAttack: .25 of the .65s reference clip.
            Aura->bStrikePending=true;Aura->PendingStrikeServer=Hero->AttackStartedServerTime+Hero->AttackDuration*(.25f/.65f);Aura->PendingAim=Hero->AttackAimLocation;
        }
    }
    if(!Aura->bStrikePending||ServerNow<Aura->PendingStrikeServer)return;
    const bool bStale=ServerNow-Aura->PendingStrikeServer>1.f;Aura->bStrikePending=false;
    const FCireAuraDef* Mod=Aura->AttackModifier(ServerNow);if(bStale||!Mod)return;
    const float Scale=UnitScale(Hero);const FVector Chest=Hero->GetActorLocation()+FVector(0,0,18*Scale);
    const bool bRanged=Hero->IsRangedBasicAttack();
    if(bRanged)
    {
        FVector Hand=Chest+Hero->GetActorForwardVector()*40*Scale;
        if(auto* Body=Hero->GetMesh();Body&&Body->GetBoneIndex(TEXT("hand_r"))!=INDEX_NONE&&Body->IsVisible())Hand=Body->GetSocketLocation(TEXT("hand_r"));
        SpawnStrike(ACireAuraStrike::EMode::Muzzle,Mod->Attack,Hand,Aura->PendingAim,Scale*.8f);
    }
    else if(SpawnStrike(ACireAuraStrike::EMode::Swipe,Mod->Attack,Chest,Aura->PendingAim,Scale,(Hero->AttackSerial&1)?1.f:-1.f))CireAuraVisuals::PlaySoundCue(TEXT("aura_swing"),Hero);
    Aura->LastStrikeLocal=LocalNow;
    FPendingHit Pending;Pending.SourceName=Hero->HeroName;Pending.Near=Aura->PendingAim;Pending.Until=LocalNow+(bRanged?2.5f:1.f);
    Pending.Attack=Mod->Attack;Pending.Scale=Scale;Pending.SoundHit=Mod->SoundHit;Pending.Unit=Hero;
    QueueHit(MoveTemp(Pending),LocalNow);
}
void UCireAuraSubsystem::NotifyAttackCue(FName SkillId,FVector From,FVector To)
{
    if(SkillId!=TEXT("npc_melee"))return;
    const float ServerNow=CireBuffs::ServerNow(GetWorld());const float LocalNow=GetWorld()->GetTimeSeconds();
    for(const auto& Weak:Components)
    {
        UCireAuraComponent* Aura=Weak.Get();auto* Monster=Aura?Cast<ACireMonster>(Aura->GetOwner()):nullptr;
        if(!Monster||FVector::DistSquared2D(Monster->GetActorLocation(),From)>FMath::Square(60.f)||Monster->IsHidden())continue;
        const FCireAuraDef* Mod=Aura->AttackModifier(ServerNow);if(!Mod)return;
        const float Scale=UnitScale(Monster);
        if(SpawnStrike(ACireAuraStrike::EMode::Swipe,Mod->Attack,Monster->GetActorLocation()+FVector(0,0,15*Scale),To,Scale,FMath::RandBool()?1.f:-1.f))CireAuraVisuals::PlaySoundCue(TEXT("aura_swing"),Monster);
        FPendingHit Pending;Pending.SourceName=Monster->MonsterName;Pending.Near=To;Pending.Until=LocalNow+1.f;Pending.Attack=Mod->Attack;Pending.Scale=Scale*.9f;
        Pending.SoundHit=Mod->SoundHit;Pending.Unit=Monster;QueueHit(MoveTemp(Pending),LocalNow);
        return;
    }
}
void UCireAuraSubsystem::HandleProjectiles()
{
    UWorld* World=GetWorld();const float ServerNow=CireBuffs::ServerNow(World);
    for(auto It=Trails.CreateIterator();It;++It)if(!It.Key().IsValid()||!It.Value().IsValid())It.RemoveCurrent();
    for(TActorIterator<ACireTargetProjectile> It(World);It;++It)
    {
        ACireTargetProjectile* Shot=*It;if(Trails.Contains(Shot)||Shot->IsHidden()||!IsValid(Shot->Attacker))continue;
        auto* Aura=Shot->Attacker->FindComponentByClass<UCireAuraComponent>();const FCireAuraDef* Mod=Aura?Aura->AttackModifier(ServerNow):nullptr;
        if(!Mod){Trails.Add(Shot,nullptr);continue;}
        if(ACireAuraStrike* Trail=SpawnStrike(ACireAuraStrike::EMode::Trail,Mod->Attack,Shot->GetActorLocation(),Shot->GetActorLocation(),UnitScale(Shot->Attacker)))
        {Trail->FollowProjectile(Shot,Mod->Attack,UnitScale(Shot->Attacker)*.8f);Trails.Add(Shot,Trail);}
    }
}
void UCireAuraSubsystem::UpdateNow(float LocalOverride)
{
    UWorld* World=GetWorld();if(!World||World->GetNetMode()==NM_DedicatedServer)return;
    const float LocalNow=LocalOverride>=0?LocalOverride:World->GetTimeSeconds();LastLocalNow=LocalNow;
    const float Anim=PreviewClock>=0?PreviewClock:LocalNow;
    const float ServerNow=CireBuffs::ServerNow(World);
    const FCireAuraLimits& Limits=CireAuraData::Limits();
    Components.RemoveAll([](const TWeakObjectPtr<UCireAuraComponent>& C){return !C.IsValid();});
    RegisteredCount=Components.Num();
    APlayerController* PC=World->GetFirstPlayerController();
    FVector CamLoc=FVector::ZeroVector;FRotator CamRot=FRotator::ZeroRotator;
    if(bCameraOverride){CamLoc=CameraOverrideLocation;CamRot=CameraOverrideRotation;}else if(PC)PC->GetPlayerViewPoint(CamLoc,CamRot);
    const AActor* Observer=ObserverOverride.IsValid()?ObserverOverride.Get():PC?static_cast<const AActor*>(PC):nullptr;
    const APawn* LocalPawn=ObserverOverride.IsValid()?Cast<APawn>(ObserverOverride.Get()):PC?static_cast<const APawn*>(PC->GetPawn()):nullptr;
    const auto* LocalHero=Cast<ACireHero>(LocalPawn);
    const AActor* Focus=LocalHero?LocalHero->Target:nullptr;
    const float Others=OtherIntensity(World);
    struct FCandidate{UCireAuraComponent* Aura;float Distance;float Score;bool bLocal;};
    TArray<FCandidate> Visible;
    for(const auto& Weak:Components)
    {
        UCireAuraComponent* Aura=Weak.Get();AActor* Unit=Aura->GetOwner();
        Aura->Synchronize(ServerNow,LocalNow);
        // Realm privacy: the same rule as replication and body visibility.
        const bool bObservable=Unit&&!Unit->IsHidden()&&Observer&&CireRealm::CanObserve(Observer,Unit);
        if(!bObservable||Aura->Instances.IsEmpty()){Aura->HideAll();Aura->bStrikePending=false;Aura->StopLoops();continue;}
        const float Distance=static_cast<float>(FVector::Dist(CamLoc,Unit->GetActorLocation()));
        int32 Priority=0;for(const auto& I:Aura->Instances)if(const auto* Def=CireAuraData::Find(I.Id))Priority=FMath::Max(Priority,Def->Priority);
        const bool bLocal=Unit==LocalPawn;
        Visible.Add({Aura,Distance,Distance-(bLocal?1e6f:0.f)-(Unit==Focus?5e5f:0.f)-Priority*12.f,bLocal});
    }
    Visible.Sort([](const FCandidate& A,const FCandidate& B){return A.Score<B.Score;});
    RenderedUnits=RenderedLayers=LitUnits=CulledUnits=0;int32 LoopBudget=CireAuraVisuals::MaxLoops;int32 FabBudget=CireAuraVisuals::MaxFabAuras;
    for(const FCandidate& Entry:Visible)
    {
        UCireAuraComponent* Aura=Entry.Aura;
        int32 Detail=Entry.Distance<Limits.FullDetailCm?2:Entry.Distance<Limits.ReducedDetailCm?1:Entry.Distance<Limits.CullCm?0:-1;
        const float Intensity=Entry.bLocal?1.f:Others;
        if(!Entry.bLocal&&Intensity<.35f)Detail=FMath::Min(Detail,0);
        if(Entry.bLocal)Detail=FMath::Max(Detail,1);
        if(Detail<0||RenderedUnits>=Limits.MaxUnits){Aura->HideAll();Aura->StopLoops();++CulledUnits;continue;}
        // Loops only for full-detail units, nearest/most important first (the sort order).
        Aura->UpdateLoops(Detail==2,LoopBudget,Entry.bLocal?1.f:.4f+.6f*Intensity);
        Aura->UpdateFabAuras(Detail>=1,FabBudget,Intensity); // fab-integration
        if(LocalOverride<0&&Detail>=1)HandleAttacks(Aura,ServerNow,LocalNow);
        // Reduced-detail units refresh every other frame; their meshes stay attached meanwhile.
        if(Detail<2&&Aura->CountVertices()>0&&(++Aura->FrameSkip&1)&&LocalOverride<0){++RenderedUnits;RenderedLayers+=Aura->CountLayers();LitUnits+=Aura->IsLightOn();continue;}
        const bool bLight=LitUnits<Limits.MaxLights&&Detail==2;
        const float Shown=Entry.bLocal?1.f:FMath::Max(.25f,Intensity);
        Aura->bLocalView=Entry.bLocal;
        Aura->Render(LocalNow,Anim,ServerNow,Detail,Limits.MaxLayersPerUnit,bLight,Shown,CamLoc,CamRot);
        if(Aura->CountVertices()>0){++RenderedUnits;RenderedLayers+=Aura->CountLayers();LitUnits+=Aura->IsLightOn();}
    }
    if(LocalOverride<0){HandleCombatEvents(LocalNow);HandleProjectiles();}
    Strikes.RemoveAll([](const TWeakObjectPtr<ACireAuraStrike>& S){return !S.IsValid()||S->IsActorBeingDestroyed();});
    LiveStrikes=Strikes.Num();
}

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------
UCireAuraSubsystem* CireAuraVisuals::Get(const UWorld* World){return World?World->GetSubsystem<UCireAuraSubsystem>():nullptr;}
UCireAuraComponent* CireAuraVisuals::Attach(AActor* Unit)
{
    if(!IsValid(Unit)||Unit->GetNetMode()==NM_DedicatedServer)return nullptr;
    if(auto* Existing=Unit->FindComponentByClass<UCireAuraComponent>())return Existing;
    auto* Component=NewObject<UCireAuraComponent>(Unit,TEXT("CireAuraVisual"));Unit->AddInstanceComponent(Component);Component->RegisterComponent();
    return Component;
}
void CireAuraVisuals::PlaySoundCue(const FString& CueId,AActor* Unit,const FVector* Location)
{
    // Ids come from BuffVisuals.json "sound". A cue registered in AudioCues.json plays as is;
    // otherwise start cues fall back to the shared aura_apply / aura_heal cues and the rest stay silent.
    if(CueId.IsEmpty()||!IsValid(Unit)||Unit->IsHidden()||Unit->GetNetMode()==NM_DedicatedServer)return;
    const APlayerController* PC=Unit->GetWorld()->GetFirstPlayerController();
    if(!PC||!CireRealm::CanObserve(PC,Unit))return; // realm privacy: never hear an unobservable unit
    FName Cue(*CueId);
    if(!CireAudio::HasCue(Cue))
    {
        if(!CueId.EndsWith(TEXT(".start")))return;
        static const TCHAR* Healing[]={TEXT("regeneration"),TEXT("wellspring"),TEXT("mana_restore"),TEXT("sanctuary")};
        bool bHeal=false;for(const TCHAR* Word:Healing)bHeal|=CueId.Contains(Word);
        Cue=bHeal?FName(TEXT("aura_heal")):FName(TEXT("aura_apply"));
    }
    const float Volume=Unit==PC->GetPawn()?1.f:.4f+.6f*OtherIntensity(Unit->GetWorld());
    CireAudio::PlayCue(Unit,Cue,Location?*Location:Unit->GetActorLocation(),Volume);
    UE_LOG(LogCireAura,Verbose,TEXT("CIRE_AURA_SOUND %s -> %s %s"),*CueId,*Cue.ToString(),*Unit->GetName());
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommand GCireAuraReload(TEXT("cire.Auras.Reload"),TEXT("Reload Content/Data/BuffVisuals.json (presentation only)."),
    FConsoleCommandDelegate::CreateLambda([]{FString Error;const bool bGood=CireAuraData::Reload(Error);UE_LOG(LogCireAura,Display,TEXT("CIRE_AURA_RELOAD_%s %s"),bGood?TEXT("PASS"):TEXT("FAIL"),*Error);}));
#endif
