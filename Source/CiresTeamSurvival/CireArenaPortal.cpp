#include "CireArenaPortal.h"
#include "CireArenas.h"
#include "CireAudio.h"
#include "CireBanners.h"
#include "CireFabVFX.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireArenaPortal, Log, All);

using namespace CireArenaPortal;

namespace
{
FConfig GPortalConfig;
bool GPortalLoaded = false;

struct FPortalServer
{
    bool bOpened = false;
    TSet<TWeakObjectPtr<ACireHero>> Staged;
};
TMap<TWeakObjectPtr<UWorld>, FPortalServer> GPortalServer;

float PNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float Default)
{
    double V = Default; return O.IsValid() && O->TryGetNumberField(Key, V) && FMath::IsFinite(V) ? static_cast<float>(V) : Default;
}
FString PStr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Default = FString())
{
    FString V; return O.IsValid() && O->TryGetStringField(Key, V) ? V : Default;
}
FLinearColor PColor(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FLinearColor Default, TArray<FString>* Errors = nullptr, const FString& Where = FString())
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (!O.IsValid() || !O->TryGetArrayField(Key, A)) return Default;
    if (A->Num() < 3) { if (Errors) Errors->Add(FString::Printf(TEXT("%s: %s needs 3 components"), *Where, Key)); return Default; }
    double C[4] = {0, 0, 0, 1};
    for (int32 I = 0; I < FMath::Min(4, A->Num()); ++I)
        if (!(*A)[I]->TryGetNumber(C[I]) || !FMath::IsFinite(C[I]) || C[I] < 0 || C[I] > 50)
        { if (Errors) Errors->Add(FString::Printf(TEXT("%s: %s out of range"), *Where, Key)); return Default; }
    return FLinearColor(C[0], C[1], C[2], C[3]);
}
TArray<FString> PList(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
{
    TArray<FString> Out; const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (O.IsValid() && O->TryGetArrayField(Key, A)) for (const auto& V : *A) { FString S; if (V->TryGetString(S) && !S.IsEmpty()) Out.Add(S); }
    else if (FString S; O.IsValid() && O->TryGetStringField(Key, S) && !S.IsEmpty()) Out.Add(S);
    return Out;
}

void LoadConfig(FConfig& C)
{
    C = FConfig();
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Arenas.json"));
    FString Text; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    { C.Errors.Add(TEXT("Arenas.json missing or malformed")); return; }
    const TSharedPtr<FJsonObject>* PO = nullptr;
    if (Root->TryGetObjectField(TEXT("portal"), PO))
    {
        const TSharedPtr<FJsonObject>& P = *PO;
        C.LeadSeconds = FMath::Clamp(PNum(P, TEXT("leadSeconds"), C.LeadSeconds), 3.f, 55.f);
        C.Radius = FMath::Clamp(PNum(P, TEXT("radius"), C.Radius), 60.f, 400.f);
        C.Height = FMath::Clamp(PNum(P, TEXT("height"), C.Height), C.Radius, 600.f);
        C.Offset = FMath::Clamp(PNum(P, TEXT("offset"), C.Offset), 150.f, 900.f);
        C.ArrivalSeconds = FMath::Clamp(PNum(P, TEXT("arrivalSeconds"), C.ArrivalSeconds), 1.f, 30.f);
        C.ReturnSeconds = FMath::Clamp(PNum(P, TEXT("returnSeconds"), C.ReturnSeconds), 1.f, 30.f);
        C.DiscMaterial = PStr(P, TEXT("discMaterial"), C.DiscMaterial);
        C.MoteMaterial = PStr(P, TEXT("moteMaterial"), C.MoteMaterial);
        const TSharedPtr<FJsonObject>* VO = nullptr;
        if (P->TryGetObjectField(TEXT("vfx"), VO))
        {
            auto Layer = [&](const TCHAR* Key, TArray<FString>& List, float& Scale)
            {
                const TSharedPtr<FJsonObject>* LO = nullptr;
                if ((*VO)->TryGetObjectField(Key, LO)) { List = PList(*LO, TEXT("candidates")); Scale = FMath::Clamp(PNum(*LO, TEXT("scale"), 1.f), .05f, 20.f); }
            };
            Layer(TEXT("ring"), C.RingVFX, C.RingScale); C.bTintRing = (*VO)->HasField(TEXT("ring")) && (*VO)->GetObjectField(TEXT("ring"))->HasTypedField<EJson::Boolean>(TEXT("tint")) && (*VO)->GetObjectField(TEXT("ring"))->GetBoolField(TEXT("tint")); Layer(TEXT("base"), C.BaseVFX, C.BaseScale);
            Layer(TEXT("open"), C.OpenVFX, C.OpenScale); Layer(TEXT("enter"), C.EnterVFX, C.EnterScale);
        }
        const TSharedPtr<FJsonObject>* SO = nullptr;
        if (P->TryGetObjectField(TEXT("sounds"), SO))
        {
            C.OpenSound = FName(*PStr(*SO, TEXT("open"), C.OpenSound.ToString()));
            C.LoopSound = FName(*PStr(*SO, TEXT("loop"), C.LoopSound.ToString()));
            C.EnterSound = FName(*PStr(*SO, TEXT("enter"), C.EnterSound.ToString()));
        }
    }
    else C.Errors.Add(TEXT("Arenas.json has no top-level 'portal' block"));
    const TArray<TSharedPtr<FJsonValue>>* Arenas = nullptr;
    if (Root->TryGetArrayField(TEXT("arenas"), Arenas))
        for (const auto& V : *Arenas)
        {
            const TSharedPtr<FJsonObject>* AO = nullptr; if (!V->TryGetObject(AO)) continue;
            const FString Id = PStr(*AO, TEXT("id"));
            const TSharedPtr<FJsonObject>* LO = nullptr;
            if (!(*AO)->TryGetObjectField(TEXT("portal"), LO)) { C.Errors.Add(FString::Printf(TEXT("%s: no portal look"), *Id)); continue; }
            FLook L; const FString Where = Id + TEXT(".portal");
            L.View = PStr(*LO, TEXT("view"));
            L.Tint = PColor(*LO, TEXT("tint"), L.Tint, &C.Errors, Where);
            L.Accent = PColor(*LO, TEXT("accent"), L.Accent, &C.Errors, Where);
            L.MoteColor = PColor(*LO, TEXT("moteColor"), L.Tint, &C.Errors, Where);
            if (!ParseMotes(PStr(*LO, TEXT("motes"), TEXT("stars")), L.Motes)) C.Errors.Add(FString::Printf(TEXT("%s: unknown motes style"), *Where));
            L.MoteCount = FMath::Clamp(static_cast<int32>(PNum(*LO, TEXT("moteCount"), L.MoteCount)), 0, 200);
            L.MoteSize = FMath::Clamp(PNum(*LO, TEXT("moteSize"), 1.f), .2f, 5.f);
            L.Label = PStr(*LO, TEXT("label"));
            L.MoteGlow = FMath::Clamp(PNum(*LO, TEXT("moteGlow"), L.MoteGlow), 0.f, 50.f);
            if (L.View.IsEmpty()) C.Errors.Add(FString::Printf(TEXT("%s: no view texture"), *Where));
            C.Looks.Add(FName(*Id), L);
        }
    for (const FString& E : C.Errors) UE_LOG(LogCireArenaPortal, Warning, TEXT("CIRE_ARENA_PORTAL_DATA %s"), *E);
}

FPortalServer* ServerState(const UWorld* World) { return World ? GPortalServer.Find(const_cast<UWorld*>(World)) : nullptr; }

/** Ground under a point (the town or the arena floor), or the point itself when nothing is below. */
FVector Ground(UWorld* World, const FVector& At, const AActor* Ignore)
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(CireArenaPortalGround), false, Ignore);
    FCollisionObjectQueryParams Objects(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FHitResult Hit;
    if (World->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 400), At - FVector(0, 0, 1500), Objects, Q)) return Hit.ImpactPoint;
    return At;
}

ACireArenaPortal* SpawnPortal(UWorld* World, int32 ArenaIndex, EKind Kind, int32 Team, const FVector& At, float Yaw, const FString& ForName)
{
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FTransform T(FRotator(0, Yaw, 0), At);
    auto* Portal = World->SpawnActorDeferred<ACireArenaPortal>(ACireArenaPortal::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Portal) return nullptr;
    Portal->ArenaIndex = ArenaIndex; Portal->Kind = static_cast<uint8>(Kind); Portal->TeamId = Team; Portal->ForHeroName = ForName;
    Portal->FinishSpawning(T);
    return Portal;
}

/** Beside the champion, in view of the default camera (ahead and to one side), on open ground inside the play bounds. */
bool PlaceBeside(UWorld* World, const ACireHero* Hero, FVector& OutAt, float& OutYaw)
{
    const FConfig& C = Config();
    const FVector From = Hero->GetActorLocation();
    const float Feet = From.Z - Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float BaseYaw = Hero->GetActorRotation().Yaw;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(CireArenaPortalPlace), false, Hero);
    for (const float Turn : {35.f, -35.f, 70.f, -70.f, 0.f, 110.f, -110.f, 180.f})
    {
        const FVector Dir = FRotator(0, BaseYaw + Turn, 0).Vector();
        const FVector Spot = From + Dir * C.Offset;
        if (World->LineTraceTestByChannel(From, Spot + FVector(0, 0, 40), ECC_Visibility, Q)) continue;        // a wall in the way
        if (World->LineTraceTestByChannel(Spot + FVector(0, 0, 60), Spot + FVector(0, 0, C.Height + C.Radius), ECC_Visibility, Q)) continue; // no headroom
        const FVector G = Ground(World, Spot, Hero);
        if (FMath::Abs(G.Z - Feet) > 120.f) continue;                                                          // a ledge or a pit
        if (!CireLanePath::InsidePlayBounds(World, Hero->TeamId, G)) continue;
        OutAt = G; OutYaw = (From - G).GetSafeNormal2D().Rotation().Yaw; return true;
    }
    OutAt = FVector(From.X, From.Y, Feet) + FRotator(0, BaseYaw, 0).Vector() * 180.f; // cramped: just ahead
    OutYaw = BaseYaw + 180.f; return false;
}

/** A team's arena spawn line: centre and the direction it faces (into the arena). */
void TeamArrival(int32 ArenaIndex, int32 Team, FVector& OutAt, float& OutYaw)
{
    FVector Sum = FVector::ZeroVector; const int32 N = 5;
    for (int32 I = 0; I < N; ++I) Sum += CireArenas::SpawnLocation(ArenaIndex, Team, I, 0.f);
    const FVector Centre = Sum / N, Origin = CireArenas::Origin();
    const FVector Inward = (Origin - Centre).GetSafeNormal2D();
    OutAt = Centre - Inward * 200.f; OutYaw = Inward.Rotation().Yaw;
}

float Ease(float X) { X = FMath::Clamp(X, 0.f, 1.f); const float C1 = 1.70158f, C3 = C1 + 1; return 1 + C3 * FMath::Pow(X - 1, 3) + C1 * FMath::Pow(X - 1, 2); }
}

// ======================================================================== data
bool CireArenaPortal::ParseMotes(const FString& Name, EMotes& Out)
{
    static const TCHAR* Names[] = {TEXT("wheat"), TEXT("snow"), TEXT("sand"), TEXT("leaves"), TEXT("bubbles"), TEXT("stars")};
    for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I) if (Name.Equals(Names[I], ESearchCase::IgnoreCase)) { Out = static_cast<EMotes>(I); return true; }
    return false;
}
const FConfig& CireArenaPortal::Config(bool bReload)
{
    if (!GPortalLoaded || bReload) { LoadConfig(GPortalConfig); GPortalLoaded = true; }
    return GPortalConfig;
}
FLook CireArenaPortal::LookFor(int32 ArenaIndex)
{
    const CireArenas::FArena* A = CireArenas::Get(ArenaIndex);
    FLook L;
    if (A) if (const FLook* Found = Config().Looks.Find(A->Id)) L = *Found;
    if (L.Label.IsEmpty()) L.Label = CireArenas::DisplayName(ArenaIndex);
    return L;
}

// ======================================================================== server
int32 CireArenaPortal::SpawnSlot(const ACireGameMode* Mode, const ACireHero* Hero)
{
    int32 Slot = 0;
    if (Mode && Hero) for (const ACireHero* H : Mode->Heroes) { if (H == Hero) break; if (IsValid(H) && H->TeamId == Hero->TeamId) ++Slot; }
    return Slot;
}
bool CireArenaPortal::IsStaged(const ACireHero* Hero)
{
    const FPortalServer* S = Hero ? ServerState(Hero->GetWorld()) : nullptr;
    return S && S->Staged.Contains(const_cast<ACireHero*>(Hero));
}
TArray<ACireArenaPortal*> CireArenaPortal::Portals(UWorld* World, int32 Kind)
{
    TArray<ACireArenaPortal*> Out;
    if (World) for (TActorIterator<ACireArenaPortal> It(World); It; ++It) if (IsValid(*It) && !It->IsActorBeingDestroyed() && (Kind < 0 || It->Kind == Kind)) Out.Add(*It);
    return Out;
}
void CireArenaPortal::ServerClear(UWorld* World)
{
    for (ACireArenaPortal* P : Portals(World)) P->Destroy();
    if (FPortalServer* S = ServerState(World)) { S->bOpened = false; S->Staged.Reset(); }
}

void CireArenaPortal::TickServer(ACireGameMode* Mode)
{
    if (!Mode || Mode->Clock.Phase() != Cires::MatchPhase::Intermission) return;
    FPortalServer& S = GPortalServer.FindOrAdd(Mode->GetWorld());
    if (!S.bOpened && Mode->Clock.RemainingSeconds() <= Config().LeadSeconds) ServerOpen(Mode);
}

int32 CireArenaPortal::ServerOpen(ACireGameMode* Mode)
{
    if (!Mode || !CireArenas::Get(Mode->ArenaIndex)) return 0;
    UWorld* World = Mode->GetWorld();
    FPortalServer& S = GPortalServer.FindOrAdd(World);
    if (S.bOpened) return Portals(World, static_cast<int32>(EKind::Entry)).Num();
    S.bOpened = true;
    CireArenas::Sync(World); // the arena (collision on a dedicated server) exists before anyone can step through
    int32 Count = 0;
    for (ACireHero* H : Mode->Heroes)
    {
        if (!IsValid(H) || H->bBot || !H->bDrafted || H->TeamId < 0 || H->TeamId > 1) continue;
        FVector At; float Yaw = 0; PlaceBeside(World, H, At, Yaw);
        if (SpawnPortal(World, Mode->ArenaIndex, EKind::Entry, H->TeamId, At, Yaw, H->HeroName)) ++Count;
    }
    if (Count == 0) // bots only (headless soaks): one portal per team at its gate so the moment still reads
        for (int32 Team = 0; Team < 2; ++Team)
        {
            const FVector Base = CireLanePath::BasePosition(World, Team, 0);
            if (SpawnPortal(World, Mode->ArenaIndex, EKind::Entry, Team, Ground(World, Base + FVector(0, 0, 100), nullptr), 0, FString())) ++Count;
        }
    if (auto* State = Mode->GetGameState<ACireGameState>())
        State->Announcement = FString::Printf(TEXT("SHADOW PORTAL | %s | Step in now, or be drawn through when the prep minute ends."), *CireArenas::DisplayName(Mode->ArenaIndex));
    UE_LOG(LogCireArenaPortal, Display, TEXT("CIRE_ARENA_PORTAL_OPEN arena=%s portals=%d seconds_left=%.1f"),
        *CireArenas::Get(Mode->ArenaIndex)->Id.ToString(), Count, Mode->Clock.RemainingSeconds());
    return Count;
}

bool CireArenaPortal::ServerEnter(ACireGameMode* Mode, ACireHero* Hero, ACireArenaPortal* Through)
{
    if (!Mode || !IsValid(Hero) || Hero->bDead || !Hero->bDrafted || Mode->Clock.Phase() != Cires::MatchPhase::Intermission) return false;
    if (Through && (Through->Kind != static_cast<uint8>(EKind::Entry) || Through->bCollapsing || Through->TeamId != Hero->TeamId)) return false;
    if (!CireArenas::Get(Mode->ArenaIndex) || IsStaged(Hero)) return false;
    UWorld* World = Mode->GetWorld();
    CireArenas::Sync(World);
    const FVector From = Hero->GetActorLocation();
    const FVector To = Mode->ArenaPosition(Hero->TeamId, SpawnSlot(Mode, Hero));
    Hero->Target = nullptr; Hero->bAutoAttack = false; Hero->PendingAttackTarget.Reset();
    Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics);
    Hero->SetActorRotation(FRotator(0, (CireArenas::Origin() - To).GetSafeNormal2D().Rotation().Yaw, 0));
    if (AController* C = Hero->GetController()) C->SetControlRotation(Hero->GetActorRotation());
    Hero->Notice = FString::Printf(TEXT("Through the shadow portal: %s. The fight begins when the prep minute ends."), *CireArenas::DisplayName(Mode->ArenaIndex));
    GPortalServer.FindOrAdd(World).Staged.Add(Hero);
    if (Through) { ++Through->Entered; Through->MulticastSwallow(From); Through->ForceNetUpdate(); }
    Hero->ForceNetUpdate();
    UE_LOG(LogCireArenaPortal, Display, TEXT("CIRE_ARENA_PORTAL_ENTER hero=%s team=%d arena=%s at=%s"), *Hero->HeroName, Hero->TeamId,
        *CireArenas::Get(Mode->ArenaIndex)->Id.ToString(), *To.ToString());
    return true;
}

void CireArenaPortal::OnPhaseChanged(ACireGameMode* Mode, int32 NewPhase)
{
    if (!Mode) return;
    UWorld* World = Mode->GetWorld();
    FPortalServer& S = GPortalServer.FindOrAdd(World);
    const FConfig& C = Config();
    if (NewPhase == 2)
    {
        // Everyone still in town is drawn through (the caller has already moved them): the town rifts fold shut.
        int32 Collapsed = 0;
        for (ACireArenaPortal* P : Portals(World)) { P->Collapse(.8f); ++Collapsed; }
        const int32 Staged = S.Staged.Num();
        S.Staged.Reset(); S.bOpened = false;
        if (!CireArenas::Get(Mode->ArenaIndex)) return;
        for (int32 Team = 0; Team < 2; ++Team)
        {
            FVector At; float Yaw; TeamArrival(Mode->ArenaIndex, Team, At, Yaw);
            if (auto* P = SpawnPortal(World, Mode->ArenaIndex, EKind::Arrival, Team, Ground(World, At, nullptr), Yaw, FString())) P->SetLifeSpan(C.ArrivalSeconds + 1.5f);
        }
        int32 InArena = 0, Drafted = 0;
        for (ACireHero* H : Mode->Heroes) if (IsValid(H) && H->bDrafted) { ++Drafted; InArena += CireArenas::InBounds(World, H->GetActorLocation(), -300.f) ? 1 : 0; }
        UE_LOG(LogCireArenaPortal, Display, TEXT("CIRE_ARENA_PORTAL_PULL arena=%s heroes=%d in_arena=%d stepped_through=%d collapsed=%d"),
            *CireArenas::Get(Mode->ArenaIndex)->Id.ToString(), Drafted, InArena, Staged, Collapsed);
    }
    else if (NewPhase == 4)
    {
        for (ACireArenaPortal* P : Portals(World)) P->Collapse(.5f);
        S.Staged.Reset(); S.bOpened = false;
        if (!CireArenas::Get(Mode->ArenaIndex)) return;
        for (int32 Team = 0; Team < 2; ++Team)
        {
            // The same return point ChangePhase(4) uses: the realm's Rift when authored, else the gate.
            FTransform Rift; FVector At; float Yaw = 0;
            if (CireLanePath::RiftTransform(World, Team, Rift, 0)) { At = Rift.GetLocation() + Rift.GetRotation().RotateVector(FVector(-120, 0, 0)); Yaw = Rift.Rotator().Yaw; }
            else { At = CireLanePath::BasePosition(World, Team, 0) + FVector(-120, 0, 0); }
            if (auto* P = SpawnPortal(World, Mode->ArenaIndex, EKind::Return, Team, Ground(World, At + FVector(0, 0, 100), nullptr), Yaw, FString())) P->SetLifeSpan(C.ReturnSeconds + 1.5f);
        }
    }
    else ServerClear(World);
}

bool CireArenaPortal::LocalViewInArena(const UWorld* World)
{
    if (!World || World->GetNetMode() == NM_DedicatedServer) return false;
    const auto* State = World->GetGameState<ACireGameState>();
    if (!State || State->Phase != 1) return false;
    const APlayerController* PC = World->GetFirstPlayerController();
    const APawn* Pawn = PC && PC->IsLocalController() ? PC->GetPawn() : nullptr;
    return Pawn && CireArenas::Get(CireArenas::CurrentIndex(World)) && CireArenas::InBounds(World, Pawn->GetActorLocation(), -600.f);
}

// ======================================================================== actor
ACireArenaPortal::ACireArenaPortal()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true; bAlwaysRelevant = true; SetReplicatingMovement(false);
    SetNetUpdateFrequency(10.f);
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root")); RootComponent = Root;
    Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
    Trigger->SetupAttachment(Root);
    Trigger->SetBoxExtent(FVector(40, 105, 100));
    Trigger->SetRelativeLocation(FVector(0, 0, 100));
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCollisionObjectType(ECC_WorldDynamic);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    Trigger->SetGenerateOverlapEvents(true);
    Trigger->SetCanEverAffectNavigation(false);
}

void ACireArenaPortal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireArenaPortal, ArenaIndex); DOREPLIFETIME(ACireArenaPortal, Kind); DOREPLIFETIME(ACireArenaPortal, TeamId);
    DOREPLIFETIME(ACireArenaPortal, ForHeroName); DOREPLIFETIME(ACireArenaPortal, bCollapsing); DOREPLIFETIME(ACireArenaPortal, Entered);
}

void ACireArenaPortal::BeginPlay()
{
    Super::BeginPlay();
    const FConfig& C = Config();
    Trigger->SetBoxExtent(FVector(45, C.Radius * .7f, C.Height * .55f));
    Trigger->SetRelativeLocation(FVector(0, 0, C.Height * .55f));
    if (HasAuthority() && Kind == static_cast<uint8>(EKind::Entry)) Trigger->OnComponentBeginOverlap.AddDynamic(this, &ACireArenaPortal::OnTriggerBegin);
    else Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (GetNetMode() != NM_DedicatedServer) BuildVisuals();
}

void ACireArenaPortal::EndPlay(const EEndPlayReason::Type Reason)
{
    for (auto& F : FX) CireFabVFX::Release(F.Get());
    FX.Reset();
    if (Hum) Hum->Stop();
    Super::EndPlay(Reason);
}

void ACireArenaPortal::OnRep_Setup() { if (HasActorBegunPlay() && GetNetMode() != NM_DedicatedServer) BuildVisuals(); }
void ACireArenaPortal::OnRep_Collapse()
{
    if (!bCollapsing) return;
    if (CollapseAge < 0) CollapseAge = 0;
    if (Hum) Hum->FadeOut(.5f, 0.f);
    for (auto& F : FX) CireFabVFX::Release(F.Get());
    FX.Reset();
}

void ACireArenaPortal::Collapse(float Seconds)
{
    if (bCollapsing) return;
    bCollapsing = true; CollapseSeconds = FMath::Max(.1f, Seconds);
    Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (HasAuthority()) { SetLifeSpan(Seconds + .6f); ForceNetUpdate(); }
    OnRep_Collapse();
}

void ACireArenaPortal::OnTriggerBegin(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
    auto* Hero = Cast<ACireHero>(Other);
    if (!Hero || Hero->bBot || !HasAuthority() || bCollapsing) return; // bots are drawn through when the minute ends
    CireArenaPortal::ServerEnter(GetWorld()->GetAuthGameMode<ACireGameMode>(), Hero, this);
}

void ACireArenaPortal::MulticastSwallow_Implementation(FVector_NetQuantize At)
{
    if (GetNetMode() == NM_DedicatedServer) return;
    Flash = 1.f;
    const FConfig& C = Config();
    CireAudio::PlayCue(this, C.EnterSound, At);
    CireFabVFX::FEntry Entry; Entry.Candidates = C.EnterVFX;
    if (UFXSystemAsset* System = CireFabVFX::Resolve(&Entry)) CireFabVFX::SpawnAt(GetWorld(), System, At, FRotator::ZeroRotator, C.EnterScale);
}

void ACireArenaPortal::BuildVisuals()
{
    if (bVisualsBuilt || ArenaIndex == INDEX_NONE || !GetWorld()) return;
    bVisualsBuilt = true;
    const FConfig& C = Config();
    const FLook L = LookFor(ArenaIndex);
    LabelText = L.Label;
    const FVector Centre(0, 0, C.Height);
    auto Keep = [this](USceneComponent* Comp) { Comp->SetupAttachment(Root); Comp->RegisterComponent(); AddInstanceComponent(Comp); };

    // The ring: the destination's painted view, wrapped in swirling shadow tinted after the arena.
    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    UMaterialInterface* DiscMat = LoadObject<UMaterialInterface>(nullptr, *C.DiscMaterial);
    Disc = NewObject<UStaticMeshComponent>(this, TEXT("PortalDisc"));
    Disc->SetStaticMesh(Plane);
    Disc->SetRelativeLocation(Centre); Disc->SetRelativeRotation(FRotator(-90, 0, 0)); // plane normal +Z -> faces +X (toward the champion)
    Disc->SetRelativeScale3D(FVector(C.Radius * 2.f / 100.f));
    Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision); Disc->SetCastShadow(false); Disc->SetCanEverAffectNavigation(false);
    Disc->TranslucencySortPriority = 2;
    Keep(Disc);
    if (DiscMat)
    {
        DiscMID = UMaterialInstanceDynamic::Create(DiscMat, this);
        UTexture* View = L.View.IsEmpty() ? nullptr : LoadObject<UTexture>(nullptr, *L.View);
        bHasView = View != nullptr;
        if (View) DiscMID->SetTextureParameterValue(TEXT("View"), View);
        DiscMID->SetScalarParameterValue(TEXT("ViewGain"), View ? .9f : 0.f);
        DiscMID->SetVectorParameterValue(TEXT("Tint"), L.Tint);
        DiscMID->SetVectorParameterValue(TEXT("Accent"), L.Accent);
        DiscMID->SetScalarParameterValue(TEXT("Open"), 0.f);
        Disc->SetMaterial(0, DiscMID);
    }
    else UE_LOG(LogCireArenaPortal, Warning, TEXT("CIRE_ARENA_PORTAL_ASSETS_MISSING material=%s"), *C.DiscMaterial);

    // Themed motes leaking out of the ring (wheat chaff, snow, red sand, leaves, bubbles, stars).
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Motes = NewObject<UInstancedStaticMeshComponent>(this, TEXT("PortalMotes"));
    Motes->SetStaticMesh(Sphere); Motes->SetCollisionEnabled(ECollisionEnabled::NoCollision); Motes->SetCastShadow(false);
    Motes->SetCanEverAffectNavigation(false); Motes->SetRelativeLocation(Centre); Motes->SetMobility(EComponentMobility::Movable);
    Keep(Motes);
    if (UMaterialInterface* MoteMat = LoadObject<UMaterialInterface>(nullptr, *C.MoteMaterial))
    {
        MoteMID = UMaterialInstanceDynamic::Create(MoteMat, this);
        MoteMID->SetVectorParameterValue(TEXT("Color"), L.MoteColor);
        MoteMID->SetScalarParameterValue(TEXT("Intensity"), L.MoteGlow);
        Motes->SetMaterial(0, MoteMID);
    }
    FRandomStream Seeds(static_cast<int32>(ArenaIndex * 7919 + Kind * 131 + TeamId * 17));
    MoteSeeds.Reset();
    TArray<FTransform> Initial;
    for (int32 I = 0; I < L.MoteCount; ++I)
    {
        MoteSeeds.Add(FVector4f(Seeds.FRand(), Seeds.FRand(), Seeds.FRand(), Seeds.FRand()));
        Initial.Add(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(.001f)));
    }
    Motes->AddInstances(Initial, false);

    // The name, title and countdown are a HUD plate above the ring (ACireHUD::DrawPortalPlates), like the merchants.

    Light = NewObject<UPointLightComponent>(this, TEXT("PortalLight"));
    Light->SetRelativeLocation(Centre + FVector(60, 0, 0)); Light->SetLightColor(L.Tint); Light->SetIntensity(0.f);
    Light->SetAttenuationRadius(C.Radius * 5.f); Light->SetCastShadows(false); Light->SetMobility(EComponentMobility::Movable);
    Keep(Light);

    // Optional Shadow_Magic layers (local Fab pack): a swirling rim stood upright and a shadow pool at the base.
    auto Layer = [&](const TArray<FString>& Candidates, float Scale, const FVector& At, const FRotator& Rot, bool bTint) {
        CireFabVFX::FEntry Entry; Entry.Candidates = Candidates;
        UFXSystemAsset* System = CireFabVFX::Resolve(&Entry);
        UFXSystemComponent* Comp = System ? CireFabVFX::SpawnAttached(System, Root, At, Scale, false) : nullptr;
        if (!Comp) return;
        Comp->SetRelativeRotation(Rot);
        if (bTint) CireFabVFX::ApplyTint(Comp, FLinearColor(L.Tint.R, L.Tint.G, L.Tint.B, 1.f));
        FX.Add(Comp); ++FabLayers;
    };
    Layer(C.RingVFX, C.RingScale * C.Radius / 150.f, Centre, FRotator(-90, 0, 0), C.bTintRing);
    Layer(C.BaseVFX, C.BaseScale * C.Radius / 150.f, FVector(0, 0, 4), FRotator::ZeroRotator, false);
    CireFabVFX::FEntry OpenEntry; OpenEntry.Candidates = C.OpenVFX;
    if (UFXSystemAsset* Burst = CireFabVFX::Resolve(&OpenEntry)) CireFabVFX::SpawnAt(GetWorld(), Burst, GetActorLocation() + GetActorRotation().RotateVector(Centre), GetActorRotation(), C.OpenScale);

    if (!bCollapsing)
    {
        CireAudio::PlayCue(this, C.OpenSound, GetActorLocation() + FVector(0, 0, C.Height));
        Hum = CireAudio::PlayAttached(C.LoopSound, Disc);
    }
    else OnRep_Collapse();

    // Telegraph: one banner per portal opening for the champion's own team (the prep banner named the arena already).
    if (Kind == static_cast<uint8>(EKind::Entry) && !bCollapsing)
    {
        const APlayerController* PC = GetWorld()->GetFirstPlayerController();
        const auto* Mine = PC ? Cast<ACireHero>(PC->GetPawn()) : nullptr;
        static TWeakObjectPtr<UWorld> BannerWorld; static double BannerAt = -100;
        const double Now = GetWorld()->GetTimeSeconds();
        if (Mine && Mine->TeamId == TeamId && (BannerWorld.Get() != GetWorld() || Now - BannerAt > 20))
        {
            BannerWorld = GetWorld(); BannerAt = Now;
            CireBanners::Show(ECireBanner::Arena, LabelText, TEXT("A shadow portal has opened beside you. Step in now, or it draws you through when the minute ends."), TEXT("SHADOW PORTAL"));
        }
    }
    UE_LOG(LogCireArenaPortal, Display, TEXT("CIRE_ARENA_PORTAL_VISUALS kind=%d team=%d arena=\"%s\" view=%d fab_layers=%d motes=%d"),
        Kind, TeamId, *LabelText, bHasView ? 1 : 0, FabLayers, L.MoteCount);
}

void ACireArenaPortal::UpdateMotes(float Time)
{
    if (!Motes || MoteSeeds.IsEmpty()) return;
    const FConfig& C = Config();
    const FLook L = LookFor(ArenaIndex);
    const float R = C.Radius, S = L.MoteSize * FMath::Max(.05f, Open);
    TArray<FTransform> T; T.Reserve(MoteSeeds.Num());
    for (int32 I = 0; I < MoteSeeds.Num(); ++I)
    {
        const FVector4f& K = MoteSeeds[I];
        FVector P = FVector::ZeroVector, Scale(.03f); FRotator Rot = FRotator::ZeroRotator;
        auto U = [&](float Rate) { return FMath::Frac(Time * Rate + K.X); };
        float Fade = 1.f;
        switch (L.Motes)
        {
        case EMotes::Wheat: { // golden chaff spiralling out and up
            const float u = U(.2f), Ang = K.Y * 2 * PI + Time * .6f, Rad = R * (.35f + .7f * K.Z) * (1 + u * .5f);
            P = FVector(u * 200.f, FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad * .8f + u * 110.f);
            Scale = FVector(.022f, .022f, .075f); Rot = FRotator(Time * 90.f * K.W, Ang * 57.3f, 0); Fade = FMath::Sin(u * PI); break; }
        case EMotes::Snow: { // flakes blown out of the ring, settling
            const float u = U(.26f), Ang = K.Y * 2 * PI, Rad = R * .9f * FMath::Sqrt(K.Z);
            P = FVector(u * 330.f, FMath::Cos(Ang) * Rad + FMath::Sin(Time * 2.f + K.W * 9.f) * 28.f * u, FMath::Sin(Ang) * Rad - u * u * 190.f);
            Scale = FVector(.02f + .018f * K.W); Fade = FMath::Sin(u * PI); break; }
        case EMotes::Sand: { // a stream of red grit on the desert wind
            const float u = U(.5f);
            P = FVector(u * 430.f, (K.Y - .5f) * 1.7f * R + FMath::Sin(u * 6.f + K.W * 6.f) * 22.f, (K.Z - .5f) * 1.3f * R - u * 45.f);
            Scale = FVector(.075f, .016f, .016f); Rot = FRotator(0, (K.Y - .5f) * 25.f, 0); Fade = FMath::Sin(u * PI); break; }
        case EMotes::Leaves: { // tumbling leaves drifting down
            const float u = U(.16f), Ang = K.Y * 2 * PI, Rad = R * .85f * FMath::Sqrt(K.Z);
            P = FVector(u * 300.f, FMath::Cos(Ang) * Rad + FMath::Sin(u * 9.f + K.W * 6.f) * 45.f, FMath::Sin(Ang) * Rad - u * 160.f);
            Scale = FVector(.07f, .045f, .01f); Rot = FRotator(Time * 140.f * (K.W - .5f), Time * 60.f + K.Y * 360.f, Time * 110.f * K.Z);
            Fade = FMath::Sin(u * PI); break; }
        case EMotes::Bubbles: { // rising bubbles with a wobble
            const float u = U(.22f);
            P = FVector(25.f + u * 110.f, (K.Y - .5f) * 1.5f * R + FMath::Sin(Time * 3.f + K.W * 10.f) * 12.f, -R + u * (2 * R + 170.f));
            Scale = FVector(.012f + .03f * K.Z); Fade = FMath::Min(1.f, (1 - u) * 5.f); break; }
        case EMotes::Stars: default: { // twinkling points turning with the ring
            const float u = U(.07f), Ang = K.Y * 2 * PI + Time * .15f, Rad = R * (.2f + .95f * K.Z);
            P = FVector(10.f + u * 150.f, FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad);
            const float Tw = .5f + .5f * FMath::Sin(Time * (3.f + 5.f * K.W) + K.X * 20.f);
            Scale = FVector(.01f + .028f * Tw); Fade = FMath::Sin(u * PI); break; }
        }
        T.Add(FTransform(Rot, P * Open, Scale * S * FMath::Max(.02f, Fade)));
    }
    Motes->BatchUpdateInstancesTransforms(0, T, false, true, true);
}

void ACireArenaPortal::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    const FConfig& C = Config();
    if (HasAuthority() && !bCollapsing && Kind != static_cast<uint8>(EKind::Entry))
        if (Age >= (Kind == static_cast<uint8>(EKind::Arrival) ? C.ArrivalSeconds : C.ReturnSeconds)) Collapse(.6f);
    if (!bVisualsBuilt) return;
    if (bCollapsing && CollapseAge >= 0) { CollapseAge += DeltaSeconds; Open = FMath::Max(0.f, 1.f - CollapseAge / CollapseSeconds); }
    else Open = Ease(Age / .7f);
    Flash = FMath::Max(0.f, Flash - DeltaSeconds * 1.6f);
    const float Pulse = 1.f + .025f * FMath::Sin(Age * 2.3f);
    const float Scale = C.Radius * 2.f / 100.f * FMath::Max(.001f, Open) * Pulse;
    if (Disc) Disc->SetRelativeScale3D(FVector(Scale, Scale, 1));
    if (DiscMID) { DiscMID->SetScalarParameterValue(TEXT("Open"), FMath::Clamp(Open, 0.f, 1.f)); DiscMID->SetScalarParameterValue(TEXT("Flash"), Flash); }
    if (Light) Light->SetIntensity((2500.f + 1200.f * FMath::Sin(Age * 3.1f) + 9000.f * Flash) * FMath::Clamp(Open, 0.f, 1.f));
    UpdateMotes(Age);
}

// ======================================================================== tests
#if !UE_BUILD_SHIPPING
bool CireArenaPortal::RunTests(UWorld* World)
{
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool b, const FString& What) { ++Checks; if (!b) { bPass = false; UE_LOG(LogCireArenaPortal, Error, TEXT("CIRE_ARENA_PORTAL_TEST_FAIL %s"), *What); } };
    const FConfig& C = Config(true);
    Check(C.Errors.IsEmpty(), TEXT("portal data parses without errors"));
    Check(C.LeadSeconds >= 5 && C.LeadSeconds <= 30, TEXT("lead time between 5 and 30 s"));
    TSet<int32> Styles; TSet<FString> Views;
    for (int32 Index : CireArenas::Rotation())
    {
        const CireArenas::FArena* A = CireArenas::Get(Index); if (!A || A->bFallbackOnly) continue;
        const FLook* L = C.Looks.Find(A->Id);
        Check(L != nullptr, FString::Printf(TEXT("%s has a portal look"), *A->Id.ToString()));
        if (!L) continue;
        Check(!L->View.IsEmpty() && LoadObject<UTexture>(nullptr, *L->View) != nullptr, FString::Printf(TEXT("%s view texture loads"), *A->Id.ToString()));
        Check(L->MoteCount > 0, FString::Printf(TEXT("%s has motes"), *A->Id.ToString()));
        Styles.Add(static_cast<int32>(L->Motes)); Views.Add(L->View);
    }
    Check(Styles.Num() >= 6, TEXT("six arenas use six different mote styles"));
    Check(Views.Num() == C.Looks.Num(), TEXT("every arena has its own view"));
    Check(LoadObject<UMaterialInterface>(nullptr, *C.DiscMaterial) != nullptr, TEXT("portal disc material loads"));
    Check(LoadObject<UMaterialInterface>(nullptr, *C.MoteMaterial) != nullptr, TEXT("portal mote material loads"));
    EMotes M; Check(ParseMotes(TEXT("Bubbles"), M) && M == EMotes::Bubbles && !ParseMotes(TEXT("confetti"), M), TEXT("motes names parse"));
    Check(CireAudio::HasCue(C.OpenSound) && CireAudio::HasCue(C.LoopSound) && CireAudio::HasCue(C.EnterSound), TEXT("portal sound cues exist"));
    // Visuals on this peer: one portal per themed arena, built and destroyed.
    if (World && World->GetNetMode() != NM_DedicatedServer)
        for (int32 Index : CireArenas::Rotation())
        {
            if (!CireArenas::Get(Index) || CireArenas::Get(Index)->bFallbackOnly) continue;
            auto* P = SpawnPortal(World, Index, EKind::Entry, 0, FVector(0, -30000, 20000), 0, TEXT("Test"));
            Check(P && P->bVisualsBuilt && P->bHasView && P->Motes && P->Motes->GetInstanceCount() == LookFor(Index).MoteCount && P->DiscMID,
                FString::Printf(TEXT("portal visuals build for %s"), *CireArenas::Get(Index)->Id.ToString()));
            Check(P && P->LabelText == CireArenas::DisplayName(Index), TEXT("portal names its arena"));
            if (P) { P->Tick(.8f); Check(P->Open > .95f, TEXT("portal opens")); P->Collapse(.2f); P->Tick(.3f); Check(P->Open <= .01f, TEXT("portal collapses")); P->Destroy(); }
        }
    UE_LOG(LogCireArenaPortal, Display, TEXT("CIRE_ARENA_PORTAL_TESTS_%s checks=%d looks=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks, C.Looks.Num());
    return bPass;
}
#endif

// ======================================================================== gallery
#if !UE_BUILD_SHIPPING
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ContentStreaming.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "GameFramework/HUD.h"

namespace
{
struct FPortalGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<ACireArenaPortal> Portal;
    TArray<int32> Arenas; TArray<FString> Files;
    FString Directory;
    int32 Current = -1, Stage = 0;
    double Started = 0, StageAt = 0;
    bool bDone = false;
} PG;

void GalleryFinish(bool bPass)
{
    if (PG.bDone) return; PG.bDone = true;
    for (const FString& F : PG.Files) { bPass &= IFileManager::Get().FileSize(*F) > 10000; UE_LOG(LogCireArenaPortal, Display, TEXT("CIRE_ARENA_GALLERY_SHOT arena=portal file=%s frame_ms=0 frames=0"), *F); }
    UE_LOG(LogCireArenaPortal, Display, TEXT("CIRE_ARENA_GALLERY_%s captures=%d arenas=%d directory=%s"), bPass ? TEXT("PASS") : TEXT("FAIL"), PG.Files.Num(), PG.Arenas.Num(), *PG.Directory);
    FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
}
void GalleryLook(const FVector& From, const FVector& To, float Fov)
{
    PG.Camera->SetActorLocation(From); PG.Camera->SetActorRotation((To - From).Rotation()); PG.Camera->GetCameraComponent()->SetFieldOfView(Fov);
}
void GalleryCapture(const TCHAR* Kind)
{
    const auto* A = CireArenas::Get(PG.Arenas[PG.Current]);
    const FString File = FPaths::Combine(PG.Directory, FString::Printf(TEXT("%02d_%s_%s.png"), PG.Current + 1, *A->Id.ToString(), Kind));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    PG.Files.Add(File);
}
}

bool CireArenaPortal::GalleryInitialize(ACireGameMode* Mode)
{
    PG = FPortalGallery();
    FString Only;
    if (!FParse::Param(FCommandLine::Get(), TEXT("CirePortalGallery")) && !FParse::Value(FCommandLine::Get(), TEXT("CirePortalGallery="), Only)) return false;
    FParse::Value(FCommandLine::Get(), TEXT("CirePortalGallery="), Only);
    TArray<FString> Ids; Only.ParseIntoArray(Ids, TEXT("+"));
    PG.Mode = Mode; PG.Started = FPlatformTime::Seconds();
    PG.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PortalGallery"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    IFileManager::Get().MakeDirectory(*PG.Directory, true);
    for (int32 I : CireArenas::Rotation()) if (!CireArenas::Get(I)->bFallbackOnly && (Ids.IsEmpty() || Ids.Contains(CireArenas::Get(I)->Id.ToString()))) PG.Arenas.Add(I);
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    if (PG.Arenas.IsEmpty()) GalleryFinish(false);
    return true;
}

bool CireArenaPortal::GalleryTick(ACireGameMode* Mode)
{
    if (PG.Mode.Get() != Mode) return false;
    if (PG.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - PG.Started > 90 + 30.0 * PG.Arenas.Num()) { UE_LOG(LogCireArenaPortal, Error, TEXT("CIRE_ARENA_GALLERY_ERROR portal gallery timed out")); GalleryFinish(false); return true; }
    UWorld* World = Mode->GetWorld();
    auto* State = Mode->GetGameState<ACireGameState>();
    if (!PG.Controller.IsValid())
    {
        auto* C = Cast<ACireController>(World->GetFirstPlayerController());
        auto* Hero = C ? Cast<ACireHero>(C->GetPawn()) : nullptr;
        if (!Hero || !C->GetHUD()) return true;
        Hero->TeamId = 0; Hero->bDrafted = true; Hero->Draft(1);
        Hero->SetActorLocation(CireLanePath::BasePosition(World, 0) + FVector(200, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        Hero->SetActorRotation(FRotator(0, 0, 0));
        Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->GetCharacterMovement()->DisableMovement();
        C->SetIgnoreMoveInput(true); C->SetIgnoreLookInput(true); C->bShowMouseCursor = false;
        PG.Camera = World->SpawnActor<ACameraActor>();
        auto* Cam = PG.Camera->GetCameraComponent(); Cam->SetAspectRatio(16.f / 9.f); Cam->bConstrainAspectRatio = true;
        Cam->PostProcessSettings.bOverride_MotionBlurAmount = true; Cam->PostProcessSettings.MotionBlurAmount = 0;
        C->SetViewTarget(PG.Camera.Get());
        Mode->Clock.BeginIntermission();
        PG.Controller = C; PG.Current = 0; PG.Stage = -1; PG.StageAt = Now;
        return true;
    }
    if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - PG.Started < 300) { PG.StageAt = Now; return true; }
    auto* Hero = Cast<ACireHero>(PG.Controller->GetPawn());
    const double Age = Now - PG.StageAt;
    const FConfig& C = Config();
    if (PG.Stage == -1)
    {
        if (PG.Portal.IsValid()) PG.Portal->Destroy();
        const int32 Index = PG.Arenas[PG.Current];
        Mode->ArenaIndex = Index;
        if (State) { State->ArenaIndex = Index; State->Phase = 1; State->SecondsLeft = 9.f; }
        FVector At; float Yaw; PlaceBeside(World, Hero, At, Yaw);
        PG.Portal = SpawnPortal(World, Index, EKind::Entry, 0, At, Yaw, Hero->HeroName);
        PG.Stage = 0; PG.StageAt = Now;
        return true;
    }
    if (!PG.Portal.IsValid()) { GalleryFinish(false); return true; }
    const FVector Centre = PG.Portal->GetActorLocation() + FVector(0, 0, C.Height);
    const FVector Front = PG.Portal->GetActorForwardVector();
    if (PG.Stage == 0)
    {
        // Gameplay framing: over the champion's shoulder toward the rift beside them.
        const FVector Pivot = Hero->GetActorLocation() + FVector(0, 0, 80);
        const FVector Dir = (Centre - Pivot).GetSafeNormal2D();
        PG.Controller->bShop = false; // prep opens the shop window; the portal (and its HUD plate) is the subject here
        GalleryLook(Pivot - Dir * 560 + FVector(0, 0, 260) - FVector::CrossProduct(Dir, FVector::UpVector) * 160, Centre + FVector(0, 0, 40), 75);
        if (Age > 1 && Age < 1.3) IStreamingManager::Get().StreamAllResources(1.f);
        if (Age > 4) { GalleryCapture(TEXT("portal")); PG.Stage = 1; PG.StageAt = Now; }
    }
    else if (PG.Stage == 1)
    {
        PG.Controller->bShop = false;
        const FVector Side = FVector::CrossProduct(Front, FVector::UpVector);
        GalleryLook(Centre + Front * 700 + Side * 160 + FVector(0, 0, 60), Centre + FVector(0, 0, 70), 55); // near straight on: the view inside the ring
        if (Age > 2.5) { GalleryCapture(TEXT("close")); PG.Stage = 2; PG.StageAt = Now; }
    }
    else if (Age > 1)
    {
        if (++PG.Current >= PG.Arenas.Num()) { if (PG.Portal.IsValid()) PG.Portal->Destroy(); GalleryFinish(true); return true; }
        PG.Stage = -1;
    }
    return true;
}
#endif
