#include "CireTownMap.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#include "CireGame.h"
#include "CireHUD.h"
#include "CireLanePath.h"
#include "CireNPCCombat.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
#include "CanvasItem.h"
#include "DrawDebugHelpers.h"
#include "Engine/LevelStreaming.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "WorldPartition/WorldPartitionSubsystem.h" // town-perf
#include "Components/BoxComponent.h"
#include "Components/SkyLightComponent.h" // town-perf
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Level.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTown, Log, All);

namespace
{
FCireTownDef GDef;
bool bDefLoaded = false;
bool bActive = false;
int32 ExploreFlag = -1;
TWeakObjectPtr<UWorld> TownWorld;
TMap<FIntPoint, float> GroundCache;
struct FLoadedRealms { TArray<TWeakObjectPtr<ULevelStreamingDynamic>> Levels; };
TMap<TWeakObjectPtr<UWorld>, FLoadedRealms> Loaded;
TSet<TWeakObjectPtr<ULevel>> PreparedLevels;
double LoadMs = 0; // town-perf
// town-perf: the pack's own optimizer script sets these while its levels load (virtual shadow maps off, i.e. a 4-cascade
// shadow atlas re-rendering ~76,000 Nanite primitives every frame). The project's values are restored after streaming.
const TCHAR* const PackRendererCvars[] = {TEXT("r.Shadow.Virtual.Enable"), TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"),
    TEXT("r.Shadow.Virtual.ResolutionLodBiasLocal"), TEXT("r.Shadow.Virtual.Clipmap.LastLevel")};
TMap<FString, FString> ProjectRendererCvars;
void SaveRendererCvars()
{
    if (!ProjectRendererCvars.IsEmpty()) return;
    for (const TCHAR* Name : PackRendererCvars)
        if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) ProjectRendererCvars.Add(Name, V->GetString());
}
// town-perf / doors: a closed door leaf is a thin, door-sized slab ("SM_Door_01"), not a wall piece with an opening
// ("SM_Wall_Door_01") or a frame. Eric's ruling: the houses stay enterable, so leaves are cleared at load (hidden, no
// collision, no navigation) on every peer, before the navmesh is built.
bool IsDoorLeaf(const UStaticMeshComponent* C)
{
    const UStaticMesh* SM = C ? C->GetStaticMesh() : nullptr;
    if (!SM) return false;
    const FString Name = SM->GetName();
    if (!Name.Contains(TEXT("Door")) || Name.Contains(TEXT("Frame")) || Name.Contains(TEXT("Wall")) || Name.Contains(TEXT("Way"))) return false;
    const FVector E = SM->GetBounds().BoxExtent * C->GetComponentScale().GetAbs();
    const float Thin = FMath::Min(E.X, E.Y), Wide = FMath::Max(E.X, E.Y);
    return Thin <= 15.f && Wide >= 30.f && Wide <= 110.f && E.Z >= 85.f && E.Z <= 160.f;
}
int32 RestoreRendererCvars()
{
    int32 Restored = 0;
    for (const auto& Pair : ProjectRendererCvars)
        if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(*Pair.Key); V && V->GetString() != Pair.Value)
        { V->Set(*Pair.Value, ECVF_SetByConsole); ++Restored; }
    return Restored;
}

// Legacy (procedural town) realm centres: the realms sit either side of the Sundering Cliff at Y = 0.
const FVector LegacyOrigins[2] = {FVector(0, -2100, 0), FVector(0, 2100, 0)};

bool Vec2(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FVector2D& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr; double X = 0, Y = 0;
    if (!O->TryGetArrayField(Key, A) || !A || A->Num() != 2 || !(*A)[0]->TryGetNumber(X) || !(*A)[1]->TryGetNumber(Y) || !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
    Out = FVector2D(X, Y); return true;
}
bool Vec3(const TSharedPtr<FJsonValue>& V, FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr; double X = 0, Y = 0, Z = 0;
    if (!V || !V->TryGetArray(A) || !A || A->Num() != 3 || !(*A)[0]->TryGetNumber(X) || !(*A)[1]->TryGetNumber(Y) || !(*A)[2]->TryGetNumber(Z)) return false;
    if (!FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z)) return false;
    Out = FVector(X, Y, Z); return true;
}
void Parse(FCireTownDef& D)
{
    D = FCireTownDef();
    FString Json;
    FString Path = FPaths::ProjectContentDir() / TEXT("Data/CastleTown.json");
    FParse::Value(FCommandLine::Get(), TEXT("CireTownJson="), Path); // town-perf: before/after captures with another look
    if (!FFileHelper::LoadFileToString(Json, *Path)) { D.Error = TEXT("CastleTown.json could not be read"); return; }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { D.Error = TEXT("CastleTown.json is not valid JSON"); return; }
    double Schema = 0;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || Schema != 1) { D.Error = TEXT("CastleTown.json needs schemaVersion 1"); return; }
    Root->TryGetStringField(TEXT("pack"), D.Pack);
    Root->TryGetStringField(TEXT("persistent"), D.Persistent);
    Root->TryGetStringField(TEXT("routes"), D.RoutesFile);
    Root->TryGetBoolField(TEXT("defaultMap"), D.bDefaultMap);
    const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
    if (!Root->TryGetArrayField(TEXT("levels"), Levels) || !Levels || Levels->IsEmpty()) { D.Error = TEXT("CastleTown.json lists no levels"); return; }
    for (const auto& L : *Levels)
    {
        FString Name;
        if (!L || !L->TryGetString(Name) || !Name.StartsWith(TEXT("/Game/")) || Name.Contains(TEXT("SL_Lighting"))) { D.Error = TEXT("levels must be /Game/ package names (SL_Lighting is never streamed per realm)"); return; }
        D.Levels.Add(Name);
    }
    // "realms": one entry per team: the translation of that copy of the town (yaw is fixed at 0: both copies are
    // identical and axis-aligned, so one realm-local layout applies to both) and its lighting.
    const TArray<TSharedPtr<FJsonValue>>* Realms = nullptr;
    if (!Root->TryGetArrayField(TEXT("realms"), Realms) || !Realms || Realms->Num() != 2) { D.Error = TEXT("realms needs two entries (team 0 and team 1)"); return; }
    bool Seen[2] = {false, false};
    for (const auto& V : *Realms)
    {
        const TSharedPtr<FJsonObject>* O = nullptr; double Team = -1;
        if (!V || !V->TryGetObject(O) || !O || !(*O)->TryGetNumberField(TEXT("team"), Team) || (Team != 0 && Team != 1) || Seen[int32(Team)])
        { D.Error = TEXT("each realm needs a unique team 0 or 1"); return; }
        const int32 T = int32(Team); Seen[T] = true;
        const TSharedPtr<FJsonValue>* Offset = (*O)->Values.Find(TEXT("offset"));
        if (!Offset || !Vec3(*Offset, D.Offsets[T])) { D.Error = TEXT("realm offset needs [x, y, z]"); return; }
        FCireRealmLighting& L = D.Lighting[T];
        (*O)->TryGetStringField(TEXT("name"), L.Name);
        const TSharedPtr<FJsonObject>* Light = nullptr;
        if ((*O)->TryGetObjectField(TEXT("lighting"), Light) && Light)
        {
            auto Num = [&](const TSharedPtr<FJsonObject>& J, const TCHAR* K, float& Out) { double X = 0; if (J->TryGetNumberField(K, X) && FMath::IsFinite(X)) Out = float(X); };
            auto Col = [&](const TSharedPtr<FJsonObject>& J, const TCHAR* K, FLinearColor& Out)
            {
                const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
                if (J->TryGetArrayField(K, A) && A && A->Num() == 3) Out = FLinearColor(float((*A)[0]->AsNumber()), float((*A)[1]->AsNumber()), float((*A)[2]->AsNumber()));
            };
            (*Light)->TryGetStringField(TEXT("sky"), L.SkyMaterial);
            Num(*Light, TEXT("sunPitch"), L.SunPitch); Num(*Light, TEXT("sunYaw"), L.SunYaw); Num(*Light, TEXT("sunIntensity"), L.SunIntensity);
            Col(*Light, TEXT("sunColor"), L.SunColor);
            Num(*Light, TEXT("exposureBias"), L.ExposureBias); Num(*Light, TEXT("saturation"), L.Saturation); Col(*Light, TEXT("tint"), L.Tint);
            Num(*Light, TEXT("interiorIntensity"), L.InteriorIntensity); Num(*Light, TEXT("interiorRadius"), L.InteriorRadius); Col(*Light, TEXT("interiorColor"), L.InteriorColor);
            Num(*Light, TEXT("skyLightIntensity"), L.SkyLightIntensity); Col(*Light, TEXT("skyLightColor"), L.SkyLightColor); // town-perf
            Num(*Light, TEXT("torchIntensity"), L.TorchIntensity); Num(*Light, TEXT("torchRadius"), L.TorchRadius); Col(*Light, TEXT("torchColor"), L.TorchColor);
        }
    }
    if (const TSharedPtr<FJsonObject>* Perf = nullptr; Root->TryGetObjectField(TEXT("performance"), Perf) && Perf) // town-perf
    {
        (*Perf)->TryGetBoolField(TEXT("packLightShadows"), D.bPackLightShadows);
        (*Perf)->TryGetBoolField(TEXT("parallelStreaming"), D.bParallelStreaming);
        (*Perf)->TryGetBoolField(TEXT("restoreRendererCvars"), D.bRestoreRendererCvars);
        (*Perf)->TryGetBoolField(TEXT("openDoors"), D.bOpenDoors);
        if (FParse::Param(FCommandLine::Get(), TEXT("CireTownSerialLoad"))) D.bParallelStreaming = false;
        if (FParse::Param(FCommandLine::Get(), TEXT("CireTownLegacyLook"))) D.bPackLightShadows = true; // before/after captures
        double Radius = 0; if ((*Perf)->TryGetNumberField(TEXT("packFxRadius"), Radius) && FMath::IsFinite(Radius)) D.PackFxRadius = float(FMath::Max(0.0, Radius));
    }
    if (!Vec2(Root, TEXT("frameCenter"), D.FrameCenter)) { D.Error = TEXT("frameCenter needs [x, y]"); return; }
    { double Radius = 0; if (Root->TryGetNumberField(TEXT("skyRadius"), Radius) && Radius > 0) D.SkyRadius = float(Radius); }
    if (Root->HasField(TEXT("zRange")) && (!Vec2(Root, TEXT("zRange"), D.ZRange) || D.ZRange.X >= D.ZRange.Y)) { D.Error = TEXT("zRange needs [min, max]"); return; }
    if (const TSharedPtr<FJsonObject>* Explore = nullptr; Root->TryGetObjectField(TEXT("explore"), Explore) && Explore)
    {
        Vec2(*Explore, TEXT("start"), D.ExploreStart);
        const TArray<TSharedPtr<FJsonValue>>* Marks = nullptr;
        if ((*Explore)->TryGetArrayField(TEXT("landmarks"), Marks) && Marks)
            for (const auto& M : *Marks)
            {
                const TSharedPtr<FJsonObject>* O = nullptr; FCireTownLandmark L;
                if (M && M->TryGetObject(O) && O && (*O)->TryGetStringField(TEXT("name"), L.Name) && Vec2(*O, TEXT("at"), L.Local)) D.Landmarks.Add(L);
            }
    }
    // The realms must never overlap: separated by more than the town's own footprint.
    if (FVector2D::Distance(FVector2D(D.Offsets[0]), FVector2D(D.Offsets[1])) < 10000.) { D.Error = TEXT("realm offsets must be at least 100 m apart"); return; }
    D.bValid = true;
}
FIntPoint CacheKey(const FVector2D& P) { return FIntPoint(FMath::RoundToInt(P.X / 25.), FMath::RoundToInt(P.Y / 25.)); }
bool HasParam(const TCHAR* Name) { return FParse::Param(FCommandLine::Get(), Name); }
}

const FCireTownDef& CireTownMap::Def()
{
    if (!bDefLoaded) { bDefLoaded = true; Parse(GDef); if (!GDef.bValid) UE_LOG(LogCireTown, Warning, TEXT("CIRE_TOWN_DEF_INVALID %s"), *GDef.Error); }
    return GDef;
}
bool CireTownMap::PackAvailable()
{
    const auto& D = Def();
    if (!D.bValid) return false;
    for (const FString& Level : D.Levels) if (!FPackageName::DoesPackageExist(Level)) return false;
    return true;
}
bool CireTownMap::IsExplore()
{
    if (ExploreFlag < 0) ExploreFlag = HasParam(TEXT("CireExplore")) ? 1 : 0;
    return ExploreFlag == 1 && bActive;
}
bool CireTownMap::WantTown()
{
    if (HasParam(TEXT("CireProcedural"))) return false;
    if (!PackAvailable())
    {
        if (HasParam(TEXT("CireTown")) || HasParam(TEXT("CireExplore")))
            UE_LOG(LogCireTown, Warning, TEXT("CIRE_TOWN_UNAVAILABLE the Medieval Kingdom pack is not installed (%s); using the procedural town"), *Def().Error);
        return false;
    }
    if (HasParam(TEXT("CireTown")) || HasParam(TEXT("CireExplore"))) return true;
    // Probes, galleries, previews, labs and soaks were authored against the procedural town's frame.
    const FString Args = FCommandLine::Get();
    for (const TCHAR* Word : {TEXT("Probe"), TEXT("Gallery"), TEXT("Preview"), TEXT("Smoke"), TEXT("BalanceLab"), TEXT("Soak"), TEXT("Tests"), TEXT("Lab"), TEXT("Capture"), TEXT("Interface"), TEXT("Expansion")})
        if (Args.Contains(Word)) return false;
    return Def().bDefaultMap;
}
bool CireTownMap::IsActive() { return bActive; }
void CireTownMap::SetActive(bool bNew)
{
    if (bNew && !Def().bValid) bNew = false;
    if (bActive == bNew) return;
    bActive = bNew; GroundCache.Reset();
    CireLanePath::Reload();
    UE_LOG(LogCireTown, Display, TEXT("CIRE_TOWN_FRAME %s routes=%s"), bActive ? TEXT("castletown") : TEXT("procedural"), *CireLanePath::DataPath());
}
FVector CireTownMap::RealmOrigin(int32 Team)
{
    Team = FMath::Clamp(Team, 0, 1);
    if (!bActive) return LegacyOrigins[Team];
    const auto& D = Def();
    return D.Offsets[Team] + FVector(D.FrameCenter, 0);
}
FVector2D CireTownMap::WorldZRange(int32 Team)
{
    if (!bActive) return FVector2D(-400, 900);
    const double Z = Def().Offsets[FMath::Clamp(Team, 0, 1)].Z;
    return Def().ZRange + FVector2D(Z, Z);
}
void CireTownMap::LogSceneStats(UWorld* World, const TCHAR* When)
{
    // town-perf: what each realm copy costs (rendering, lights, ticking), to profile against rather than guess.
    if (!World || !bActive) return;
    struct FRealmCount { int32 NavRelevant = 0, NavSmall = 0, NavNoCollision = 0, Levels = 0, Actors = 0, Prims = 0, Instances = 0, Nanite = 0, Skel = 0, Lights = 0, MovableLights = 0, ShadowLights = 0, Niagara = 0, Cascade = 0, TickingActors = 0, TickingComps = 0; };
    FRealmCount C[2];
    TMap<FString, int32> Tickers;
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level || Level == World->PersistentLevel || !Level->bIsVisible) continue;
        int32 Realm = -1;
        for (AActor* A : Level->Actors)
        {
            if (!A) continue;
            const int32 T = RealmAt(A->GetActorLocation());
            if (Realm < 0) { Realm = T; ++C[T].Levels; }
            FRealmCount& R = C[T];
            ++R.Actors;
            if (A->IsActorTickEnabled() && A->PrimaryActorTick.bCanEverTick) { ++R.TickingActors; ++Tickers.FindOrAdd(A->GetClass()->GetName()); }
            TInlineComponentArray<UActorComponent*> Components(A);
            for (UActorComponent* Comp : Components)
            {
                if (Comp->IsComponentTickEnabled() && Comp->PrimaryComponentTick.bCanEverTick) { ++R.TickingComps; ++Tickers.FindOrAdd(Comp->GetClass()->GetName()); }
                if (auto* Light = Cast<ULocalLightComponent>(Comp))
                {
                    if (!Light->IsVisible()) continue;
                    ++R.Lights; R.MovableLights += Light->Mobility == EComponentMobility::Movable ? 1 : 0; R.ShadowLights += Light->CastShadows ? 1 : 0;
                }
                else if (auto* Prim = Cast<UPrimitiveComponent>(Comp))
                {
                    ++R.Prims;
                    if (Prim->CanEverAffectNavigation())
                    {
                        ++R.NavRelevant;
                        if (!Prim->IsCollisionEnabled()) ++R.NavNoCollision;
                        else if (Prim->Bounds.BoxExtent.GetMax() < 30.f) ++R.NavSmall;
                    }
                    if (auto* ISM = Cast<UInstancedStaticMeshComponent>(Prim)) R.Instances += ISM->GetInstanceCount();
                    if (auto* SM = Cast<UStaticMeshComponent>(Prim)) R.Nanite += SM->GetStaticMesh() && SM->GetStaticMesh()->IsNaniteEnabled() ? 1 : 0;
                    if (Prim->IsA<USkinnedMeshComponent>()) ++R.Skel;
                    const FString Class = Prim->GetClass()->GetName();
                    if (Class.Contains(TEXT("Niagara"))) ++R.Niagara;
                    else if (Class == TEXT("ParticleSystemComponent")) ++R.Cascade;
                }
            }
        }
    }
    for (int32 T = 0; T < 2; ++T)
        UE_LOG(LogCireTown, Display, TEXT("CIRE_TOWN_SCENE_STATS when=%s realm=%d nav_relevant=%d nav_small=%d nav_nocollision=%d levels=%d actors=%d prims=%d ism_instances=%d nanite_sm=%d skinned=%d local_lights=%d movable=%d shadowed=%d niagara=%d cascade=%d ticking_actors=%d ticking_components=%d"),
            When, T, C[T].NavRelevant, C[T].NavSmall, C[T].NavNoCollision, C[T].Levels, C[T].Actors, C[T].Prims, C[T].Instances, C[T].Nanite, C[T].Skel, C[T].Lights, C[T].MovableLights, C[T].ShadowLights, C[T].Niagara, C[T].Cascade, C[T].TickingActors, C[T].TickingComps);
    Tickers.ValueSort([](int32 A, int32 B) { return A > B; });
    FString Top; int32 N = 0;
    for (const auto& P : Tickers) { if (N++ >= 12) break; Top += FString::Printf(TEXT(" %s=%d"), *P.Key, P.Value); }
    UE_LOG(LogCireTown, Display, TEXT("CIRE_TOWN_SCENE_TICKERS when=%s%s"), When, *Top);
}
int32 CireTownMap::PrepareRealmLevels(UWorld* World)
{
    // Runs after the realms (and the pack's nested Level Instances: the houses, the castle pieces) have streamed in.
    // 1. The pack's own sky, fog, grading volumes, lights-of-the-sun, cinematic cameras/sequences and player start are
    //    removed: each realm brings its own lighting (SL_Lighting is never streamed).
    // 2. Lighting channels keep the realms' suns apart: realm 0 stays on channel 0 (the default), every primitive and
    //    local light of the realm-1 copy moves to channel 1, lit only by the realm-1 sun.
    if (!World || !bActive) return 0;
    static const TSet<FString> Strip = {TEXT("SkyAtmosphere"), TEXT("PostProcessVolume"), TEXT("CineCameraActor"), TEXT("LevelSequenceActor"),
        TEXT("PlayerStart"), TEXT("BP_Optimizer_C"), TEXT("DirectionalLight"), TEXT("SkyLight"), TEXT("ExponentialHeightFog"), TEXT("VolumetricCloud")};
    int32 NewLevels = 0, Channelled = 0, Lights = 0, Removed = 0, NavMuted = 0, Unshadowed = 0, DoorsOpened = 0, CameraClear = 0;
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level || Level == World->PersistentLevel || !Level->bIsVisible || PreparedLevels.Contains(Level)) continue;
        PreparedLevels.Add(Level); ++NewLevels;
        TArray<AActor*> Doomed;
        for (AActor* A : Level->Actors)
        {
            if (!A) continue;
            if (Strip.Contains(A->GetClass()->GetName())) { Doomed.Add(A); continue; }
            const bool bRealm1 = RealmAt(A->GetActorLocation()) == 1;
            TInlineComponentArray<UActorComponent*> Components(A);
            for (UActorComponent* C : Components)
            {
                if (auto* Light = Cast<ULocalLightComponent>(C))
                {
                    if (bRealm1) { Light->LightingChannels.bChannel0 = false; Light->LightingChannels.bChannel1 = true; Light->MarkRenderStateDirty(); ++Lights; }
                    // town-perf: ~200 of each realm's ~350 pack lights (torches, lanterns, chandeliers) are movable and cast
                    // cube-map shadows, ~20 ms of GPU shadow depths per frame. They keep their light, not their shadows.
                    if (!Def().bPackLightShadows && Light->CastShadows) { Light->SetCastShadows(false); ++Unshadowed; }
                }
                else if (auto* Prim = Cast<UPrimitiveComponent>(C))
                {
                    if (bRealm1) { Prim->SetLightingChannels(false, true, false); ++Channelled; }
                    // town-perf: market awnings, cloths, canopies, banners and flags overhang the streets; the camera boom
                    // (collision test on ECC_Camera) snapped in under them. They still block units and projectiles.
                    if (const auto* SMC = Cast<UStaticMeshComponent>(Prim); (SMC && SMC->GetStaticMesh()) || Prim->IsA<USkinnedMeshComponent>())
                    {
                        const FString Mesh = SMC && SMC->GetStaticMesh() ? SMC->GetStaticMesh()->GetName() : Prim->GetName();
                        for (const TCHAR* Word : {TEXT("Cloth"), TEXT("Awning"), TEXT("Canopy"), TEXT("Tarp"), TEXT("Banner"), TEXT("Flag"), TEXT("Tent")})
                            if (Mesh.Contains(Word)) { Prim->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore); ++CameraClear; break; }
                    }
                    if (Def().bOpenDoors && IsDoorLeaf(Cast<UStaticMeshComponent>(Prim)))
                    {
                        Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision); Prim->SetCanEverAffectNavigation(false);
                        Prim->SetVisibility(false); Prim->SetHiddenInGame(true); ++DoorsOpened;
                        continue;
                    }
                    // Swaying banners, ropes, chains and flags would keep the runtime navmesh dirty forever.
                    if (Prim->Mobility == EComponentMobility::Movable && Prim->CanEverAffectNavigation()) { Prim->SetCanEverAffectNavigation(false); ++NavMuted; }
                }
            }
        }
        for (AActor* A : Doomed) if (A->Destroy()) ++Removed; else A->SetActorHiddenInGame(true);
    }
    for (auto It = PreparedLevels.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
    if (Def().bRestoreRendererCvars && !HasParam(TEXT("CireTownPackShadowCvars")))
        if (const int32 Restored = RestoreRendererCvars()) UE_LOG(LogCireTown, Display, TEXT("CIRE_TOWN_RENDERER_RESTORED cvars=%d (the pack's optimizer had turned virtual shadow maps off)"), Restored);
    if (NewLevels > 0)
        UE_LOG(LogCireTown, Display, TEXT("CIRE_TOWN_REALM_PREPARED levels=%d realm1_primitives=%d realm1_lights=%d removed=%d movable_nav_off=%d lights_unshadowed=%d doors_opened=%d camera_clear=%d"), NewLevels, Channelled, Lights, Removed, NavMuted, Unshadowed, DoorsOpened, CameraClear);
    return NewLevels;
}
bool CireTownMap::LoadRealms(UWorld* World)
{
    if (!World || !bActive) return false;
    auto& Entry = Loaded.FindOrAdd(World);
    if (!Entry.Levels.IsEmpty()) return true;
    const double Started = FPlatformTime::Seconds();
    const auto& D = Def();
    SaveRendererCvars(); // town-perf
    int32 Count = 0;
    for (int32 Team = 0; Team < 2; ++Team)
        for (const FString& Level : D.Levels)
        {
            // SL_Landscape is a World Partition level whose landscape and WorldDataLayers are external actors
            // (Content/__ExternalActors__/CastleTown). Without them the engine asserts in -game, so skip it.
            if (Level.Contains(TEXT("SL_Landscape")) && !FPaths::DirectoryExists(FPaths::ProjectContentDir() / TEXT("__ExternalActors__/CastleTown/Levels/SubLevels/SL_Landscape")))
            {
                if (Team == 0) UE_LOG(LogCireTown, Warning, TEXT("CIRE_TOWN_NO_LANDSCAPE the pack's external actors are missing (Content/__ExternalActors__/CastleTown); the town has no terrain"));
                continue;
            }
            // Deterministic names: server and clients must stream the same level package names.
            const FString Name = FString::Printf(TEXT("%s_CireRealm%d"), *FPackageName::GetShortName(Level), Team);
            bool bOk = false;
            ULevelStreamingDynamic* S = ULevelStreamingDynamic::LoadLevelInstance(World, Level, D.Offsets[Team], FRotator::ZeroRotator, bOk, Name);
            if (!bOk || !S) { UE_LOG(LogCireTown, Error, TEXT("CIRE_TOWN_LEVEL_FAIL %s realm=%d"), *Level, Team); continue; }
            // town-perf: with parallelStreaming every level is requested before one flush, so their packages load together
            // instead of one blocking flush per level.
            S->SetShouldBeLoaded(true); S->SetShouldBeVisible(true); S->bShouldBlockOnLoad = !D.bParallelStreaming;
            Entry.Levels.Add(S); ++Count;
        }
    World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    // The pack's nested Level Instances (houses, castle pieces) register on the first flush and would otherwise stream in
    // over the next minutes, dirtying the navmesh the whole time: load them now, before the navmesh is built.
    int32 Pending = 0, Passes = 0, Known = -1;
    for (; Passes < 12; ++Passes)
    {
        const int32 Before = World->GetStreamingLevels().Num();
        if (auto* LevelInstances = World->GetSubsystem<ULevelInstanceSubsystem>()) LevelInstances->OnUpdateStreamingState();
        // town-perf: SL_Landscape is a World Partition level; its cells (the terrain, water) otherwise stream in on the first
        // frame, after the navmesh was built without the ground, and dirty both realms for a second full rebuild.
        if (auto* Partitions = World->GetSubsystem<UWorldPartitionSubsystem>()) Partitions->OnUpdateStreamingState();
        World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
        Pending = 0;
        for (ULevelStreaming* L : World->GetStreamingLevels()) if (L && L->ShouldBeVisible() && !L->IsLevelVisible()) ++Pending;
        // Settled when nothing is pending and the pass registered no new level (World Partition cells appear one pass late).
        if (Pending == 0 && Passes > 0 && World->GetStreamingLevels().Num() == Before && Before == Known) break;
        Known = World->GetStreamingLevels().Num();
    }
    UE_LOG(LogCireTown, Display, TEXT("CIRE_TOWN_NESTED_LEVELS streaming=%d pending=%d passes=%d"), World->GetStreamingLevels().Num(), Pending, Passes + 1);
    TownWorld = World; GroundCache.Reset();
    static FDelegateHandle Added;
    if (!Added.IsValid())
        Added = FWorldDelegates::LevelAddedToWorld.AddLambda([](ULevel* Level, UWorld* In) { if (Level && In && bActive && Loaded.Contains(In)) CireTownMap::PrepareRealmLevels(In); });
    CireTownMap::PrepareRealmLevels(World);
    int32 Visible = 0;
    for (const auto& S : Entry.Levels) if (S.IsValid() && S->GetLoadedLevel() && S->GetLoadedLevel()->bIsVisible) ++Visible;
    LoadMs = (FPlatformTime::Seconds() - Started) * 1000.0; // town-perf
    UE_LOG(LogCireTown, Display, TEXT("CIRE_TOWN_REALMS_LOADED netmode=%d levels=%d visible=%d ms=%.0f"), static_cast<int32>(World->GetNetMode()), Count, Visible,
        (FPlatformTime::Seconds() - Started) * 1000.0);
    return Visible == Count && Count > 0;
}
double CireTownMap::LastLoadMs() { return LoadMs; }
int32 CireTownMap::LoadedLevels(const UWorld* World)
{
    const auto* Entry = Loaded.Find(const_cast<UWorld*>(World));
    int32 Visible = 0;
    if (Entry) for (const auto& S : Entry->Levels) if (S.IsValid() && S->GetLoadedLevel() && S->GetLoadedLevel()->bIsVisible) ++Visible;
    return Visible;
}
float CireTownMap::Ground(const UWorld* World, const FVector2D& XY)
{
    if (!bActive) return 0.f;
    if (!World) World = TownWorld.Get();
    const FIntPoint Key = CacheKey(XY);
    if (const float* Found = GroundCache.Find(Key)) return *Found;
    // Which realm: the nearer offset decides the Z window.
    const auto& D = Def();
    const int32 Team = FVector2D::DistSquared(XY, FVector2D(D.Offsets[0]) + D.FrameCenter) <= FVector2D::DistSquared(XY, FVector2D(D.Offsets[1]) + D.FrameCenter) ? 0 : 1;
    const FVector2D Range = WorldZRange(Team);
    float Z = D.Offsets[Team].Z;
    if (World && LoadedLevels(World) > 0)
    {
        // Top-down, every static hit: the ground is the first walkable surface (skips pitched roofs, canopies and walls).
        TArray<FHitResult> Hits;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(CireTownGround), false);
        World->LineTraceMultiByObjectType(Hits, FVector(XY, Range.Y), FVector(XY, Range.X), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
        Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.ImpactPoint.Z > B.ImpactPoint.Z; });
        const FHitResult* Pick = nullptr;
        for (const FHitResult& H : Hits) if (H.ImpactNormal.Z >= .7f) { Pick = &H; break; }
        if (!Pick && Hits.Num()) Pick = &Hits[0];
        if (Pick) { Z = Pick->ImpactPoint.Z; GroundCache.Add(Key, Z); }
    }
    return Z;
}
void CireTownMap::InitializeServer(ACireGameMode* Mode)
{
    if (!Mode || !Mode->GetWorld()) return;
    SetActive(WantTown());
    if (!bActive) return;
    LoadRealms(Mode->GetWorld());
}

void CireTownMap::PlaceHeroes(ACireGameMode* Mode)
{
    if (!bActive || !Mode) return;
    int32 Slot[2] = {0, 0};
    for (ACireHero* H : Mode->Heroes)
    {
        if (!IsValid(H)) continue;
        const int32 Team = FMath::Clamp(H->TeamId, 0, 1);
        H->HomePosition = Mode->BasePosition(Team);
        H->GetCharacterMovement()->StopMovementImmediately();
        // layout-wiring: the Player Spawn markers (with facing) when authored.
        const FTransform Spawn = CireLanePath::PlayerSpawnTransform(Mode->GetWorld(), Team, Slot[Team]++);
        H->SetActorLocation(Spawn.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
        H->SetActorRotation(Spawn.Rotator());
    }
}

int32 CireTownMap::RealmAt(const FVector& W)
{
    if (!bActive) return W.Y < 0 ? 0 : 1;
    const FVector2D P(W);
    return FVector2D::DistSquared(P, FVector2D(RealmOrigin(0))) <= FVector2D::DistSquared(P, FVector2D(RealmOrigin(1))) ? 0 : 1;
}
namespace { TMap<TWeakObjectPtr<AActor>, int32> ActorRealm; }
void CireTownMap::ApplyActorRealm(AActor* Actor)
{
    if (!bActive || !IsValid(Actor)) return;
    int32 Team = RealmAt(Actor->GetActorLocation());
    {   // Outside both towns (the PvP arena) everything is on the default channel 0.
        const auto& R = CireLanePath::Get(Actor->GetWorld());
        const FVector2D L = CireLanePath::ToLocal(Team, Actor->GetActorLocation());
        if (L.X < R.MinX - 3000 || L.X > R.MaxX + 3000 || FMath::Abs(L.Y) > R.HalfWidth + 3000) Team = 0;
    }
    int32& Known = ActorRealm.FindOrAdd(Actor, -1);
    if (Known == Team) return;
    Known = Team;
    if (ActorRealm.Num() > 4096) for (auto It = ActorRealm.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    TInlineComponentArray<UPrimitiveComponent*> Prims(Actor);
    for (UPrimitiveComponent* P : Prims) P->SetLightingChannels(Team == 0, Team == 1, false);
}
namespace
{
// town-perf: what each realm's own lighting spawned (per-view culling hides the far realm's copy).
struct FRealmVisuals { TWeakObjectPtr<ADirectionalLight> Sun; TWeakObjectPtr<UStaticMeshComponent> Dome; TArray<TWeakObjectPtr<APointLight>> Fills; };
FRealmVisuals RealmVisuals[2];
double RealmSkyRadius = 0;
}
void CireTownMap::BuildRealmLighting(AActor* Owner)
{
    if (!bActive || !Owner || !Owner->GetWorld()) return;
    UWorld* World = Owner->GetWorld();
    if (World->GetNetMode() == NM_DedicatedServer) return; // town-perf: nothing is rendered there
    const auto& D = Def();
    const double Separation = FVector2D::Distance(FVector2D(RealmOrigin(0)), FVector2D(RealmOrigin(1)));
    const FCireBattlefieldRoutes& R = CireLanePath::Get(World);
    const double Reach = FVector2D(FMath::Max(-R.MinX, R.MaxX), R.HalfWidth).Size();
    // Each realm sits inside its own sky sphere, smaller than half the separation, so neither camera ever sees the other
    // realm or its sky.
    const double SkyRadius = RealmSkyRadius = D.SkyRadius > 0 ? FMath::Min<double>(D.SkyRadius, Separation * .49) : FMath::Max(Reach * 1.25, FMath::Min(Separation * .48, Reach * 3.0));
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, *(D.Pack + TEXT("/SkySphere/Meshes/SM_sphere.SM_sphere")));
    for (int32 Team = 0; Team < 2; ++Team)
    {
        const FCireRealmLighting& L = D.Lighting[Team];
        const FVector Centre = FVector(FVector2D(RealmOrigin(Team)), D.Offsets[Team].Z);
        if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(Centre + FVector(0, 0, 4000), FRotator(L.SunPitch, L.SunYaw, 0)))
        {
            auto* C = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
            C->SetMobility(EComponentMobility::Movable);
            C->SetIntensity(L.SunIntensity); C->SetLightColor(L.SunColor);
            C->LightingChannels.bChannel0 = Team == 0; C->LightingChannels.bChannel1 = Team == 1; C->LightingChannels.bChannel2 = false;
            C->ForwardShadingPriority = Team == 0 ? 1 : 0; // translucency / fog / forward use one sun: the daylight one
            C->MarkRenderStateDirty();
            RealmVisuals[Team].Sun = Sun; // town-perf
        }
        UMaterialInterface* SkyMat = L.SkyMaterial.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *L.SkyMaterial);
        if (Sphere && SkyMat)
        {
            auto* Dome = NewObject<UStaticMeshComponent>(Owner, *FString::Printf(TEXT("RealmSky%d"), Team));
            Dome->SetupAttachment(Owner->GetRootComponent()); Dome->SetStaticMesh(Sphere); Dome->SetMaterial(0, SkyMat);
            const float MeshRadius = FMath::Max(1.f, Sphere->GetBounds().SphereRadius);
            Dome->SetWorldLocation(Centre); Dome->SetWorldScale3D(FVector(SkyRadius / MeshRadius));
            Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision); Dome->SetCastShadow(false);
            Dome->bAffectDistanceFieldLighting = false; Dome->SetVisibleInRayTracing(false); Dome->bAffectDynamicIndirectLighting = false;
            Dome->RegisterComponent(); Owner->AddInstanceComponent(Dome);
            RealmVisuals[Team].Dome = Dome; // town-perf
        }
        // Bounded colour grade for the realm (priority above the town grade, below the arena grade).
        auto* Box = NewObject<UBoxComponent>(Owner, *FString::Printf(TEXT("RealmGradeBounds%d"), Team));
        Box->SetupAttachment(Owner->GetRootComponent()); Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Box->SetWorldLocation(Centre); Box->SetBoxExtent(FVector(SkyRadius, SkyRadius, SkyRadius)); Box->SetHiddenInGame(true);
        Box->RegisterComponent(); Owner->AddInstanceComponent(Box);
        auto* Grade = NewObject<UPostProcessComponent>(Owner, *FString::Printf(TEXT("RealmGrade%d"), Team));
        Grade->SetupAttachment(Box); Grade->bUnbound = false; Grade->Priority = 1.f; Grade->BlendRadius = 100.f; Grade->BlendWeight = 1.f;
        auto& P = Grade->Settings;
        P.bOverride_AutoExposureBias = true; P.AutoExposureBias = L.ExposureBias;
        P.bOverride_ColorSaturation = true; P.ColorSaturation = FVector4(L.Saturation, L.Saturation, L.Saturation, 1.f);
        P.bOverride_ColorGain = true; P.ColorGain = FVector4(L.Tint.R, L.Tint.G, L.Tint.B, 1.f);
        Grade->RegisterComponent(); Owner->AddInstanceComponent(Grade);
    }
    // Warm fill at the pack's torches and lanterns of any realm that asks for it (the night side).
    int32 Fills = 0;
    for (int32 Team = 0; Team < 2; ++Team)
    {
        const FCireRealmLighting& L = D.Lighting[Team];
        if (L.TorchIntensity <= 0) continue;
        TArray<FVector> Spots;
        const auto* Entry = Loaded.Find(World);
        if (!Entry) continue;
        for (int32 I = Team * D.Levels.Num(); I < (Team + 1) * D.Levels.Num() && I < Entry->Levels.Num(); ++I)
        {
            const ULevel* Level = Entry->Levels[I].IsValid() ? Entry->Levels[I]->GetLoadedLevel() : nullptr;
            if (!Level) continue;
            for (AActor* A : Level->Actors)
            {
                if (!A) continue;
                const FString Class = A->GetClass()->GetName();
                if (!Class.Contains(TEXT("Torch")) && !Class.Contains(TEXT("Lantern")) && !Class.Contains(TEXT("Chandelier"))) continue;
                const FVector At = A->GetActorLocation();
                bool bNear = false; for (const FVector& S : Spots) if (FVector::DistSquared(S, At) < FMath::Square(L.TorchRadius * .6f)) { bNear = true; break; }
                if (!bNear) Spots.Add(At);
            }
        }
        for (const FVector& At : Spots)
        {
            if (Fills >= 160) break;
            if (APointLight* Fill = World->SpawnActor<APointLight>(At + FVector(0, 0, 60), FRotator::ZeroRotator))
            {
                UPointLightComponent* C = Fill->PointLightComponent;
                C->SetMobility(EComponentMobility::Movable);
                C->SetIntensity(L.TorchIntensity); C->SetLightColor(L.TorchColor); C->SetAttenuationRadius(L.TorchRadius); C->SetCastShadows(false);
                C->LightingChannels.bChannel0 = Team == 0; C->LightingChannels.bChannel1 = Team == 1; C->MarkRenderStateDirty();
                ++Fills; RealmVisuals[Team].Fills.Add(Fill); // town-perf
            }
        }
    }
    // town-perf: interior fill. The pack's interior light lived in SL_Lighting (never streamed), so the houses were black
    // inside. One warm, non-shadowing point light just inside every building doorway (the side with a roof), in both
    // realms at the same spot; each realm sets its own brightness and colour (warmer at night).
    int32 Interior = 0;
    {
        TArray<FCireDoorway> Doors; FindDoorways(World, Doors);
        TArray<FVector> Placed[2];
        for (const FCireDoorway& Door : Doors)
        {
            const int32 Team = FMath::Clamp(Door.Realm, 0, 1);
            const FCireRealmLighting& L = D.Lighting[Team];
            if (!Door.bRoofed || L.InteriorIntensity <= 0) continue;
            const FVector At = Door.Center + Door.Through * 260.f + FVector(0, 0, 220.f);
            bool bNear = false; for (const FVector& P : Placed[Team]) if (FVector::DistSquared(P, At) < FMath::Square(450.f)) { bNear = true; break; }
            if (bNear) continue;
            Placed[Team].Add(At);
            if (APointLight* Fill = World->SpawnActor<APointLight>(At, FRotator::ZeroRotator))
            {
                UPointLightComponent* C = Fill->PointLightComponent;
                C->SetMobility(EComponentMobility::Movable);
                C->SetIntensity(L.InteriorIntensity); C->SetLightColor(L.InteriorColor); C->SetAttenuationRadius(L.InteriorRadius);
                C->SetCastShadows(false); C->SetSourceRadius(20.f);
                C->LightingChannels.bChannel0 = Team == 0; C->LightingChannels.bChannel1 = Team == 1; C->MarkRenderStateDirty();
                RealmVisuals[Team].Fills.Add(Fill); ++Interior;
            }
        }
    }
    UE_LOG(LogCireTown, Display, TEXT("CIRE_TOWN_LIGHTING realms=%s/%s sky_radius=%.0f separation=%.0f torch_fills=%d interior_fills=%d"), *D.Lighting[0].Name, *D.Lighting[1].Name, SkyRadius, Separation, Fills, Interior);
}


// ------------------------------------------------------------------ town-perf: doorways (interior light, door probe)
void CireTownMap::FindDoorways(UWorld* World, TArray<FCireDoorway>& Out)
{
    // Every pack mesh with "Door" in its name (door leaves, frames, wall pieces with a door, castle wall doors), one per
    // 1.5 m. The side with something solid overhead is the inside.
    Out.Reset();
    if (!World || !bActive) return;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CireDoorRoof), true);
    auto Roofed = [&](const FVector& At) { FHitResult Hit; return World->LineTraceSingleByChannel(Hit, At + FVector(0, 0, 120), At + FVector(0, 0, 800), ECC_Visibility, Params); };
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level || Level == World->PersistentLevel || !Level->bIsVisible) continue;
        for (AActor* A : Level->Actors)
        {
            if (!A) continue;
            TInlineComponentArray<UStaticMeshComponent*> Meshes(A);
            for (UStaticMeshComponent* C : Meshes)
            {
                const UStaticMesh* SM = C->GetStaticMesh();
                if (!SM || !SM->GetName().Contains(TEXT("Door"))) continue;
                const FBoxSphereBounds Local = SM->GetBounds();
                const FVector Ext = Local.BoxExtent * C->GetComponentScale().GetAbs();
                if (Ext.Z < 80.f) continue; // trims, handles, hinges
                const FTransform T = C->GetComponentTransform();
                FCireDoorway D; D.Mesh = SM->GetName(); D.Level = Level->GetOuter() ? Level->GetOuter()->GetName() : TEXT("?");
                D.bLeaf = IsDoorLeaf(C);
                D.Center = T.TransformPosition(Local.Origin); D.Realm = RealmAt(D.Center);
                FVector Axis = T.GetUnitAxis(Ext.X <= Ext.Y ? EAxis::X : EAxis::Y); Axis.Z = 0;
                D.Through = Axis.GetSafeNormal();
                D.Center.Z -= Ext.Z; // the threshold
                if (D.Through.IsNearlyZero()) continue;
                bool bDup = false;
                for (const FCireDoorway& O : Out) if (FVector::DistSquared(O.Center, D.Center) < 150.f * 150.f) { bDup = true; break; }
                if (bDup) continue;
                const bool bA = Roofed(D.Center - D.Through * 170.f), bB = Roofed(D.Center + D.Through * 170.f);
                D.bRoofed = bA != bB;           // exactly one side covered: a building's door, not an arch or a gate passage
                if (bA && !bB) D.Through = -D.Through; // Through points inside
                Out.Add(D);
            }
        }
    }
}

// ------------------------------------------------------------------ town-perf: per-view realm culling
namespace
{
TAutoConsoleVariable<int32> CVarBothRealms(TEXT("cire.TownRenderBothRealms"), 0,
    TEXT("0 (default) = each viewer renders and animates only the realm its camera is in; 1 = render both copies (profiling A/B)."));
struct FCulled { TWeakObjectPtr<UActorComponent> Component; TWeakObjectPtr<AActor> Actor; bool bVisible = false, bTick = false, bPaused = false; };
struct FViewState
{
    int32 Realm = -1;                 // realm the local view is in (-1 none yet, 2 = dedicated server: no view)
    TArray<FCulled> Culled;
    TSet<TWeakObjectPtr<ULevel>> Done;
    TWeakObjectPtr<ASkyLight> Sky;
    float SkyIntensity = -1; FLinearColor SkyColor = FLinearColor::White; bool bSkyRealtime = true;
    // The viewed realm's pack particles and cloth: simulated only near the camera (Def().PackFxRadius).
    struct FNear { TWeakObjectPtr<UActorComponent> Component; bool bOff = false; };
    TArray<FNear> Near;
    int32 NearCursor = 0;
};
TMap<TWeakObjectPtr<UWorld>, FViewState> Views;

bool IsCosmeticTicker(const UActorComponent* C)
{
    // Animation, cloth (the market awnings and flags are Chaos cloth) and particles: nothing gameplay reads.
    return C->IsA<USkinnedMeshComponent>() || C->IsA<UFXSystemComponent>();
}
void Cull(FViewState& V, AActor* A, bool bRender)
{
    // bRender false (dedicated server): only stop the cosmetic ticking, never touch visibility or collision.
    if (bRender && A->IsActorTickEnabled() && A->PrimaryActorTick.bCanEverTick)
    {
        FCulled C; C.Actor = A; C.bTick = true; A->SetActorTickEnabled(false); V.Culled.Add(C);
    }
    TInlineComponentArray<UActorComponent*> Components(A);
    for (UActorComponent* Comp : Components)
    {
        FCulled C; C.Component = Comp;
        auto* Scene = Cast<USceneComponent>(Comp);
        if (bRender && Scene && (Scene->IsA<UPrimitiveComponent>() || Scene->IsA<ULightComponentBase>()) && Scene->IsVisible())
        { C.bVisible = true; Scene->SetVisibility(false, false); }
        if (IsCosmeticTicker(Comp) && Comp->IsComponentTickEnabled()) { C.bTick = true; Comp->SetComponentTickEnabled(false); }
        if (auto* Fx = Cast<UNiagaraComponent>(Comp); Fx && Fx->IsActive() && !Fx->IsPaused()) { C.bPaused = true; Fx->SetPaused(true); }
        if (C.bVisible || C.bTick || C.bPaused) V.Culled.Add(C);
    }
}
void WakeNear(FViewState& V)
{
    for (auto& N : V.Near)
    {
        UActorComponent* Comp = N.Component.Get();
        if (!Comp || !N.bOff) continue;
        if (auto* Fx = Cast<UNiagaraComponent>(Comp)) Fx->SetPaused(false); else Comp->SetComponentTickEnabled(true);
    }
    V.Near.Reset(); V.NearCursor = 0;
}
void TickNear(FViewState& V, const FVector& Cam)
{
    // Pack torches, braziers, smoke and cloth awnings far from the camera freeze (they keep drawing their last frame) and
    // resume when the camera comes back: ~800 particle systems and ~50 cloth meshes per realm otherwise all simulate.
    const float Radius = CireTownMap::Def().PackFxRadius;
    if (Radius <= 0 || V.Near.IsEmpty()) return;
    const int32 Budget = FMath::Min(V.Near.Num(), 400);
    for (int32 I = 0; I < Budget; ++I)
    {
        auto& N = V.Near[V.NearCursor++ % V.Near.Num()];
        auto* Scene = Cast<USceneComponent>(N.Component.Get());
        if (!Scene) continue;
        const double D = FVector::Dist(Scene->GetComponentLocation(), Cam);
        const bool bOff = N.bOff ? D > Radius * .9 : D > Radius; // hysteresis
        if (bOff == N.bOff) continue;
        auto* Fx = Cast<UNiagaraComponent>(Scene);
        if (Fx) { if (bOff && (!Fx->IsActive() || Fx->IsPaused())) continue; Fx->SetPaused(bOff); }
        else { if (bOff && !Scene->IsComponentTickEnabled()) continue; Scene->SetComponentTickEnabled(!bOff); }
        N.bOff = bOff;
    }
    V.NearCursor %= FMath::Max(1, V.Near.Num());
}
void RestoreAll(FViewState& V)
{
    WakeNear(V);
    for (const FCulled& C : V.Culled)
    {
        if (AActor* A = C.Actor.Get()) { if (C.bTick) A->SetActorTickEnabled(true); continue; }
        UActorComponent* Comp = C.Component.Get();
        if (!Comp) continue;
        if (C.bVisible) if (auto* Scene = Cast<USceneComponent>(Comp)) Scene->SetVisibility(true, false);
        if (C.bTick) Comp->SetComponentTickEnabled(true);
        if (C.bPaused) if (auto* Fx = Cast<UNiagaraComponent>(Comp)) Fx->SetPaused(false);
    }
    V.Culled.Reset(); V.Done.Reset();
}
void CullLevels(UWorld* World, FViewState& V)
{
    const bool bRender = V.Realm != 2;
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level || Level == World->PersistentLevel || !Level->bIsVisible || V.Done.Contains(Level)) continue;
        V.Done.Add(Level);
        for (AActor* A : Level->Actors)
        {
            if (!A) continue;
            if (V.Realm == 2 || CireTownMap::RealmAt(A->GetActorLocation()) != V.Realm) { Cull(V, A, bRender); continue; }
            TInlineComponentArray<UActorComponent*> Components(A);
            for (UActorComponent* Comp : Components) if (IsCosmeticTicker(Comp)) V.Near.Add({Comp, false});
        }
    }
    for (auto It = V.Done.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
}
void ApplySky(UWorld* World, FViewState& V, int32 Realm)
{
    // One sky light, re-captured for the realm being viewed: it moves inside that realm's sky sphere and captures it
    // (only geometry beyond SkyDistanceThreshold, i.e. the sphere), so DAYLIGHT gets the day HDRI's ambient and DARKNIGHT the
    // night one. The old real-time capture found no sky material at all and left every shaded facade near black.
    if (HasParam(TEXT("CireTownLegacyLook"))) return; // before/after captures: the shared real-time sky light as it was
    if (!V.Sky.IsValid()) for (TCireActorIterator<ASkyLight> It(World); It; ++It) { V.Sky = *It; break; }
    ASkyLight* Sky = V.Sky.Get();
    if (!Sky) return;
    USkyLightComponent* C = Sky->GetLightComponent();
    if (V.SkyIntensity < 0) { V.SkyIntensity = C->Intensity; V.SkyColor = C->GetLightColor(); V.bSkyRealtime = C->bRealTimeCapture; }
    const FCireRealmLighting& L = CireTownMap::Def().Lighting[Realm];
    Sky->SetActorLocation(CireTownMap::RealmOrigin(Realm) + FVector(0, 0, 3000));
    C->bRealTimeCapture = false;
    C->SourceType = SLS_CapturedScene;
    C->SkyDistanceThreshold = FMath::Max(100000.f, float(RealmSkyRadius) * .5f);
    C->bLowerHemisphereIsBlack = false;
    C->SetIntensity(L.SkyLightIntensity); C->SetLightColor(L.SkyLightColor);
    C->MarkRenderStateDirty();
    C->RecaptureSky();
}
}

void CireTownMap::UpdateLocalView(UWorld* World)
{
    if (!bActive || !World || !World->IsGameWorld()) return;
    // The pack's optimizer may run again as late Level Instances begin play: keep the project's shadow settings.
    if (Def().bRestoreRendererCvars && !HasParam(TEXT("CireTownPackShadowCvars")) && World->GetNetMode() != NM_DedicatedServer && RestoreRendererCvars())
        UE_LOG(LogCireTown, Display, TEXT("CIRE_TOWN_RENDERER_RESTORED late"));
    FViewState& V = Views.FindOrAdd(World);
    const bool bBoth = CVarBothRealms.GetValueOnGameThread() != 0;
    int32 Want = V.Realm;
    FVector Cam = FVector::ZeroVector; bool bCam = false;
    if (World->GetNetMode() == NM_DedicatedServer) Want = 2;
    else if (bBoth) Want = -1;
    else if (APlayerController* PC = World->GetFirstPlayerController(); PC && PC->IsLocalController())
    {
        // The realm the camera is in; outside both skies (the PvP arena, menus) the last realm stays as it was.
        Cam = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : (PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector);
        bCam = true;
        const int32 R = RealmAt(Cam);
        const double Reach = RealmSkyRadius > 0 ? RealmSkyRadius : 0.45 * FVector2D::Distance(FVector2D(RealmOrigin(0)), FVector2D(RealmOrigin(1)));
        if (FVector2D::Distance(FVector2D(Cam), FVector2D(RealmOrigin(R))) < Reach) Want = R;
        else if (V.Realm < 0)
            if (const auto* Hero = Cast<ACireHero>(PC->GetPawn()); Hero && Hero->TeamId >= 0) Want = FMath::Clamp(Hero->TeamId, 0, 1);
    }
    if (Want != V.Realm)
    {
        const double Started = FPlatformTime::Seconds();
        RestoreAll(V);
        V.Realm = Want;
        if (V.Realm >= 0)
        {
            CullLevels(World, V);
            if (V.Realm <= 1)
            {
                const int32 Far = 1 - V.Realm;
                // The far realm's own sun (a second full set of virtual shadow map clipmaps), sky sphere and torch fills.
                auto HideOwn = [&](USceneComponent* C) { if (C && C->IsVisible()) { FCulled X; X.Component = C; X.bVisible = true; C->SetVisibility(false, false); V.Culled.Add(X); } };
                if (ADirectionalLight* Sun = RealmVisuals[Far].Sun.Get()) HideOwn(Sun->GetLightComponent());
                HideOwn(RealmVisuals[Far].Dome.Get());
                for (const auto& Fill : RealmVisuals[Far].Fills) if (Fill.IsValid()) HideOwn(Fill->PointLightComponent);
                ApplySky(World, V, V.Realm);
            }
        }
        else if (V.Sky.IsValid() && V.SkyIntensity >= 0)
        {
            USkyLightComponent* C = V.Sky->GetLightComponent();
            C->bRealTimeCapture = V.bSkyRealtime; C->SetIntensity(V.SkyIntensity); C->SetLightColor(V.SkyColor); C->RecaptureSky();
        }
        UE_LOG(LogCireTown, Display, TEXT("CIRE_TOWN_VIEW realm=%d culled=%d ms=%.0f"), V.Realm, V.Culled.Num(), (FPlatformTime::Seconds() - Started) * 1000.0);
    }
    else if (V.Realm >= 0) CullLevels(World, V); // late Level Instances of the far realm
    if (bCam && V.Realm >= 0 && V.Realm <= 1) TickNear(V, Cam);
}
int32 CireTownMap::ViewRealm(const UWorld* World)
{
    const FViewState* V = Views.Find(const_cast<UWorld*>(World));
    return V ? V->Realm : -1;
}

// ------------------------------------------------------------------ explore mode
namespace
{
struct FExplore
{
    bool bFly = false, bStarted = false;
    int32 Landmark = -1;
    FString LastMark;
    float LastMarkAt = -100.f;
    TArray<TSharedPtr<FJsonValue>> Marks;
};
FExplore GExplore;

FString MarksPath() { return FPaths::ProjectSavedDir() / TEXT("Explore/TownMarks.json"); }
void SaveMarks()
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetStringField(TEXT("frame"), TEXT("castletown realm-local centimetres (pack coordinates - frameCenter); z = world height"));
    Root->SetArrayField(TEXT("marks"), GExplore.Marks);
    FString Out; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(MarksPath()), true);
    FFileHelper::SaveStringToFile(Out, *MarksPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
void Mark(ACireHero* Hero, const TCHAR* Kind)
{
    const FVector P = Hero->GetActorLocation();
    const FVector2D Local = CireLanePath::ToLocal(Hero->TeamId, P);
    const float Floor = CireTownMap::Ground(Hero->GetWorld(), FVector2D(P));
    TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
    M->SetStringField(TEXT("kind"), Kind);
    M->SetNumberField(TEXT("realm"), Hero->TeamId);
    TArray<TSharedPtr<FJsonValue>> XY = {MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Local.X)), MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Local.Y))};
    M->SetArrayField(TEXT("local"), XY);
    M->SetNumberField(TEXT("groundZ"), FMath::RoundToDouble(Floor));
    M->SetNumberField(TEXT("yaw"), FMath::RoundToDouble(Hero->GetActorRotation().Yaw));
    M->SetStringField(TEXT("time"), FDateTime::Now().ToIso8601());
    GExplore.Marks.Add(MakeShared<FJsonValueObject>(M));
    SaveMarks();
    GExplore.LastMark = FString::Printf(TEXT("%s #%d at (%.0f, %.0f)"), Kind, GExplore.Marks.Num(), Local.X, Local.Y);
    GExplore.LastMarkAt = Hero->GetWorld()->GetRealTimeSeconds();
    UE_LOG(LogCireTown, Display, TEXT("CIRE_EXPLORE_MARK kind=%s realm=%d local=%.0f,%.0f ground=%.0f file=%s"), Kind, Hero->TeamId, Local.X, Local.Y, Floor, *MarksPath());
}
void Teleport(ACireHero* Hero, int32 Team, const FVector2D& Local)
{
    const FVector W = CireLanePath::ToWorld(Team, Local, 150.f);
    Hero->TeamId = Team;
    Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->SetActorLocation(W, false, nullptr, ETeleportType::TeleportPhysics);
}

// -CireExploreCapture: fixed town views, then a small column of marchers followed to the castle goal.
struct FCaptureView { FString Name; FVector From, At; };
struct FCapture
{
    bool bEnabled = false, bDone = false;
    int32 Stage = -1, Shot = 0;
    double StageAt = 0;
    FString Directory;
    TArray<FCaptureView> Views;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<ACireMonster>> Marchers;
    int32 MarchShots = 0, Leaked = 0;
    double MarchAt = 0, LastLog = 0;
    uint64 StageFrame = 0, ShotFrame = 0;
    TArray<FString> Files;
};
FCapture GCapture;

void Shoot(const FString& Name)
{
    const FString File = FPaths::Combine(GCapture.Directory, FString::Printf(TEXT("%02d_%s.png"), ++GCapture.Shot, *Name));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true); // scene only: the route is drawn as debug lines
    GCapture.Files.Add(File);
    UE_LOG(LogCireTown, Display, TEXT("CIRE_EXPLORE_CAPTURE shot=%s"), *File);
}
void Aim(const FVector& From, const FVector& At)
{
    if (!GCapture.Camera.IsValid()) return;
    GCapture.Camera->SetActorLocationAndRotation(From, (At - From).Rotation());
}
bool TickCapture(ACireGameMode* Mode, ACireHero* Hero, APlayerController* PC)
{
    if (!GCapture.bEnabled || GCapture.bDone) return false;
    UWorld* World = Mode->GetWorld();
    const double Now = FPlatformTime::Seconds();
    if (GCapture.Stage < 0)
    {
        if (PC->MyHUD) PC->MyHUD->bShowHUD = false; // scene only
        GCapture.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TownGallery"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
        IFileManager::Get().MakeDirectory(*GCapture.Directory, true);
        FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
        GCapture.Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
        if (GCapture.Camera.IsValid())
        {
            auto* Cam = GCapture.Camera->GetCameraComponent(); Cam->SetFieldOfView(70.f); Cam->SetAspectRatio(16.f / 9.f); Cam->bConstrainAspectRatio = true;
            Cam->PostProcessSettings.bOverride_MotionBlurAmount = true; Cam->PostProcessSettings.MotionBlurAmount = 0;
            PC->SetViewTarget(GCapture.Camera.Get());
        }
        const auto& R = CireLanePath::Get(World);
        const double Extent = FMath::Max<double>(R.MaxX - R.MinX, 2 * R.HalfWidth);
        auto W = [&](int32 Team, FVector2D L, float Z) { return CireLanePath::ToWorld(Team, L, Z); };
        const FVector2D Mid = FVector2D(R.MinX + R.MaxX, 0) * .5;
        const FVector2D Goal = R.GoalCenter, Spawn = R.LocalPoints[0][0], Base = R.BaseLocal;
        const FVector2D ToGoal = (Goal - Spawn).GetSafeNormal();
        GCapture.Views = {
            {TEXT("realm0_overview"), W(0, Mid - FVector2D(0, Extent * .55), Extent * .55f), W(0, Mid, 0)},
            {TEXT("realm0_topdown_route"), W(0, Mid + FVector2D(1, 0), Extent * .62f), W(0, Mid, 0)},
            {TEXT("two_realms"), FVector((CireLanePath::RealmOrigin(0) + CireLanePath::RealmOrigin(1)) * .5 + FVector2D(-Extent * 1.6, 0), CireTownMap::RealmOrigin(0).Z + Extent * .9),
                FVector((CireLanePath::RealmOrigin(0) + CireLanePath::RealmOrigin(1)) * .5, CireTownMap::RealmOrigin(0).Z)},
            {TEXT("castle_goal"), W(0, Goal - ToGoal * 3200 + FVector2D(-ToGoal.Y, ToGoal.X) * 900, 1400), W(0, Goal, 500)},
            {TEXT("breach"), W(0, Spawn - ToGoal * 1800, 900), W(0, Spawn + ToGoal * 1500, 100)},
            {TEXT("hero_base_street"), W(0, Base - ToGoal * 900, 260), W(0, Base + ToGoal * 1200, 150)},
            {TEXT("realm1_overview"), W(1, Mid - FVector2D(0, Extent * .55), Extent * .55f), W(1, Mid, 0)},
            {TEXT("realm1_base_street"), W(1, Base - ToGoal * 900, 260), W(1, Base + ToGoal * 1200, 150)},
            {TEXT("realm1_market_plaza"), W(1, CireTownMap::Def().ExploreStart + FVector2D(-900, -900), 450), W(1, CireTownMap::Def().ExploreStart, 150)},
            {TEXT("realm1_castle_goal"), W(1, Goal - ToGoal * 3200 + FVector2D(-ToGoal.Y, ToGoal.X) * 900, 1400), W(1, Goal, 500)}};
        for (const auto& L : CireTownMap::Def().Landmarks)
            GCapture.Views.Add({TEXT("landmark_") + L.Name.Replace(TEXT(" "), TEXT("_")), W(0, L.Local + FVector2D(-600, -600), 300), W(0, L.Local, 150)});
        // The provisional spots, drawn into the scene for the pictures (both realms).
        for (int32 Team = 0; Team < 2; ++Team)
        {
            const auto Points = CireLanePath::RoutePoints(World, Team, 120.f);
            for (int32 I = 1; I < Points.Num(); ++I) DrawDebugLine(World, Points[I - 1], Points[I], FColor(255, 196, 64), true, -1.f, 0, 40.f);
            for (const FVector& P : Points) DrawDebugSphere(World, P, 120.f, 12, FColor(255, 196, 64), true, -1.f, 0, 12.f);
            const FVector2D E = CireLanePath::GoalZoneExtent(World);
            DrawDebugBox(World, CireLanePath::GoalZoneCenter(World, Team, 300.f), FVector(E, 300.f), FColor(90, 255, 200), true, -1.f, 0, 30.f);
            DrawDebugCylinder(World, CireLanePath::SpawnPosition(World, Team, 0), CireLanePath::SpawnPosition(World, Team, 800.f), 400.f, 24, FColor(255, 70, 40), true, -1.f, 0, 25.f);
            DrawDebugCylinder(World, CireLanePath::BasePosition(World, Team, 0), CireLanePath::BasePosition(World, Team, 500.f), 300.f, 24, FColor(80, 160, 255), true, -1.f, 0, 25.f);
            for (int32 Tier = 1; Tier <= 3; ++Tier)
                DrawDebugCylinder(World, CireLanePath::ChallengePosition(World, Team, Tier, 0), CireLanePath::ChallengePosition(World, Team, Tier, 400.f), 470.f, 24, FColor(255, 110, 230), true, -1.f, 0, 20.f);
        }
        GCapture.Stage = 0; GCapture.StageAt = Now + 15.0; // the first view also waits for streaming and shaders
        Aim(GCapture.Views[0].From, GCapture.Views[0].At);
        return true;
    }
    if (GCapture.Stage < GCapture.Views.Num())
    {
        const double Age = Now - GCapture.StageAt;
        // Frame-gated as well as timed: while shaders compile a frame can take seconds.
        if (Age > 8.0 && GFrameCounter - GCapture.StageFrame > 30 && GCapture.Shot == GCapture.Stage) { Shoot(GCapture.Views[GCapture.Stage].Name); GCapture.ShotFrame = GFrameCounter; }
        if (GCapture.Shot > GCapture.Stage && GFrameCounter - GCapture.ShotFrame > 5)
        {
            ++GCapture.Stage; GCapture.StageAt = Now; GCapture.StageFrame = GFrameCounter;
            if (GCapture.Stage < GCapture.Views.Num()) Aim(GCapture.Views[GCapture.Stage].From, GCapture.Views[GCapture.Stage].At);
        }
        return true;
    }
    if (GCapture.Stage == GCapture.Views.Num())
    {
        // A small wave column in realm 0, marched by the normal lane AI to the castle goal.
        const FName Mix[] = {TEXT("hollow_infantry"), TEXT("ironbound_bruiser"), TEXT("hollow_shieldbearer"), TEXT("barbed_hunter"), TEXT("hollow_infantry"), TEXT("blight_caster")};
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        for (int32 I = 0; I < 6; ++I)
        {
            const FVector At = CireLanePath::ClampToLane(World, 0, CireLanePath::SpawnPosition(World, 0) + FVector((I / 3) * 160.f, (I % 3 - 1) * 150.f, 0), 80);
            if (auto* M = World->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), At, FRotator::ZeroRotator, Params))
            { M->Lane = 0; CireNPCCombat::ConfigureArchetype(M, Mix[I], 1, 0, 1, false); Mode->Monsters.Add(M); GCapture.Marchers.Add(M); }
        }
        Hero->bDrafted = false; // monsters ignore undrafted heroes: the column only marches
        UE_LOG(LogCireTown, Display, TEXT("CIRE_EXPLORE_MARCH_START units=%d route=%.0f"), GCapture.Marchers.Num(), CireLanePath::RouteLength(World, 0));
        GCapture.Stage++; GCapture.MarchAt = Now;
        return true;
    }
    // Follow the leading marcher; shoot at 30 % and 70 % of the route and when the column reaches the goal.
    ACireMonster* Lead = nullptr; float Best = -1; int32 Alive = 0;
    for (const auto& M : GCapture.Marchers)
        if (M.IsValid() && !M->IsActorBeingDestroyed() && Mode->Monsters.Contains(M.Get()))
        {
            ++Alive; const float P = CireLanePath::RouteProgress(World, 0, M->GetActorLocation());
            if (P > Best) { Best = P; Lead = M.Get(); }
        }
    GCapture.Leaked = GCapture.Marchers.Num() - Alive;
    if (Now - GCapture.LastLog > 10.0)
    {
        GCapture.LastLog = Now;
        FString Units;
        for (const auto& M : GCapture.Marchers)
            if (M.IsValid() && Mode->Monsters.Contains(M.Get()))
            {
                const FVector2D L = CireLanePath::ToLocal(0, M->GetActorLocation());
                Units += FString::Printf(TEXT(" [%.0f,%.0f z%.0f p%.2f wp%d v%.0f]"), L.X, L.Y, M->GetActorLocation().Z, CireLanePath::RouteProgress(World, 0, M->GetActorLocation()), M->LaneWaypointIndex, M->GetVelocity().Size2D());
            }
        UE_LOG(LogCireTown, Display, TEXT("CIRE_EXPLORE_MARCH t=%.0f alive=%d reached=%d lead=%.2f%s"), Now - GCapture.MarchAt, Alive, GCapture.Leaked, Best, *Units);
    }
    if (Lead)
    {
        const FVector L = Lead->GetActorLocation();
        const FVector Ahead = CireLanePath::PointAlongRoute(World, 0, FMath::Min(1.f, Best + .03f), L.Z);
        const FVector Back = (L - Ahead).GetSafeNormal2D();
        Aim(L + Back * 900 + FVector(0, 0, 650), L + FVector(0, 0, 60));
        const float Marks[] = {.3f, .7f, .93f};
        if (GCapture.MarchShots < 3 && Best >= Marks[GCapture.MarchShots]) Shoot(FString::Printf(TEXT("march_%02.0f"), Marks[GCapture.MarchShots++] * 100));
    }
    const double Took = Now - GCapture.MarchAt;
    if (Alive == 0 || Took > 600.0)
    {
        auto* State = Mode->GetGameState<ACireGameState>();
        const bool bPass = GCapture.Leaked == GCapture.Marchers.Num() && GCapture.Marchers.Num() > 0;
        UE_LOG(LogCireTown, Display, TEXT("CIRE_EXPLORE_CAPTURE_%s shots=%d reached_castle=%d/%d seconds=%.0f lives=%d dir=%s"), bPass ? TEXT("PASS") : TEXT("FAIL"),
            GCapture.Files.Num(), GCapture.Leaked, GCapture.Marchers.Num(), Took, State ? State->EmberLives : -1, *GCapture.Directory);
        GCapture.bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
    }
    return true;
}
}

bool CireTownMap::TickExplore(ACireGameMode* Mode, float DeltaSeconds)
{
    if (!IsExplore() || !Mode) return false;
    UWorld* World = Mode->GetWorld();
    for (ACireHero* H : Mode->Heroes)
    {
        if (!IsValid(H) || H->bBot) continue;
        H->Offers.Reset(); // no opening-ability modal while exploring
        if (!H->bDrafted && GCapture.MarchAt <= 0) { H->Draft(0); H->Notice = TEXT("Explore mode: walk the town. F5 fly, F6 mark route point, F7 mark pack spot, F4 mark vendor spot."); }
        auto* PC = Cast<APlayerController>(H->GetController());
        if (!PC || !PC->IsLocalController()) continue;
        if (!GExplore.bStarted) GCapture.bEnabled = HasParam(TEXT("CireExploreCapture"));
        if (GExplore.bStarted && TickCapture(Mode, H, PC)) continue;
        if (!GExplore.bStarted)
        {
            GExplore.bStarted = true;
            Teleport(H, 0, Def().ExploreStart);
            UE_LOG(LogCireTown, Display, TEXT("CIRE_EXPLORE_READY start=%s marks=%s"), *CireLanePath::ToWorld(0, Def().ExploreStart, 150.f).ToString(), *MarksPath());
        }
        auto* Move = H->GetCharacterMovement();
        const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
        if (PC->WasInputKeyJustPressed(EKeys::F5))
        {
            GExplore.bFly = !GExplore.bFly;
            Move->SetMovementMode(GExplore.bFly ? MOVE_Flying : MOVE_Falling);
        }
        if (GExplore.bFly)
        {
            if (Move->MovementMode != MOVE_Flying) Move->SetMovementMode(MOVE_Flying);
            Move->MaxFlySpeed = bShift ? 5200.f : 1600.f; Move->BrakingDecelerationFlying = 4000.f;
            if (PC->IsInputKeyDown(EKeys::SpaceBar)) H->AddMovementInput(FVector::UpVector, 1.f);
            if (PC->IsInputKeyDown(EKeys::X)) H->AddMovementInput(FVector::UpVector, -1.f);
        }
        if (PC->WasInputKeyJustPressed(EKeys::F6)) Mark(H, TEXT("route"));
        if (PC->WasInputKeyJustPressed(EKeys::F7)) Mark(H, TEXT("pack"));
        if (PC->WasInputKeyJustPressed(EKeys::F4)) Mark(H, TEXT("vendor"));
        if (PC->WasInputKeyJustPressed(EKeys::BackSpace) && GExplore.Marks.Num() > 0)
        {
            GExplore.Marks.Pop(); SaveMarks(); GExplore.LastMark = TEXT("last mark removed"); GExplore.LastMarkAt = World->GetRealTimeSeconds();
        }
        if (PC->WasInputKeyJustPressed(EKeys::F2))
        {
            // Cycle the landmarks: explore start, base, castle goal, breach, challenge spots, then the authored list.
            TArray<TPair<FString, FVector2D>> Stops;
            const auto& R = CireLanePath::Get(World);
            Stops.Add({TEXT("Explore start"), Def().ExploreStart});
            Stops.Add({TEXT("Hero base (provisional)"), R.BaseLocal});
            Stops.Add({TEXT("Castle goal (provisional)"), R.GoalCenter});
            Stops.Add({TEXT("Breach (provisional)"), R.LocalPoints[H->TeamId][0]});
            for (int32 Tier = 1; Tier <= 3; ++Tier) Stops.Add({FString::Printf(TEXT("Challenge %d (provisional)"), Tier), CireLanePath::BayPoint(R, H->TeamId, Tier)});
            for (const auto& L : Def().Landmarks) Stops.Add({L.Name, L.Local});
            GExplore.Landmark = (GExplore.Landmark + (bShift ? Stops.Num() - 1 : 1)) % Stops.Num();
            Teleport(H, H->TeamId, Stops[GExplore.Landmark].Value);
            GExplore.LastMark = FString::Printf(TEXT("Teleported: %s"), *Stops[GExplore.Landmark].Key); GExplore.LastMarkAt = World->GetRealTimeSeconds();
        }
        if (PC->WasInputKeyJustPressed(EKeys::Home))
        {
            // Same spot in the other realm (both copies are identical).
            const FVector2D Local = CireLanePath::ToLocal(H->TeamId, H->GetActorLocation());
            Teleport(H, 1 - H->TeamId, Local);
            GExplore.LastMark = FString::Printf(TEXT("Realm %d"), H->TeamId); GExplore.LastMarkAt = World->GetRealTimeSeconds();
        }
    }
    return true;
}

void ACireHUD::DrawExploreOverlay()
{
    if (!CireTownMap::IsExplore() || !Canvas || !PlayerOwner) return;
    auto* Hero = Cast<ACireHero>(PlayerOwner->GetPawn());
    if (!Hero) return;
    UWorld* World = GetWorld();
    const auto& R = CireLanePath::Get(World);
    // Provisional route, breach, goal and challenge spots projected into the view.
    auto Project = [&](const FVector& W, FVector2D& Out) { const FVector S = Canvas->Project(W); Out = FVector2D(S.X, S.Y); return S.Z > 0; };
    const FLinearColor RouteColor(1.f, .78f, .25f, .9f);
    const auto Points = CireLanePath::RoutePoints(World, Hero->TeamId, 60.f);
    for (int32 I = 1; I < Points.Num(); ++I)
    {
        FVector2D A, B;
        if (Project(Points[I - 1], A) && Project(Points[I], B))
        {
            FCanvasLineItem L(A, B); L.SetColor(RouteColor); L.LineThickness = 3.f; Canvas->DrawItem(L);
        }
    }
    auto Tag = [&](const FVector& W, const FString& Text, const FLinearColor& Color)
    {
        FVector2D S;
        if (!Project(W, S)) return;
        FCanvasTextItem T(S, FText::FromString(Text), GEngine->GetMediumFont(), Color); T.bOutlined = true; T.bCentreX = true; Canvas->DrawItem(T);
    };
    for (int32 I = 0; I < Points.Num(); ++I) Tag(Points[I] + FVector(0, 0, 80), FString::Printf(TEXT("route %d"), I), RouteColor);
    Tag(CireLanePath::SpawnPosition(World, Hero->TeamId, 300.f), TEXT("BREACH (provisional)"), FLinearColor(1.f, .35f, .2f));
    Tag(CireLanePath::GoalZoneCenter(World, Hero->TeamId, 300.f), TEXT("CASTLE GOAL (provisional)"), FLinearColor(.4f, 1.f, .8f));
    for (int32 Tier = 1; Tier <= 3; ++Tier) Tag(CireLanePath::ChallengePosition(World, Hero->TeamId, Tier, 300.f), FString::Printf(TEXT("PACK %d (provisional)"), Tier), FLinearColor(1.f, .6f, .9f));
    // Panel: where am I, and the keys.
    const FVector P = Hero->GetActorLocation();
    const FVector2D Local = CireLanePath::ToLocal(Hero->TeamId, P);
    TArray<FString> Lines = {
        FString::Printf(TEXT("EXPLORE MODE  |  Medieval Kingdom town  |  realm %d (%s)"), Hero->TeamId, Hero->TeamId == 0 ? TEXT("Ember") : TEXT("Dusk")),
        FString::Printf(TEXT("local  x %.0f   y %.0f   |  world z %.0f   ground %.0f"), Local.X, Local.Y, P.Z, CireTownMap::Ground(World, FVector2D(P))),
        FString::Printf(TEXT("%s  |  %d marks -> Saved/Explore/TownMarks.json"), GExplore.bFly ? TEXT("FLYING (Space up, X down, Shift fast)") : TEXT("walking"), GExplore.Marks.Num()),
        TEXT("F5 fly/walk   F6 mark route point   F7 mark pack spot   F4 mark vendor   Backspace undo"),
        TEXT("F2 next landmark (Shift+F2 back)   Home other realm")};
    if (World->GetRealTimeSeconds() - GExplore.LastMarkAt < 4.f) Lines.Add(GExplore.LastMark);
    const float X = 16, Y0 = 16, LineH = 20;
    FCanvasTileItem Back(FVector2D(X - 8, Y0 - 6), FVector2D(620, LineH * Lines.Num() + 12), FLinearColor(0, 0, 0, .55f));
    Back.BlendMode = SE_BLEND_Translucent; Canvas->DrawItem(Back);
    for (int32 I = 0; I < Lines.Num(); ++I)
    {
        FCanvasTextItem T(FVector2D(X, Y0 + I * LineH), FText::FromString(Lines[I]), GEngine->GetMediumFont(), I == 0 ? FLinearColor(1.f, .85f, .45f) : FLinearColor::White);
        T.bOutlined = true; Canvas->DrawItem(T);
    }
}
