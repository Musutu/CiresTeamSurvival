#include "CireWeaponPresentation.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "CireGame.h"
#include "CireChampionRoster.h"
#include "CireChampionArt.h" // paladin-hq: prop material specs
#include "CireChampionActions.h" // weapon-grips: motion class for the Fab set
#include "CireWeaponSockets.h" // weapon-grips: animation-authored grips
#include "Components/SkeletalMeshComponent.h"
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
    float Size=1;bool bHideOnRelease=false;
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
            if((*PartRow)->HasField(TEXT("scale"))&&(!(*PartRow)->TryGetNumberField(TEXT("scale"),Size)||!FMath::IsFinite(Size)||Size<.35||Size>2))return Bad(TEXT("Weapon scale must be .35..2"));
            Part.Size=Size;
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
    for(const auto& Profile:CireChampionRoster::All())if(!Candidate.Profiles.Contains(Profile.Id))
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
    const FVector& OffsetCm,const FRotator& Rotation,float Size,bool bPrimary)
{
    auto* Body=Hero.GetMesh();
    if(!Body||!Body->GetSkeletalMeshAsset()||Body->GetBoneIndex(BoneName)==INDEX_NONE)return nullptr;
    // A non-humanoid custom body must not inherit a humanoid profile's weapons.
    if(Body->GetBoneIndex(TEXT("hand_l"))==INDEX_NONE||Body->GetBoneIndex(TEXT("hand_r"))==INDEX_NONE)return nullptr;
    auto* Asset=LoadObject<UStaticMesh>(nullptr,*AssetPath);
    if(!Asset){UE_LOG(LogTemp,Warning,TEXT("CIRE_WEAPON_ASSET_MISSING %s"),*AssetPath);return nullptr;}
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
    Info.LengthCm=static_cast<float>(Asset->GetBounds().BoxExtent.GetMax()*2*Size);
    if(const auto* Grip=CireGrip::FindWeapon(Asset))
    {
        const CireGrip::FPlacement Placement=CireGrip::Place(Mesh,BoneName,*Grip,Size,static_cast<float>(Body->GetRelativeScale3D().X));
        if(Placement.bValid)
        {
            Part->SetAbsolute(false,false,false);
            Part->AttachToComponent(Body,FAttachmentTransformRules::KeepRelativeTransform,Placement.Bone);
            Part->SetRelativeTransform(Placement.Relative);
            Info.Mode=TEXT("bind");Info.Bone=Placement.Bone;
        }
        // weapon-grips: a body playing Fab clips holds the prop the way the clip was authored (CireWeaponSockets).
        if(!PlaceAuthored(Hero,*Part,*Grip,BoneName,Size,bPrimary,Info)&&Placement.bValid)CireGrip::AddToHands(Mesh,Placement,GripHands);
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
    const FTransform PropGrip=CireWeaponSockets::PropFrame(*Part.GetStaticMesh(),Grip.Handle,Grip.Axis,Grip.Edge,Grip.bShield);
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
        if(CireGrip::SwapsHands(EquippedLoadout))Bone=Bone==TEXT("hand_l")?FName(TEXT("hand_r")):Bone==TEXT("hand_r")?FName(TEXT("hand_l")):Bone;
        float FabSize=Spec.Size;TSharedPtr<FJsonObject> PropMaterials;bool bProfileProp=false;
        const FString Mesh=CireWeaponFab::ResolveMesh(Profile,Spec.Token,Spec.Asset,FabSize,PropMaterials,bProfileProp); // fab-integration, paladin-hq
        auto* Part=Attach(Hero,Mesh,Bone,Spec.Offset,Spec.Rotation,FabSize,Spec.Role==TEXT("primary"));if(!Part)continue;
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
    if(Body.GetBoneIndex(TEXT("head"))!=INDEX_NONE&&Body.GetBoneIndex(TEXT("foot_l"))!=INDEX_NONE&&Body.GetBoneIndex(TEXT("foot_r"))!=INDEX_NONE)
        return static_cast<float>((Body.GetSocketLocation(TEXT("head")).Z-FMath::Min(Body.GetSocketLocation(TEXT("foot_l")).Z,Body.GetSocketLocation(TEXT("foot_r")).Z))*1.1);
    return static_cast<float>(Body.CalcBounds(Body.GetComponentTransform()).BoxExtent.Z*2);
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
FString CireWeapons::DescribeGrips(const ACireHero& Hero)
{
    const auto* Weapons=Hero.FindComponentByClass<UCireWeaponPresentation>();
    const USkeletalMeshComponent* Body=Hero.GetMesh();
    if(!Weapons||!Body)return TEXT("props=none");
    const float Height=FMath::Max(1.f,SkeletonHeight(*Body));
    FString Out=FString::Printf(TEXT("loadout=%s set=%s legacy=%d"),*Weapons->GetEquippedLoadout(),Weapons->GetGripSet().IsEmpty()?TEXT("-"):*Weapons->GetGripSet(),LegacyGrips()?1:0);
    if(const auto* Mesh=Body->GetSkeletalMeshAsset())
    {
        const auto& Cal=CireWeaponSockets::Calibration(*Mesh);
        Out+=Cal.bValid?FString::Printf(TEXT(" calib=%s spread=%.1f scale=%.3f"),*Cal.Clip,Cal.SpreadDeg,Cal.Scale):FString(TEXT(" calib=none"));
    }
    for(const auto& Info:Weapons->GetGripInfo())
    {
        const UStaticMeshComponent* Part=Info.Part.Get();
        if(!Part||!Part->GetStaticMesh())continue;
        Out+=FString::Printf(TEXT(" | %s@%s mode=%s bindTipDev=%.0f bindEdgeDev=%.0f len=%.0fcm(%.2fxbody) twoHand=%d hidden=%d"),*Part->GetStaticMesh()->GetName(),*Part->GetAttachSocketName().ToString(),
            *Info.Mode,Info.TipDeviationDeg,Info.EdgeDeviationDeg,Info.LengthCm,Info.LengthCm/Height,Info.bTwoHand?1:0,Part->bHiddenInGame?1:0);
        if(Info.LengthCm>=100.f&&Part->GetAttachSocketName().ToString().StartsWith(TEXT("hand_")))
        {float Torso,Legs;Clearance(*Body,*Part,Torso,Legs);Out+=FString::Printf(TEXT(" clearTorso=%.0fcm clearLegs=%.0fcm"),Torso,Legs);}
    }
    const auto& Hands=Weapons->GripHands;
    if(Hands.bTwoHand&&Hands.Arm[Hands.MainSide][2]!=INDEX_NONE&&Hands.Arm[1-Hands.MainSide][2]!=INDEX_NONE)
    {
        const FTransform Main=Body->GetBoneTransform(Hands.Arm[Hands.MainSide][2]);
        const FVector Target=(Hands.OffHandInMain*Main).GetLocation(),Off=Body->GetBoneTransform(Hands.Arm[1-Hands.MainSide][2]).GetLocation();
        Out+=FString::Printf(TEXT(" | offHandFromTarget=%.1fcm"),FVector::Dist(Target,Off));
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
