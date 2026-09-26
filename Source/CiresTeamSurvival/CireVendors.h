#pragma once
// vendors: the three town merchants (Eric 2026-09-26). Content/Data/Vendors.json defines each merchant
// (name, stat, art, stall dressing) and the rules that give every purchasable item to exactly one of
// them; Content/Data/TownVendors.json (TownVendors.provisional.json until the town layout lands) places
// them in each team's realm. ACireVendor is a purely cosmetic, locally spawned actor: shopping stays
// server-enforced by the item shop (UCireInventory), and the B key keeps opening every merchant at once.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CireVendors.generated.h"

class ACireGameMode;
class ACireHero;
class ACireController;
class UCapsuleComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UAnimSequence;
class UTexture2D;
class UMaterialInstanceDynamic;

struct CIRESTEAMSURVIVAL_API FCireStallProp
{
    FString Mesh;
    FVector Offset = FVector::ZeroVector; // actor-local cm (+X = toward customers)
    FRotator Rotation = FRotator::ZeroRotator;
    float Fit = 0.f;                      // largest bounding extent scaled to this many cm (0 = native size)
    bool bOnCounter = false;              // rests on the counter top instead of the ground
    bool bCounter = false;                // this prop IS the counter: its top becomes the counter height
    FString Fallback;                     // "counter": a primitive counter when the mesh is missing
};

struct CIRESTEAMSURVIVAL_API FCireVendorDef
{
    FName Id;
    FString Name, Keeper, Stat, StatLabel, Tagline, Greeting;
    FLinearColor Accent = FLinearColor::White;
    FString Sign, Emblem, Mesh;
    TMap<FName, FString> Anims;           // idle, look, greet, agree
    float Height = 185.f;
    float MeshYaw = -90.f;                // Tripo UE5-preset rigs face +Y
    TArray<FCireStallProp> Stall;
    // "layout" defaults, relative to the merchant (+X = his facing): used when a placement omits the anchor.
    FVector SignOffset = FVector(150, 0, 262);
    float SignYaw = 0.f;
    float SignWidth = 190.f;               // cm
    FVector StallOffset = FVector(40, 0, 0);
    float StallYaw = 0.f;
    FVector StallSize = FVector(340, 460, 320); // depth, width, height (cm)
    float CounterTop = 95.f;               // cm at the default stall height
};

/** One TownVendors.json entry: {vendorId, team, npc {pos, yaw}, sign {pos, yaw}, stall {pos, yaw, size}} (realm-local cm). */
struct CIRESTEAMSURVIVAL_API FCireVendorSpot
{
    FName Id;
    int32 Team = -1;                      // -1 = both realms
    FVector Local = FVector::ZeroVector;  // npc: realm-local (y relative to the team realm centre)
    float Yaw = 0.f;
    int32 Mode = 0;                       // 0 as authored, 1 outer, 2 inner (TownLayout.json semantics)
    bool bSign = false;
    FVector SignLocal = FVector::ZeroVector;
    float SignYaw = 0.f;
    bool bStall = false;
    FVector StallLocal = FVector::ZeroVector;
    float StallYaw = 0.f;
    FVector StallSize = FVector::ZeroVector; // depth, width, height (0 = the vendor default)
};

struct CIRESTEAMSURVIVAL_API FCireVendorData
{
    TArray<FCireVendorDef> Vendors;
    TArray<FCireVendorSpot> Spots;
    FString SpotSource;                   // "TownVendors.json" or "TownVendors.provisional.json"
    bool bProvisional = false;
    float InteractRange = 450.f;
    float NameplateRange = 2600.f;
    TArray<FString> SharedTags;
    TArray<TPair<TArray<FString>, FName>> Rules;
    FName Fallback;
    TMap<FName, FName> Overrides;
    TMap<FName, FName> ItemVendor;        // purchasable item -> vendor (NAME_None = every merchant)
    FString Error;
    bool bValid = false;
};

namespace CireVendors
{
    CIRESTEAMSURVIVAL_API const FCireVendorData& Get();
    CIRESTEAMSURVIVAL_API bool Reload();
    CIRESTEAMSURVIVAL_API bool ParseVendors(const FString& Json, FCireVendorData& Out, FString& Error);
    CIRESTEAMSURVIVAL_API bool ParseSpots(const FString& Json, TArray<FCireVendorSpot>& Out, FString& Error);
    CIRESTEAMSURVIVAL_API const FCireVendorDef* Find(FName VendorId);
    CIRESTEAMSURVIVAL_API int32 IndexOf(FName VendorId);
    /** The merchant that sells an item; NAME_None for items every merchant sells (potions, tomes) or unknown ids. */
    CIRESTEAMSURVIVAL_API FName VendorOf(FName ItemId);
    /** Whether a merchant's tab lists this item (its own items plus the shared ones). */
    CIRESTEAMSURVIVAL_API bool Sells(FName VendorId, FName ItemId);
    /** World transform of a vendor spot in a team's realm. */
    CIRESTEAMSURVIVAL_API FTransform SpotTransform(const FCireVendorSpot& Spot, int32 Team);
    CIRESTEAMSURVIVAL_API UTexture2D* Emblem(FName VendorId);
    CIRESTEAMSURVIVAL_API UTexture2D* Sign(FName VendorId);

    /** Spawns every merchant in both realms on this peer (ACireWorld::BeginPlay; skipped on dedicated servers). */
    CIRESTEAMSURVIVAL_API void SpawnAll(AActor* WorldActor);
    /** The nearest merchant of the hero's realm within interact range, or null. */
    CIRESTEAMSURVIVAL_API class ACireVendor* NearestInRange(const ACireHero* Hero, float Slack = 0.f);
    /** Interact key / click: opens the shop on that merchant's tab (the greeting plays). Returns false when none is near. */
    CIRESTEAMSURVIVAL_API bool Interact(ACireController* Controller, class ACireVendor* Vendor = nullptr);
    /** The shop reports a purchase so the merchant who sold it nods (client cosmetic). */
    CIRESTEAMSURVIVAL_API void OnPurchased(const UWorld* World, FName ItemId);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}

/** A town merchant: Tripo body on the idle loop, stall dressing, hanging shop sign. Local-only (not replicated). */
UCLASS(NotPlaceable, Transient)
class CIRESTEAMSURVIVAL_API ACireVendor : public AActor
{
    GENERATED_BODY()
public:
    ACireVendor();
    void Setup(const FCireVendorDef& Def, int32 InTeam);
    virtual void Tick(float DeltaSeconds) override;
    /** Plays a one-shot gesture ("greet", "agree"), then returns to the idle loop. */
    void Gesture(FName Clip);
    /** Head-height anchor for nameplates. */
    FVector PlateAnchor() const;
    /** Where customers stand (the stall front), for the interact range. */
    FVector InteractPoint() const;
    /** Stall props at the stall anchor, scaled to Size (depth, width, height); sign board at its anchor. */
    void BuildStall(const FCireVendorDef& Def, const FTransform& Anchor, const FVector& Size);
    void BuildSign(const FCireVendorDef& Def, const FTransform& Anchor);

    UPROPERTY(VisibleAnywhere) TObjectPtr<UCapsuleComponent> Capsule;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> SignBoard;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> StallParts;
    UPROPERTY() TMap<FName, TObjectPtr<UAnimSequence>> Clips;
    FName VendorId;
    int32 Team = 0;
    bool bHasBody = false;       // the Tripo body loaded (else a placeholder)
    float CounterTop = 95.f;     // cm above the floor
    FVector StallFront = FVector::ZeroVector;
private:
    void PlayLoop();
    double GestureEnd = 0, NextLook = 0;
    float BaseYaw = 0.f, CurrentYaw = 0.f;
};
