#include "CireVendors.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireLanePath.h"
#include "CireShopUI.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireVendors, Log, All);

namespace
{
FCireVendorData GData;
bool bLoaded = false;

FString DataPath(const TCHAR* Name) { return FPaths::ProjectContentDir() / TEXT("Data") / Name; }

bool ReadVector(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, FVector& Out)
{
    if (!O) return false;
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (O->TryGetArrayField(Field, A) && A && A->Num() >= 2)
    {
        Out.X = (*A)[0]->AsNumber(); Out.Y = (*A)[1]->AsNumber(); Out.Z = A->Num() > 2 ? (*A)[2]->AsNumber() : 0.0;
        return true;
    }
    const TSharedPtr<FJsonObject>* V = nullptr;
    if (O->TryGetObjectField(Field, V) && V && V->IsValid())
    {
        double X = 0, Y = 0, Z = 0;
        (*V)->TryGetNumberField(TEXT("x"), X); (*V)->TryGetNumberField(TEXT("y"), Y); (*V)->TryGetNumberField(TEXT("z"), Z);
        Out = FVector(X, Y, Z);
        return true;
    }
    return false;
}

float Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float Default)
{
    double V = Default;
    return O && O->TryGetNumberField(Field, V) ? static_cast<float>(V) : Default;
}

FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
{
    FString V;
    if (O) O->TryGetStringField(Field, V);
    return V;
}

// Anchor (pos + yaw) of a TownVendors.json entry part ("npc", "sign", "stall").
bool ReadAnchor(const TSharedPtr<FJsonObject>& Entry, const TCHAR* Part, FVector& Pos, float& Yaw, FVector* Size = nullptr)
{
    const TSharedPtr<FJsonObject>* P = nullptr;
    if (!Entry->TryGetObjectField(Part, P) || !P || !P->IsValid()) return false;
    if (!ReadVector(*P, TEXT("pos"), Pos)) return false;
    Yaw = Num(*P, TEXT("yaw"), 0.f);
    if (Size)
    {
        double Scalar = 0;
        if (!ReadVector(*P, TEXT("size"), *Size) && (*P)->TryGetNumberField(TEXT("size"), Scalar)) *Size = FVector(Scalar, Scalar, 0);
    }
    return true;
}


void AssignItems(FCireVendorData& D)
{
    D.ItemVendor.Reset();
    const auto& Items = CireItems::Get();
    for (const FName Id : Items.Order)
    {
        const auto* Item = CireItems::Find(Id);
        if (!Item || !Item->Purchasable) continue;
        FName Vendor;
        if (const FName* Override = D.Overrides.Find(Id)) Vendor = *Override;
        else
        {
            bool bShared = false;
            for (const FString& Tag : D.SharedTags) if (Item->HasTag(TCHAR_TO_UTF8(*Tag))) bShared = true;
            if (!bShared)
            {
                for (const auto& Rule : D.Rules)
                {
                    for (const FString& Tag : Rule.Key) if (Item->HasTag(TCHAR_TO_UTF8(*Tag))) { Vendor = Rule.Value; break; }
                    if (!Vendor.IsNone()) break;
                }
                if (Vendor.IsNone()) Vendor = D.Fallback;
            }
        }
        D.ItemVendor.Add(Id, Vendor);
    }
}

FVector2D MirrorY(const FCireVendorSpot& Spot, int32 Team, float& Yaw)
{
    FVector2D L(Spot.Local.X, Spot.Local.Y);
    Yaw = Spot.Yaw;
    const float Outer = Team == 0 ? -1.f : 1.f;
    if (Spot.Mode == 1) { L.Y = FMath::Abs(L.Y) * Outer; if (Outer < 0) Yaw = -Yaw; }
    else if (Spot.Mode == 2) { L.Y = -FMath::Abs(L.Y) * Outer; if (Outer > 0) Yaw = -Yaw; }
    return L;
}

double NowSeconds(const UWorld* World) { return World ? World->GetTimeSeconds() : FPlatformTime::Seconds(); }
} // namespace

// ------------------------------------------------------------------ data

bool CireVendors::ParseVendors(const FString& Json, FCireVendorData& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("Vendors.json is not valid JSON"); return false; }
    Out.InteractRange = Num(Root, TEXT("interactRange"), 450.f);
    Out.NameplateRange = Num(Root, TEXT("nameplateRange"), 2600.f);
    Out.Fallback = FName(Str(Root, TEXT("fallback")));
    Root->TryGetStringArrayField(TEXT("shared"), Out.SharedTags);
    const TArray<TSharedPtr<FJsonValue>>* Rules = nullptr;
    if (Root->TryGetArrayField(TEXT("rules"), Rules))
        for (const auto& V : *Rules)
        {
            const auto O = V->AsObject();
            TArray<FString> Tags; O->TryGetStringArrayField(TEXT("tags"), Tags);
            Out.Rules.Add({Tags, FName(Str(O, TEXT("vendor")))});
        }
    const TSharedPtr<FJsonObject>* Overrides = nullptr;
    if (Root->TryGetObjectField(TEXT("overrides"), Overrides))
        for (const auto& Pair : (*Overrides)->Values) Out.Overrides.Add(FName(Pair.Key), FName(Pair.Value->AsString()));
    const TArray<TSharedPtr<FJsonValue>>* Vendors = nullptr;
    if (!Root->TryGetArrayField(TEXT("vendors"), Vendors) || Vendors->Num() == 0) { Error = TEXT("Vendors.json has no vendors"); return false; }
    for (const auto& V : *Vendors)
    {
        const auto O = V->AsObject();
        FCireVendorDef& D = Out.Vendors.AddDefaulted_GetRef();
        D.Id = FName(Str(O, TEXT("id")));
        D.Name = Str(O, TEXT("name")); D.Keeper = Str(O, TEXT("keeper")); D.Stat = Str(O, TEXT("stat"));
        D.StatLabel = Str(O, TEXT("statLabel")); D.Tagline = Str(O, TEXT("tagline")); D.Greeting = Str(O, TEXT("greeting"));
        D.Sign = Str(O, TEXT("sign")); D.Emblem = Str(O, TEXT("emblem")); D.Mesh = Str(O, TEXT("mesh"));
        D.Height = Num(O, TEXT("height"), 185.f); D.MeshYaw = Num(O, TEXT("meshYaw"), -90.f);
        FVector Accent(1, 1, 1);
        if (ReadVector(O, TEXT("accent"), Accent)) D.Accent = FLinearColor(Accent.X, Accent.Y, Accent.Z, 1.f);
        const TSharedPtr<FJsonObject>* Anims = nullptr;
        if (O->TryGetObjectField(TEXT("anims"), Anims))
            for (const auto& Pair : (*Anims)->Values) D.Anims.Add(FName(Pair.Key), Pair.Value->AsString());
        // Layout defaults (relative to the merchant) for placements that omit a sign or stall.
        const TSharedPtr<FJsonObject>* Layout = nullptr;
        if (O->TryGetObjectField(TEXT("layout"), Layout))
        {
            const TSharedPtr<FJsonObject>* Sign = nullptr;
            if ((*Layout)->TryGetObjectField(TEXT("sign"), Sign))
            {
                ReadVector(*Sign, TEXT("offset"), D.SignOffset);
                D.SignYaw = Num(*Sign, TEXT("yaw"), 0.f);
                D.SignWidth = Num(*Sign, TEXT("width"), D.SignWidth);
            }
            const TSharedPtr<FJsonObject>* Stall = nullptr;
            if ((*Layout)->TryGetObjectField(TEXT("stall"), Stall))
            {
                ReadVector(*Stall, TEXT("offset"), D.StallOffset);
                D.StallYaw = Num(*Stall, TEXT("yaw"), 0.f);
                ReadVector(*Stall, TEXT("size"), D.StallSize);
                D.CounterTop = Num(*Stall, TEXT("counterTop"), D.CounterTop);
            }
        }
        const TArray<TSharedPtr<FJsonValue>>* Stall = nullptr;
        if (O->TryGetArrayField(TEXT("stall"), Stall))
            for (const auto& P : *Stall)
            {
                const auto S = P->AsObject();
                FCireStallProp& Prop = D.Stall.AddDefaulted_GetRef();
                Prop.Mesh = Str(S, TEXT("mesh"));
                Prop.Offset = FVector(Num(S, TEXT("x"), 0), Num(S, TEXT("y"), 0), Num(S, TEXT("z"), 0));
                Prop.Rotation = FRotator(Num(S, TEXT("pitch"), 0), Num(S, TEXT("yaw"), 0), Num(S, TEXT("roll"), 0));
                Prop.Fit = Num(S, TEXT("fit"), 0);
                S->TryGetBoolField(TEXT("onCounter"), Prop.bOnCounter);
                S->TryGetBoolField(TEXT("counter"), Prop.bCounter);
                Prop.Fallback = Str(S, TEXT("fallback"));
            }
        if (D.Id.IsNone() || D.Name.IsEmpty()) { Error = TEXT("a vendor is missing its id or name"); return false; }
    }
    for (const auto& Rule : Out.Rules)
        if (!Out.Vendors.ContainsByPredicate([&](const FCireVendorDef& D) { return D.Id == Rule.Value; })) { Error = FString::Printf(TEXT("rule names unknown vendor %s"), *Rule.Value.ToString()); return false; }
    if (!Out.Vendors.ContainsByPredicate([&](const FCireVendorDef& D) { return D.Id == Out.Fallback; })) { Error = TEXT("fallback vendor is unknown"); return false; }
    return true;
}

bool CireVendors::ParseSpots(const FString& Json, TArray<FCireVendorSpot>& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("TownVendors is not valid JSON"); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Root->TryGetArrayField(TEXT("vendors"), Rows)) { Error = TEXT("TownVendors has no vendors array"); return false; }
    for (const auto& V : *Rows)
    {
        const auto O = V->AsObject();
        if (!O) continue;
        FCireVendorSpot S;
        FString Id = Str(O, TEXT("vendorId"));
        if (Id.IsEmpty()) Id = Str(O, TEXT("id"));
        S.Id = FName(Id);
        double Team = -1;
        if (O->TryGetNumberField(TEXT("team"), Team)) S.Team = static_cast<int32>(Team);
        const FString Mode = Str(O, TEXT("mode"));
        S.Mode = Mode == TEXT("outer") ? 1 : Mode == TEXT("inner") ? 2 : 0;
        if (!ReadAnchor(O, TEXT("npc"), S.Local, S.Yaw))
        {
            // Flat rows ({"id","x","y","z","yaw"}) are accepted too.
            if (!O->HasField(TEXT("x"))) { Error = FString::Printf(TEXT("vendor %s has no npc position"), *Id); return false; }
            S.Local = FVector(Num(O, TEXT("x"), 0), Num(O, TEXT("y"), 0), Num(O, TEXT("z"), 0));
            S.Yaw = Num(O, TEXT("yaw"), 0);
        }
        S.bSign = ReadAnchor(O, TEXT("sign"), S.SignLocal, S.SignYaw);
        S.bStall = ReadAnchor(O, TEXT("stall"), S.StallLocal, S.StallYaw, &S.StallSize);
        if (S.Id.IsNone()) { Error = TEXT("a TownVendors row has no vendorId"); return false; }
        Out.Add(S);
    }
    return true;
}

bool CireVendors::Reload()
{
    FCireVendorData D;
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *DataPath(TEXT("Vendors.json"))) || !ParseVendors(Json, D, D.Error))
    {
        if (D.Error.IsEmpty()) D.Error = TEXT("Vendors.json is missing");
        UE_LOG(LogCireVendors, Error, TEXT("Vendors: %s"), *D.Error);
        GData = D; bLoaded = true;
        return false;
    }
    // TownVendors.json (the town layout / Eric's layout editor) wins; the provisional spots are the fallback.
    for (const TCHAR* File : {TEXT("TownVendors.json"), TEXT("TownVendors.provisional.json")})
    {
        FString Spots;
        if (!FFileHelper::LoadFileToString(Spots, *DataPath(File))) continue;
        FString SpotError;
        TArray<FCireVendorSpot> Parsed;
        if (!ParseSpots(Spots, Parsed, SpotError)) { UE_LOG(LogCireVendors, Error, TEXT("Vendors: %s: %s"), File, *SpotError); continue; }
        D.Spots = MoveTemp(Parsed);
        D.SpotSource = File;
        D.bProvisional = FCString::Strstr(File, TEXT("provisional")) != nullptr;
        break;
    }
    AssignItems(D);
    D.bValid = true;
    GData = MoveTemp(D);
    bLoaded = true;
    UE_LOG(LogCireVendors, Display, TEXT("Vendors: %d merchants, %d placements from %s, %d items assigned"), GData.Vendors.Num(), GData.Spots.Num(), *GData.SpotSource, GData.ItemVendor.Num());
    return true;
}

const FCireVendorData& CireVendors::Get()
{
    if (!bLoaded) Reload();
    return GData;
}

const FCireVendorDef* CireVendors::Find(FName VendorId)
{
    return Get().Vendors.FindByPredicate([&](const FCireVendorDef& D) { return D.Id == VendorId; });
}

int32 CireVendors::IndexOf(FName VendorId)
{
    return Get().Vendors.IndexOfByPredicate([&](const FCireVendorDef& D) { return D.Id == VendorId; });
}

FName CireVendors::VendorOf(FName ItemId)
{
    const FName* V = Get().ItemVendor.Find(ItemId);
    return V ? *V : NAME_None;
}

bool CireVendors::Sells(FName VendorId, FName ItemId)
{
    const FName* V = Get().ItemVendor.Find(ItemId);
    return V && (V->IsNone() || *V == VendorId);
}

FTransform CireVendors::SpotTransform(const FCireVendorSpot& Spot, int32 Team)
{
    float Yaw = 0;
    const FVector2D L = MirrorY(Spot, Team, Yaw);
    return FTransform(FRotator(0, Yaw, 0), FVector(L.X, L.Y + CireLanePath::CenterY(Team), Spot.Local.Z));
}

namespace
{
// Realm-local anchor -> world, honouring the spot's mirror mode.
FTransform AnchorTransform(const FCireVendorSpot& Spot, int32 Team, const FVector& Local, float LocalYaw)
{
    FCireVendorSpot Copy = Spot;
    Copy.Local = Local; Copy.Yaw = LocalYaw;
    return CireVendors::SpotTransform(Copy, Team);
}

UTexture2D* LoadTexture(const FString& Path)
{
    static TMap<FString, TWeakObjectPtr<UTexture2D>> Cache;
    if (Path.IsEmpty()) return nullptr;
    if (const auto* Hit = Cache.Find(Path); Hit && Hit->IsValid()) return Hit->Get();
    UTexture2D* T = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    Cache.Add(Path, T);
    return T;
}
}

UTexture2D* CireVendors::Emblem(FName VendorId) { const auto* D = Find(VendorId); return D ? LoadTexture(D->Emblem) : nullptr; }
UTexture2D* CireVendors::Sign(FName VendorId) { const auto* D = Find(VendorId); return D ? LoadTexture(D->Sign) : nullptr; }

// ------------------------------------------------------------------ spawning / interaction

void CireVendors::SpawnAll(AActor* WorldActor)
{
    UWorld* World = WorldActor ? WorldActor->GetWorld() : nullptr;
    if (!World) return;
    // Every peer builds the same merchants (collision must match for client movement prediction); a dedicated
    // server skips only the visuals (body, sign).
    Reload();
    const auto& D = Get();
    if (!D.bValid) return;
    int32 Count = 0;
    for (int32 Team = 0; Team < 2; ++Team)
        for (const FCireVendorSpot& Spot : D.Spots)
        {
            if (Spot.Team >= 0 && Spot.Team != Team) continue;
            const FCireVendorDef* Def = Find(Spot.Id);
            if (!Def) { UE_LOG(LogCireVendors, Warning, TEXT("Vendors: placement for unknown vendor %s"), *Spot.Id.ToString()); continue; }
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Params.ObjectFlags |= RF_Transient;
            ACireVendor* Vendor = World->SpawnActor<ACireVendor>(ACireVendor::StaticClass(), SpotTransform(Spot, Team), Params);
            if (!Vendor) continue;
            Vendor->Setup(*Def, Team);
            // Sign and stall: authored anchors, else the vendor's layout defaults relative to the merchant.
            const FTransform Npc = Vendor->GetActorTransform();
            const FTransform Sign = Spot.bSign ? AnchorTransform(Spot, Team, Spot.SignLocal, Spot.SignYaw)
                : FTransform(FRotator(0, Def->SignYaw, 0), Def->SignOffset) * Npc;
            const FTransform Stall = Spot.bStall ? AnchorTransform(Spot, Team, Spot.StallLocal, Spot.StallYaw)
                : FTransform(FRotator(0, Def->StallYaw, 0), Def->StallOffset) * Npc;
            FVector Size = Spot.bStall && Spot.StallSize.X > 0 ? Spot.StallSize : Def->StallSize;
            if (Size.Y <= 0) Size.Y = Def->StallSize.Y;
            if (Size.Z <= 0) Size.Z = Def->StallSize.Z;
            Vendor->BuildStall(*Def, Stall, Size);
            Vendor->BuildSign(*Def, Sign);
            ++Count;
        }
    UE_LOG(LogCireVendors, Display, TEXT("CIRE_VENDORS_SPAWNED count=%d source=%s provisional=%d"), Count, *D.SpotSource, D.bProvisional ? 1 : 0);
}

ACireVendor* CireVendors::NearestInRange(const ACireHero* Hero, float Slack)
{
    if (!Hero || !Hero->GetWorld()) return nullptr;
    ACireVendor* Best = nullptr;
    float BestDist = Get().InteractRange + Slack;
    for (TActorIterator<ACireVendor> It(Hero->GetWorld()); It; ++It)
    {
        if (It->Team != Hero->TeamId) continue;
        const float Dist = FVector::Dist2D(It->InteractPoint(), Hero->GetActorLocation());
        if (Dist <= BestDist) { BestDist = Dist; Best = *It; }
    }
    return Best;
}

bool CireVendors::Interact(ACireController* Controller, ACireVendor* Vendor)
{
    const ACireHero* Hero = Controller ? Cast<ACireHero>(Controller->GetPawn()) : nullptr;
    if (!Hero || !Hero->bDrafted) return false;
    if (!Vendor) Vendor = NearestInRange(Hero);
    if (!Vendor || Vendor->Team != Hero->TeamId) return false;
    CireShopUI::OpenVendor(Controller, Vendor->VendorId);
    Vendor->Gesture(TEXT("greet"));
    UE_LOG(LogCireVendors, Display, TEXT("CIRE_VENDOR_INTERACT vendor=%s team=%d"), *Vendor->VendorId.ToString(), Vendor->Team);
    return true;
}

void CireVendors::OnPurchased(const UWorld* World, FName ItemId)
{
    if (!World) return;
    const APlayerController* PC = World->GetFirstPlayerController();
    const ACireHero* Hero = PC ? Cast<ACireHero>(PC->GetPawn()) : nullptr;
    if (!Hero) return;
    const FName Seller = VendorOf(ItemId);
    ACireVendor* Best = nullptr;
    float BestDist = 3000.f;
    for (TActorIterator<ACireVendor> It(World); It; ++It)
    {
        if (It->Team != Hero->TeamId || (!Seller.IsNone() && It->VendorId != Seller)) continue;
        const float Dist = FVector::Dist2D(It->GetActorLocation(), Hero->GetActorLocation());
        if (Dist < BestDist) { BestDist = Dist; Best = *It; }
    }
    if (Best) Best->Gesture(TEXT("agree"));
}

// ------------------------------------------------------------------ ACireVendor

ACireVendor::ACireVendor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.f;
    bReplicates = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("VendorRoot"));
    Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("VendorCapsule"));
    Capsule->SetupAttachment(RootComponent);
    Capsule->InitCapsuleSize(42.f, 92.f);
    Capsule->SetRelativeLocation(FVector(0, 0, 92.f));
    Capsule->SetCollisionProfileName(TEXT("BlockAll"));
    Capsule->SetCollisionResponseToAllChannels(ECR_Block);
    Capsule->SetCanEverAffectNavigation(false);
    Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VendorBody"));
    Body->SetupAttachment(RootComponent);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    SignBoard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VendorSign"));
    SignBoard->SetupAttachment(RootComponent);
    SignBoard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SignBoard->SetCastShadow(false);
}

void ACireVendor::Setup(const FCireVendorDef& Def, int32 InTeam)
{
    VendorId = Def.Id;
    Team = InTeam;
    BaseYaw = CurrentYaw = GetActorRotation().Yaw;
    Capsule->SetCapsuleSize(42.f, Def.Height * .5f);
    Capsule->SetRelativeLocation(FVector(0, 0, Def.Height * .5f));
    if (GetNetMode() == NM_DedicatedServer) { Body->SetVisibility(false); SetActorTickEnabled(false); return; }
    USkeletalMesh* Mesh = Def.Mesh.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *Def.Mesh, nullptr, LOAD_NoWarn | LOAD_Quiet);
    for (const auto& Pair : Def.Anims)
        if (UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Pair.Value, nullptr, LOAD_NoWarn | LOAD_Quiet)) Clips.Add(Pair.Key, Clip);
    if (!Mesh)
    {
        UE_LOG(LogCireVendors, Warning, TEXT("Vendors: %s body %s is missing; the stall and sign stand alone"), *Def.Id.ToString(), *Def.Mesh);
        Body->SetVisibility(false);
        return;
    }
    bHasBody = true;
    Body->SetSkeletalMesh(Mesh);
    // Scale to the authored height, feet on the floor, facing the actor's +X (Tripo UE5-preset rigs face +Y).
    const FBoxSphereBounds B = Mesh->GetImportedBounds();
    const float MeshHeight = FMath::Max(1.f, static_cast<float>(B.BoxExtent.Z * 2.0));
    const float Scale = Def.Height / MeshHeight;
    Body->SetRelativeScale3D(FVector(Scale));
    Body->SetRelativeRotation(FRotator(0, Def.MeshYaw, 0));
    Body->SetRelativeLocation(FVector(0, 0, -(B.Origin.Z - B.BoxExtent.Z) * Scale));
    Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    PlayLoop();
    NextLook = NowSeconds(GetWorld()) + FMath::FRandRange(8.f, 16.f);
}

FVector ACireVendor::InteractPoint() const
{
    // Customers stand at the counter: measure from the stall front when there is one.
    return StallParts.Num() > 0 ? StallFront : GetActorLocation();
}

void ACireVendor::BuildStall(const FCireVendorDef& Def, const FTransform& Anchor, const FVector& Size)
{
    const FVector Base = Def.StallSize;
    const FVector Axis(Base.X > 0 ? Size.X / Base.X : 1.f, Base.Y > 0 ? Size.Y / Base.Y : 1.f, Base.Z > 0 ? Size.Z / Base.Z : 1.f);
    const float Uniform = FMath::Min(Axis.X, Axis.Y);
    CounterTop = Def.CounterTop * Axis.Z;
    StallFront = Anchor.TransformPosition(FVector(Size.X * .5f + 60.f, 0, 0));
    for (const FCireStallProp& Prop : Def.Stall)
    {
        UStaticMesh* Mesh = Prop.Mesh.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *Prop.Mesh, nullptr, LOAD_NoWarn | LOAD_Quiet);
        const bool bCounter = !Mesh && Prop.Fallback == TEXT("counter");
        if (!Mesh && bCounter) Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (!Mesh) continue;
        auto* Part = NewObject<UStaticMeshComponent>(this);
        Part->SetStaticMesh(Mesh);
        Part->SetupAttachment(RootComponent);
        Part->SetMobility(EComponentMobility::Movable);
        Part->SetCollisionEnabled(Prop.bOnCounter ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
        Part->SetCollisionResponseToAllChannels(ECR_Block);
        Part->SetCanEverAffectNavigation(false);
        const FBox Box = Mesh->GetBoundingBox();
        FVector Scale3(1.f);
        if (bCounter)
        {
            Scale3 = FVector(70.f, 260.f, CounterTop) / 100.f * FVector(Axis.X, Axis.Y, 1.f);
            Part->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
        }
        else if (Prop.Fit > 0) Scale3 = FVector(Prop.Fit * Uniform / FMath::Max(1.0, Box.GetSize().GetMax()));
        else Scale3 = FVector(Uniform);
        // Rest the mesh's lowest point on the floor (or the counter top) at the prop's offset.
        const FRotator Rot = Prop.Rotation;
        const FTransform Local(Rot, FVector::ZeroVector, Scale3);
        const FBox Rotated = Box.TransformBy(Local);
        const float Floor = Prop.bOnCounter ? CounterTop : 0.f;
        const FVector Offset(Prop.Offset.X * Axis.X, Prop.Offset.Y * Axis.Y, Floor + Prop.Offset.Z - Rotated.Min.Z);
        const FVector Centre(Rotated.GetCenter().X, Rotated.GetCenter().Y, 0);
        const FTransform World = FTransform(Rot, Offset - Centre, Scale3) * Anchor;
        Part->SetWorldTransform(World);
        if (Prop.bCounter && !bCounter) CounterTop = Floor + Prop.Offset.Z + static_cast<float>(Rotated.GetSize().Z); // the table top
        Part->RegisterComponent();
        AddInstanceComponent(Part);
        StallParts.Add(Part);
    }
}

void ACireVendor::BuildSign(const FCireVendorDef& Def, const FTransform& Anchor)
{
    if (GetNetMode() == NM_DedicatedServer) { SignBoard->SetVisibility(false); return; }
    UTexture2D* Texture = LoadTexture(Def.Sign);
    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Masked.Widget3DPassThrough_Masked"));
    if (!Texture || !Plane || !Base) { SignBoard->SetVisibility(false); return; }
    auto* MID = UMaterialInstanceDynamic::Create(Base, this);
    MID->SetTextureParameterValue(TEXT("SlateUI"), Texture);
    MID->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor::White);
    MID->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.f);
    SignBoard->SetStaticMesh(Plane);
    SignBoard->SetMaterial(0, MID);
    const float Aspect = Texture->GetSizeX() > 0 ? static_cast<float>(Texture->GetSizeY()) / Texture->GetSizeX() : .66f;
    // The engine plane lies in XY (100 cm, normal +Z, U along +X, V along +Y). Stand it up facing the anchor's +X
    // with U running along the anchor's -Y (left to right for a customer) and V downward.
    const FTransform Local(FRotationMatrix::MakeFromXY(FVector(0, -1, 0), FVector(0, 0, -1)).Rotator(), FVector::ZeroVector,
        FVector(Def.SignWidth / 100.f, Def.SignWidth * Aspect / 100.f, 1.f));
    SignBoard->SetWorldTransform(Local * Anchor);
}

FVector ACireVendor::PlateAnchor() const
{
    const FCireVendorDef* Def = CireVendors::Find(VendorId);
    return GetActorLocation() + FVector(0, 0, (Def ? Def->Height : 185.f) + 30.f);
}

void ACireVendor::PlayLoop()
{
    UAnimSequence* Idle = Clips.FindRef(TEXT("idle"));
    if (bHasBody && Idle) Body->PlayAnimation(Idle, true);
    GestureEnd = 0;
}

void ACireVendor::Gesture(FName Clip)
{
    UAnimSequence* Anim = Clips.FindRef(Clip);
    if (!bHasBody || !Anim) return;
    Body->PlayAnimation(Anim, false);
    GestureEnd = NowSeconds(GetWorld()) + Anim->GetPlayLength() / FMath::Max(.1f, Anim->RateScale);
}

void ACireVendor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bHasBody) return;
    const double Now = NowSeconds(GetWorld());
    if (GestureEnd > 0 && Now >= GestureEnd) PlayLoop();
    if (GestureEnd == 0 && Now >= NextLook)
    {
        NextLook = Now + FMath::FRandRange(12.f, 22.f);
        Gesture(TEXT("look"));
    }
    // Turn a little toward the local champion when he walks up (clamped so he stays behind his counter).
    float Target = BaseYaw;
    if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
        if (const APawn* Pawn = PC->GetPawn())
        {
            const FVector To = Pawn->GetActorLocation() - GetActorLocation();
            if (To.Size2D() < 900.f)
                Target = BaseYaw + FMath::Clamp(FMath::FindDeltaAngleDegrees(BaseYaw, static_cast<float>(To.Rotation().Yaw)), -35.f, 35.f);
        }
    const float Yaw = FMath::FixedTurn(CurrentYaw, Target, 90.f * DeltaSeconds);
    // Only the merchant turns; the stall and sign keep their world placement.
    const FCireVendorDef* Def = CireVendors::Find(VendorId);
    Body->SetWorldRotation(FRotator(0, Yaw + (Def ? Def->MeshYaw : -90.f), 0));
    Capsule->SetWorldRotation(FRotator(0, Yaw, 0));
    CurrentYaw = Yaw;
}

// ------------------------------------------------------------------ smoke (RunExpansionChecks native)
#if !UE_BUILD_SHIPPING
bool CireVendors::RunSmoke(ACireGameMode* Mode)
{
    int32 Count = 0; bool bPass = true;
    auto Check = [&](bool bOk, const TCHAR* Label) { ++Count; if (!bOk) { bPass = false; UE_LOG(LogCireVendors, Error, TEXT("CIRE_VENDORS_CHECK_FAIL %s"), Label); } };
    Reload();
    const auto& D = Get();
    Check(D.bValid && D.Vendors.Num() == 3, TEXT("Vendors.json loads three merchants"));
    Check(Find(TEXT("arcane")) && Find(TEXT("armory")) && Find(TEXT("weaponsmith")), TEXT("arcane, armory and weaponsmith exist"));
    // Every purchasable item has exactly one seller (or is sold by all), and each merchant has a real stock.
    int32 Purchasable = 0; TMap<FName, int32> Stock;
    for (const FName Id : CireItems::Get().Order)
        if (const auto* Item = CireItems::Find(Id); Item && Item->Purchasable) { ++Purchasable; if (const FName* V = D.ItemVendor.Find(Id)) Stock.FindOrAdd(*V)++; }
    Check(Purchasable > 0 && D.ItemVendor.Num() == Purchasable, TEXT("every purchasable item is assigned"));
    for (const FCireVendorDef& V : D.Vendors) Check(Stock.FindRef(V.Id) >= 8, *FString::Printf(TEXT("%s stocks at least 8 items of its own"), *V.Id.ToString()));
    Check(VendorOf(TEXT("nightfall_reaver")) == FName(TEXT("weaponsmith")) && VendorOf(TEXT("bone_dagger")) == FName(TEXT("weaponsmith")), TEXT("attack items -> weaponsmith (AGI / bruisers)"));
    Check(VendorOf(TEXT("gravewarden_bulwark")) == FName(TEXT("armory")) && VendorOf(TEXT("boiled_jerkin")) == FName(TEXT("armory")), TEXT("armour and block -> armory (STR / tanks)"));
    Check(VendorOf(TEXT("voidglass_orb")) == FName(TEXT("arcane")) && VendorOf(TEXT("ashwood_wand")) == FName(TEXT("arcane")), TEXT("spell and mana -> arcane (INT)"));
    Check(VendorOf(TEXT("vial_of_crimson")).IsNone() && Sells(TEXT("arcane"), TEXT("vial_of_crimson")) && Sells(TEXT("armory"), TEXT("vial_of_crimson")), TEXT("potions are sold by every merchant"));
    Check(!Sells(TEXT("arcane"), TEXT("nightfall_reaver")) && Sells(TEXT("weaponsmith"), TEXT("nightfall_reaver")), TEXT("a merchant's tab lists only his wares"));
    // Placements: both realms get all three; the provisional spots sit inside the town shopping radius.
    int32 PerTeam[2] = {0, 0};
    for (const FCireVendorSpot& S : D.Spots)
        for (int32 Team = 0; Team < 2; ++Team)
            if (S.Team < 0 || S.Team == Team)
            {
                ++PerTeam[Team];
                if (D.bProvisional)
                    Check(FVector::Dist2D(SpotTransform(S, Team).GetLocation(), Mode->BasePosition(Team)) <= CireItems::Get().Shop.TownRadius, TEXT("provisional merchant stands inside the town radius"));
            }
    Check(PerTeam[0] == 3 && PerTeam[1] == 3, TEXT("three merchants per realm"));
    // The layout editor's schema (vendorId, team, npc/sign/stall anchors, stall size) and flat rows both parse.
    {
        TArray<FCireVendorSpot> Parsed; FString Error;
        const bool bOk = ParseSpots(TEXT("{\"vendors\":[{\"vendorId\":\"arcane\",\"team\":1,\"npc\":{\"pos\":[10,20,0],\"yaw\":90},\"sign\":{\"pos\":[30,20,250],\"yaw\":90},\"stall\":{\"pos\":[40,20,0],\"yaw\":90,\"size\":[300,400,300]}},{\"id\":\"armory\",\"x\":5,\"y\":6,\"yaw\":7}]}"), Parsed, Error);
        Check(bOk && Parsed.Num() == 2 && Parsed[0].Team == 1 && Parsed[0].bSign && Parsed[0].bStall && Parsed[0].StallSize.Y == 400.f && Parsed[0].Yaw == 90.f, TEXT("layout-editor TownVendors rows parse"));
        Check(bOk && Parsed.Num() == 2 && Parsed[1].Team == -1 && !Parsed[1].bStall && Parsed[1].Local.X == 5.f, TEXT("flat TownVendors rows parse"));
        const FTransform T = SpotTransform(Parsed.Num() ? Parsed[0] : FCireVendorSpot(), 1);
        Check(FMath::IsNearlyEqual(T.GetLocation().Y, 20.f + CireLanePath::CenterY(1)), TEXT("realm-local y is relative to the team's realm centre"));
    }
    // The spawned merchants: six (three per realm), each with a stall and a sign.
    TArray<ACireVendor*> Vendors;
    for (TActorIterator<ACireVendor> It(Mode->GetWorld()); It; ++It) Vendors.Add(*It);
    Check(Vendors.Num() == 6, TEXT("six merchants spawned (three per realm)"));
    bool bStalls = Vendors.Num() > 0, bSigns = Vendors.Num() > 0, bBodies = Vendors.Num() > 0;
    for (ACireVendor* V : Vendors) { bStalls &= V->StallParts.Num() > 0; bSigns &= V->SignBoard->GetStaticMesh() != nullptr; bBodies &= V->bHasBody; }
    Check(bStalls, TEXT("every merchant has a stall"));
    Check(bSigns, TEXT("every merchant hangs his sign"));
    if (!bBodies) UE_LOG(LogCireVendors, Warning, TEXT("CIRE_VENDORS_NOTE some merchant bodies are missing (Tripo import not present)"));
    // Interact range: at the counter he is in reach, across the square he is not, never across realms.
    if (Vendors.Num() > 0)
    {
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ACireVendor* V = Vendors[0];
        if (ACireHero* Hero = Mode->GetWorld()->SpawnActor<ACireHero>(V->InteractPoint() + FVector(0, 0, 100), FRotator::ZeroRotator, Params))
        {
            Hero->SetActorTickEnabled(false);
            Hero->TeamId = V->Team;
            Check(NearestInRange(Hero) == V, TEXT("a champion at the counter can trade"));
            Hero->SetActorLocation(V->InteractPoint() + FVector(1600, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
            Check(NearestInRange(Hero) == nullptr, TEXT("a champion across the square cannot"));
            Hero->SetActorLocation(V->InteractPoint() + FVector(0, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
            Hero->TeamId = 1 - V->Team;
            Check(NearestInRange(Hero) != V, TEXT("the other realm's merchant is never in reach"));
            Hero->Destroy();
        }
    }
    UE_LOG(LogCireVendors, Display, TEXT("CIRE_VENDORS_%s checks=%d source=%s"), bPass ? TEXT("PASS") : TEXT("FAIL"), Count, *D.SpotSource);
    return bPass;
}
#endif
