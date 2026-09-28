#pragma once
// Arena portals (Docs/Arenas.md "Shadow portals").
//
// When the prep minute runs down (Arenas.json "portal.leadSeconds"), the server opens one shadow portal beside every human
// champion. The portal is a dark rift themed after the arena that was picked for this cycle: a painted view of the
// destination inside the ring, the arena's tint, its own motes (wheat, snow, sand, leaves, bubbles, stars), its name and a
// countdown. Walking into it takes that champion to their arena spawn at once (the arena shows for them while the others
// finish shopping); when the minute ends the arena phase pulls everyone else through as before, so nobody is left behind.
// A matching rift opens at each team's arena spawn on arrival and at the town return point in recovery.
//
// Server-authoritative: the server spawns the replicated portal actors and performs every teleport. Clients only build the
// visuals from Arenas.json (the arena index is replicated), so the look is data-driven per arena.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CireArenaPortal.generated.h"

class ACireGameMode;
class ACireGameState;
class ACireHero;
class UBoxComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class UFXSystemComponent;
class UAudioComponent;

namespace CireArenaPortal
{
    enum class EMotes : uint8 { Wheat, Snow, Sand, Leaves, Bubbles, Stars };
    enum class EKind : uint8 { Entry = 0, Arrival = 1, Return = 2 };

    /** Per-arena look ("portal" in each Arenas.json arena). */
    struct FLook
    {
        FString View;                                   // texture object path: the painted view of the arena inside the ring
        FLinearColor Tint = FLinearColor(.55f, .3f, 1.f);   // rim glow and light
        FLinearColor Accent = FLinearColor(1, 1, 1);        // name text
        FLinearColor MoteColor = FLinearColor(1, 1, 1);
        EMotes Motes = EMotes::Stars;
        int32 MoteCount = 56;
        float MoteSize = 1.f;
        float MoteGlow = 2.5f;                          // emissive strength of the motes
        FString Label;                                  // optional name override (defaults to the arena name)
    };

    /** Shared portal settings (top-level "portal" in Arenas.json). */
    struct FConfig
    {
        float LeadSeconds = 12.f;                       // the portals open this long before the prep minute ends
        float Radius = 150.f, Height = 175.f;           // ring radius and centre height above the ground (cm)
        float Offset = 330.f;                           // distance from the champion when it opens
        float ArrivalSeconds = 4.f, ReturnSeconds = 6.f;
        float CountdownSeconds = 7.f;                   // arena-flow: once everyone is loaded, the fight starts this many seconds later
        // arena-flow (Arenas.json "flow"): the PvP prep, rewards and the PvP schedule (Docs/Arenas.md "Arena flow").
        float PrepSeconds = 30.f;                       // prep before stragglers are pulled through (the countdown follows)
        int32 KillGold = 50;                            // per killing blow on a champion in the arena, win or lose
        int32 WinGold = 250;                            // split across the winning team
        float PvEBuffPercent = 15.f, PvEDebuffPercent = 15.f; // per stack: winners' / losers' damage to monsters
        bool bLegacyPowerLoot = false;                  // also grant the old +3% power / +8% loot per win (applies to PvP too)
        TArray<int32> PvPAfterWaves = {5, 10, 15, 20};  // fallback schedule until feat/waves-modes publishes one
        FString DiscMaterial = TEXT("/Game/Arenas/Portal/M_ArenaPortal.M_ArenaPortal");
        FString MoteMaterial = TEXT("/Game/Arenas/Portal/M_ArenaPortalMote.M_ArenaPortalMote");
        TArray<FString> RingVFX, BaseVFX, OpenVFX, EnterVFX; // optional Shadow_Magic Niagara layers (local Fab pack)
        float RingScale = 1.f, BaseScale = 1.f, OpenScale = 1.f, EnterScale = 1.f;
        bool bTintRing = false;                         // push the arena tint into the ring layer (else it stays shadow-dark)
        FName OpenSound = TEXT("arena_portal_open"), LoopSound = TEXT("arena_portal_loop"), EnterSound = TEXT("arena_portal_enter");
        TMap<FName, FLook> Looks;                       // arena id -> look
        TArray<FString> Errors;
    };
    CIRESTEAMSURVIVAL_API const FConfig& Config(bool bReload = false);
    /** The look for an arena index (a neutral shadow look when the arena has none). */
    CIRESTEAMSURVIVAL_API FLook LookFor(int32 ArenaIndex);
    CIRESTEAMSURVIVAL_API bool ParseMotes(const FString& Name, EMotes& Out);

    // ---- server ----
    /** Called every server tick: opens the entry portals when the prep minute reaches the lead time. */
    CIRESTEAMSURVIVAL_API void TickServer(ACireGameMode* Mode);
    /** Opens an entry portal beside every drafted human champion (idempotent per prep phase). Returns the portal count. */
    CIRESTEAMSURVIVAL_API int32 ServerOpen(ACireGameMode* Mode);
    /** A champion stepped into a portal during prep: it goes to its arena spawn now. False when not allowed. */
    CIRESTEAMSURVIVAL_API bool ServerEnter(ACireGameMode* Mode, ACireHero* Hero, class ACireArenaPortal* Through);
    /** Phase change hook: 2 collapses the entry portals (everyone is pulled through) and opens arrival rifts,
     *  4 opens the return rifts in town, 0/1 clear everything. */
    CIRESTEAMSURVIVAL_API void OnPhaseChanged(ACireGameMode* Mode, int32 NewPhase);
    /** True while a champion that stepped through early waits in the arena for the prep minute to end. */
    CIRESTEAMSURVIVAL_API bool IsStaged(const ACireHero* Hero);
    /** True once every human champion stepped through this prep (the prep minute was cut to the countdown). */
    CIRESTEAMSURVIVAL_API bool AllThrough(const UWorld* World);
    /** Arena spawn slot of a champion: the same ordering ACireGameMode::ChangePhase(2) uses. */
    CIRESTEAMSURVIVAL_API int32 SpawnSlot(const ACireGameMode* Mode, const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API void ServerClear(UWorld* World);
    CIRESTEAMSURVIVAL_API TArray<class ACireArenaPortal*> Portals(UWorld* World, int32 Kind = -1);

    // ---- client ----
    /** True when this peer's own champion stands inside the picked arena during prep (it stepped through early). */
    CIRESTEAMSURVIVAL_API bool LocalViewInArena(const UWorld* World);

    /** Pulls every drafted champion still in town through (prep timer end) and starts the countdown. Returns the count moved. */
    CIRESTEAMSURVIVAL_API int32 ServerPullAll(ACireGameMode* Mode);

#if !UE_BUILD_SHIPPING
    /** Native checks: data (every themed arena has a look, a view texture and a motes style), placement and the visuals. */
    CIRESTEAMSURVIVAL_API bool RunTests(UWorld* World);
    /** -CirePortalGallery: renders every arena's portal in town (Tools/RunArenaGallery.py --portals). */
    bool GalleryInitialize(ACireGameMode* Mode);
    bool GalleryTick(ACireGameMode* Mode);
#endif
}

// arena-flow: the PvP schedule adapter, the rewards and the team PvE buff (Docs/Arenas.md "Arena flow").
namespace CireArenaFlow
{
    inline const FName VictorId = TEXT("arena_victor");         // stacking team buff: +PvE damage per arena win
    inline const FName VanquishedId = TEXT("arena_vanquished"); // stacking team debuff: -PvE damage per arena loss
    /** The PvP schedule: waves after which an arena round runs. MERGE POINT with feat/waves-modes: switch the body of
     *  PvPAfterWaves (CireArenaPortal.cpp) to its schedule; everything else reads through these two calls. */
    CIRESTEAMSURVIVAL_API TArray<int32> PvPAfterWaves(const UWorld* World);
    CIRESTEAMSURVIVAL_API bool IsPvPAfterWave(const UWorld* World, int32 WavesCleared);
    /** A cycle ended on a wave that is not in the schedule: advance the clock to the next cycle with no arena. */
    CIRESTEAMSURVIVAL_API void SkipArena(ACireGameMode* Mode);
    /** Server: a champion died. A killing blow by an enemy champion (or its pet/summon/construct) in the arena pays gold. */
    CIRESTEAMSURVIVAL_API void OnHeroKilled(ACireGameMode* Mode, ACireHero* Victim, AActor* Causer);
    /** Server: the arena resolved (Winner 0/1, -1 draw). Gold split + stacking team buff/debuff. Returns the announcement. */
    CIRESTEAMSURVIVAL_API FString AwardResult(ACireGameMode* Mode, int32 Winner, bool bGrantExperience = true);
    CIRESTEAMSURVIVAL_API int32 BuffStacks(const UWorld* World, int32 Team);
    CIRESTEAMSURVIVAL_API int32 DebuffStacks(const UWorld* World, int32 Team);
    /** Multiplier on a team's damage to monsters (never to champions). */
    CIRESTEAMSURVIVAL_API float PvEDamageMultiplier(const UWorld* World, int32 Team);
    /** Seconds to show on prep timers: while the portals are open the countdown is not part of the prep. */
    CIRESTEAMSURVIVAL_API float PrepSecondsLeft(const ACireGameState* State);
    /** The champion (owner) behind a damage causer: the hero itself, or the owner/instigator of a pet, summon or construct. */
    CIRESTEAMSURVIVAL_API ACireHero* KillerHero(AActor* Causer);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
#endif
}

/** One shadow portal. Spawned and destroyed by the server; clients build the visuals from the replicated arena index. */
UCLASS(NotPlaceable)
class CIRESTEAMSURVIVAL_API ACireArenaPortal : public AActor
{
    GENERATED_BODY()
public:
    ACireArenaPortal();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(ReplicatedUsing = OnRep_Setup) int32 ArenaIndex = INDEX_NONE;
    UPROPERTY(ReplicatedUsing = OnRep_Setup) uint8 Kind = 0;          // CireArenaPortal::EKind
    UPROPERTY(Replicated) int32 TeamId = -1;
    UPROPERTY(Replicated) FString ForHeroName;                          // the champion it opened for (label only)
    UPROPERTY(ReplicatedUsing = OnRep_Collapse) bool bCollapsing = false;
    UPROPERTY(Replicated) int32 Entered = 0;                            // champions that stepped through early
    UPROPERTY(ReplicatedUsing = OnRep_AllThrough) bool bAllThrough = false; // every human is through: the arena countdown runs
    UFUNCTION() void OnRep_AllThrough();

    UFUNCTION() void OnRep_Setup();
    UFUNCTION() void OnRep_Collapse();
    /** Visual + sound when a champion is swallowed (all clients that see the portal). */
    UFUNCTION(NetMulticast, Unreliable) void MulticastSwallow(FVector_NetQuantize At);

    void Collapse(float Seconds);

    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Trigger;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Disc;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Motes;
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> Light;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DiscMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MoteMID;
    UPROPERTY(Transient) TArray<TObjectPtr<UFXSystemComponent>> FX;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Hum;

    // Client presentation state (tests read these).
    bool bVisualsBuilt = false;
    bool bHasView = false;          // the destination view texture resolved
    int32 FabLayers = 0;            // Shadow_Magic layers spawned
    FString LabelText;
    float Age = 0.f, CollapseAge = -1.f, CollapseSeconds = .6f;
    float Open = 0.f;               // 0..1 opening scale
    float Flash = 0.f;
private:
    void BuildVisuals();
    void UpdateMotes(float Time);
    UFUNCTION() void OnTriggerBegin(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
    TArray<FVector4f> MoteSeeds;
};
