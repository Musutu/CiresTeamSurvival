#include "CireWeaponPresentation.h"
#include "CireParagonChampions.h" // paragon-champions

#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "CireGame.h"
#include "CireMobility.h" // blender-rig: roll state for the muzzle check
#include "CireChampionRoster.h"
#include "CireChampionArt.h" // paladin-hq: prop material specs
#include "CireChampionActions.h" // weapon-grips: motion class for the Fab set
#include "CireWeaponSockets.h" // weapon-grips: animation-authored grips
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h" // weapon-grips: gallery metrics
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
struct FPart
{
    FString Asset,Role,Token;FName Bone;FVector Offset=FVector::ZeroVector;FRotator Rotation=FRotator::ZeroRotator;
    float Size=1;float Girth=1;bool bHideOnRelease=false; // blender-rig: girth = cross-section / length scale
    FString SizeClass; // blender-rig
    float MaxBodyFraction=0.f,BaseSize=1.f; // blender-rig: size-class cap (prop length / body height) and the size before the class
};
struct FLoadout {FString Motion;TArray<FPart> Parts;};
struct FDatabase
{
    TMap<FString,FLoadout> Presets;TMap<FString,FString> Profiles;TMap<FString,TArray<FString>> Options;
};
FDatabase Database;
int32 Revision=0;
bool bLoaded=false;
const FName ReleaseTag(TEXT("CireHideOnRelease"));
bool NaturalAttacks(const FString& Id)
{
    return Id==TEXT("bear")||Id==TEXT("whisp")||Id.StartsWith(TEXT("ether_golem_"));
}
FString AssetPath(const FString& Token)
{
    // tripo-races: Tripo champion props (/Game/Tripo/Props/<Name>/CTS_Prop_<Name>, Content/Data/ChampionArt.tripo.json).
    static const TSet<FString> Tripo={TEXT("GunbladePistol"),TEXT("GunbladeSword"),TEXT("HuntressGlaiveLauncher"),TEXT("WitchSlayerBlade"),TEXT("WitchSlayerBlunderbuss")};
    if(Token.StartsWith(TEXT("tripo/")))
    {
        const FString Kind=Token.Mid(6);if(!Tripo.Contains(Kind))return FString();
        return FString::Printf(TEXT("/Game/Tripo/Props/%s/CTS_Prop_%s.CTS_Prop_%s"),*Kind,*Kind,*Kind);
    }
    static const TSet<FString> Legacy={TEXT("Sword"),TEXT("Shield"),TEXT("Bow"),TEXT("Arrow"),TEXT("Lance")};
    static const TSet<FString> Armory={TEXT("ArcaneStaff"),TEXT("RiftStaff"),TEXT("EmberStaff"),TEXT("GroveStaff"),TEXT("LanternStaff"),
        TEXT("Dagger"),TEXT("WarAxe"),TEXT("ThrowingAxe"),TEXT("WarHammer"),TEXT("PickHammer"),TEXT("Flail"),TEXT("Totem"),TEXT("Crossbow"),TEXT("Bolt")};
    if(Token.StartsWith(TEXT("legacy/")))
    {
        const FString Kind=Token.Mid(7);if(!Legacy.Contains(Kind))return FString();
        return FString::Printf(TEXT("/Game/Art/Weapons/CombatPrototype01/SM_Prototype%s.SM_Prototype%s"),*Kind,*Kind);
    }
    // new-champions: hunter/* props (Tools/BuildNewChampionContent.py -> /Game/Art/NewChampions01/Props).
    static const TSet<FString> Hunter={TEXT("Flintlock"),TEXT("Falchion"),TEXT("ArcaneBlunderbuss"),TEXT("SpectralBlade"),TEXT("Glaive"),
        TEXT("GlaiveLauncher"),TEXT("AetherStaff"),TEXT("AetherHalberd")};
    if(Token.StartsWith(TEXT("hunter/")))
    {
        const FString Kind=Token.Mid(7);if(!Hunter.Contains(Kind))return FString();
        return FString::Printf(TEXT("/Game/Art/NewChampions01/Props/SM_%s.SM_%s"),*Kind,*Kind);
    }
    if(!Armory.Contains(Token))return FString();
    return FString::Printf(TEXT("/Game/Art/Weapons/ArmoryPrototype01/SM_%s.SM_%s"),*Token,*Token);
}
bool ReadVector(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,FVector& Out,float Limit)
{
    if(!Object->HasField(Key))return true;
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!Object->TryGetArrayField(Key,Values)||Values->Num()!=3)return false;
    double Data[3];
    for(int32 I=0;I<3;++I)if(!(*Values)[I]->TryGetNumber(Data[I])||!FMath::IsFinite(Data[I])||FMath::Abs(Data[I])>Limit)return false;
    Out=FVector(Data[0],Data[1],Data[2]);return true;
}
bool Parse(const FString& Text,FDatabase& Out,FString& Error)
{
    auto Bad=[&](const FString& Why){Error=Why;return false;};
    TSharedPtr<FJsonObject> Root;const auto Reader=TJsonReaderFactory<>::Create(Text);
    if(!FJsonSerializer::Deserialize(Reader,Root)||!Root)return Bad(TEXT("Invalid weapon loadout JSON"));
    double Version=0;const TSharedPtr<FJsonObject>* Presets=nullptr;const TSharedPtr<FJsonObject>* Profiles=nullptr;
    if(!Root->TryGetNumberField(TEXT("schemaVersion"),Version)||Version!=1||!Root->TryGetObjectField(TEXT("presets"),Presets)||
       !Root->TryGetObjectField(TEXT("profiles"),Profiles)||(*Presets)->Values.IsEmpty()||(*Profiles)->Values.IsEmpty()||
       (*Presets)->Values.Num()>64||(*Profiles)->Values.Num()>128)
        return Bad(TEXT("Expected schemaVersion 1, presets and profiles"));
    FDatabase Candidate;
    // blender-rig: "sizeClasses": {"one_hand": {"scale": 2, "girth": .7}, ...}; a part's "sizeClass" multiplies its scale
    // and sets its girth, so one knob resizes every one-handed weapon (Eric, 2026-09-26: 1H weapons and maces 2x).
    TMap<FString,TPair<double,double>> SizeClasses;TMap<FString,double> SizeCaps;
    if(const TSharedPtr<FJsonObject>* Classes=nullptr;Root->TryGetObjectField(TEXT("sizeClasses"),Classes))
        for(const auto& Pair:(*Classes)->Values)
        {
            const TSharedPtr<FJsonObject>* Row=nullptr;double Scale=1,Girth=1;
            if(!Pair.Value->TryGetObject(Row)||!(*Row)->TryGetNumberField(TEXT("scale"),Scale)||!FMath::IsFinite(Scale)||Scale<.35||Scale>4)return Bad(TEXT("Invalid size class: ")+FString(Pair.Key.ToView()));
            if((*Row)->HasField(TEXT("girth"))&&(!(*Row)->TryGetNumberField(TEXT("girth"),Girth)||!FMath::IsFinite(Girth)||Girth<.4||Girth>1))return Bad(TEXT("Size class girth must be .4..1"));
            SizeClasses.Add(FString(Pair.Key.ToView()),TPair<double,double>(Scale,Girth));
            double Cap=0;if((*Row)->TryGetNumberField(TEXT("maxBodyFraction"),Cap)&&FMath::IsFinite(Cap)&&Cap>.1&&Cap<=2)SizeCaps.Add(FString(Pair.Key.ToView()),Cap);
        }
    for(const auto& Pair:(*Presets)->Values)
    {
        const FString Key(Pair.Key.ToView());
        const TSharedPtr<FJsonObject>* Row=nullptr;const TArray<TSharedPtr<FJsonValue>>* Parts=nullptr;FLoadout Loadout;
        if(Key.IsEmpty()||Key.Len()>64||!Pair.Value->TryGetObject(Row)||!(*Row)->TryGetStringField(TEXT("motion"),Loadout.Motion)||
            !(*Row)->TryGetArrayField(TEXT("parts"),Parts)||Parts->Num()>6)return Bad(TEXT("Invalid weapon preset: ")+Key);
        if(Loadout.Motion!=TEXT("none")&&Loadout.Motion!=TEXT("melee")&&Loadout.Motion!=TEXT("cast")&&Loadout.Motion!=TEXT("bow")&&
            Loadout.Motion!=TEXT("crossbow")&&Loadout.Motion!=TEXT("throw"))return Bad(TEXT("Unknown weapon motion: ")+Loadout.Motion);
        int32 Primaries=0,Ammunition=0;
        for(const auto& Value:*Parts)
        {
            const TSharedPtr<FJsonObject>* PartRow=nullptr;FPart Part;FString Token,Bone;
            if(!Value->TryGetObject(PartRow)||!(*PartRow)->TryGetStringField(TEXT("asset"),Token)||!(*PartRow)->TryGetStringField(TEXT("bone"),Bone))
                return Bad(TEXT("Weapon part requires asset and bone"));
            Part.Asset=AssetPath(Token);Part.Bone=FName(*Bone);Part.Token=Token;
            if(Part.Asset.IsEmpty()||(Bone!=TEXT("hand_l")&&Bone!=TEXT("hand_r")&&Bone!=TEXT("pelvis")&&Bone!=TEXT("spine_03")))
                return Bad(TEXT("Unsupported asset or attachment bone: ")+Token+TEXT(" / ")+Bone);
            FVector Rotation=FVector::ZeroVector;
            if(!ReadVector(*PartRow,TEXT("offsetCm"),Part.Offset,100)||!ReadVector(*PartRow,TEXT("rotation"),Rotation,360))return Bad(TEXT("Invalid grip offset/rotation"));
            Part.Rotation=FRotator(Rotation.X,Rotation.Y,Rotation.Z);
            double Size=1;
            if((*PartRow)->HasField(TEXT("scale"))&&(!(*PartRow)->TryGetNumberField(TEXT("scale"),Size)||!FMath::IsFinite(Size)||Size<.35||Size>4))return Bad(TEXT("Weapon scale must be .35..4"));
            Part.Size=Size;
            // blender-rig: "girth" (.4..1) thins the cross-section of an upscaled prop, so a 2x weapon keeps a handle the fist
            // closes around (the length scales by "scale", the handle radius and blade width by scale * girth).
            double Girth=1;
            if((*PartRow)->HasField(TEXT("girth"))&&(!(*PartRow)->TryGetNumberField(TEXT("girth"),Girth)||!FMath::IsFinite(Girth)||Girth<.4||Girth>1))return Bad(TEXT("Weapon girth must be .4..1"));
            Part.Girth=Girth;
            if(FString SizeClass;(*PartRow)->TryGetStringField(TEXT("sizeClass"),SizeClass))
            {
                const auto* Class=SizeClasses.Find(SizeClass);if(!Class)return Bad(TEXT("Unknown size class: ")+SizeClass);
                Part.BaseSize=Part.Size;Part.Size*=Class->Key;Part.Girth*=Class->Value;Part.SizeClass=SizeClass;
                if(const double* Cap=SizeCaps.Find(SizeClass))Part.MaxBodyFraction=*Cap;
                if(Part.Size>4||Part.Girth<.4)return Bad(TEXT("Size class makes the part scale exceed 4 or its girth drop below .4"));
            }
            if((*PartRow)->HasField(TEXT("hideOnRelease"))&&!(*PartRow)->TryGetBoolField(TEXT("hideOnRelease"),Part.bHideOnRelease))return Bad(TEXT("Invalid release visibility"));
            if((*PartRow)->HasField(TEXT("role"))&&!(*PartRow)->TryGetStringField(TEXT("role"),Part.Role))return Bad(TEXT("Invalid weapon part role"));
            if(!Part.Role.IsEmpty()&&Part.Role!=TEXT("primary")&&Part.Role!=TEXT("ammunition"))return Bad(TEXT("Unsupported weapon part role"));
            Primaries+=Part.Role==TEXT("primary");Ammunition+=Part.Role==TEXT("ammunition");Loadout.Parts.Add(Part);
        }
        if(Loadout.Motion==TEXT("none")?Parts->Num()!=0:Primaries!=1)return Bad(TEXT("Preset requires exactly one primary, or no parts for none"));
        const bool bRanged=Loadout.Motion==TEXT("bow")||Loadout.Motion==TEXT("crossbow");
        if(Ammunition!=(bRanged?1:0))return Bad(TEXT("Bow/crossbow require one ammunition prop"));
        Candidate.Presets.Add(Key,Loadout);
    }
    for(const auto& Pair:(*Profiles)->Values)
    {
        const FString Key(Pair.Key.ToView());
        FString Preset;if(Key.IsEmpty()||Key.Len()>64||!Pair.Value->TryGetString(Preset)||!Candidate.Presets.Contains(Preset))return Bad(TEXT("Unknown profile preset: ")+Key);
        if(NaturalAttacks(Key)&&!Candidate.Presets[Preset].Parts.IsEmpty())return Bad(TEXT("Natural-attack profile cannot carry humanoid equipment: ")+Key);
        Candidate.Profiles.Add(Key,Preset);
    }
    const TSharedPtr<FJsonObject>* Options=nullptr;
    if(Root->HasField(TEXT("previewOptions")))
    {
        if(!Root->TryGetObjectField(TEXT("previewOptions"),Options))return Bad(TEXT("Invalid previewOptions"));
        for(const auto& Pair:(*Options)->Values)
        {
            const FString Key(Pair.Key.ToView());
            const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;TArray<FString> Names;
            if(!Candidate.Profiles.Contains(Key)||NaturalAttacks(Key)||!Pair.Value->TryGetArray(Values)||Values->Num()<2||Values->Num()>8)return Bad(TEXT("Invalid profile preview options"));
            for(const auto& Value:*Values)
            {
                FString Name;if(!Value->TryGetString(Name)||!Candidate.Presets.Contains(Name)||Names.Contains(Name))return Bad(TEXT("Unknown or duplicate preview preset"));
                Names.Add(Name);
            }
            if(Names[0]!=Candidate.Profiles[Key])return Bad(TEXT("First preview option must be the profile default"));
            Candidate.Options.Add(Key,Names);
        }
    }
    Out=MoveTemp(Candidate);Error.Reset();return true;
}
void EnsureLoaded()
{
    if(bLoaded)return;bLoaded=true;FString Error;
    if(!CireWeapons::Reload(Error))UE_LOG(LogTemp,Warning,TEXT("CIRE_WEAPONS_DATA_ERROR %s"),*Error);
}
FString ProfileKey(const ACireHero& Hero,int32 Archetype)
{
    if(!Hero.ChampionProfileId.IsEmpty())return Hero.ChampionProfileId;
    const TCHAR* Legacy[]={TEXT("knight"),TEXT("ranger"),TEXT("scholar"),TEXT("lancer"),TEXT("summoner")};
    return Archetype>=0&&Archetype<UE_ARRAY_COUNT(Legacy)?FString(Legacy[Archetype]):FString();
}
FTransform ReferenceBone(const USkeletalMesh& Mesh,FName Bone)
{
    const auto& Skeleton=Mesh.GetRefSkeleton();int32 Index=Skeleton.FindBoneIndex(Bone);
    if(Index==INDEX_NONE)return FTransform::Identity;
    FTransform Result=Skeleton.GetRefBonePose()[Index];
    while((Index=Skeleton.GetParentIndex(Index))!=INDEX_NONE)Result=Result*Skeleton.GetRefBonePose()[Index];
    return Result;
}
void VisualOnly(UStaticMeshComponent& Part)
{
    Part.SetCollisionEnabled(ECollisionEnabled::NoCollision);Part.SetGenerateOverlapEvents(false);
    Part.SetCanEverAffectNavigation(false);Part.SetCastShadow(true);
    Part.ComponentTags.AddUnique(TEXT("CireWeaponProp"));
    // Imported hand transforms inherit root scale100. Props use real centimeters.
    Part.SetAbsolute(false,false,true);Part.SetWorldScale3D(FVector::OneVector);
}
}

bool CireWeapons::Reload(FString& Error)
{
    FString Text;const FString Path=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/WeaponLoadouts.json"));
    if(!FFileHelper::LoadFileToString(Text,*Path)||Text.Len()>200000){Error=TEXT("Missing or oversized WeaponLoadouts.json");return false;}
    FDatabase Candidate;if(!Parse(Text,Candidate,Error))return false;
    for(const auto& Profile:CireChampionRoster::All())if(!Candidate.Profiles.Contains(Profile.Id)&&!CireParagonChampions::IsParagon(Profile.Id)) // paragon-champions: weapons are in the Paragon mesh
    {Error=TEXT("Missing profile loadout: ")+Profile.Id;return false;}
    Database=MoveTemp(Candidate);bLoaded=true;++Revision;return true;
}
bool CireWeapons::RunValidationSmoke(bool bRequireAssets)
{
    EnsureLoaded();int32 Checks=0;bool Passed=true;
    auto Check=[&](bool Value,const FString& Why){++Checks;if(!Value){Passed=false;UE_LOG(LogTemp,Error,TEXT("CIRE_WEAPONS_CHECK_FAIL %s"),*Why);}};
    TSet<FString> Paths;
    for(const auto& Profile:CireChampionRoster::All())
    {
        if(CireParagonChampions::IsParagon(Profile.Id))continue; // paragon-champions: weapons are skinned into the Paragon mesh
        const FString* Preset=Database.Profiles.Find(Profile.Id);Check(Preset!=nullptr,Profile.Id+TEXT(" has an explicit loadout"));

        if(Preset){const auto* Loadout=Database.Presets.Find(*Preset);Check(Loadout!=nullptr,Profile.Id+TEXT(" preset exists"));
            if(Loadout)Check(NaturalAttacks(Profile.Id)==Loadout->Parts.IsEmpty(),Profile.Id+TEXT(" natural attacks are unarmed"));}
    }
    for(const auto& Pair:Database.Presets)for(const auto& Part:Pair.Value.Parts)Paths.Add(Part.Asset);
    if(bRequireAssets)for(const auto& Path:Paths)
    {
        auto* Asset=LoadObject<UStaticMesh>(nullptr,*Path);Check(Asset!=nullptr,Path);
        if(Asset){const FVector Size=Asset->GetBounds().BoxExtent*2;Check(!Size.ContainsNaN()&&Size.GetMax()>5&&Size.GetMax()<350,Path+TEXT(" centimeters"));
            for(int32 Slot=0;Slot<Asset->GetStaticMaterials().Num();++Slot)Check(Asset->GetMaterial(Slot)!=nullptr,Path+TEXT(" material"));}
    }
    FString Error;FDatabase Rejected=Database;const int32 Before=Rejected.Presets.Num();
    Check(!Parse(TEXT("{\"schemaVersion\":1,\"presets\":{},\"profiles\":{\"knight\":\"missing\"}}"),Rejected,Error),TEXT("rejects missing preset"));
    Check(!Parse(TEXT("{\"schemaVersion\":1,\"presets\":{\"bad\":{\"motion\":\"melee\",\"parts\":[{\"asset\":\"../unsafe\",\"bone\":\"hand_r\",\"role\":\"primary\"}]}},\"profiles\":{}}"),Rejected,Error),TEXT("rejects asset traversal"));
    Check(!Parse(TEXT("{\"schemaVersion\":1,\"presets\":{\"armed\":{\"motion\":\"melee\",\"parts\":[{\"asset\":\"legacy/Sword\",\"bone\":\"hand_r\",\"role\":\"primary\"}]}},\"profiles\":{\"bear\":\"armed\"}}"),Rejected,Error),TEXT("rejects equipment on natural-attack body"));
    Check(Rejected.Presets.Num()==Before,TEXT("invalid parse retains previous data"));
    UE_LOG(LogTemp,Display,TEXT("CIRE_WEAPONS_SMOKE_%s checks=%d presets=%d asset_validation=%d"),Passed?TEXT("PASS"):TEXT("FAIL"),Checks,Database.Presets.Num(),bRequireAssets);return Passed;
}

void UCireWeaponPresentation::Clear()
{
    for(UStaticMeshComponent* Part:Parts)if(Part)Part->DestroyComponent();
    Parts.Reset();BowStrings.Reset();Primary=nullptr;Arrow=nullptr;EquippedMesh=nullptr;GripInfo.Reset();GripSet.Reset(); // weapon-grips
    GripHands=CireGrip::FHands();DrawPose=CireGrip::FHandPose(); // creature-anim
    EquippedProfile.Reset();EquippedLoadout.Reset();Motion.Reset();AppliedRevision=INDEX_NONE;PrimarySize=1;
}
namespace
{
// fab-integration: WeaponLoadouts.fab.json replaces an asset token with a Fab weapon mesh when the pack is installed.
struct FFabWeapon { FString Mesh; float Scale = 1.f; TSharedPtr<FJsonObject> Materials; };
TMap<FString,TMap<FString,FFabWeapon>> GFabProfileWeapons; // paladin-hq: profile -> token -> prop
void ReadFabWeapons(const TSharedPtr<FJsonObject>& Rows,TMap<FString,FFabWeapon>& Map)
{
    for(const auto& Pair:Rows->Values)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FFabWeapon W;double Scale=1;const TSharedPtr<FJsonObject>* Materials=nullptr;
        if(!Pair.Value->TryGetObject(O)||!(*O)->TryGetStringField(TEXT("mesh"),W.Mesh)||!W.Mesh.StartsWith(TEXT("/Game/")))continue;
        if((*O)->TryGetNumberField(TEXT("scale"),Scale)&&FMath::IsFinite(Scale))W.Scale=FMath::Clamp(static_cast<float>(Scale),.2f,3.f);
        if((*O)->TryGetObjectField(TEXT("materials"),Materials))W.Materials=*Materials;
        Map.Add(FString(Pair.Key.ToView()),W);
    }
}
const TMap<FString,FFabWeapon>& FabWeapons()
{
    static TMap<FString,FFabWeapon> Map; static bool bFabLoaded=false;
    if(bFabLoaded)return Map; bFabLoaded=true;
    FString Text;TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Rows=nullptr;
    if(FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/WeaponLoadouts.fab.json")))&&
       FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)&&Root)
    {
        if(Root->TryGetObjectField(TEXT("overrides"),Rows))ReadFabWeapons(*Rows,Map);
        if(Root->TryGetObjectField(TEXT("profiles"),Rows))
            for(const auto& Pair:(*Rows)->Values){const TSharedPtr<FJsonObject>* P=nullptr;if(Pair.Value->TryGetObject(P))ReadFabWeapons(*P,GFabProfileWeapons.Add(FString(Pair.Key.ToView())));}
    }
    return Map;
}
}
FString CireWeaponFab::ResolveMesh(const FString& Profile,const FString& Token,const FString& Fallback,float& InOutSize,TSharedPtr<FJsonObject>& OutMaterials,bool& bOutProfileProp)
{
    OutMaterials.Reset();bOutProfileProp=false;
    static const bool bOff=FParse::Param(FCommandLine::Get(),TEXT("CireNoFabWeapons"))||FParse::Param(FCommandLine::Get(),TEXT("CireNoFabCreatures"))||FParse::Param(FCommandLine::Get(),TEXT("CireNoFab"));
    FabWeapons();
    const auto* Props=bOff?nullptr:GFabProfileWeapons.Find(Profile);
    if(const FFabWeapon* W=Props?Props->Find(Token):nullptr;W&&FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(W->Mesh)))
    {InOutSize*=W->Scale;OutMaterials=W->Materials;bOutProfileProp=true;return W->Mesh;}
    return ResolveMesh(Token,Fallback,InOutSize);
}
FString CireWeaponFab::ResolveMesh(const FString& Token,const FString& Fallback,float& InOutSize)
{
    static const bool bOff=FParse::Param(FCommandLine::Get(),TEXT("CireNoFabWeapons"))||FParse::Param(FCommandLine::Get(),TEXT("CireNoFabCreatures"))||FParse::Param(FCommandLine::Get(),TEXT("CireNoFab"));
    const FFabWeapon* W=bOff?nullptr:FabWeapons().Find(Token);
    if(!W||!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(W->Mesh)))return Fallback;
    InOutSize*=W->Scale;return W->Mesh;
}
UStaticMeshComponent* UCireWeaponPresentation::Attach(ACireHero& Hero,const FString& AssetPath,FName BoneName,
    const FVector& OffsetCm,const FRotator& Rotation,float Size,bool bPrimary,float Girth,float MaxBodyFraction,float MinSize)
{
    auto* Body=Hero.GetMesh();
    if(!Body||!Body->GetSkeletalMeshAsset()||Body->GetBoneIndex(BoneName)==INDEX_NONE)return nullptr;
    // A non-humanoid custom body must not inherit a humanoid profile's weapons.
    if(Body->GetBoneIndex(TEXT("hand_l"))==INDEX_NONE||Body->GetBoneIndex(TEXT("hand_r"))==INDEX_NONE)return nullptr;
    auto* Asset=LoadObject<UStaticMesh>(nullptr,*AssetPath);
    if(!Asset){UE_LOG(LogTemp,Warning,TEXT("CIRE_WEAPON_ASSET_MISSING %s"),*AssetPath);return nullptr;}
    if(MaxBodyFraction>0.f)
    {   // blender-rig: a size class never makes a prop longer than MaxBodyFraction of the body's bind height (a 2x knight
        // sword would be a 1.8 m greatsword), and never shrinks it below its size before the class.
        const USkeletalMesh& Ref=*Body->GetSkeletalMeshAsset();
        const float Head=static_cast<float>(ReferenceBone(Ref,TEXT("head")).GetLocation().Z),Foot=static_cast<float>(FMath::Min(ReferenceBone(Ref,TEXT("foot_l")).GetLocation().Z,ReferenceBone(Ref,TEXT("foot_r")).GetLocation().Z));
        const float Height=(Head-Foot)*1.1f*static_cast<float>(Body->GetComponentScale().Z);
        const float Length=static_cast<float>(Asset->GetBounds().BoxExtent.GetMax()*2);
        if(Height>50.f&&Length>1.f&&Length*Size>MaxBodyFraction*Height)Size=FMath::Max(MinSize,MaxBodyFraction*Height/Length);
    }
    auto* Part=NewObject<UStaticMeshComponent>(&Hero);Hero.AddInstanceComponent(Part);
    Part->SetupAttachment(Body,BoneName);Part->SetStaticMesh(Asset);Part->RegisterComponent();VisualOnly(*Part);
    const USkeletalMesh& Mesh=*Body->GetSkeletalMeshAsset();
    FVector Forward=((ReferenceBone(Mesh,TEXT("ball_l")).GetLocation()-ReferenceBone(Mesh,TEXT("foot_l")).GetLocation())+
        (ReferenceBone(Mesh,TEXT("ball_r")).GetLocation()-ReferenceBone(Mesh,TEXT("foot_r")).GetLocation())).GetSafeNormal2D();
    if(Forward.IsNearlyZero())Forward=FVector::ForwardVector;
    const FQuat Upright=FRotationMatrix::MakeFromXZ(Forward,FVector::UpVector).ToQuat();
    const FTransform BoneReference=ReferenceBone(Mesh,BoneName);
    FQuat Frame=Upright;
    // creature-anim: melee weapons and shields use the bind-pose hand grip (handle across the palm, blade on the
    // thumb side, face on the back of the hand) so the Tripo slash clips swing the blade instead of its flat side.
    if(Motion==TEXT("melee")&&(BoneName==TEXT("hand_l")||BoneName==TEXT("hand_r")))
    {
        const TCHAR* Side=BoneName==TEXT("hand_l")?TEXT("_l"):TEXT("_r");
        const FVector Arm=(ReferenceBone(Mesh,FName(FString(TEXT("middle_01"))+Side)).GetLocation()-BoneReference.GetLocation()).GetSafeNormal();
        FVector Across=ReferenceBone(Mesh,FName(FString(TEXT("index_01"))+Side)).GetLocation()-ReferenceBone(Mesh,FName(FString(TEXT("pinky_01"))+Side)).GetLocation();
        Across=(Across-Arm*FVector::DotProduct(Across,Arm)).GetSafeNormal();
        FVector Back=FVector::UpVector-Arm*FVector::DotProduct(FVector::UpVector,Arm)-Across*FVector::DotProduct(FVector::UpVector,Across);
        Back=Back.GetSafeNormal();
        if(!Arm.IsNearlyZero()&&!Across.IsNearlyZero()&&!Back.IsNearlyZero())Frame=FRotationMatrix::MakeFromXZ(Back,Across).ToQuat();
    }
    Part->SetRelativeRotation(BoneReference.GetRotation().Inverse()*Frame*Rotation.Quaternion());
    FVector GripOffset=FVector::ZeroVector;
    if(BoneName==TEXT("hand_l")||BoneName==TEXT("hand_r"))
    {
        const FName Knuckle(BoneName==TEXT("hand_l")?TEXT("middle_01_l"):TEXT("middle_01_r"));
        if(Mesh.GetRefSkeleton().FindBoneIndex(Knuckle)!=INDEX_NONE)
            GripOffset=BoneReference.InverseTransformPosition((BoneReference.GetLocation()+ReferenceBone(Mesh,Knuckle).GetLocation())*.5);
    }
    const FVector BodyScale=Body->GetComponentScale();
    if(BodyScale.GetAbsMin()>SMALL_NUMBER)
        GripOffset+=BoneReference.InverseTransformVector(Upright.RotateVector(OffsetCm)/BodyScale);
    Part->SetRelativeLocation(GripOffset);Part->SetWorldScale3D(FVector(Size));
    // creature-anim: with grip data the handle sits inside the curled fist (CireGrip); shields strap onto the forearm.
    FGripInfo Info;Info.Part=Part;Info.Bone=BoneName;Info.Mode=TEXT("offset"); // weapon-grips
    Info.LengthCm=static_cast<float>(Asset->GetBounds().BoxExtent.GetMax()*2*Size);Info.Size=Size;Info.BaseSize=MinSize>0.f?MinSize:Size;
    if(const auto* Grip=CireGrip::FindWeapon(Asset))
    {
        // blender-rig: an upscaled prop is placed (and the fingers curl) at its thinner cross-section scale, then stretched along
        // its handle axis about the handle point, so the grip stays closed at the handle while the weapon grows.
        const int32 AxisIndex=CireWeapons::PrincipalAxis(Grip->Axis);
        if(Girth<.999f&&(AxisIndex==INDEX_NONE||Grip->bShield||!Grip->OffHand.IsNearlyZero()))Girth=1.f;
        const float Thick=Size*Girth;Info.Girth=Girth;
        CireGrip::FWeapon Pointed=*Grip;Pointed.Axis=CireWeapons::BusinessAxis(*Asset,*Grip); // blender-rig: business end on the thumb side
        // blender-rig: an upscaled one-hander of 1.2 m or more no longer hangs from a straight arm at rest (its head went
        // through the floor): it is carried upright at the side, tip forward, and the attack clips take it from there.
        if(!Pointed.bCarry&&!Pointed.bShield&&!Pointed.bAmmo&&Pointed.OffHand.IsNearlyZero()&&Girth<.999f&&Info.LengthCm>=120.f)
        {Pointed.bCarry=true;Pointed.CarryAt=FVector(.24f,-.08f,.72f);Pointed.CarryUp=FVector(.45f,.2f,1.f);}
        CireGrip::FPlacement Placement=CireGrip::Place(Mesh,BoneName,Pointed,Thick,static_cast<float>(Body->GetRelativeScale3D().X));
        // blender-rig: a carried stock-gripped prop (crossbow, blunderbuss, launcher) must rest muzzle forward; Update()
        // checks the real idle carry pose once and spins it about its handle if the muzzle points back.
        Info.bMuzzleCheck=Placement.bValid&&Placement.bCarry&&!Pointed.OffHand.IsNearlyZero()&&!Pointed.OffAxis.IsNearlyZero();
        Info.MuzzleAxis=Pointed.OffAxis.GetSafeNormal();Info.HandleAxis=Pointed.Axis.GetSafeNormal();Info.Handle=Pointed.Handle;
        if(Placement.bValid)
        {
            Part->SetAbsolute(false,false,false);
            Part->AttachToComponent(Body,FAttachmentTransformRules::KeepRelativeTransform,Placement.Bone);
            Part->SetRelativeTransform(Placement.Relative);
            Info.Mode=TEXT("bind");Info.Bone=Placement.Bone;
        }
        // weapon-grips: a body playing Fab clips holds the prop the way the clip was authored (CireWeaponSockets).
        if(!PlaceAuthored(Hero,*Part,*Grip,BoneName,Thick,bPrimary,Info)&&Placement.bValid)CireGrip::AddToHands(Mesh,Placement,GripHands);
        if(Girth<.999f&&Info.Mode!=TEXT("offset"))Part->SetRelativeTransform(CireWeapons::HandleStretch(*Grip,1.f/Girth)*Part->GetRelativeTransform());
        else if(Girth<.999f)Part->SetWorldScale3D(FVector(Size));
    }
    GripInfo.Add(Info);
    Part->SetVisibility(Body->IsVisible());Parts.Add(Part);return Part;
}
namespace
{
float AngleDeg(const FVector& A,const FVector& B){return static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(),B.GetSafeNormal()),-1.,1.))));}
}
bool UCireWeaponPresentation::PlaceAuthored(ACireHero& Hero,UStaticMeshComponent& Part,const CireGrip::FWeapon& Grip,FName BoneName,float Size,bool bPrimary,FGripInfo& Info)
{
    USkeletalMeshComponent* Body=Hero.GetMesh();
    if(GripSet.IsEmpty()||Grip.bAmmo||!Part.GetStaticMesh()||!Body||!Body->GetSkeletalMeshAsset()||(BoneName!=TEXT("hand_l")&&BoneName!=TEXT("hand_r")))return false;
    const USkeletalMesh& Mesh=*Body->GetSkeletalMeshAsset();
    FTransform Intended;CireWeaponSockets::FHandFrame Frame;
    if(!CireWeaponSockets::Intended(Mesh,GripSet,BoneName,Intended,Frame)||Frame.bShield!=Grip.bShield)return false;
    const FReferenceSkeleton& Ref=Mesh.GetRefSkeleton();
    const FTransform PropGrip=CireWeaponSockets::PropGrip(*Part.GetStaticMesh(),Grip,Frame); // stock holds grip the crossbow fore-end
    Info.Set=GripSet;
    {   // How far the bind-pose placement is from the authored one (rigid on the hand, so the same in every frame).
        const FTransform Current=Part.GetRelativeTransform()*CireGrip::ReferenceComponent(Ref,Part.GetAttachSocketName());
        const FQuat Now=Current.GetRotation()*PropGrip.GetRotation();
        Info.TipDeviationDeg=AngleDeg(Now.GetAxisZ(),Intended.GetRotation().GetAxisZ());
        Info.EdgeDeviationDeg=AngleDeg(Now.GetAxisX(),Intended.GetRotation().GetAxisX());
    }
    if(CireWeaponSockets::Legacy())return false;
    const float MeshScale=static_cast<float>(Body->GetRelativeScale3D().X);
    if(MeshScale<=UE_SMALL_NUMBER)return false;
    const float S=Size/MeshScale; // mesh units per prop centimetre
    const bool bRight=BoneName==TEXT("hand_r");
    const FString Side=bRight?TEXT("_r"):TEXT("_l");
    const FName M1(*(TEXT("middle_01")+Side)),M2(*(TEXT("middle_02")+Side));
    const bool bFingers=Ref.FindBoneIndex(M1)!=INDEX_NONE&&Ref.FindBoneIndex(M2)!=INDEX_NONE;
    const float Finger=bFingers?.3f*static_cast<float>((CireGrip::ReferenceComponent(Ref,M2).GetLocation()-CireGrip::ReferenceComponent(Ref,M1).GetLocation()).Size()):.4f;
    CireGrip::FPlacement P;P.Bone=Frame.Bone;
    FVector Point=Intended.GetLocation();
    const FVector Axis=Intended.GetRotation().GetAxisZ();
    // Fingers close on the handle: its authored direction runs through the palm centre of this hand (CireGrip curl).
    if(Grip.bShield)P.Pose=CireGrip::BuildHandPose(Mesh,bRight,CireGrip::EHand::Power,1.3f*S+Finger);
    else
    {
        P.Pose=CireGrip::BuildHandPoseAlong(Mesh,bRight,CireGrip::EHand::Power,Grip.RadiusCm*S+Finger,Axis);
        if(P.Pose.bValid)Point=P.Pose.GripComponent.GetLocation();
    }
    const FQuat Rot=(Intended.GetRotation()*PropGrip.GetRotation().Inverse()).GetNormalized();
    P.Component=FTransform(Rot,Point-Rot.RotateVector(PropGrip.GetLocation()*S),FVector(S));
    P.Relative=P.Component.GetRelativeTransform(CireGrip::ReferenceComponent(Ref,P.Bone));
    P.bValid=!P.Relative.ContainsNaN();
    if(!P.bValid)return false;
    // Two-handed sets: the second hand goes where the clip puts it, slid onto this prop's handle line.
    FTransform OffInMain;
    if(bPrimary&&!Grip.bShield&&P.Pose.bValid&&CireWeaponSockets::IntendedOffHand(Mesh,GripSet,BoneName,OffInMain))
    {
        const FName OffBone=bRight?FName(TEXT("hand_l")):FName(TEXT("hand_r"));
        const FTransform MainRef=CireGrip::ReferenceComponent(Ref,BoneName),OffRef=CireGrip::ReferenceComponent(Ref,OffBone);
        FTransform OffComponent=OffInMain*MainRef;
        const FVector AxisInOff=OffComponent.GetRotation().UnrotateVector(Axis);
        P.OffPose=CireGrip::BuildHandPoseAlong(Mesh,!bRight,CireGrip::EHand::Power,Grip.RadiusCm*S+Finger,OffRef.GetRotation().RotateVector(AxisInOff));
        if(P.OffPose.bValid)
        {
            const FVector Palm=(P.OffPose.GripInHand*OffComponent).GetLocation();
            const FVector Away=(Palm-Point)-Axis*FVector::DotProduct(Palm-Point,Axis);
            OffComponent.AddToTranslation(-Away);
            P.OffHandInMain=OffComponent.GetRelativeTransform(MainRef);P.OffHandBone=OffBone;P.bTwoHand=!P.OffHandInMain.ContainsNaN();
        }
    }
    Part.SetAbsolute(false,false,false);
    Part.AttachToComponent(Body,FAttachmentTransformRules::KeepRelativeTransform,P.Bone);
    Part.SetRelativeTransform(P.Relative);
    CireGrip::AddToHands(Mesh,P,GripHands);
    Info.Mode=TEXT("authored");Info.Bone=P.Bone;Info.bTwoHand=P.bTwoHand;
    return true;
}
void UCireWeaponPresentation::Apply(ACireHero& Hero,int32 Archetype)
{
    const FString Profile=ProfileKey(Hero,Archetype);
    Clear();EnsureLoaded();EquippedProfile=Profile;EquippedMesh=Hero.GetMesh()->GetSkeletalMeshAsset();AppliedRevision=Revision;
    const auto* Options=Database.Options.Find(Profile);
    if(PreviewProfile!=Profile||!Options||!Options->Contains(PreviewLoadout))PreviewLoadout.Reset();
    const FString* Default=Database.Profiles.Find(Profile);if(!Default||NaturalAttacks(Profile))return;
    EquippedLoadout=PreviewLoadout.IsEmpty()?*Default:PreviewLoadout;
#if !UE_BUILD_SHIPPING
    // fab-integration (review captures): -CireAltLoadout=ranger,... starts those profiles on their second loadout option.
    if(FString Alt;PreviewLoadout.IsEmpty()&&Options&&Options->Num()>1&&FParse::Value(FCommandLine::Get(),TEXT("CireAltLoadout="),Alt,false))
    {TArray<FString> Ids;Alt.ParseIntoArray(Ids,TEXT(","),true);if(Ids.Contains(Profile))EquippedLoadout=(*Options)[1];}
#endif
    const FLoadout* Loadout=Database.Presets.Find(EquippedLoadout);if(!Loadout)return;Motion=Loadout->Motion;
    GripSet=CireWeaponSockets::SetFor(Hero.GetMesh()->GetSkeletalMeshAsset(),EquippedLoadout,CireChampionActions::MotionFor(Hero)); // weapon-grips
    for(const auto& Spec:Loadout->Parts)
    {
        // creature-anim: presets whose Tripo clips hold the weapon in the other hand swap their hand props.
        FName Bone=Spec.Bone;
        // weapon-grips: not when the body plays a Fab set that authors this hand (the Crossbow pack holds the stock in the
        // left hand, as the preset lists it); legacy grips keep the old swap.
        const bool bAuthoredHand=!CireWeaponSockets::Legacy()&&!GripSet.IsEmpty()&&CireWeaponSockets::Frame(GripSet,Spec.Bone).bValid;
        if(CireGrip::SwapsHands(EquippedLoadout)&&!bAuthoredHand)Bone=Bone==TEXT("hand_l")?FName(TEXT("hand_r")):Bone==TEXT("hand_r")?FName(TEXT("hand_l")):Bone;
        float FabSize=Spec.Size;TSharedPtr<FJsonObject> PropMaterials;bool bProfileProp=false;
        const FString Mesh=CireWeaponFab::ResolveMesh(Profile,Spec.Token,Spec.Asset,FabSize,PropMaterials,bProfileProp); // fab-integration, paladin-hq
        const float FabFactor=Spec.Size>0.f?FabSize/Spec.Size:1.f;
        auto* Part=Attach(Hero,Mesh,Bone,Spec.Offset,Spec.Rotation,FabSize,Spec.Role==TEXT("primary"),Spec.Girth,Spec.MaxBodyFraction,Spec.BaseSize*FabFactor);if(!Part)continue;
        if(!GripInfo.IsEmpty()&&GripInfo.Last().Part.Get()==Part)GripInfo.Last().SizeClass=Spec.SizeClass; // blender-rig
        if(PropMaterials.IsValid())UCireChampionArt::ApplyMaterialSpec(Part,PropMaterials,&Hero); // paladin-hq
        if(bProfileProp)Part->SetForcedLodModel(1); // paladin-hq: hero props stay on LOD 0 (the set's shield has broken reduction LODs)
        if(Spec.bHideOnRelease)Part->ComponentTags.Add(ReleaseTag);
        if(Spec.Role==TEXT("primary")){Primary=Part;PrimarySize=Spec.Size;}
        else if(Spec.Role==TEXT("ammunition"))Arrow=Part;
    }
    if(Primary&&Arrow)
    {
        Arrow->AttachToComponent(Primary,FAttachmentTransformRules::KeepWorldTransform);
        Arrow->SetRelativeLocation(FVector(0,0,Motion==TEXT("crossbow")?9:2));Arrow->SetRelativeRotation(FRotator::ZeroRotator);
    }
    // creature-anim: the string hand pinches the nock while drawing.
    if(Motion==TEXT("bow")&&Primary&&Hero.GetMesh()->GetSkeletalMeshAsset())
        DrawPose=CireGrip::BuildHandPose(*Hero.GetMesh()->GetSkeletalMeshAsset(),true,CireGrip::EHand::Pinch,1.f);
    if(Motion==TEXT("bow")&&Primary)for(int32 I=0;I<2;++I)
    {
        auto* String=NewObject<UStaticMeshComponent>(&Hero);Hero.AddInstanceComponent(String);String->SetupAttachment(Hero.GetMesh());
        String->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
        String->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_Metal.M_Metal")));
        String->RegisterComponent();VisualOnly(*String);Parts.Add(String);BowStrings.Add(String);
    }
}
void UCireWeaponPresentation::Update(ACireHero& Hero,float AttackElapsed)
{
    if(AppliedRevision!=Revision||EquippedProfile!=ProfileKey(Hero,Hero.Archetype)||EquippedMesh.Get()!=Hero.GetMesh()->GetSkeletalMeshAsset())Apply(Hero,Hero.Archetype);
    constexpr float ReleaseAt=.25f-KINDA_SMALL_NUMBER;
    const bool bHidden=Hero.bDead||Hero.IsHidden()||Hero.GetMesh()->bHiddenInGame;
    for(UStaticMeshComponent* Part:Parts)if(Part)
    {
        Part->SetVisibility(Hero.GetMesh()->IsVisible());
        Part->SetHiddenInGame(bHidden||(Part->ComponentHasTag(ReleaseTag)&&AttackElapsed>=ReleaseAt&&AttackElapsed<.53f));
    }
    if(Arrow)Arrow->SetHiddenInGame(bHidden||AttackElapsed>=ReleaseAt);
    for(FGripInfo& Info:GripInfo)
    {   // blender-rig: one-shot muzzle check on the settled idle carry (see Attach)
        UStaticMeshComponent* Part=Info.Part.Get();
        if(!Info.bMuzzleCheck||!Part||AttackElapsed>=0.f||Hero.bDead||(Hero.Mobility&&Hero.Mobility->IsRolling()))continue;
        if(++Info.MuzzleFrames<4)continue;
        Info.bMuzzleCheck=false;
        const FVector Muzzle=Part->GetComponentTransform().TransformVectorNoScale(Info.MuzzleAxis);
        if(FVector::DotProduct(Muzzle,Hero.GetActorForwardVector())>=0.)continue;
        const FQuat Spin(Info.HandleAxis,PI);
        Part->SetRelativeTransform(FTransform(Spin,Info.Handle-Spin.RotateVector(Info.Handle),FVector::OneVector)*Part->GetRelativeTransform());
        Info.bMuzzleSpun=true;
    }
    if(Motion!=TEXT("bow")||!Primary)return;
    const bool bDrawing=AttackElapsed>=0&&AttackElapsed<ReleaseAt;const FTransform Bow=Primary->GetComponentTransform();
    FVector Nock=bDrawing?Hero.GetMesh()->GetSocketLocation(TEXT("hand_r")):Bow.TransformPosition(FVector(-9,0,0));
    // creature-anim: the string rides the pinch between thumb and index while drawing.
    if(DrawPose.bValid&&(!GripHands.Pose[1].bValid||GripHands.Pose[1].Type==CireGrip::EHand::Pinch))
    {
        GripHands.Pose[1]=DrawPose;GripHands.Weight[1]=bDrawing?1.f:0.f;
        if(bDrawing)Nock=Hero.GetMesh()->GetSocketTransform(TEXT("hand_r"),RTS_World).TransformPosition(DrawPose.PinchInHand);
    }
    for(int32 I=0;I<BowStrings.Num();++I)
    {
        const FVector Tip=Bow.TransformPosition(FVector(-9,0,I==0?52.f:-52.f));const FVector Segment=Nock-Tip;
        BowStrings[I]->SetWorldLocation((Tip+Nock)*.5);BowStrings[I]->SetWorldRotation(FRotationMatrix::MakeFromZ(Segment).ToQuat());
        BowStrings[I]->SetWorldScale3D(FVector(.005*PrimarySize,.005*PrimarySize,Segment.Size()/100));
    }
}

bool CireWeapons::LegacyGrips(){return CireWeaponSockets::Legacy();}
namespace
{
/** Standing height from the skeleton (head bone above the lower foot, plus the skull), else the mesh bounds. */
float SkeletonHeight(const USkeletalMeshComponent& Body)
{
    const float Bounds=static_cast<float>(Body.CalcBounds(Body.GetComponentTransform()).BoxExtent.Z*2);
    if(Body.GetBoneIndex(TEXT("head"))!=INDEX_NONE&&Body.GetBoneIndex(TEXT("foot_l"))!=INDEX_NONE&&Body.GetBoneIndex(TEXT("foot_r"))!=INDEX_NONE)
    {
        const float Standing=static_cast<float>((Body.GetSocketLocation(TEXT("head")).Z-FMath::Min(Body.GetSocketLocation(TEXT("foot_l")).Z,Body.GetSocketLocation(TEXT("foot_r")).Z))*1.1);
        if(Standing>.5f*Bounds)return Standing; // weapon-grips: a roll folds the head below the feet line; keep the bounds then
    }
    return Bounds;
}
/** Closest approach (cm) of a long prop's centre line to the torso and to the legs in the current pose. */
void Clearance(const USkeletalMeshComponent& Body,const UStaticMeshComponent& Part,float& OutTorso,float& OutLegs)
{
    OutTorso=OutLegs=-1.f;
    const FBox Box=Part.GetStaticMesh()->GetBoundingBox();const FVector Extent=Box.GetExtent(),Centre=Box.GetCenter();
    const int32 Long=Extent.X>=Extent.Y&&Extent.X>=Extent.Z?0:Extent.Y>=Extent.Z?1:2;
    FVector Half=FVector::ZeroVector;Half[Long]=Extent[Long]*.92; // the very ends are ornaments / butt caps
    const FVector A=Part.GetComponentTransform().TransformPosition(Centre-Half),B=Part.GetComponentTransform().TransformPosition(Centre+Half);
    auto Dist=[&](FName From,FName To)->float
    {
        if(Body.GetBoneIndex(From)==INDEX_NONE||Body.GetBoneIndex(To)==INDEX_NONE)return -1.f;
        FVector P,Q;FMath::SegmentDistToSegmentSafe(A,B,Body.GetSocketLocation(From),Body.GetSocketLocation(To),P,Q);return static_cast<float>(FVector::Dist(P,Q));
    };
    auto Min=[](float X,float Y){return X<0?Y:Y<0?X:FMath::Min(X,Y);};
    OutTorso=Dist(TEXT("pelvis"),TEXT("neck_01"));
    for(const TCHAR* S:{TEXT("_l"),TEXT("_r")})
    {
        OutLegs=Min(OutLegs,Dist(FName(FString(TEXT("thigh"))+S),FName(FString(TEXT("calf"))+S)));
        OutLegs=Min(OutLegs,Dist(FName(FString(TEXT("calf"))+S),FName(FString(TEXT("foot"))+S)));
    }
}
}
int32 CireWeapons::PrincipalAxis(const FVector& Axis)
{
    const FVector A=Axis.GetSafeNormal().GetAbs();
    for(int32 I=0;I<3;++I)if(A[I]>.99)return I;
    return INDEX_NONE;
}
FTransform CireWeapons::HandleStretch(const CireGrip::FWeapon& Grip,float Factor)
{
    const int32 I=PrincipalAxis(Grip.Axis);
    if(I==INDEX_NONE||!FMath::IsFinite(Factor)||Factor<=0.f)return FTransform::Identity;
    FVector K=FVector::OneVector;K[I]=Factor;
    return FTransform(FQuat::Identity,Grip.Handle-Grip.Handle*K,K);
}
FVector CireWeapons::BusinessAxis(const UStaticMesh& Mesh,const CireGrip::FWeapon& Grip)
{
    const FVector Axis=Grip.Axis.GetSafeNormal();
    // Props with a second grip (crossbow, blunderbuss, launchers): the axis is the stock's, and the muzzle is OffAxis.
    if(Grip.bShield||Grip.bAmmo||Axis.IsNearlyZero()||!Grip.OffHand.IsNearlyZero())return Axis;
    const FBox Box=Mesh.GetBoundingBox();
    double Far=-1.e9,Near=1.e9;
    for(int32 I=0;I<8;++I)
    {
        const FVector Corner((I&1)?Box.Max.X:Box.Min.X,(I&2)?Box.Max.Y:Box.Min.Y,(I&4)?Box.Max.Z:Box.Min.Z);
        const double D=FVector::DotProduct(Corner-Grip.Handle,Axis);Far=FMath::Max(Far,D);Near=FMath::Min(Near,D);
    }
    return -Near>2.0*FMath::Max(Far,1.0)?-Axis:Axis;
}
FString CireWeapons::DescribeGrips(const ACireHero& Hero)
{
    const auto* Weapons=Hero.FindComponentByClass<UCireWeaponPresentation>();
    const USkeletalMeshComponent* Body=Hero.GetMesh();
    if(!Weapons||!Body)return TEXT("props=none");
    const float Height=FMath::Max(1.f,SkeletonHeight(*Body));
    FString Out=FString::Printf(TEXT("loadout=%s set=%s legacy=%d"),*Weapons->GetEquippedLoadout(),Weapons->GetGripSet().IsEmpty()?TEXT("-"):*Weapons->GetGripSet(),LegacyGrips()?1:0);
    if(const auto* Mesh=Body->GetSkeletalMeshAsset())
    {
        const auto& Cal=CireWeaponSockets::Calibration(*Mesh,Weapons->GetGripSet());
        Out+=Cal.bValid?FString::Printf(TEXT(" calib=%s spread=%.1f scale=%.3f"),*Cal.Clip,Cal.SpreadDeg,Cal.Scale):FString(TEXT(" calib=none"));
    }
    if(UAnimSingleNodeInstance* Single=const_cast<USkeletalMeshComponent*>(Body)->GetSingleNodeInstance())
    {   // weapon-grips: what the body plays (locomotion BlendSpace and its input) for the gallery metrics
        const FVector In=Single->GetFilterLastOutput();
        Out+=FString::Printf(TEXT(" anim=%s blendIn=%.0f,%.0f"),Single->GetAnimationAsset()?*Single->GetAnimationAsset()->GetName():TEXT("-"),In.X,In.Y);
        if(const auto* Combat=Cast<UCireCombatAnimInstance>(Single))
            Out+=FString::Printf(TEXT(" action=%s actionWeight=%.2f"),Combat->AttackSequence?*Combat->AttackSequence->GetName():TEXT("-"),Combat->AttackWeight);
    }
    for(const auto& Info:Weapons->GetGripInfo())
    {
        const UStaticMeshComponent* Part=Info.Part.Get();
        if(!Part||!Part->GetStaticMesh())continue;
        Out+=FString::Printf(TEXT(" | %s@%s mode=%s bindTipDev=%.0f bindEdgeDev=%.0f len=%.0fcm(%.2fxbody) twoHand=%d hidden=%d muzzleSpun=%d"),*Part->GetStaticMesh()->GetName(),*Part->GetAttachSocketName().ToString(),
            *Info.Mode,Info.TipDeviationDeg,Info.EdgeDeviationDeg,Info.LengthCm,Info.LengthCm/Height,Info.bTwoHand?1:0,Part->bHiddenInGame?1:0,Info.bMuzzleSpun?1:0);
        Out+=FString::Printf(TEXT(" size=%.2f base=%.2f girth=%.2f class=%s"),Info.Size,Info.BaseSize,Info.Girth,Info.SizeClass.IsEmpty()?TEXT("-"):*Info.SizeClass); // blender-rig
        if(const CireGrip::FWeapon* G=CireGrip::FindWeapon(Part->GetStaticMesh());G&&!G->bAmmo&&!G->bShield)
        {   // blender-rig: where the business end points in the champion's frame (forward, right, up)
            const FTransform Frame=CireWeaponSockets::PropFrame(*Part->GetStaticMesh(),G->Handle,G->Axis,G->Edge,false);
            const FVector Tip=Part->GetComponentTransform().TransformVectorNoScale(Frame.GetRotation().GetAxisZ()).GetSafeNormal();
            Out+=FString::Printf(TEXT(" tipF=%.2f tipR=%.2f tipU=%.2f"),FVector::DotProduct(Tip,Hero.GetActorForwardVector()),FVector::DotProduct(Tip,Hero.GetActorRightVector()),Tip.Z);
            if(!G->OffHand.IsNearlyZero()&&!G->OffAxis.IsNearlyZero())
            {   // muzzle / fore-grip direction (crossbow, blunderbuss, launchers)
                const FVector M=Part->GetComponentTransform().TransformVectorNoScale(G->OffAxis).GetSafeNormal();
                Out+=FString::Printf(TEXT(" offF=%.2f offU=%.2f"),FVector::DotProduct(M,Hero.GetActorForwardVector()),M.Z);
            }
        }
        {   // blender-rig: the handle point stays in the closed fist after scaling (palm grip point vs. prop handle, world cm)
            const FString Socket=Part->GetAttachSocketName().ToString();
            const int32 Side=Socket.EndsWith(TEXT("_r"))?1:Socket.EndsWith(TEXT("_l"))?0:INDEX_NONE;
            const CireGrip::FWeapon* Grip=CireGrip::FindWeapon(Part->GetStaticMesh());
            const auto& P=Side==INDEX_NONE?Weapons->GripHands.Pose[0]:Weapons->GripHands.Pose[Side];
            if(Side!=INDEX_NONE&&Grip&&!Grip->bShield&&!Grip->bAmmo&&P.bValid&&P.Hand!=INDEX_NONE&&Socket.StartsWith(TEXT("hand_")))
            {
                const FVector Palm=(P.GripInHand*Body->GetBoneTransform(P.Hand)).GetLocation();
                const FVector Handle=Part->GetComponentTransform().TransformPosition(Grip->Handle);
                Out+=FString::Printf(TEXT(" handleGap=%.1fcm"),FVector::Dist(Palm,Handle));
            }
        }
        if(Info.LengthCm>=100.f&&Part->GetAttachSocketName().ToString().StartsWith(TEXT("hand_")))
        {float Torso,Legs;Clearance(*Body,*Part,Torso,Legs);Out+=FString::Printf(TEXT(" clearTorso=%.0fcm clearLegs=%.0fcm"),Torso,Legs);}
    }
    const auto& Hands=Weapons->GripHands;
    if(Hands.bTwoHand&&Hands.Arm[Hands.MainSide][2]!=INDEX_NONE&&Hands.Arm[1-Hands.MainSide][2]!=INDEX_NONE)
    {
        const FTransform Main=Body->GetBoneTransform(Hands.Arm[Hands.MainSide][2]);
        const FVector Target=(Hands.OffHandInMain*Main).GetLocation(),Off=Body->GetBoneTransform(Hands.Arm[1-Hands.MainSide][2]).GetLocation();
        const int32 OffSide=1-Hands.MainSide;
        const FVector Shoulder=Body->GetBoneTransform(Hands.Arm[OffSide][0]).GetLocation(),Elbow=Body->GetBoneTransform(Hands.Arm[OffSide][1]).GetLocation();
        const float Arm=static_cast<float>(FVector::Dist(Shoulder,Elbow)+FVector::Dist(Elbow,Off));
        Out+=FString::Printf(TEXT(" | offHandFromTarget=%.1fcm targetFromShoulder=%.1fcm arm=%.1fcm"),FVector::Dist(Target,Off),FVector::Dist(Target,Shoulder),Arm);
    }
    return Out;
}

#if !UE_BUILD_SHIPPING
bool UCireWeaponPresentation::CyclePreview(ACireHero& Hero,FString& Message)
{
    EnsureLoaded();const FString Profile=ProfileKey(Hero,Hero.Archetype);const auto* Choices=Database.Options.Find(Profile);
    if(!Choices||Choices->Num()<2||NaturalAttacks(Profile)){Message=TEXT("This champion uses its authored equipment or natural attacks.");return false;}
    const int32 Current=Choices->IndexOfByKey(EquippedLoadout);PreviewProfile=Profile;
    PreviewLoadout=(*Choices)[(Current+1)%Choices->Num()];Apply(Hero,Hero.Archetype);
    FString Display=EquippedLoadout.Replace(TEXT("_"),TEXT(" "));
    if(!Display.IsEmpty())Display[0]=FChar::ToUpper(Display[0]);
    Message=TEXT("Visual equipment: ")+Display+TEXT(" (combat rules unchanged)");return true;
}
void UCireWeaponPresentation::ResetPreview(ACireHero& Hero){PreviewLoadout.Reset();PreviewProfile.Reset();Apply(Hero,Hero.Archetype);}
bool CireWeapons::CyclePreview(ACireHero& Hero,FString& Message)
{
    auto* Weapons=Hero.FindComponentByClass<UCireWeaponPresentation>();
    if(!Weapons){Message=TEXT("Champion equipment is not available on this body.");return false;}return Weapons->CyclePreview(Hero,Message);
}
bool CireWeapons::ResetPreview(ACireHero& Hero,FString& Message)
{
    auto* Weapons=Hero.FindComponentByClass<UCireWeaponPresentation>();if(!Weapons)return false;
    Weapons->ResetPreview(Hero);Message=TEXT("Restored profile equipment.");return true;
}
static FAutoConsoleCommand ReloadWeapons(TEXT("cire.Weapons.Reload"),TEXT("Reload local visual WeaponLoadouts.json; no combat rules change."),
    FConsoleCommandDelegate::CreateLambda([]{FString Error;const bool Good=CireWeapons::Reload(Error);UE_LOG(LogTemp,Display,TEXT("CIRE_WEAPONS_RELOAD_%s %s"),Good?TEXT("PASS"):TEXT("FAIL"),*Error);}));
#endif
