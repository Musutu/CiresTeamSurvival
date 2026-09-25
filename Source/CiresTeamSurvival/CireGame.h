#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "Rules/CiresRules.h"
#include "CireHUD.h"
#include "CireCombatEvents.h"
#include "CireChat.h"
#include "CireSpellPresentation.h"
#include "CireNPCArchetypes.h" // npc-boss: role/classification enums for ACireMonster read API
#include "CireGame.generated.h"

class USpringArmComponent;
class UCameraComponent;
class ACireMonster;
class UCireChampionArt;
class UCireMobility;
struct FCireChampionProfile;

UCLASS()
class CIRESTEAMSURVIVAL_API ACireGameState : public AGameStateBase {
    GENERATED_BODY()
public:
    ACireGameState();
    UPROPERTY(Replicated) int32 Phase = 0;
    UPROPERTY(Replicated) float SecondsLeft = 300;
    UPROPERTY(Replicated) int32 Round = 1;
    UPROPERTY(Replicated) int32 Wave = 0;
    UPROPERTY(Replicated) int32 CycleWavesDone = 0;
    UPROPERTY(Replicated) int32 WavesPerCycle = 3;
    UPROPERTY(Replicated) float NextWaveSeconds = 0;
    UPROPERTY(Replicated) int32 EmberLives = 100;
    UPROPERTY(Replicated) int32 DuskLives = 100;
    UPROPERTY(Replicated) int32 EmberWins = 0;
    UPROPERTY(Replicated) int32 DuskWins = 0;
    UPROPERTY(Replicated) int32 ArenaIndex = 0;
    UPROPERTY(Replicated) FString Announcement;
    // progression-shop: skill progression mode, 0 = Classic Draft (level-up offers), 1 = Skill Shop (default).
    UPROPERTY(Replicated) uint8 ProgressionMode = 1;
    // wave-director: current / next wave for the match plate (CireWaves.h).
    UPROPERTY(Replicated) FString WaveLabel;
    UPROPERTY(Replicated) FString NextWaveLabel;
    // wave-director: breather Ready (human players ready / human players; 0/0 = no humans).
    UPROPERTY(Replicated) int32 BreatherReady = 0;
    UPROPERTY(Replicated) int32 BreatherPlayers = 0;
    // monster-races: this match's monster skill seed (CireRaces.h) and the race of the current wave.
    UPROPERTY(Replicated) int32 MonsterSkillSeed = 0;
    UPROPERTY(Replicated) FName WaveRace;
    UPROPERTY(ReplicatedUsing=OnRepLaneRoutes) FVector LaneBounds = FVector(-2350,13000,1120);
    UPROPERTY(ReplicatedUsing=OnRepLaneRoutes) TArray<FVector2D> LanePoints0;
    UPROPERTY(ReplicatedUsing=OnRepLaneRoutes) TArray<FVector2D> LanePoints1;
    UPROPERTY(ReplicatedUsing=OnRepLaneRoutes) uint32 LaneRouteVersion = 0;
    // nav-paths: lane width, goal zone and challenge bay overrides (packed by CireLanePath::PublishState).
    UPROPERTY(ReplicatedUsing=OnRepLaneRoutes) TArray<float> LaneLayout;
    UFUNCTION() void OnRepLaneRoutes();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};

UCLASS()
class CIRESTEAMSURVIVAL_API ACireHero : public ACharacter {
    GENERATED_BODY()
public:
    ACireHero();
    virtual bool CanJumpInternal_Implementation() const override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual float TakeDamage(float Amount, FDamageEvent const& Event, AController* InstigatorController, AActor* Causer) override;
    virtual void FellOutOfWorld(const UDamageType& DamageType) override;
    virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
    UPROPERTY(VisibleAnywhere) USpringArmComponent* Arm;
    UPROPERTY(VisibleAnywhere) UCameraComponent* Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCireChampionArt> ChampionArt;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCireMobility> Mobility;
    // progression-shop: replicated items, belt, elixirs and teleport-to-base (CireItems.h).
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UCireInventory> Inventory;
    UPROPERTY(Replicated) int32 TeamId = -1;
    UPROPERTY(Replicated) int32 Archetype = 0;
    // Server-authored gameplay snapshot; Archetype remains the fallback body.
    UPROPERTY(Replicated) FString ChampionProfileId;
    UPROPERTY(Replicated) int32 StatPrimaryOverride = INDEX_NONE;
    UPROPERTY(Replicated) float ProfileBasicAttackRange = 0;
    UPROPERTY(Replicated) float ProfileAttackSeconds = 0;
    UPROPERTY(Replicated) FString ProfileAttackStyle;
    UPROPERTY(Replicated) FString ProfileThreatRole;
    UPROPERTY(Replicated) TArray<FString> ProfileRoles;
    UPROPERTY(Replicated) bool bBot = false;
    UPROPERTY(Replicated) bool bDrafted = false;
    // champion-select: pick timer (server world seconds; 0 = untimed) and the champion this
    // player has selected but not locked yet (teammates see it in their team column).
    UPROPERTY(Replicated) float DraftDeadline = 0;
    UPROPERTY(Replicated) float DraftTimerTotal = 0;
    UPROPERTY(Replicated) FString DraftHoverId;
    UPROPERTY(Replicated) bool bDead = false;
    UPROPERTY(Replicated) FString HeroName = TEXT("Unbound");
    UPROPERTY(Replicated) float Health = 500;
    UPROPERTY(Replicated) float MaxHealth = 500;
    UPROPERTY(Replicated) float Mana = 300;
    UPROPERTY(Replicated) float MaxMana = 300;
    UPROPERTY(Replicated) float Energy = 100;
    UPROPERTY(Replicated) int32 Level = 1;
    UPROPERTY(Replicated) int32 Strength = 20;
    UPROPERTY(Replicated) int32 Agility = 10;
    UPROPERTY(Replicated) int32 Intelligence = 10;
    UPROPERTY(Replicated) int32 Gold = 120;
    UPROPERTY(Replicated) int32 Experience = 0;
    UPROPERTY(Replicated) int32 GearRank = 0;
    UPROPERTY(Replicated) float CDR = 0;
    UPROPERTY(Replicated) float DamageDone = 0;
    UPROPERTY(Replicated) float HealingDone = 0;
    UPROPERTY(Replicated) float CriticalChance = .05f;
    UPROPERTY(Replicated) float CriticalMultiplier = 1.5f;
    UPROPERTY(Replicated) int32 PoisonAreaCount = 0;
    UPROPERTY(Replicated) float PoisonEndsAt = 0;
    UPROPERTY(Replicated) uint32 AttackSerial = 0;
    UPROPERTY(Replicated) FVector_NetQuantize AttackAimLocation;
    UPROPERTY(Replicated) float AttackStartedServerTime = 0;
    UPROPERTY(Replicated) float AttackDuration = .65f;
    // champion-draft: timed casts (heals etc.); replicated for cast bars. See CireCrowdControl.
    UPROPERTY(Replicated) FName CastSkill;
    UPROPERTY(Replicated) float CastStartTime = 0;
    UPROPERTY(Replicated) float CastEndTime = 0;
    UPROPERTY(Replicated) TArray<FString> Skills;
    UPROPERTY(Replicated) TArray<FString> Offers;
    UPROPERTY(Replicated) TArray<float> Cooldowns;
    UPROPERTY(Replicated) AActor* Target = nullptr;
    UPROPERTY(Replicated) FString Notice;
    Cires::Progression Progression;
    Cires::AugmentOffer CurrentOffer;
    float BasicTimer = 0;
    TWeakObjectPtr<AActor> PendingAttackTarget;
    float AttackReleaseTimer = 0;
    float PendingAttackDamage = 0;
    FVector CastAimPoint = FVector::ZeroVector;
    bool bHasCastAim = false;
    float RespawnTimer = 0;
    UPROPERTY(Replicated) float ShieldUntil = 0;
    UPROPERTY(Replicated) float TauntUntil = 0;
    float GlobalCooldown = 0;
    UPROPERTY(Replicated) float SlowUntil = 0;
    float BotDecisionTimer = 0;
    UPROPERTY(Replicated) bool bAutoAttack = false;
    // pets: the owner's companion timers and chosen stance (the pet actor may be away; CirePets.h).
    UPROPERTY(Replicated) float PetResummonAt = 0;
    UPROPERTY(Replicated) float PetReviveReadyAt = 0;
    UPROPERTY(Replicated) uint8 PetStance = 1;
    // pets: a companion granted by a talent / skill to any champion (overrides the profile's own pet).
    UPROPERTY(Replicated) FName PetGrant;
    FVector HomePosition;
    void Draft(int32 Choice);
    bool DraftProfile(const FString& Id);
    const FCireChampionProfile* ChampionProfile() const;
    Cires::PrimaryStat PrimaryStat() const;
    int32 PrimaryAttribute() const;
    float BasicAttackRange() const;
    float BaseAttackSeconds() const;
    FString BasicAttackStyle() const;
    bool IsRangedBasicAttack() const;
    bool HasChampionRole(const FString& Role) const;
    float DamageThreatMultiplier() const;
    // Developer fixture only; normal drafting always starts with zero skills.
    bool LoadThematicBuild();
    void Recalculate(bool bFill);
    void GrantExperience(int32 Amount);
    void RefreshOffer();
    void Learn(int32 Choice);
    void Cast(int32 Slot);
    void CastAt(int32 Slot,FVector Aim);
    void BasicAttack();
    void Purchase(int32 Item);
    void ReviveAt(FVector Location);
    void BotThink(float DeltaSeconds);
    bool IsHostile(AActor* Other) const;
    bool InRange(AActor* Other, float Distance) const;
    bool HasSkill(const FString& Id) const;
    float AttackDamage() const;
    static FString SkillName(const FString& Id);
    static FString SkillDescription(const FString& Id);
    static bool IsPassive(const FString& Id);
    static bool IsUltimate(const FString& Id);
    int32 ActiveSkillSlot(int32 Ordinal) const;
    int32 UltimateSkillSlot() const;
    UFUNCTION(NetMulticast, Unreliable) void MulticastCombatFx(FVector From, FVector To, FLinearColor Color);
};

UCLASS()
class CIRESTEAMSURVIVAL_API ACireMonster : public ACharacter {
    GENERATED_BODY()
public:
    ACireMonster();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual float TakeDamage(float Amount, FDamageEvent const& Event, AController* InstigatorController, AActor* Causer) override;
    virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
    UPROPERTY(Replicated) int32 Lane = -1;
    UPROPERTY(Replicated) int32 Tier = 0;
    UPROPERTY(Replicated) int32 PackId = -1;
    UPROPERTY(Replicated) bool bBoss = false;
    UPROPERTY(Replicated) bool bArmoredEscort = false;
    // wave-director: neutral challenge pack (yellow nameplate) until a player attacks it (CireWaves.h).
    UPROPERTY(Replicated) bool bNeutral = false;
    UPROPERTY(Replicated) int32 LeakCostOverride = 0;
    uint32 LaneRouteRevision = 0;
    int32 LaneWaypointIndex = 0;
    float EscortCollisionRefreshAt = 0;
    UPROPERTY(Replicated) float Health = 120;
    UPROPERTY(Replicated) float MaxHealth = 120;
    UPROPERTY(Replicated) int32 PoisonAreaCount = 0;
    UPROPERTY(Replicated) float PoisonEndsAt = 0;
    UPROPERTY(Replicated) FString MonsterName = TEXT("Hollow infantry");
    UPROPERTY(Replicated) ACireHero* Victim = nullptr;
    UPROPERTY(Replicated) int32 CombatArchetype = 0; // basic, bruiser, caster, ranged
    UPROPERTY(Replicated) FString CastingAbility;
    UPROPERTY(Replicated) float CastEndsAt = 0;
    UPROPERTY(Replicated) float CastStartedAt = 0;
    TMap<TWeakObjectPtr<ACireHero>,float> Threat;
    TWeakObjectPtr<ACireHero> ForcedVictim;
    float ForcedVictimUntil = 0;
    float AbilityTimer = 4;
    FVector PendingAim;
    bool bPendingSkillshot = false;
    FVector SpawnPosition;
    float Damage = 12;
    float AttackTimer = 0;
    float LeashTimer = 0;
    UPROPERTY(Replicated) float SlowUntil = 0;
    float BaseMoveSpeed = 0;
    bool bEngaged = false;
    // ---- NPC roles / boss / threat read API (feat/npc-boss; see Docs/NPCs.md) ----
    // Replicated role, classification, cast, status and threat-table state lives
    // in NPCState; these accessors are safe on server and clients.
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UCireNPCState> NPCState;
    // creature-anim: animated Tripo body, swing timing and death presentation (CireMonsterArt.h).
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UCireMonsterArt> MonsterArt;
    ECireNPCRole GetNPCRole() const;
    ECireNPCClass GetNPCClassification() const;
    FString GetNPCDisplayName() const;
    // True only for lane bosses that cost 10 lives on reaching town (pack leaders are
    // boss-classified but never leak).
    bool IsLaneBoss() const { return bBoss; }
    // ---- end NPC read API ----
};

UCLASS()
class CIRESTEAMSURVIVAL_API ACireWorld : public AActor {
    GENERATED_BODY()
public:
    ACireWorld();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void RefreshRouteVisuals();
    uint32 RenderedRouteRevision = MAX_uint32;
    // nav-paths: the castle goal actors follow the (editable) goal zone on the server.
    void SyncGoalZones();
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> RouteRoad;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> RouteEdge;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> RouteArrows;
};

UCLASS()
class CIRESTEAMSURVIVAL_API ACireGameMode : public AGameModeBase {
    GENERATED_BODY()
public:
    ACireGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    Cires::MatchClock Clock;
    // champion-select: per-player pick timer; at 0 the selected (else a random free) champion
    // is locked. Game.ini [/Script/CiresTeamSurvival.CireGameMode] DraftPickSeconds, or
    // -CireDraftSeconds=N; 0 disables it. Developer probes/galleries run untimed.
    float DraftPickSeconds = 90.f;
    void TickDraftTimer();
    Cires::TeamRewards Rewards[2];
    UPROPERTY() TArray<ACireHero*> Heroes;
    UPROPERTY() TArray<ACireMonster*> Monsters;
    int32 NextPlayer = 0;
    float WaveTimer = 10;
    int32 CycleWavesSpawned = 0;
    float RecoverySeconds = 15.f;
    float WaveBreatherSeconds = 8.f;
    bool bAutomaticReplayAttempted=false;
    float ReplayStopAt=0;
    float BotFillTimer = 2;
    bool bBotsFilled = false;
    bool bSmoke = false;
    float SmokeElapsed = 0;
    int32 ArenaIndex = 0;
    bool bCyclesComplete = false; // wave-director: finite Waves.json cycle count reached
    TSet<int32> RewardedPacks;
    void SpawnBots();
    void SpawnWave();
    void SpawnPacks();
    void ChangePhase(int32 NewPhase);
    void ResolveArena();
    void Leak(ACireMonster* Monster);
    void MonsterKilled(ACireMonster* Monster, ACireHero* Killer);
    void HeroKilled(ACireHero* Hero);
    void EndSurvival(int32 Winner);
    void AwardTeam(int32 Team, int32 XP, int32 Gold);
    bool IsCombatPhase() const;
    bool CanFight(const ACireHero* A, const ACireHero* B) const;
    FVector BasePosition(int32 Team) const;
    FVector ArenaPosition(int32 Team, int32 Slot) const;
    float Power(int32 Team) const;
    float Loot(int32 Team) const;
};

UCLASS()
class CIRESTEAMSURVIVAL_API ACireController : public APlayerController {
    GENERATED_BODY()
public:
    ACireController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void PlayerTick(float DeltaSeconds) override;
    virtual void PawnLeavingGame() override;
    UFUNCTION(Server,Reliable) void ServerAction(int32 Action, int32 Value, AActor* Selected);
    UFUNCTION(Server,Reliable) void ServerDraftProfile(const FString& ProfileId);
    UFUNCTION(Server,Reliable) void ServerDraftHover(const FString& ProfileId); // champion-select: selected, not locked
    UFUNCTION(Server,Reliable) void ServerCastAt(int32 Slot,FVector_NetQuantize Aim);
    UFUNCTION(Server,Reliable) void ServerSummonCommand(int32 Command,AActor* Target,FVector_NetQuantize Destination);
    UFUNCTION(Server,Reliable) void ServerPetCommand(uint8 Command); // pets: ECirePetCommand on the caller's companion
    UFUNCTION(Server,Reliable) void ServerSendChat(const FString& Message, bool bTeamOnly);
    UFUNCTION(Client,Reliable) void ClientChatMessage(const FCireChatMessage& Message);
    UFUNCTION(Client,Unreliable) void ClientCombatEvent(const FCireCombatEvent& Event);
    UFUNCTION(Client,Unreliable) void ClientSpellEffect(FName SkillId,FVector_NetQuantize From,FVector_NetQuantize To,ECireSpellCue Cue,float EffectScale,bool bSound);
    UPROPERTY() TArray<FCireChatMessage> ChatMessages;
    UPROPERTY() TArray<FCireCombatEvent> CombatEvents;
    uint32 CombatEventSequence = 0;
    UPROPERTY() TObjectPtr<AActor> FocusTarget = nullptr;
    void SetFocusTarget(AActor* Actor);
    void BeginChat();
    void SubmitChat();
    void CancelChat();
    void AppendChatCharacter(TCHAR Character);
    // champion-select: the draft screen's search box owns typed characters while focused.
    bool bDraftSearch = false;
    FString DraftSearch;
    void AppendDraftSearchCharacter(TCHAR Character);
    bool bChatInput = false;
    bool bChatTeamOnly = true;
    FString ChatDraft;
    double LastChatTime = -10;
    void CycleTarget(bool bFriendly);
    void RequestCast(int32 Slot);
    FVector CursorAim() const;
    bool bShop = false;
    bool bSummonMoveTargeting = false;
    bool bHelp = false;
};
