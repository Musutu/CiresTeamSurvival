#include "CireEnvironmentProps.h"
#include "Misc/CommandLine.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireNav.h" // nav-paths
#include "CireWorldDressing.h" // world-dressing
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h" // world-scale: collision proxies
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTown,Log,All);

namespace
{
// nav-paths: the route margin follows the editable lane width: half the road (default 520 -> 260) + 70 cm escort capsule clearance = 330.
float RouteMarginFor(const UWorld* World){return CireLanePath::LaneWidth(World)*.5f+70.f;}
constexpr float BayMargin=450.f;     // challenge pack arena around each bay centre
constexpr float SpawnMargin=420.f;
constexpr float DividerMargin=60.f;  // nothing may cross into the gap between the two realms
constexpr int32 MaxLightsPerTeam=140; // world-scale: 3x longer realm (lights fade out beyond LightDrawDistance)
constexpr float LightDrawDistance=9000.f,LightFadeRange=2500.f;

struct FCandidate
{
    FString Path,Source,MaterialOverride;TArray<FString> Parts;int32 Priority=0;
    FVector Scale=FVector::OneVector,Offset=FVector::ZeroVector;FRotator Rotation=FRotator::ZeroRotator;bool bFit=false;
};
struct FSlotLight {bool bEnabled=false;FVector Offset=FVector::ZeroVector;FLinearColor Color=FLinearColor(1,.6f,.3f);float Intensity=4000,Radius=800;
    float Flicker=0; // world-dressing: 0..1 torch/brazier flicker amount
};
enum class EClearance:uint8 {Route,Bays,None};
struct FSlot
{
    FName Id;bool bMaterial=false,bCollision=true,bShadow=true,bRequiresPassage=false;FName MeshSlot;FVector Footprint=FVector::ZeroVector;
    EClearance Clearance=EClearance::Route;FSlotLight Light;TArray<FCandidate> Candidates;
    // resolved
    TWeakObjectPtr<UStaticMesh> Mesh;TArray<TWeakObjectPtr<UStaticMesh>> Parts;TWeakObjectPtr<UMaterialInterface> Material,MeshMaterial;FTransform Local=FTransform::Identity;
    FBox LocalBox=FBox(ForceInit);FString ResolvedSource;
    float CullStart=0,CullEnd=0; // world-dressing: HISM cull distances (cm), 0 = never culled
    bool bStatic=false; // world-scale: "component": "static" = one plain mesh component per placement (huge backdrop meshes)
};
struct FPlacement {FName Slot;FVector Location=FVector::ZeroVector;float Yaw=0,Scale=1;uint8 Mode=0;/*0 raw,1 outer,2 inner*/};
struct FPlaced {int32 Team=0;FName Slot;FTransform Transform;FBox Box;EClearance Clearance=EClearance::Route;};
struct FWorldTown
{
    TMap<FName,TArray<TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>>> Components;
    TArray<TWeakObjectPtr<UPointLightComponent>> Lights;TArray<FPlaced> Placed;int32 Visible=0,Suppressed=0;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> Statics; // world-scale: "component": "static" slots
    // world-scale: invisible box proxies for colliding slots whose mesh has no collision body (arena-kit hay bales, windmill).
    TMap<FName,TWeakObjectPtr<UInstancedStaticMeshComponent>> Proxies;
};
struct FTownData
{
    bool bLoaded=false,bValid=false;TMap<FName,FSlot> Slots;TArray<FPlacement> Placements;
    TArray<CireEnvironmentProps::FTownDistrict> Districts;
    TArray<CireEnvironmentProps::FTownSurface> Surfaces; // world-scale
} Town;
TMap<TWeakObjectPtr<ACireWorld>,FWorldTown> Worlds;

FString DataPath(const TCHAR* Name){return FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data"),Name);}
bool ReadJson(const FString& Path,TSharedPtr<FJsonObject>& Out)
{
    FString Text;
    return FFileHelper::LoadFileToString(Text,*Path)&&Text.Len()<1024*1024&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)&&Out.IsValid();
}
bool Finite(double V,double Low,double High){return FMath::IsFinite(V)&&V>=Low&&V<=High;}
bool Vec(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,FVector& Out,double Low,double High)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;double N=0;
    if(!O->HasField(Key))return true;
    if(O->TryGetNumberField(Key,N)){if(!Finite(N,Low,High))return false;Out=FVector(N);return true;}
    if(!O->TryGetArrayField(Key,A)||A->Num()!=3)return false;
    for(int32 I=0;I<3;++I){double V=0;if(!(*A)[I]->TryGetNumber(V)||!Finite(V,Low,High))return false;Out[I]=V;}
    return true;
}
bool Num(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,float& Out,double Low,double High)
{
    double N=0;if(!O->HasField(Key))return true;
    if(!O->TryGetNumberField(Key,N)||!Finite(N,Low,High))return false;Out=static_cast<float>(N);return true;
}
FString ObjectPath(FString Path)
{
    // Accept "/Game/A/B" or "/Game/A/B.B"; reject anything outside /Game or with traversal.
    if(!Path.StartsWith(TEXT("/Game/"))||Path.Contains(TEXT(".."))||Path.Contains(TEXT("\\")))return FString();
    if(!Path.Contains(TEXT(".")))Path+=TEXT(".")+FPaths::GetBaseFilename(Path);
    return Path;
}
bool ReadCandidate(const TSharedPtr<FJsonObject>& O,const TCHAR* PathKey,const TCHAR* Prefix,int32 Priority,const FString& Source,bool bBase,FCandidate& Out)
{
    Out=FCandidate();
    FString Path;if(!O->TryGetStringField(PathKey,Path))return false;
    Out.Path=ObjectPath(Path);if(Out.Path.IsEmpty())return false;
    Out.Priority=Priority;Out.Source=Source;
    const FString S=FString(Prefix)+TEXT("scale"),R=FString(Prefix)+TEXT("rotation"),F=FString(Prefix)+TEXT("offset"),Fit=FString(Prefix)+TEXT("fit");
    FVector Rot=FVector::ZeroVector;
    if(!Vec(O,*S,Out.Scale,.001,1000)||!Vec(O,*R,Rot,-360,360)||!Vec(O,*F,Out.Offset,-20000,20000))return false;
    Out.Rotation=FRotator(Rot.X,Rot.Y,Rot.Z);FString FitMode;
    // Multi-part props (e.g. a crate body, lid and latch) share one pivot and transform.
    const TArray<TSharedPtr<FJsonValue>>* Parts=nullptr;const FString PartsKey=FString(Prefix)+TEXT("parts");
    FString Override;const FString OverrideKey=FString(Prefix)+TEXT("materialOverride");
    if(O->TryGetStringField(*OverrideKey,Override))Out.MaterialOverride=ObjectPath(Override);
    if(O->TryGetArrayField(*PartsKey,Parts))for(const auto& V:*Parts)
    {FString Part;if(V->TryGetString(Part)&&!ObjectPath(Part).IsEmpty()&&Out.Parts.Num()<32)Out.Parts.Add(ObjectPath(Part));}
    // Overlay art of unknown scale is fitted into the slot footprint unless it opts out with "fit": "none".
    const bool bHasFit=O->TryGetStringField(*Fit,FitMode);
    Out.bFit=bHasFit?FitMode==TEXT("footprint"):!bBase;
    return true;
}
bool MergeManifest(const FString& File,int32 DefaultPriority,const FString& DefaultSource,bool bBase)
{
    TSharedPtr<FJsonObject> Root;double Version=0;const TSharedPtr<FJsonObject>* Slots=nullptr;
    if(!ReadJson(File,Root)||!Root->TryGetNumberField(TEXT("schemaVersion"),Version)||Version!=1||!Root->TryGetObjectField(TEXT("slots"),Slots))
    {UE_LOG(LogCireTown,Warning,TEXT("Town slot manifest ignored (unreadable or wrong schema): %s"),*File);return false;}
    double Priority=DefaultPriority;Root->TryGetNumberField(TEXT("priority"),Priority);
    FString Source=DefaultSource;Root->TryGetStringField(TEXT("source"),Source);
    const int32 P=FMath::Clamp(FMath::RoundToInt(Priority),bBase?0:1,1000);
    int32 Added=0,Skipped=0;
    for(const auto& Pair:(*Slots)->Values)
    {
        const TSharedPtr<FJsonObject> O=Pair.Value->AsObject();const FName Id(*FString(Pair.Key));
        if(!O||Id.IsNone()||FString(Pair.Key).Len()>64)continue;
        // Overlay manifests may gate entries on an import status (e.g. Fab "pending_download").
        FString Status;if(!bBase&&O->TryGetStringField(TEXT("status"),Status)&&Status!=TEXT("imported")){++Skipped;continue;}
        FString Kind;if(!O->TryGetStringField(TEXT("kind"),Kind))O->TryGetStringField(TEXT("type"),Kind);
        const bool bMaterialEntry=Kind==TEXT("material");
        FSlot* Existing=Town.Slots.Find(Id);
        const bool bMaterial=Existing?Existing->bMaterial:bMaterialEntry;
        const TCHAR* Key=bMaterial?TEXT("material"):TEXT("mesh");
        TArray<FCandidate> Found;FCandidate C;
        if(ReadCandidate(O,Key,TEXT(""),P,Source,bBase,C))Found.Add(C);
        if(ReadCandidate(O,TEXT("fallback"),TEXT("fallback_"),-1,TEXT("fallback"),true,C))Found.Add(C);
        // Array form: "candidates":[{"mesh":...,"priority":..}] for manifests with several quality tiers.
        const TArray<TSharedPtr<FJsonValue>>* List=nullptr;
        if(O->TryGetArrayField(TEXT("candidates"),List))for(const auto& V:*List)
        {
            const auto CO=V->AsObject();double CP=P;if(!CO)continue;CO->TryGetNumberField(TEXT("priority"),CP);
            if(ReadCandidate(CO,Key,TEXT(""),FMath::Clamp(FMath::RoundToInt(CP),bBase?0:1,1000),Source,bBase,C))Found.Add(C);
        }
        if(Existing&&!bBase&&Existing->bRequiresPassage)
        {
            // Gates span the march road: a replacement must declare its walkable passage or it could wall off the route.
            bool bPassage=false;O->TryGetBoolField(TEXT("passage"),bPassage);
            if(!bPassage&&!Found.IsEmpty()){UE_LOG(LogCireTown,Warning,TEXT("%s: overlay for gate slot %s ignored (set \"passage\": true once the mesh has an open road passage on local Y=0)."),*FPaths::GetCleanFilename(File),*Id.ToString());++Skipped;continue;}
        }
        if(!Existing&&Found.IsEmpty())continue; // overlay-only slot with nothing loadable yet
        FSlot& Slot=Existing?*Existing:Town.Slots.Add(Id);
        if(!Existing)
        {
            // Slot attributes come from the first manifest that defines the slot (normally the base file).
            Slot.Id=Id;Slot.bMaterial=bMaterial;
            FString MeshSlot;if(O->TryGetStringField(TEXT("meshSlot"),MeshSlot))Slot.MeshSlot=FName(*MeshSlot);
            O->TryGetBoolField(TEXT("collision"),Slot.bCollision);O->TryGetBoolField(TEXT("castShadow"),Slot.bShadow);
            O->TryGetBoolField(TEXT("requiresPassage"),Slot.bRequiresPassage);
            FString Component;if(O->TryGetStringField(TEXT("component"),Component))Slot.bStatic=Component==TEXT("static"); // world-scale
            if(!Vec(O,TEXT("footprint"),Slot.Footprint,0,20000)){UE_LOG(LogCireTown,Warning,TEXT("Slot %s footprint invalid in %s"),*Id.ToString(),*File);Slot.Footprint=FVector::ZeroVector;}
            FString Clear;if(O->TryGetStringField(TEXT("clearance"),Clear))Slot.Clearance=Clear==TEXT("none")?EClearance::None:Clear==TEXT("bays")?EClearance::Bays:EClearance::Route;
            const TSharedPtr<FJsonObject>* Light=nullptr;
            if(O->TryGetObjectField(TEXT("light"),Light))
            {
                FVector Color(1,.6,.3);Slot.Light.bEnabled=true;
                if(!Vec(*Light,TEXT("offset"),Slot.Light.Offset,-5000,5000)||!Vec(*Light,TEXT("color"),Color,0,20)||
                   !Num(*Light,TEXT("intensity"),Slot.Light.Intensity,0,200000)||!Num(*Light,TEXT("radius"),Slot.Light.Radius,50,5000))Slot.Light.bEnabled=false;
                Slot.Light.Color=FLinearColor(Color.X,Color.Y,Color.Z);
                if(!Num(*Light,TEXT("flicker"),Slot.Light.Flicker,0,1))Slot.Light.Flicker=0; // world-dressing
            }
            // world-dressing: "cullDistance": end or [start, end] in cm; small clutter fades out with distance.
            {
                const TArray<TSharedPtr<FJsonValue>>* Cull=nullptr;double End=0;
                if(O->TryGetNumberField(TEXT("cullDistance"),End)&&Finite(End,0,200000)){Slot.CullStart=static_cast<float>(End*.8);Slot.CullEnd=static_cast<float>(End);}
                else if(O->TryGetArrayField(TEXT("cullDistance"),Cull)&&Cull->Num()==2)
                {
                    double A=0,B=0;
                    if((*Cull)[0]->TryGetNumber(A)&&(*Cull)[1]->TryGetNumber(B)&&Finite(A,0,200000)&&Finite(B,A,200000)){Slot.CullStart=static_cast<float>(A);Slot.CullEnd=static_cast<float>(B);}
                }
            }
        }
        Slot.Candidates.Append(Found);Added+=Found.Num();
    }
    if(Skipped)UE_LOG(LogCireTown,Display,TEXT("%s: %d slot entries not yet usable (pending import or no declared gate passage)."),*FPaths::GetCleanFilename(File),Skipped);
    UE_LOG(LogCireTown,Display,TEXT("CIRE_TOWN_SLOTS_MERGED file=%s source=%s priority=%d candidates=%d"),*FPaths::GetCleanFilename(File),*Source,P,Added);
    return true;
}
void Resolve(FSlot& Slot)
{
    Slot.Candidates.StableSort([](const FCandidate& A,const FCandidate& B){return A.Priority>B.Priority;});
    for(const FCandidate& C:Slot.Candidates)
    {
        if(Slot.bMaterial)
        {
            if(auto* M=LoadObject<UMaterialInterface>(nullptr,*C.Path,nullptr,LOAD_NoWarn|LOAD_Quiet)){Slot.Material=M;Slot.ResolvedSource=C.Source;return;}
            continue;
        }
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*C.Path,nullptr,LOAD_NoWarn|LOAD_Quiet);if(!Mesh)continue;
        FTransform Local(C.Rotation,FVector::ZeroVector,C.Scale);
        FBox Raw=Mesh->GetBoundingBox();TArray<TWeakObjectPtr<UStaticMesh>> Parts;
        for(const FString& PartPath:C.Parts)if(auto* Part=LoadObject<UStaticMesh>(nullptr,*PartPath,nullptr,LOAD_NoWarn|LOAD_Quiet)){Parts.Add(Part);Raw+=Part->GetBoundingBox();}
        FBox Rotated=Raw.TransformBy(Local);
        if(C.bFit&&Slot.Footprint.X>1&&Slot.Footprint.Y>1)
        {
            // Unknown third-party scale: fit uniformly into the authored footprint, centre it and seat it on the ground.
            const FVector Size=Rotated.GetSize();
            float Fit=FMath::Min(Slot.Footprint.X/FMath::Max(1.,Size.X),Slot.Footprint.Y/FMath::Max(1.,Size.Y));
            if(Slot.Footprint.Z>1)Fit=FMath::Min(Fit,static_cast<float>(Slot.Footprint.Z/FMath::Max(1.,Size.Z)));
            Local.SetScale3D(C.Scale*Fit);Rotated=Raw.TransformBy(Local);
            const FVector Centre=Rotated.GetCenter();Local.SetTranslation(FVector(-Centre.X,-Centre.Y,-Rotated.Min.Z)+C.Offset);
        }
        else Local.SetTranslation(C.Offset);
        Slot.MeshMaterial=C.MaterialOverride.IsEmpty()?nullptr:LoadObject<UMaterialInterface>(nullptr,*C.MaterialOverride,nullptr,LOAD_NoWarn|LOAD_Quiet);
        Slot.Mesh=Mesh;Slot.Parts=MoveTemp(Parts);Slot.Local=Local;Slot.LocalBox=Raw.TransformBy(Local);Slot.ResolvedSource=C.Source;
        if(Slot.Footprint.X>1&&Slot.Footprint.Y>1&&C.bFit)
            Slot.LocalBox=FBox(FVector(-Slot.Footprint.X*.5,-Slot.Footprint.Y*.5,0),FVector(Slot.Footprint.X*.5,Slot.Footprint.Y*.5,FMath::Max(Slot.Footprint.Z,10.)));
        return;
    }
    UE_LOG(LogCireTown,Warning,TEXT("Town slot %s has no loadable %s; its placements are skipped."),*Slot.Id.ToString(),Slot.bMaterial?TEXT("material"):TEXT("mesh"));
}
bool LoadTown()
{
    if(Town.bLoaded)return Town.bValid;
    Town=FTownData();Town.bLoaded=true;
    if(!MergeManifest(DataPath(TEXT("TownAssetSlots.json")),0,TEXT("base"),true))return false;
    TArray<FString> Overlays;IFileManager::Get().FindFiles(Overlays,*DataPath(TEXT("TownAssetSlots.*.json")),true,false);
    Overlays.Sort();
    for(const FString& Name:Overlays)
    {
        // Default quality order when a manifest omits "priority": tripo < fab.
        const FString Source=Name.Mid(15).LeftChop(5);
        const int32 Default=Source.Contains(TEXT("fab"))?20:Source.Contains(TEXT("tripo"))?10:5;
        // fab-integration: -CireNoFab / -CireNoFabTown hides the purchased-pack overlays (before/after captures).
        if(Name.Contains(TEXT(".fab"))&&(FParse::Param(FCommandLine::Get(),TEXT("CireNoFab"))||FParse::Param(FCommandLine::Get(),TEXT("CireNoFabTown"))))continue;
        MergeManifest(DataPath(*Name),Default,Source,false);
    }
    for(auto& Pair:Town.Slots)Resolve(Pair.Value);

    TSharedPtr<FJsonObject> Root;double Version=0;const TArray<TSharedPtr<FJsonValue>>*Placements=nullptr,*Districts=nullptr;
    if(!ReadJson(DataPath(TEXT("TownLayout.json")),Root)||!Root->TryGetNumberField(TEXT("schemaVersion"),Version)||Version!=1||
       !Root->TryGetArrayField(TEXT("placements"),Placements)||Placements->Num()>4096)
    {UE_LOG(LogCireTown,Error,TEXT("TownLayout.json is missing or invalid; town not built."));return false;}
    if(Root->TryGetArrayField(TEXT("districts"),Districts))for(const auto& V:*Districts)
    {
        const auto O=V->AsObject();CireEnvironmentProps::FTownDistrict D;FString Id;
        if(!O||!O->TryGetStringField(TEXT("id"),Id)||!O->TryGetStringField(TEXT("name"),D.Name)||!Num(O,TEXT("minX"),D.MinX,-80000,80000)||!Num(O,TEXT("maxX"),D.MaxX,-80000,80000)||D.MaxX<=D.MinX)continue;
        D.Id=FName(*Id);Town.Districts.Add(D);
    }
    // world-scale: world-aligned ground surfaces per district ({slot, x, length, width[, y]}; width 0 = full realm floor).
    const TArray<TSharedPtr<FJsonValue>>* Surfaces=nullptr;
    if(Root->TryGetArrayField(TEXT("surfaces"),Surfaces))for(const auto& V:*Surfaces)
    {
        const auto O=V->AsObject();CireEnvironmentProps::FTownSurface S;FString Slot;
        if(!O||!O->TryGetStringField(TEXT("slot"),Slot)||!Num(O,TEXT("x"),S.X,-80000,80000)||!Num(O,TEXT("length"),S.Length,10,100000)||
           !Num(O,TEXT("width"),S.Width,0,20000)||!Num(O,TEXT("y"),S.Y,-5000,5000)||Town.Surfaces.Num()>=64)continue;
        S.Slot=FName(*Slot);Town.Surfaces.Add(S);
    }
    int32 Rejected=0;
    for(const auto& V:*Placements)
    {
        const auto O=V->AsObject();FPlacement P;FString Slot,Mode;
        if(!O||!O->TryGetStringField(TEXT("slot"),Slot)){++Rejected;continue;}
        P.Slot=FName(*Slot);float X=0,Y=0,Z=0;
        if(!Town.Slots.Contains(P.Slot)||Town.Slots[P.Slot].bMaterial||!Num(O,TEXT("x"),X,-80000,80000)||!Num(O,TEXT("y"),Y,-40000,40000)||
           !Num(O,TEXT("z"),Z,-500,5000)||!Num(O,TEXT("yaw"),P.Yaw,-720,720)||!Num(O,TEXT("scale"),P.Scale,.05,20)){++Rejected;continue;}
        if(O->TryGetStringField(TEXT("mode"),Mode))P.Mode=Mode==TEXT("outer")?1:Mode==TEXT("inner")?2:0;
        P.Location=FVector(X,Y,Z);Town.Placements.Add(P);
    }
    if(Rejected)UE_LOG(LogCireTown,Warning,TEXT("TownLayout.json: %d placement rows rejected (unknown slot or out of range)."),Rejected);
    // world-dressing: additive layout overlays (TownLayout.<source>.json, e.g. .dressing.json) append placements;
    // they never remove or move base rows and pass the same clearance checks at runtime.
    {
        TArray<FString> LayoutOverlays;IFileManager::Get().FindFiles(LayoutOverlays,*DataPath(TEXT("TownLayout.*.json")),true,false);
        LayoutOverlays.Sort();
        for(const FString& Name:LayoutOverlays)
        {
            TSharedPtr<FJsonObject> Extra;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;double ExtraVersion=0;
            if(!ReadJson(DataPath(*Name),Extra)||!Extra->TryGetNumberField(TEXT("schemaVersion"),ExtraVersion)||ExtraVersion!=1||
               !Extra->TryGetArrayField(TEXT("placements"),Rows)||Rows->Num()>8192)
            {UE_LOG(LogCireTown,Warning,TEXT("%s ignored (unreadable, wrong schema or too many rows)."),*Name);continue;}
            int32 Added=0,Bad=0;
            for(const auto& V:*Rows)
            {
                const auto O=V->AsObject();FPlacement P;FString Slot,Mode;float X=0,Y=0,Z=0;
                if(!O||!O->TryGetStringField(TEXT("slot"),Slot)){++Bad;continue;}
                P.Slot=FName(*Slot);
                if(!Town.Slots.Contains(P.Slot)||Town.Slots[P.Slot].bMaterial||!Num(O,TEXT("x"),X,-80000,80000)||!Num(O,TEXT("y"),Y,-40000,40000)||
                   !Num(O,TEXT("z"),Z,-500,5000)||!Num(O,TEXT("yaw"),P.Yaw,-720,720)||!Num(O,TEXT("scale"),P.Scale,.05,20)){++Bad;continue;}
                if(O->TryGetStringField(TEXT("mode"),Mode))P.Mode=Mode==TEXT("outer")?1:Mode==TEXT("inner")?2:0;
                P.Location=FVector(X,Y,Z);Town.Placements.Add(P);++Added;
            }
            UE_LOG(LogCireTown,Display,TEXT("CIRE_TOWN_LAYOUT_OVERLAY file=%s placements=%d rejected=%d"),*Name,Added,Bad);
        }
    }
    Town.bValid=!Town.Placements.IsEmpty();return Town.bValid;
}
FTransform WorldTransform(const FPlacement& P,int32 Team)
{
    FVector L=P.Location;float Yaw=P.Yaw;
    // outer/inner rows are authored on +Y and mirrored per team so they always face the same realm edge.
    const float Outer=Team==0?-1.f:1.f;
    if(P.Mode==1){L.Y=FMath::Abs(L.Y)*Outer;if(Outer<0)Yaw=-Yaw;}
    else if(P.Mode==2){L.Y=-FMath::Abs(L.Y)*Outer;if(Outer>0)Yaw=-Yaw;}
    return FTransform(FRotator(0,Yaw,0),FVector(L.X,L.Y+CireLanePath::CenterY(Team),L.Z),FVector(P.Scale));
}
bool BoxHitsPoint(const FBox& Box,const FTransform& T,const FVector2D& P,float Margin)
{
    const FVector Local=T.InverseTransformPosition(FVector(P.X,P.Y,0));
    const float M=Margin/FMath::Max(.05f,static_cast<float>(T.GetScale3D().X));
    return Local.X>Box.Min.X-M&&Local.X<Box.Max.X+M&&Local.Y>Box.Min.Y-M&&Local.Y<Box.Max.Y+M;
}
bool Safe(const UWorld* World,int32 Team,const FBox& Box,const FTransform& T,EClearance Clearance)
{
    // Realm divider: the box must stay entirely on its own side of world Y=0.
    for(int32 I=0;I<4;++I)
    {
        const FVector C=T.TransformPosition(FVector(I&1?Box.Max.X:Box.Min.X,I&2?Box.Max.Y:Box.Min.Y,0));
        if(Team==0?C.Y>-DividerMargin:C.Y<DividerMargin)return false;
    }
    if(Clearance==EClearance::None)return true;
    const FVector Spawn=CireLanePath::SpawnPosition(World,Team,0);
    if(BoxHitsPoint(Box,T,FVector2D(Spawn),SpawnMargin))return false;
    // dev-route-tools: every challenge pack (1..16) keeps its own radius clear.
    for(int32 Bay=1,Count=CireLanePath::BayCount(World,Team);Bay<=Count;++Bay)
        if(BoxHitsPoint(Box,T,FVector2D(CireLanePath::ChallengePosition(World,Team,Bay,0)),CireLanePath::ChallengeRadius(World,Team,Bay)))return false;
    if(Clearance==EClearance::Bays)return true;
    const auto& Points=CireLanePath::Get(World).LocalPoints[Team];const float CY=CireLanePath::CenterY(Team);
    // world-scale: the route is three times longer; skip segments that cannot reach the footprint before sampling them.
    const float Reach=FMath::Max(FMath::Max(FVector2D(Box.Min).Size(),FVector2D(Box.Max).Size()),FMath::Max(FVector2D(Box.Min.X,Box.Max.Y).Size(),FVector2D(Box.Max.X,Box.Min.Y).Size()))
        *static_cast<float>(T.GetScale3D().GetMax())+RouteMarginFor(World)+5.f;
    const FVector2D Centre(T.GetLocation());
    for(int32 I=1;I<Points.Num();++I)
    {
        const FVector2D A(Points[I-1].X,Points[I-1].Y+CY),B(Points[I].X,Points[I].Y+CY);
        {
            const FVector2D AB=B-A;const double Alpha=FMath::Clamp(FVector2D::DotProduct(Centre-A,AB)/FMath::Max(1.,AB.SizeSquared()),0.,1.);
            if(FVector2D::DistSquared(Centre,A+AB*Alpha)>FMath::Square(Reach))continue;
        }
        const int32 Steps=FMath::Max(1,FMath::CeilToInt(FVector2D::Distance(A,B)/40.f));
        const float RouteMargin=RouteMarginFor(World);
        for(int32 S=0;S<=Steps;++S)if(BoxHitsPoint(Box,T,FMath::Lerp(A,B,S/static_cast<double>(Steps)),RouteMargin))return false;
    }
    return true;
}
}

void CireEnvironmentProps::Reload(){Town=FTownData();LoadTown();}
void CireEnvironmentProps::Build(ACireWorld* WorldActor)
{
    if(!IsValid(WorldActor))return;
    for(auto I=Worlds.CreateIterator();I;++I)if(!I.Key().IsValid())I.RemoveCurrent();
    if(!LoadTown())return;
    FWorldTown& Data=Worlds.FindOrAdd(WorldActor);
    for(auto& Pair:Data.Components)for(auto& C:Pair.Value)if(C.IsValid())C->DestroyComponent();
    Data.Components.Reset();
    for(auto& Pair:Data.Proxies)if(Pair.Value.IsValid())Pair.Value->DestroyComponent();
    Data.Proxies.Reset();
    // Material overlays (e.g. a Fab plaster) replace the matching mesh slot on every town mesh.
    TArray<const FSlot*> MaterialOverrides;
    for(const auto& Pair:Town.Slots)if(Pair.Value.bMaterial&&Pair.Value.Material.IsValid()&&!Pair.Value.MeshSlot.IsNone()&&Pair.Value.ResolvedSource!=TEXT("base")&&Pair.Value.ResolvedSource!=TEXT("fallback"))
        MaterialOverrides.Add(&Pair.Value);
    for(const auto& Pair:Town.Slots)
    {
        const FSlot& Slot=Pair.Value;if(Slot.bMaterial||!Slot.Mesh.IsValid())continue;
        if(Slot.bStatic){Data.Components.FindOrAdd(Slot.Id);continue;} // world-scale: placed as plain components in Refresh
        TArray<UStaticMesh*> Meshes={Slot.Mesh.Get()};for(const auto& Part:Slot.Parts)if(Part.IsValid())Meshes.Add(Part.Get());
        for(int32 Index=0;Index<Meshes.Num();++Index)
        {
        auto* C=NewObject<UHierarchicalInstancedStaticMeshComponent>(WorldActor,*FString::Printf(TEXT("Town_%s_%d"),*Slot.Id.ToString(),Index));
        C->SetupAttachment(WorldActor->GetRootComponent());C->SetStaticMesh(Meshes[Index]);C->SetMobility(EComponentMobility::Movable);
        C->SetCollisionObjectType(ECC_WorldStatic);C->SetCollisionResponseToAllChannels(ECR_Block);
        C->SetCollisionEnabled(Slot.bCollision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
        // nav-paths: colliding town pieces (houses, walls, the shrine, stalls, crates) carve the navmesh.
        C->SetGenerateOverlapEvents(false);C->SetCanEverAffectNavigation(Slot.bCollision);C->SetCastShadow(Slot.bShadow);
        C->ComponentTags.Add(TEXT("CireWorldProp"));C->ComponentTags.Add(TEXT("CireTown"));C->ComponentTags.Add(Slot.Id);
        if(Slot.CullEnd>0)C->SetCullDistances(FMath::RoundToInt(Slot.CullStart),FMath::RoundToInt(Slot.CullEnd)); // world-dressing
        for(const FSlot* M:MaterialOverrides)C->SetMaterialByName(M->MeshSlot,M->Material.Get());
        if(Slot.MeshMaterial.IsValid())for(int32 I=0;I<C->GetNumMaterials();++I)C->SetMaterial(I,Slot.MeshMaterial.Get());
        WorldActor->AddInstanceComponent(C);C->RegisterComponent();Data.Components.FindOrAdd(Slot.Id).Add(C);
        }
        // world-scale: a colliding slot whose mesh has no collision body would neither block units nor carve the navmesh;
        // give it an invisible box of its footprint instead (same idea as the arenas' blocker proxies).
        const UBodySetup* Body=Slot.Mesh->GetBodySetup();
        const bool bHasBody=Body&&(Body->AggGeom.GetElementCount()>0||Body->CollisionTraceFlag==CTF_UseComplexAsSimple);
        if(Slot.bCollision&&!bHasBody)
        {
            auto* P=NewObject<UInstancedStaticMeshComponent>(WorldActor,*FString::Printf(TEXT("TownProxy_%s"),*Slot.Id.ToString()));
            P->SetupAttachment(WorldActor->GetRootComponent());P->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            P->SetMobility(EComponentMobility::Movable);P->SetHiddenInGame(true);P->SetVisibility(false);P->SetCastShadow(false);
            P->SetCollisionObjectType(ECC_WorldStatic);P->SetCollisionResponseToAllChannels(ECR_Block);P->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            P->SetGenerateOverlapEvents(false);P->SetCanEverAffectNavigation(true);
            P->ComponentTags.Add(TEXT("CireWorldProp"));P->ComponentTags.Add(TEXT("CireTown"));P->ComponentTags.Add(TEXT("CireTownProxy"));
            WorldActor->AddInstanceComponent(P);P->RegisterComponent();Data.Proxies.Add(Slot.Id,P);
            UE_LOG(LogCireTown,Display,TEXT("CIRE_TOWN_COLLISION_PROXY slot=%s (mesh %s has no collision body)"),*Slot.Id.ToString(),*Slot.Mesh->GetName());
        }
    }
    Refresh(WorldActor);
}
void CireEnvironmentProps::Refresh(ACireWorld* WorldActor)
{
    auto* Data=Worlds.Find(WorldActor);if(!Data||!Town.bValid)return;
    UWorld* World=WorldActor->GetWorld();
    for(auto& Pair:Data->Components)for(auto& C:Pair.Value)if(C.IsValid())C->ClearInstances();
    for(auto& Pair:Data->Proxies)if(Pair.Value.IsValid())Pair.Value->ClearInstances(); // world-scale
    TMap<FName,TArray<FTransform>> ProxyBatches;
    for(auto& L:Data->Lights)if(L.IsValid())L->DestroyComponent();
    for(auto& M:Data->Statics)if(M.IsValid())M->DestroyComponent(); // world-scale
    Data->Statics.Reset();
    CireWorldDressing::ResetFlicker(WorldActor); // world-dressing
    Data->Lights.Reset();Data->Placed.Reset();Data->Visible=Data->Suppressed=0;
    int32 Lights[2]={0,0};
    TMap<FName,TArray<FTransform>> Batches;
    for(const FPlacement& P:Town.Placements)
    {
        const FSlot& Slot=Town.Slots[P.Slot];if(!Data->Components.Contains(P.Slot)||!Slot.Mesh.IsValid())continue;
        for(int32 Team=0;Team<2;++Team)
        {
            const FTransform T=WorldTransform(P,Team);
            if(!Safe(World,Team,Slot.LocalBox,T,Slot.Clearance)){++Data->Suppressed;continue;}
            if(Slot.bStatic)
            {
                // world-scale: huge backdrop meshes (mountains) whose vendor materials lack the instanced-mesh usage flag.
                TArray<UStaticMesh*> Meshes={Slot.Mesh.Get()};for(const auto& Part:Slot.Parts)if(Part.IsValid())Meshes.Add(Part.Get());
                for(UStaticMesh* Mesh:Meshes)
                {
                    auto* C=NewObject<UStaticMeshComponent>(WorldActor);C->SetupAttachment(WorldActor->GetRootComponent());C->SetStaticMesh(Mesh);
                    C->SetMobility(EComponentMobility::Movable);C->SetWorldTransform(Slot.Local*T);C->SetCastShadow(Slot.bShadow);
                    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);C->bAffectDistanceFieldLighting=false;
                    C->ComponentTags.Add(TEXT("CireWorldProp"));C->ComponentTags.Add(TEXT("CireTown"));C->ComponentTags.Add(Slot.Id);
                    if(Slot.MeshMaterial.IsValid())for(int32 I=0;I<C->GetNumMaterials();++I)C->SetMaterial(I,Slot.MeshMaterial.Get());
                    C->RegisterComponent();WorldActor->AddInstanceComponent(C);Data->Statics.Add(C);
                }
            }
            else Batches.FindOrAdd(P.Slot).Add(Slot.Local*T);
            if(Data->Proxies.Contains(P.Slot)&&Slot.LocalBox.IsValid) // world-scale: the footprint box as an invisible blocker
                ProxyBatches.FindOrAdd(P.Slot).Add(FTransform(FRotator::ZeroRotator,Slot.LocalBox.GetCenter(),Slot.LocalBox.GetSize()/100.f)*T);
            Data->Placed.Add({Team,P.Slot,T,Slot.LocalBox,Slot.Clearance});++Data->Visible;
            if(Slot.Light.bEnabled&&Lights[Team]<MaxLightsPerTeam)
            {
                auto* L=NewObject<UPointLightComponent>(WorldActor);L->SetupAttachment(WorldActor->GetRootComponent());
                L->SetWorldLocation(T.TransformPosition(Slot.Light.Offset));L->SetIntensity(Slot.Light.Intensity);
                L->SetLightColor(Slot.Light.Color);L->SetAttenuationRadius(Slot.Light.Radius);L->SetCastShadows(false);
                L->SetSourceRadius(8.f);L->MaxDrawDistance=LightDrawDistance;L->MaxDistanceFadeRange=LightFadeRange; // world-scale
                L->RegisterComponent();WorldActor->AddInstanceComponent(L);
                Data->Lights.Add(L);++Lights[Team];
                if(Slot.Light.Flicker>0)CireWorldDressing::AddFlicker(WorldActor,L,Slot.Light.Flicker); // world-dressing
            }
        }
    }
    for(auto& Pair:Batches)for(auto& C:Data->Components[Pair.Key])if(C.IsValid())C->AddInstances(Pair.Value,false,true);
    for(auto& Pair:ProxyBatches)if(auto* P=Data->Proxies.FindRef(Pair.Key).Get())P->AddInstances(Pair.Value,false,true); // world-scale
    CireNav::RefreshActor(WorldActor); // nav-paths: re-placed pieces carve the navmesh (live route edits)
    UE_LOG(LogCireTown,Display,TEXT("CIRE_ENVIRONMENT_PROPS_READY instances=%d suppressed_for_route_clearance=%d slots=%d lights=%d"),
        Data->Visible,Data->Suppressed,Data->Components.Num(),Data->Lights.Num());
}
int32 CireEnvironmentProps::InstanceCount(const ACireWorld* WorldActor)
{const auto* Data=Worlds.Find(TWeakObjectPtr<ACireWorld>(const_cast<ACireWorld*>(WorldActor)));return Data?Data->Visible:0;}
int32 CireEnvironmentProps::SuppressedCount(const ACireWorld* WorldActor)
{const auto* Data=Worlds.Find(TWeakObjectPtr<ACireWorld>(const_cast<ACireWorld*>(WorldActor)));return Data?Data->Suppressed:0;}
int32 CireEnvironmentProps::LightCount(const ACireWorld* WorldActor)
{const auto* Data=Worlds.Find(TWeakObjectPtr<ACireWorld>(const_cast<ACireWorld*>(WorldActor)));return Data?Data->Lights.Num():0;}
bool CireEnvironmentProps::HasSafeClearance(const ACireWorld* WorldActor)
{
    const auto* Data=Worlds.Find(TWeakObjectPtr<ACireWorld>(const_cast<ACireWorld*>(WorldActor)));if(!Data)return false;
    for(const auto& P:Data->Placed)if(!Safe(WorldActor->GetWorld(),P.Team,P.Box,P.Transform,P.Clearance))return false;
    return true;
}
// nav-paths: path-editor validation and navigation tests.
int32 CireEnvironmentProps::RouteConflicts(const UWorld* World,int32 Team,const FVector2D& LocalA,const FVector2D& LocalB,float LaneWidth,TArray<FName>* Slots)
{
    if(!LoadTown()||Team<0||Team>1)return 0;
    const float CY=CireLanePath::CenterY(Team),Margin=LaneWidth*.5f+70.f;
    const FVector2D A(LocalA.X,LocalA.Y+CY),B(LocalB.X,LocalB.Y+CY);
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(FVector2D::Distance(A,B)/40.f));
    int32 Count=0;
    for(const FPlacement& P:Town.Placements)
    {
        const FSlot& Slot=Town.Slots[P.Slot];
        if(Slot.bMaterial||!Slot.Mesh.IsValid()||Slot.Clearance!=EClearance::Route)continue;
        const FTransform T=WorldTransform(P,Team);
        for(int32 S=0;S<=Steps;++S)if(BoxHitsPoint(Slot.LocalBox,T,FMath::Lerp(A,B,S/static_cast<double>(Steps)),Margin))
        {++Count;if(Slots)Slots->AddUnique(P.Slot);break;}
    }
    return Count;
}
int32 CireEnvironmentProps::BayConflicts(const UWorld* World,int32 Team,const FVector2D& LocalBay,TArray<FName>* Slots,float Radius)
{
    if(!LoadTown()||Team<0||Team>1)return 0;
    const FVector2D Bay(CireLanePath::ToWorld(Team,LocalBay));
    int32 Count=0;
    for(const FPlacement& P:Town.Placements)
    {
        const FSlot& Slot=Town.Slots[P.Slot];
        if(Slot.bMaterial||!Slot.Mesh.IsValid()||Slot.Clearance==EClearance::None)continue;
        if(BoxHitsPoint(Slot.LocalBox,WorldTransform(P,Team),Bay,FMath::IsFinite(Radius)&&Radius>0?Radius:BayMargin)){++Count;if(Slots)Slots->AddUnique(P.Slot);}
    }
    return Count;
}
TArray<CireEnvironmentProps::FPlacedProp> CireEnvironmentProps::PlacedProps(const ACireWorld* WorldActor)
{
    TArray<FPlacedProp> Out;
    const auto* Data=Worlds.Find(TWeakObjectPtr<ACireWorld>(const_cast<ACireWorld*>(WorldActor)));if(!Data)return Out;
    for(const auto& P:Data->Placed)
    {
        const FSlot* Slot=Town.Slots.Find(P.Slot);
        Out.Add({P.Team,P.Slot,P.Transform,P.Box,Slot&&Slot->bCollision});
    }
    return Out;
}
const TArray<CireEnvironmentProps::FTownDistrict>& CireEnvironmentProps::Districts(){LoadTown();return Town.Districts;}
const TArray<CireEnvironmentProps::FTownSurface>& CireEnvironmentProps::Surfaces(){LoadTown();return Town.Surfaces;}
FName CireEnvironmentProps::DistrictAt(const UWorld* World,int32 Team,const FVector& Location)
{
    if(Team<0||Team>1||!CireLanePath::Contains(World,Team,Location,0))return NAME_None;
    for(const auto& D:Districts())if(Location.X>=D.MinX&&Location.X<D.MaxX)return D.Id;
    return NAME_None;
}
FString CireEnvironmentProps::DistrictName(FName Id)
{
    for(const auto& D:Districts())if(D.Id==Id)return D.Name;
    return FString();
}
UMaterialInterface* CireEnvironmentProps::SurfaceMaterial(const TCHAR* SlotId,const TCHAR* FallbackPath)
{
    LoadTown();if(const FSlot* Slot=Town.Slots.Find(FName(SlotId));Slot&&Slot->Material.IsValid())return Slot->Material.Get();
    return LoadObject<UMaterialInterface>(nullptr,FallbackPath);
}
FString CireEnvironmentProps::SlotSource(FName SlotId)
{
    LoadTown();const FSlot* Slot=Town.Slots.Find(SlotId);return Slot?Slot->ResolvedSource:FString();
}
