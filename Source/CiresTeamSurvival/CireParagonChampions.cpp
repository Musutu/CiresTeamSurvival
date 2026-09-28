// paragon-champions: Epic's Paragon heroes as playable champions (see CireParagonChampions.h, Docs/ParagonChampions.md).
#include "CireParagonChampions.h"
#include "CireFabVFX.h" // vfx-loop-fix
#include "CireActorIterator.h"
#include "CireAbilityDB.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"
#include "CireBuffs.h"
#include "CireChampionRoster.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCrowdControl.h"
#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireScalingKits.h"
#include "CireSkillRuntime.h"
#include "CireSkillShop.h"
#include "CireSkillshot.h"
#include "CireSkillTuning.h"
#include "CireSpellPresentation.h"
#include "CireThreat.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireParagon, Log, All);

namespace
{
// Delivery recipes. Every number (cost, cooldown, effect, range, radius, duration, crowd control) is Ability DB data;
// the recipe only says how the skill lands, plus the Paragon FX / voice of the hero it came from.
enum class EPgDelivery : uint8
{
    Passive,
    Projectile,  // skillshot that stops on the first enemy
    Pierce,      // skillshot through up to Hits enemies
    Cone,        // cone from the caster (Angle)
    Circle,      // aimed ground circle (Warning)
    Line,        // ground line from the caster (Radius = half width)
    SelfBurst,   // circle around the caster (Warning)
    Strike,      // targeted hit on the selected enemy
    Lunge,       // close on the target, then strike
    Leap,        // jump to the aimed spot, burst on landing
    Dash,        // move to the aimed spot; optional self buff
    Pull,        // first enemy in the corridor is struck and dragged to the caster
    AllyHeal,    // heal the selected ally (or self)
    PartyHeal,   // heal every ally around the caster (and self)
    SelfBuff,    // timed buff on the caster (Buff, magnitude = DB effect)
    Storm,       // persistent damaging area around the caster (or at the aim with bAtAim), Duration seconds
    Mark,        // debuff the target: takes +effect% damage (pg_marked)
};
struct FPgRecipe
{
    FString Id, Hero;
    EPgDelivery D = EPgDelivery::Strike;
    float Angle = 70, Warning = 0, Speed = 2400, FxLife = 2.5f, BuffPct = 0, FxScale = 1.f;
    int32 Hits = 1;
    bool bAtAim = false, bTaunt = false;
    FString Style = TEXT("arcane"), Passive;
    FName Buff;
    TArray<FString> CastFX, ImpactFX;
    FString Voice;
};
struct FPgHero
{
    FString Id, Pack, Mesh, Portrait, VoiceSelect, VoiceLock, Background;
    TArray<FString> Own, Skins; // Skins: "<key>|<name>" (installed reskins)
    bool bInstalled = false;
};
struct FPgData
{
    bool bLoaded = false, bEnabled = false;
    TSharedPtr<FJsonObject> Root;
    TMap<FString, FPgRecipe> Recipes;
    TArray<FString> RecipeIds;
    TArray<FPgHero> Heroes;
    TArray<FString> Installed;
};
FPgData GPg;

const FName PgGuard(TEXT("pg_guard")), PgHaste(TEXT("pg_haste")), PgFrenzy(TEXT("pg_frenzy")), PgEmpower(TEXT("pg_empower")), PgMarked(TEXT("pg_marked"));

bool PgPresent(const FString& Path)
{
    if (!Path.StartsWith(TEXT("/Game/"))) return false;
    const FString Package = FPackageName::ObjectPathToPackageName(Path);
    return FPackageName::IsValidLongPackageName(Package) && FPackageName::DoesPackageExist(Package);
}
EPgDelivery PgParseDelivery(const FString& S, bool& bOk)
{
    static const TMap<FString, EPgDelivery> Map = {
        {TEXT("passive"), EPgDelivery::Passive}, {TEXT("projectile"), EPgDelivery::Projectile}, {TEXT("pierce"), EPgDelivery::Pierce},
        {TEXT("cone"), EPgDelivery::Cone}, {TEXT("circle"), EPgDelivery::Circle}, {TEXT("line"), EPgDelivery::Line},
        {TEXT("self_burst"), EPgDelivery::SelfBurst}, {TEXT("strike"), EPgDelivery::Strike}, {TEXT("lunge"), EPgDelivery::Lunge},
        {TEXT("leap"), EPgDelivery::Leap}, {TEXT("dash"), EPgDelivery::Dash}, {TEXT("pull"), EPgDelivery::Pull},
        {TEXT("ally_heal"), EPgDelivery::AllyHeal}, {TEXT("party_heal"), EPgDelivery::PartyHeal}, {TEXT("self_buff"), EPgDelivery::SelfBuff},
        {TEXT("storm"), EPgDelivery::Storm}, {TEXT("mark"), EPgDelivery::Mark}};
    const EPgDelivery* D = Map.Find(S); bOk = D != nullptr; return D ? *D : EPgDelivery::Strike;
}
TArray<FString> PgStrings(const TSharedPtr<FJsonObject>& J, const TCHAR* Key)
{
    TArray<FString> Out; const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (J.IsValid() && J->TryGetArrayField(Key, A)) for (const auto& V : *A) { FString S; if (V->TryGetString(S)) Out.Add(S); }
    return Out;
}
void PgLoad()
{
    if (GPg.bLoaded) return;
    GPg.bLoaded = true;
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *CireParagonChampions::DataPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), GPg.Root) || !GPg.Root.IsValid())
    { UE_LOG(LogCireParagon, Warning, TEXT("ParagonChampions.json missing or invalid; no Paragon champions.")); GPg.Root.Reset(); return; }
    double Version = 0;
    if (!GPg.Root->TryGetNumberField(TEXT("schemaVersion"), Version) || Version != 1) { UE_LOG(LogCireParagon, Error, TEXT("ParagonChampions.json: schemaVersion 1 expected")); GPg.Root.Reset(); return; }
    const TSharedPtr<FJsonObject>* Recipes = nullptr;
    if (GPg.Root->TryGetObjectField(TEXT("recipes"), Recipes))
        for (const auto& Pair : (*Recipes)->Values)
        {
            const TSharedPtr<FJsonObject>* O = nullptr; if (!Pair.Value->TryGetObject(O)) continue;
            FPgRecipe R; R.Id = FString(Pair.Key); FString Delivery; bool bOk = false;
            (*O)->TryGetStringField(TEXT("hero"), R.Hero);
            if (!(*O)->TryGetStringField(TEXT("delivery"), Delivery)) continue;
            R.D = PgParseDelivery(Delivery, bOk);
            if (!bOk) { UE_LOG(LogCireParagon, Error, TEXT("Unknown delivery %s for %s"), *Delivery, *R.Id); continue; }
            double N = 0;
            if ((*O)->TryGetNumberField(TEXT("angle"), N)) R.Angle = FMath::Clamp(static_cast<float>(N), 10.f, 170.f);
            if ((*O)->TryGetNumberField(TEXT("warning"), N)) R.Warning = FMath::Clamp(static_cast<float>(N), 0.f, 3.f);
            if ((*O)->TryGetNumberField(TEXT("speed"), N)) R.Speed = FMath::Clamp(static_cast<float>(N), 400.f, 8000.f);
            if ((*O)->TryGetNumberField(TEXT("hits"), N)) R.Hits = FMath::Clamp(static_cast<int32>(N), 1, 12);
            if ((*O)->TryGetNumberField(TEXT("buffPct"), N)) R.BuffPct = FMath::Clamp(static_cast<float>(N), 0.f, 80.f);
            if ((*O)->TryGetNumberField(TEXT("fxLife"), N)) R.FxLife = FMath::Clamp(static_cast<float>(N), .3f, 8.f);
            if ((*O)->TryGetNumberField(TEXT("fxScale"), N)) R.FxScale = FMath::Clamp(static_cast<float>(N), .2f, 4.f);
            (*O)->TryGetBoolField(TEXT("atAim"), R.bAtAim); (*O)->TryGetBoolField(TEXT("taunt"), R.bTaunt);
            (*O)->TryGetStringField(TEXT("style"), R.Style); (*O)->TryGetStringField(TEXT("passive"), R.Passive);
            FString Buff; if ((*O)->TryGetStringField(TEXT("buff"), Buff) && !Buff.IsEmpty()) R.Buff = FName(*Buff);
            const TSharedPtr<FJsonObject>* Fx = nullptr;
            if ((*O)->TryGetObjectField(TEXT("fx"), Fx)) { R.CastFX = PgStrings(*Fx, TEXT("cast")); R.ImpactFX = PgStrings(*Fx, TEXT("impact")); }
            (*O)->TryGetStringField(TEXT("voice"), R.Voice);
            GPg.RecipeIds.Add(R.Id); GPg.Recipes.Add(R.Id, MoveTemp(R));
        }
    const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("CireNoParagon")) || FParse::Param(FCommandLine::Get(), TEXT("CireNoFab"));
    const TArray<TSharedPtr<FJsonValue>>* Heroes = nullptr;
    if (GPg.Root->TryGetArrayField(TEXT("heroes"), Heroes))
        for (const auto& V : *Heroes)
        {
            const TSharedPtr<FJsonObject>* O = nullptr; if (!V->TryGetObject(O)) continue;
            FPgHero H; (*O)->TryGetStringField(TEXT("id"), H.Id); (*O)->TryGetStringField(TEXT("pack"), H.Pack); (*O)->TryGetStringField(TEXT("mesh"), H.Mesh);
            (*O)->TryGetStringField(TEXT("portrait"), H.Portrait); (*O)->TryGetStringField(TEXT("background"), H.Background); H.Own = PgStrings(*O, TEXT("own"));
            const TSharedPtr<FJsonObject>* Voice = nullptr;
            if ((*O)->TryGetObjectField(TEXT("voice"), Voice)) { (*Voice)->TryGetStringField(TEXT("select"), H.VoiceSelect); (*Voice)->TryGetStringField(TEXT("lock"), H.VoiceLock); }
            if (const TArray<TSharedPtr<FJsonValue>>* SkinRows = nullptr; (*O)->TryGetArrayField(TEXT("skins"), SkinRows))
                for (const auto& SkinValue : *SkinRows)
                {
                    const TSharedPtr<FJsonObject>* SO = nullptr; FString Key, Name, SkinMesh;
                    if (SkinValue->TryGetObject(SO) && (*SO)->TryGetStringField(TEXT("key"), Key) && (*SO)->TryGetStringField(TEXT("name"), Name) &&
                        (*SO)->TryGetStringField(TEXT("mesh"), SkinMesh) && PgPresent(SkinMesh)) H.Skins.Add(Key + TEXT("|") + Name);
                }
            bool bPlayable = true; (*O)->TryGetBoolField(TEXT("playable"), bPlayable);
            H.bInstalled = !bOff && bPlayable && !H.Id.IsEmpty() && PgPresent(H.Mesh);
            if (H.bInstalled) GPg.Installed.Add(H.Id);
            GPg.Heroes.Add(MoveTemp(H));
        }
    GPg.bEnabled = GPg.Installed.Num() > 0;
    UE_LOG(LogCireParagon, Display, TEXT("CIRE_PARAGON_LOADED authored=%d installed=%d recipes=%d"), GPg.Heroes.Num(), GPg.Installed.Num(), GPg.Recipes.Num());
}
const FPgRecipe* PgFind(const FString& Id) { PgLoad(); return GPg.Recipes.Find(Id); }
const FPgHero* PgHero(const FString& Id) { PgLoad(); return GPg.Heroes.FindByPredicate([&Id](const FPgHero& H) { return H.Id == Id; }); }

// ---- server helpers (unique names: bUseUnity=false keeps TUs apart, but these stay greppable) ----
bool PgSight(const AActor* From, const AActor* To)
{
    if (!IsValid(From) || !IsValid(To)) return false;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(CireParagonSight), false, From);
    const bool bBlocked = From->GetWorld()->LineTraceSingleByChannel(Hit, From->GetActorLocation() + FVector(0, 0, 45), To->GetActorLocation() + FVector(0, 0, 35), ECC_Visibility, Q);
    return !bBlocked || Hit.GetActor() == To;
}
float PgBody(const AActor* U) { const auto* C = Cast<ACharacter>(U); return C ? C->GetCapsuleComponent()->GetScaledCapsuleRadius() : 30.f; }
TArray<AActor*> PgEnemies(ACireHero* Hero, FVector Center, float Radius)
{
    TArray<AActor*> Out; UWorld* World = Hero->GetWorld();
    auto Consider = [&](AActor* U)
    {
        if (!U || U == Hero || !CireCombat::AreHostile(Hero, U) || !CireCombat::IsAlive(U)) return;
        if (FVector::DistSquared2D(Center, U->GetActorLocation()) <= FMath::Square(Radius + PgBody(U)) && FMath::Abs(U->GetActorLocation().Z - Center.Z) < 400.f) Out.Add(U);
    };
    if (auto* Mode = World->GetAuthGameMode<ACireGameMode>()) for (auto* M : Mode->Monsters) if (IsValid(M)) Consider(M);
    for (TCireActorIterator<ACireHero> It(World); It; ++It) Consider(*It);
    for (TCireActorIterator<ACireConstruct> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) Consider(*It);
    return Out;
}
TArray<ACireHero*> PgAllies(ACireHero* Hero, FVector Center, float Radius)
{
    TArray<ACireHero*> Out;
    for (TCireActorIterator<ACireHero> It(Hero->GetWorld()); It; ++It)
        if (It->TeamId == Hero->TeamId && !It->bDead && It->bDrafted && FVector::DistSquared2D(Center, It->GetActorLocation()) <= FMath::Square(Radius)) Out.Add(*It);
    Out.AddUnique(Hero);
    return Out;
}
bool PgGroundAim(ACireHero* Hero, FVector& Aim)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireParagonGround), false, Hero); FHitResult Hit;
    if (!Hero->GetWorld()->LineTraceSingleByObjectType(Hit, Aim + FVector(0, 0, 300), Aim - FVector(0, 0, 500), FCollisionObjectQueryParams(ECC_WorldStatic), Query) || Hit.ImpactNormal.Z < .75f) return false;
    Aim = Hit.ImpactPoint; return true;
}
bool PgGroundSight(ACireHero* Hero, FVector Ground)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireParagonPlacement), false, Hero); FHitResult Hit;
    return !Hero->GetWorld()->LineTraceSingleByObjectType(Hit, Hero->GetActorLocation() + FVector(0, 0, 35), Ground + FVector(0, 0, 60), FCollisionObjectQueryParams(ECC_WorldStatic), Query) &&
        !ACireConstruct::FindBlockingConstruct(Hero, Ground);
}
FLinearColor PgTint(const FCireAbilityDef& D)
{
    ECireSchool S = ECireSchool::Arcane; CireAbilityShapes::ParseSchool(D.School, S);
    const FLinearColor C = CireAbilityShapes::SchoolColor(S);
    return FLinearColor(FMath::Min(C.R, 1.f), FMath::Min(C.G, 1.f), FMath::Min(C.B, 1.f), .38f);
}
FCireAreaSpec PgArea(const FCireAbilityDef& D, ECireAreaShape Shape, float Warning, float Burst)
{
    FCireAreaSpec A; A.Shape = Shape; A.Radius = FMath::Max(10.f, D.Radius > 0 ? D.Radius : 260.f); A.ConeAngleDegrees = 60; A.WarningSeconds = Warning;
    A.DurationSeconds = .1f; A.TickInterval = .5f; A.DamagePerSecond = 0; A.BurstDamage = Burst; A.bPersistent = false; A.bPoison = false;
    A.VerticalTolerance = 90; A.Color = PgTint(D); A.AbilityName = D.Name; return A;
}
void PgLater(UWorld* World, float Seconds, TFunction<void()> Work)
{
    if (!World) return; FTimerHandle Handle;
    World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(Work)), FMath::Max(.01f, Seconds), false);
}
bool PgMoveTo(ACireHero* Hero, FVector Ground, float MaxRange)
{
    const FVector From = Hero->GetActorLocation();
    FVector To = Ground; To.Z = Ground.Z + Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
    const FVector Flat = (To - From).GetSafeNormal2D();
    if (FVector::Dist2D(From, To) > MaxRange) To = FVector(From.X, From.Y, To.Z) + Flat * MaxRange;
    if (ACireConstruct::FindBlockingConstruct(Hero, To)) return false;
    // A dash / leap passes through units (a sweep would stop on the first monster); TeleportTo settles on free ground.
    const bool bMoved = Hero->TeleportTo(To, Flat.IsNearlyZero() ? Hero->GetActorRotation() : Flat.Rotation(), false, false);

    if (bMoved) { if (!Flat.IsNearlyZero()) Hero->SetActorRotation(Flat.Rotation()); ACireAreaEffect::ClearForActor(Hero); }
    return bMoved && FVector::DistSquared2D(From, Hero->GetActorLocation()) > FMath::Square(40.f);
}
float PgSegment(FVector P, FVector A, FVector B, float* OutAlong)
{
    const FVector2D AB(B.X - A.X, B.Y - A.Y), AP(P.X - A.X, P.Y - A.Y);
    const float T = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(AP, AB)) / FMath::Max(1.f, static_cast<float>(AB.SizeSquared())), 0.f, 1.f);
    if (OutAlong) *OutAlong = T;
    return static_cast<float>((AP - AB * T).Size());
}
const FCireBuffEntry* PgActive(const AActor* Unit, FName Id)
{
    if (!Unit || !CireBuffs::IsActive(Unit, Id)) return nullptr;
    const auto* State = CireBuffs::Get(Unit); return State ? State->Find(Id) : nullptr;
}
float PgFraction(const AActor* Unit, FName Id) { const auto* E = PgActive(Unit, Id); return E ? E->Stacks / 100.f : 0.f; }
/** Sum of a passive kind across the hero's learned Paragon passives (percent). */
float PgPassive(const ACireHero* Hero, const TCHAR* Kind)
{
    if (!Hero) return 0.f;
    float Sum = 0.f;
    for (const FString& S : Hero->Skills)
        if (const FPgRecipe* R = PgFind(S); R && R->D == EPgDelivery::Passive && R->Passive == Kind)
            if (const FCireAbilityDef* D = CireAbilityDB::Find(S)) Sum += CireKits::ScaledEffect(Hero, S, D->Base.Effect);
    return FMath::Clamp(Sum, 0.f, 60.f);
}
} // namespace

// ================================================================================================ data
FString CireParagonChampions::DataPath() { return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/ParagonChampions.json")); }
bool CireParagonChampions::IsEnabled() { PgLoad(); return GPg.bEnabled; }
const TArray<FString>& CireParagonChampions::HeroIds() { PgLoad(); return GPg.Installed; }
bool CireParagonChampions::IsParagon(const FString& ProfileId) { PgLoad(); return GPg.Installed.Contains(ProfileId); }
int32 CireParagonChampions::AuthoredCount() { PgLoad(); return GPg.Heroes.Num(); }
TArray<FString> CireParagonChampions::OwnAbilities(const FString& ProfileId) { const FPgHero* H = PgHero(ProfileId); return H ? H->Own : TArray<FString>(); }
FString CireParagonChampions::DraftBackground(const FString& ProfileId) { const FPgHero* H = PgHero(ProfileId); return H && H->bInstalled ? H->Background : FString(); }
TArray<FString> CireParagonChampions::Skins(const FString& ProfileId) { const FPgHero* H = PgHero(ProfileId); return H && H->bInstalled ? H->Skins : TArray<FString>(); }
bool CireParagonChampions::HasSkin(const FString& ProfileId, const FString& SkinKey)
{
    if (SkinKey.IsEmpty()) return true;
    for (const FString& S : Skins(ProfileId)) if (S.StartsWith(SkinKey + TEXT("|"))) return true;
    return false;
}
// Champion select: the skin applies to the drafted (or hovered) profile; validated against the hero's installed reskins.
void ACireController::ServerSetChampionSkin_Implementation(const FString& ProfileId, const FString& Skin)
{
    auto* Hero = Cast<ACireHero>(GetPawn());
    if (!Hero || Skin.Len() > 64 || !CireParagonChampions::HasSkin(ProfileId, Skin)) return;
    if (!Hero->ChampionProfileId.IsEmpty() && Hero->ChampionProfileId != ProfileId) return;
    Hero->ChampionSkin = Skin; Hero->ForceNetUpdate();
}



void CireParagonChampions::AppendProfiles(TArray<FCireChampionProfile>& InOut)
{
    PgLoad();
    if (!GPg.bEnabled || !GPg.Root.IsValid()) return;
    const TSharedPtr<FJsonObject>* Roster = nullptr;
    if (!GPg.Root->TryGetObjectField(TEXT("roster"), Roster)) return;
    FString Json; FJsonSerializer::Serialize(Roster->ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
    TArray<FCireChampionProfile> Parsed; FString Error;
    if (!CireChampionRoster::ParseJson(Json, Parsed, Error)) { UE_LOG(LogCireParagon, Error, TEXT("Paragon roster rejected: %s"), *Error); return; }
    int32 Added = 0;
    for (FCireChampionProfile& P : Parsed)
        if (GPg.Installed.Contains(P.Id) && !InOut.ContainsByPredicate([&P](const FCireChampionProfile& Q) { return Q.Id == P.Id; })) { InOut.Add(MoveTemp(P)); ++Added; }
    UE_LOG(LogCireParagon, Display, TEXT("CIRE_PARAGON_ROSTER added=%d total=%d"), Added, InOut.Num());
}

void CireParagonChampions::MergeAbilities(TArray<FCireAbilityDef>& Abilities, TMap<FString, FCireChampionKit>& Kits)
{
    PgLoad();
    if (!GPg.bEnabled || !GPg.Root.IsValid()) return;
    // The overlay's kits also list base-DB skills (the role template's pool): parse with the kits reduced to overlay
    // ids, then restore the full lists against the merged DB.
    const TSharedPtr<FJsonObject>* KitsJson = nullptr;
    TMap<FString, TArray<FString>> FullPurchasable;
    TSharedPtr<FJsonObject> Reduced = MakeShared<FJsonObject>(*GPg.Root);
    TSet<FString> OverlayIds;
    if (const TSharedPtr<FJsonObject>* AbilitiesJson = nullptr; GPg.Root->TryGetObjectField(TEXT("abilities"), AbilitiesJson))
        for (const auto& Pair : (*AbilitiesJson)->Values) OverlayIds.Add(FString(Pair.Key));
    if (GPg.Root->TryGetObjectField(TEXT("champions"), KitsJson))
    {
        TSharedPtr<FJsonObject> ReducedKits = MakeShared<FJsonObject>();
        for (const auto& Pair : (*KitsJson)->Values)
        {
            const TSharedPtr<FJsonObject>* KitObject = nullptr; if (!Pair.Value->TryGetObject(KitObject)) continue;
            TSharedPtr<FJsonObject> Copy = MakeShared<FJsonObject>(**KitObject);
            const TArray<FString> Full = PgStrings(*KitObject, TEXT("purchasable"));
            FullPurchasable.Add(FString(Pair.Key), Full);
            TArray<TSharedPtr<FJsonValue>> Own;
            for (const FString& S : Full) if (OverlayIds.Contains(S)) Own.Add(MakeShared<FJsonValueString>(S));
            Copy->SetArrayField(TEXT("purchasable"), Own); Copy->SetArrayField(TEXT("purchasableImplemented"), Own);
            ReducedKits->SetObjectField(FString(Pair.Key), Copy);
        }
        Reduced->SetObjectField(TEXT("champions"), ReducedKits);
    }
    FString Json; FJsonSerializer::Serialize(Reduced.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
    TArray<FCireAbilityDef> A; TMap<FString, FCireChampionKit> K; TMap<FName, TArray<FCireModifier>> M; FString Error;
    if (!CireAbilityDB::ParseJson(Json, A, K, M, Error)) { UE_LOG(LogCireParagon, Error, TEXT("Paragon ability overlay rejected: %s"), *Error); return; }
    TSet<FString> Seen; for (const FCireAbilityDef& D : Abilities) Seen.Add(D.Id);
    int32 Added = 0;
    for (FCireAbilityDef& D : A) if (!Seen.Contains(D.Id)) { Seen.Add(D.Id); Abilities.Add(MoveTemp(D)); ++Added; }
    TSet<FString> Implemented; for (const FCireAbilityDef& D : Abilities) if (D.IsImplemented()) Implemented.Add(D.Id);
    for (auto& Pair : K)
        if (const TArray<FString>* Full = FullPurchasable.Find(Pair.Key))
        {
            Pair.Value.Purchasable.Reset(); Pair.Value.PurchasableImplemented.Reset();
            for (const FString& S : *Full) if (Seen.Contains(S)) { Pair.Value.Purchasable.AddUnique(S); if (Implemented.Contains(S)) Pair.Value.PurchasableImplemented.AddUnique(S); }
        }


    for (auto& Pair : K) if (GPg.Installed.Contains(Pair.Key) && !Kits.Contains(Pair.Key)) Kits.Add(Pair.Key, Pair.Value);
    // Pool: the Paragon abilities flagged "pool" join every champion whose kit roles share a type (DPS/TANK/HEAL).
    int32 Pooled = 0;
    const TSharedPtr<FJsonObject>* Pool = nullptr;
    if (GPg.Root->TryGetObjectField(TEXT("poolAdditions"), Pool))
        for (const auto& Pair : (*Pool)->Values)
        {
            const FString Type(Pair.Key); TArray<FString> Ids; const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
            if (!Pair.Value->TryGetArray(List)) continue;
            for (const auto& V : *List) { FString S; if (V->TryGetString(S) && Seen.Contains(S)) Ids.Add(S); }
            for (auto& Kit : Kits)
            {
                if (!Kit.Value.Roles.Contains(Type) && Kit.Value.PrimaryRole != Type) continue;
                for (const FString& S : Ids)
                {
                    if (Kit.Value.Signature.Contains(S)) continue;
                    if (!Kit.Value.Purchasable.Contains(S)) { Kit.Value.Purchasable.Add(S); ++Pooled; }
                    Kit.Value.PurchasableImplemented.AddUnique(S);
                }
            }
        }
    // A pooled ability is learnable by every champion whose kit now lists it.
    for (FCireAbilityDef& D : Abilities)
        if (GPg.Recipes.Contains(D.Id))
            for (const auto& Kit : Kits) if (Kit.Value.Purchasable.Contains(D.Id)) D.Champions.AddUnique(Kit.Key);
    UE_LOG(LogCireParagon, Display, TEXT("CIRE_PARAGON_ABILITIES added=%d kits=%d pooled=%d"), Added, K.Num(), Pooled);
}

// ================================================================================================ skills
bool CireParagonChampions::Knows(const FString& Id) { return PgFind(Id) != nullptr; }
bool CireParagonChampions::Handles(const FString& Id) { const FPgRecipe* R = PgFind(Id); return R && R->D != EPgDelivery::Passive; }
bool CireParagonChampions::IsPassive(const FString& Id) { const FPgRecipe* R = PgFind(Id); return R && R->D == EPgDelivery::Passive; }
const TArray<FString>& CireParagonChampions::AllIds() { PgLoad(); return GPg.RecipeIds; }
const TArray<FName>& CireParagonChampions::BuffIds() { static const TArray<FName> Ids = {PgGuard, PgHaste, PgFrenzy, PgEmpower, PgMarked}; return Ids; }

bool CireParagonChampions::Cast(ACireHero* Hero, int32 Slot, const FString& Id)
{
    const FPgRecipe* R = PgFind(Id);
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    if (!IsValid(Hero) || !Hero->HasAuthority() || !R || !Def || R->D == EPgDelivery::Passive || !CireSkillRuntime::Alive(Hero) ||
        !Hero->Skills.IsValidIndex(Slot) || Hero->Skills[Slot] != Id || !Hero->Cooldowns.IsValidIndex(Slot) || Hero->Cooldowns[Slot] > 0 || Hero->GlobalCooldown > 0) return false;
    UWorld* World = Hero->GetWorld();
    auto* Mode = World->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !Mode->IsCombatPhase()) return false;
    auto Fail = [&](const FString& Message) { Hero->Notice = Message; return false; };
    const float Mana = Def->Base.ManaCost, Energy = Def->Base.EnergyCost;
    if (!CireSkillShop::CanPayCast(Hero, Id, Mana, Energy)) return Fail(*CireSkillShop::CostFailText());
    const float Range = Def->Range > 0 ? Def->Range : 900.f;
    const float Radius = Def->Radius > 0 ? Def->Radius : 260.f;
    const float Amount = FMath::Min(10000.f, CireKits::Amount(Hero, Id, Def->Base.Effect, 1.f) * Mode->Power(Hero->TeamId));
    AActor* Target = Hero->Target;
    const bool bHostile = CireCombat::AreHostile(Hero, Target);
    FVector Aim = Hero->bHasCastAim ? Hero->CastAimPoint : bHostile ? Target->GetActorLocation() :
        Hero->GetActorLocation() + Hero->GetActorForwardVector().GetSafeNormal2D() * FMath::Min(500.f, Range);
    if (Aim.ContainsNaN()) return Fail(TEXT("Invalid aim."));
    const FVector Origin = Hero->GetActorLocation();
    const FVector Feet = Origin - FVector(0, 0, Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FVector Direction = (Aim - Origin).GetSafeNormal2D().IsNearlyZero() ? Hero->GetActorForwardVector().GetSafeNormal2D() : (Aim - Origin).GetSafeNormal2D();
    auto NeedEnemy = [&](float Reach) { return bHostile && Hero->InRange(Target, Reach + 40.f) && PgSight(Hero, Target); };
    auto NeedGround = [&](bool bSight) -> bool
    {
        if (!CireSkillRuntime::InRealmBounds(Mode, Hero->TeamId, Aim) || FVector::DistSquared2D(Origin, Aim) > FMath::Square(Range + 60.f)) { Hero->Notice = TEXT("Aim within your realm and casting range."); return false; }
        if (!PgGroundAim(Hero, Aim)) { Hero->Notice = TEXT("Aim at supported battlefield ground."); return false; }
        if (bSight && !PgGroundSight(Hero, Aim)) { Hero->Notice = TEXT("A wall or world object blocks that spot."); return false; }
        return true;
    };
    auto BuffMagnitude = [&]() { return static_cast<int32>(FMath::Clamp(CireKits::ScaledEffect(Hero, Id, Def->Base.Effect), 1.f, 80.f)); };
    const float Seconds = FMath::Clamp(Def->Duration > 0 ? Def->Duration : 4.f, .5f, 20.f);
    switch (R->D)
    {
    case EPgDelivery::Projectile:
    case EPgDelivery::Pierce:
    {
        FCireSkillshotSpec S;
        S.Speed = R->Speed; S.Radius = FMath::Clamp(Def->Radius > 0 ? Def->Radius : 32.f, 12.f, 90.f);
        S.MaxRange = Range; S.LifetimeSeconds = Range / S.Speed + .3f; S.Damage = Amount; S.WarningSeconds = .05f; S.CastRange = Range;
        S.HitLimit = R->D == EPgDelivery::Pierce ? FMath::Max(2, R->Hits) : 1;
        S.PlayerCollision = S.MonsterCollision = R->D == EPgDelivery::Pierce ? ECireProjectileCollision::Pierce : ECireProjectileCollision::Stop;
        S.VisualStyle = R->Style; S.Color = PgTint(*Def); S.Color.A = .9f; S.AbilityName = Def->Name;
        const FVector ShotAim = Origin + Direction * Range;
        if (!ACireSkillshot::Spawn(Hero, S, ShotAim, Def->Name)) return Fail(TEXT("Cannot fire from here."));
        Aim = ShotAim; break;
    }
    case EPgDelivery::Cone:
    {
        FCireAreaSpec A = PgArea(*Def, ECireAreaShape::Cone, FMath::Max(.08f, R->Warning), Amount); A.ConeAngleDegrees = R->Angle;
        if (!ACireAreaEffect::Spawn(Hero, A, Feet, Direction.Rotation())) return Fail(TEXT("Cannot unleash that here."));
        Hero->SetActorRotation(Direction.Rotation()); Aim = Origin + Direction * Radius; break;
    }
    case EPgDelivery::Circle:
    {
        if (!NeedGround(true)) return false;
        if (!ACireAreaEffect::Spawn(Hero, PgArea(*Def, ECireAreaShape::Circle, R->Warning, Amount), Aim, FRotator::ZeroRotator)) return Fail(TEXT("Cannot create that area here."));
        break;
    }
    case EPgDelivery::Line:
    {
        FCireAreaSpec A = PgArea(*Def, ECireAreaShape::Line, FMath::Max(.1f, R->Warning), Amount);
        A.Length = Range; A.Width = FMath::Clamp(Radius * 2.f, 80.f, 600.f);
        if (!ACireAreaEffect::Spawn(Hero, A, Feet, Direction.Rotation())) return Fail(TEXT("Cannot unleash that here."));
        Hero->SetActorRotation(Direction.Rotation()); Aim = Origin + Direction * Range; break;
    }
    case EPgDelivery::SelfBurst:
    {
        if (!ACireAreaEffect::Spawn(Hero, PgArea(*Def, ECireAreaShape::Circle, R->Warning, Amount), Feet, FRotator::ZeroRotator)) return Fail(TEXT("Cannot do that here."));
        if (R->bTaunt) for (AActor* U : PgEnemies(Hero, Origin, Radius)) if (auto* M = Cast<ACireMonster>(U)) CireThreat::Taunt(M, Hero, 2.f);
        Aim = Feet; break;
    }
    case EPgDelivery::Strike:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        CireCombat::ApplyStrike(Hero, Target, Amount, Def->Name);
        if (R->bTaunt) if (auto* M = Cast<ACireMonster>(Target)) CireThreat::Taunt(M, Hero, 2.5f);
        Aim = Target->GetActorLocation(); break;
    }
    case EPgDelivery::Lunge:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        const FVector Toward = (Target->GetActorLocation() - Origin).GetSafeNormal2D();
        FVector Ground = Target->GetActorLocation() - Toward * (PgBody(Target) + 90.f); PgGroundAim(Hero, Ground);
        if (FVector::Dist2D(Origin, Target->GetActorLocation()) > 260.f) PgMoveTo(Hero, Ground, Range);
        CireCombat::ApplyStrike(Hero, Target, Amount, Def->Name);
        Aim = Target->GetActorLocation(); break;
    }
    case EPgDelivery::Leap:
    case EPgDelivery::Dash:
    {
        if (!NeedGround(false)) return false;
        if (!PgMoveTo(Hero, Aim, Range)) return Fail(TEXT("The path is blocked."));
        const FVector Land = Hero->GetActorLocation() - FVector(0, 0, Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if (R->D == EPgDelivery::Leap) ACireAreaEffect::Spawn(Hero, PgArea(*Def, ECireAreaShape::Circle, 0.f, Amount), Land, FRotator::ZeroRotator);
        if (!R->Buff.IsNone()) CireBuffs::Apply(Hero, R->Buff, Seconds, Hero, static_cast<int32>(R->BuffPct > 0 ? R->BuffPct : BuffMagnitude()));
        Aim = Land; break;
    }
    case EPgDelivery::Pull:
    {
        AActor* Hooked = nullptr; float Best = 2.f; const FVector End = Origin + Direction * Range;
        for (AActor* U : PgEnemies(Hero, Origin + Direction * Range * .5f, Range * .5f + 200.f))
        {
            float Along = 0; const float Off = PgSegment(U->GetActorLocation(), Origin, End, &Along);
            if (Off <= FMath::Max(50.f, Def->Radius) + PgBody(U) && Along < Best && PgSight(Hero, U)) { Best = Along; Hooked = U; }
        }
        if (!Hooked) return Fail(TEXT("Nothing to catch in that direction."));
        CireCombat::ApplyStrike(Hero, Hooked, Amount, Def->Name);
        if (auto* M = Cast<ACireMonster>(Hooked); !M || !(M->IsLaneBoss() || M->bBoss))
        {
            const FVector Drop = Origin + Direction * (PgBody(Hero) + PgBody(Hooked) + 60.f);
            Hooked->SetActorLocation(FVector(Drop.X, Drop.Y, Hooked->GetActorLocation().Z), true, nullptr, ETeleportType::TeleportPhysics);
        }
        Aim = Hooked->GetActorLocation(); break;
    }
    case EPgDelivery::AllyHeal:
    {
        ACireHero* Ally = Cast<ACireHero>(Target);
        if (!Ally || Ally->TeamId != Hero->TeamId || Ally->bDead || !Ally->bDrafted) Ally = Hero;
        if (!Hero->InRange(Ally, Range + 40.f) || (Ally != Hero && !PgSight(Hero, Ally))) return Fail(TEXT("Ally is unavailable or out of range."));
        CireCombat::ApplyHealing(Hero, Ally, Amount, Def->Name);
        if (!R->Buff.IsNone()) CireBuffs::Apply(Ally, R->Buff, Seconds, Hero, static_cast<int32>(FMath::Max(1.f, R->BuffPct)));
        Aim = Ally->GetActorLocation(); break;
    }
    case EPgDelivery::PartyHeal:
    {
        for (ACireHero* Ally : PgAllies(Hero, Origin, Radius))
        {
            CireCombat::ApplyHealing(Hero, Ally, Amount, Def->Name);
            if (!R->Buff.IsNone()) CireBuffs::Apply(Ally, R->Buff, Seconds, Hero, static_cast<int32>(FMath::Max(1.f, R->BuffPct)));
        }
        Aim = Feet; break;
    }
    case EPgDelivery::SelfBuff:
    {
        if (R->Buff.IsNone()) return false;
        CireBuffs::Apply(Hero, R->Buff, Seconds, Hero, BuffMagnitude());
        if (R->bTaunt) for (AActor* U : PgEnemies(Hero, Origin, Radius)) if (auto* M = Cast<ACireMonster>(U)) CireThreat::Taunt(M, Hero, 2.f);
        Aim = Feet; break;
    }
    case EPgDelivery::Storm:
    {
        FVector Center = Feet;
        if (R->bAtAim) { if (!NeedGround(true)) return false; Center = Aim; }
        FCireAreaSpec A = PgArea(*Def, ECireAreaShape::Circle, R->Warning, 0.f);
        A.bPersistent = true; A.DurationSeconds = Seconds; A.TickInterval = .5f; A.DamagePerSecond = Amount / Seconds;
        if (!ACireAreaEffect::Spawn(Hero, A, Center, Direction.Rotation())) return Fail(TEXT("Cannot raise that here."));
        Aim = Center; break;
    }
    case EPgDelivery::Mark:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        CireBuffs::Apply(Target, PgMarked, Seconds, Hero, BuffMagnitude());
        Aim = Target->GetActorLocation(); break;
    }
    default: return false;
    }
    Hero->Mana -= Mana; Hero->Energy -= Energy;
    Hero->Cooldowns[Slot] = static_cast<float>(Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(World, Def->Base.Cooldown), Hero->CDR));
    CireSkillShop::ApplyCastLevel(Hero, Slot, Id, Mana, Energy);
    Hero->GlobalCooldown = .9f; Hero->Notice = Def->Name; Hero->ForceNetUpdate();
    CireCombat::PlayCue(Hero, bHostile ? Target : nullptr, FName(*Id), Origin, Aim, ECireSpellCue::Cast);
    UE_LOG(LogCireParagon, Verbose, TEXT("CIRE_PARAGON_CAST %s by %s"), *Id, *Hero->HeroName);
    return true;
}

bool CireParagonChampions::DescribeShape(const FString& Id, FCireHitShape& R)
{
    const FPgRecipe* X = PgFind(Id);
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    if (!X || !Def) return false;
    const float Range = Def->Range > 0 ? Def->Range : 900.f, Radius = Def->Radius > 0 ? Def->Radius : 260.f;
    auto Circle = [&](float Rad, bool bSelf) { R.Kind = ECireHitShape::Circle; R.Radius = FMath::Max(40.f, Rad); R.bFromCaster = bSelf; R.bAtTarget = !bSelf; };
    R.LingerSeconds = .6f;
    switch (X->D)
    {
    case EPgDelivery::Projectile: case EPgDelivery::Pierce:
        R.Kind = ECireHitShape::Line; R.bFromCaster = true; R.bProjectile = true; R.bGroundAim = true;
        R.Width = FMath::Max(24.f, (Def->Radius > 0 ? Def->Radius : 32.f) * 2.f); R.Length = Range; R.Speed = X->Speed; R.WarningSeconds = .05f; R.LingerSeconds = .4f; break;
    case EPgDelivery::Cone: R.Kind = ECireHitShape::Cone; R.bFromCaster = true; R.bGroundAim = true; R.Radius = Radius; R.Angle = X->Angle; R.WarningSeconds = FMath::Max(.08f, X->Warning); break;
    case EPgDelivery::Line: R.Kind = ECireHitShape::Line; R.bFromCaster = true; R.bGroundAim = true; R.Length = Range; R.Width = FMath::Clamp(Radius * 2.f, 80.f, 600.f); R.WarningSeconds = FMath::Max(.1f, X->Warning); break;
    case EPgDelivery::Circle: Circle(Radius, false); R.bGroundAim = true; R.WarningSeconds = X->Warning; R.LingerSeconds = .4f; break;
    case EPgDelivery::SelfBurst: Circle(Radius, true); R.WarningSeconds = X->Warning; break;
    case EPgDelivery::Storm: Circle(Radius, !X->bAtAim); R.bGroundAim = X->bAtAim; R.LingerSeconds = FMath::Max(.5f, Def->Duration); break;
    case EPgDelivery::Leap: Circle(Radius, false); R.bGroundAim = true; break;
    case EPgDelivery::Dash: Circle(70.f, false); R.bGroundAim = true; R.bHostileOnly = false; break;
    case EPgDelivery::Pull: R.Kind = ECireHitShape::Line; R.bFromCaster = true; R.bGroundAim = true; R.Length = Range; R.Width = FMath::Max(100.f, Def->Radius * 2.f); R.LingerSeconds = .4f; break;
    case EPgDelivery::AllyHeal: R.Kind = ECireHitShape::Unit; R.bHostileOnly = false; R.bHeal = true; R.LingerSeconds = .8f; break;
    case EPgDelivery::PartyHeal: Circle(Radius, true); R.bHostileOnly = false; R.bHeal = true; R.LingerSeconds = .8f; break;
    case EPgDelivery::SelfBuff: R.Kind = ECireHitShape::Self; R.bHostileOnly = false; R.bBuff = true; R.LingerSeconds = 1.f; break;
    case EPgDelivery::Passive: R.Kind = ECireHitShape::None; break;
    default: R.Kind = ECireHitShape::Unit; break; // strike, lunge, mark
    }
    return true;
}

// ================================================================================================ hooks
float CireParagonChampions::ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName)
{
    if (!IsValid(Target) || Amount <= 0) return Amount;
    PgLoad();
    if (GPg.Recipes.IsEmpty()) return Amount;
    float M = 1.f;
    if (const auto* E = PgActive(Target, PgGuard)) M *= 1.f - FMath::Clamp(E->Stacks / 100.f, 0.f, .7f);
    if (const auto* E = PgActive(Target, PgMarked)) M *= 1.f + FMath::Clamp(E->Stacks / 100.f, 0.f, .5f);
    if (const auto* E = Source ? PgActive(Source, PgEmpower) : nullptr) M *= 1.f + FMath::Clamp(E->Stacks / 100.f, 0.f, .6f);
    if (const auto* H = Cast<ACireHero>(Target)) M *= 1.f - PgPassive(H, TEXT("guard")) / 100.f;
    if (const auto* H = Cast<ACireHero>(Source); H && CireAbilityDB::FindByName(AbilityName)) M *= 1.f + PgPassive(H, TEXT("power")) / 100.f;
    return Amount * M;
}
void CireParagonChampions::OnAbilityHit(AActor* Source, AActor* Target, const FString& AbilityName, float Applied)
{
    auto* Hero = Cast<ACireHero>(Source);
    if (!Hero || !Hero->HasAuthority() || Applied <= 0 || Hero->bDead) return;
    const float Steal = PgPassive(Hero, TEXT("lifesteal"));
    if (Steal > 0) CireCombat::ApplyHealing(Hero, Hero, Applied * Steal / 100.f, TEXT("Lifesteal"));
}
float CireParagonChampions::MoveSpeedMultiplier(const ACireHero* Hero)
{
    if (!Hero) return 1.f;
    return (1.f + FMath::Clamp(PgFraction(Hero, PgHaste), 0.f, .6f)) * (1.f + PgPassive(Hero, TEXT("haste")) / 100.f);
}
float CireParagonChampions::AttackSpeedBonus(const ACireHero* Hero)
{
    return Hero ? FMath::Clamp(PgFraction(Hero, PgFrenzy), 0.f, .8f) + PgPassive(Hero, TEXT("frenzy")) / 100.f : 0.f;
}

// ================================================================================================ presentation
int32 CireParagonChampions::PlayFX(UWorld* World, FName SkillId, FVector From, FVector To, ECireSpellCue Cue, float Scale)
{
    if (!World || World->GetNetMode() == NM_DedicatedServer || (Cue != ECireSpellCue::Cast && Cue != ECireSpellCue::Impact)) return 0;
    const FPgRecipe* R = PgFind(SkillId.ToString());
    if (!R || !IsParagon(R->Hero)) return 0;
    const FRotator Facing = (To - From).GetSafeNormal2D().IsNearlyZero() ? FRotator::ZeroRotator : (To - From).GetSafeNormal2D().Rotation();
    int32 Spawned = 0;
    auto Spawn = [&](const FString& Path, const FVector& At)
    {
        UObject* Asset = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
        const FVector Size(FMath::Clamp(Scale, .3f, 3.f) * R->FxScale);
        USceneComponent* C = nullptr;
        if (auto* PS = Cast<UParticleSystem>(Asset)) C = UGameplayStatics::SpawnEmitterAtLocation(World, PS, At, Facing, Size, true, EPSCPoolMethod::None, true);
        else if (auto* NS = Cast<UNiagaraSystem>(Asset)) C = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, NS, At, Facing, Size, true, true, ENCPoolMethod::None, true);
        if (!C) return;
        ++Spawned;
        // Paragon loops (auras, channels) would run forever outside their ability: stop them after the ability's beat.
        TWeakObjectPtr<USceneComponent> Weak(C);
        // vfx-loop-fix: CireFabVFX::Release also hard-stops a system that ignores the deactivation.
        PgLater(World, R->FxLife, [Weak]() { CireFabVFX::Release(Cast<UFXSystemComponent>(Weak.Get())); });
    };
    if (Cue == ECireSpellCue::Cast)
    {
        for (const FString& P : R->CastFX) Spawn(P, From);
        if (!R->ImpactFX.IsEmpty())
        {
            TWeakObjectPtr<UWorld> WeakWorld(World); const FString Id = SkillId.ToString();
            const float Delay = R->Warning > 0 ? R->Warning : (R->D == EPgDelivery::Projectile || R->D == EPgDelivery::Pierce) ? 0.f : .05f;
            if (R->D != EPgDelivery::Projectile && R->D != EPgDelivery::Pierce)
                PgLater(World, Delay, [WeakWorld, Id, From, To, Scale]() { if (WeakWorld.IsValid()) CireParagonChampions::PlayFX(WeakWorld.Get(), FName(*Id), From, To, ECireSpellCue::Impact, Scale); });
        }
        if (!R->Voice.IsEmpty())
            if (auto* Sound = LoadObject<USoundBase>(nullptr, *R->Voice, nullptr, LOAD_Quiet | LOAD_NoWarn)) UGameplayStatics::PlaySoundAtLocation(World, Sound, From, .8f);
    }
    else for (const FString& P : R->ImpactFX) Spawn(P, To);
    return Spawned;
}
UTexture2D* CireParagonChampions::Portrait(const FString& ProfileId)
{
    const FPgHero* H = PgHero(ProfileId);
    if (!H || !H->bInstalled || H->Portrait.IsEmpty()) return nullptr;
    static TMap<FString, TWeakObjectPtr<UTexture2D>> Cache;
    if (auto* Found = Cache.Find(ProfileId); Found && Found->IsValid()) return Found->Get();
    auto* T = LoadObject<UTexture2D>(nullptr, *H->Portrait, nullptr, LOAD_Quiet | LOAD_NoWarn);
    Cache.Add(ProfileId, T); return T;
}
USoundBase* CireParagonChampions::Voice(const FString& ProfileId, const FString& Key)
{
    const FPgHero* H = PgHero(ProfileId);
    if (!H || !H->bInstalled) return nullptr;
    const FString& Path = Key == TEXT("lock") ? H->VoiceLock : H->VoiceSelect;
    return Path.IsEmpty() ? nullptr : LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
}
