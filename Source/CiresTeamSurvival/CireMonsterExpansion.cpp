// monster-expansion: Bestiary.json creatures, race variants, Rare Spawns and Bonus Loot Wave creatures. Docs/MonsterExpansion.md.
#include "CireMonsterExpansion.h"
#include "CireAudio.h"
#include "CireBanners.h"
#include "CireFabVFX.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireMonsterArt.h"
#include "CireNav.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireRaces.h"
#include "CireThreat.h"
#include "CireWaves.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireExpansion, Log, All);

namespace
{
TArray<CireMonsterExpansion::FCreature> GCreatures;
FLinearColor GRareColor(.2f, .95f, 1.f), GBonusColor(1.f, .78f, .12f);
TMap<TWeakObjectPtr<UWorld>, int32> GEscaped;

FLinearColor ReadColor(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FLinearColor Fallback)
{
    const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
    if (!O || !O->TryGetArrayField(Key, V) || V->Num() < 3) return Fallback;
    return FLinearColor((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber(), 1.f);
}
float NowOf(const UObject* O) { return O && O->GetWorld() ? O->GetWorld()->GetTimeSeconds() : 0.f; }
const FCireNPCArchetype* ArchetypeOf(const ACireMonster* M) { return M && M->NPCState ? M->NPCState->Archetype() : nullptr; }
}

// ============================================================================================ data
bool CireMonsterExpansion::MergeInto(FCireNPCDatabase& Database, FString& Error)
{
    GCreatures.Reset();
    FString Json;
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Bestiary.json"));
    if (!FFileHelper::LoadFileToString(Json, *Path)) { Error = TEXT("Cannot read Content/Data/Bestiary.json"); return false; }
    TSharedPtr<FJsonObject> Root;
    if (Json.Len() > 1024 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
    { Error = TEXT("Bestiary.json is not a bounded JSON object"); return false; }
    double Version = 0;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"), Version) || Version != 1) { Error = TEXT("Bestiary.json schemaVersion must be 1"); return false; }
    if (const TSharedPtr<FJsonObject>* Colors = nullptr; Root->TryGetObjectField(TEXT("specialColors"), Colors))
    { GRareColor = ReadColor(*Colors, TEXT("rare"), GRareColor); GBonusColor = ReadColor(*Colors, TEXT("bonus"), GBonusColor); }
    const TSharedPtr<FJsonObject>* Units = nullptr;
    if (!Root->TryGetObjectField(TEXT("units"), Units)) { Error = TEXT("Bestiary.json needs units"); return false; }
    FCireNPCDatabase Work = Database;
    TArray<CireMonsterExpansion::FCreature> Creatures;
    for (const auto& Pair : (*Units)->Values)
    {
        const FString Key(*Pair.Key); const FName Id(*Key);
        const TSharedPtr<FJsonObject>* UO = nullptr; const TSharedPtr<FJsonObject>* AO = nullptr;
        if (!Pair.Value->TryGetObject(UO) || !(*UO)->TryGetObjectField(TEXT("archetype"), AO)) { Error = TEXT("Bestiary unit ") + Key + TEXT(" needs an archetype object"); return false; }
        if (Work.Archetypes.Contains(Id)) { Error = TEXT("Bestiary unit ") + Key + TEXT(" duplicates an existing archetype"); return false; }
        FCireNPCArchetype Parsed;
        if (!CireNPCArchetypes::ParseArchetypeObject(Key, *AO, Parsed, Error)) { Error = TEXT("Bestiary ") + Key + TEXT(": ") + Error; return false; }
        FString Fallback, Text;
        (*UO)->TryGetStringField(TEXT("fallback"), Fallback);
        if (Fallback.IsEmpty() || !Work.Archetypes.Contains(FName(*Fallback))) { Error = TEXT("Bestiary unit ") + Key + TEXT(" needs a known fallback body"); return false; }
        Parsed.FallbackBody = FName(*Fallback);
        CireMonsterExpansion::FCreature C; C.Id = Id;
        if (const TArray<TSharedPtr<FJsonValue>>* Kinds = nullptr; (*UO)->TryGetArrayField(TEXT("kind"), Kinds))
            for (const auto& V : *Kinds) C.Kinds.Add(V->AsString());
        (*UO)->TryGetStringField(TEXT("look"), C.Look);
        if ((*UO)->TryGetStringField(TEXT("race"), Text) && !Text.IsEmpty())
        {
            C.Race = FName(*Text);
            // (CireRaces::FindRace would re-enter the archetype load: a race is known when one of its units is in the database.)
            bool bKnown = false; for (const auto& Arch : Work.Archetypes) bKnown |= Arch.Value.RaceId == C.Race;
            if (!bKnown) { Error = TEXT("Bestiary unit ") + Key + TEXT(" names unknown race ") + Text; return false; }
            // A race variant wears its race's palette on fallback bodies and reports its race; it is not one of the race's slots.
            Parsed.RaceId = C.Race;
        }
        if ((*UO)->TryGetStringField(TEXT("slot"), Text)) C.Slot = FName(*Text);
        double Every = 0; (*UO)->TryGetNumberField(TEXT("every"), Every); C.Every = FMath::Clamp(static_cast<int32>(Every), 0, 20);
        if (const TSharedPtr<FJsonObject>* Sounds = nullptr; (*UO)->TryGetObjectField(TEXT("sounds"), Sounds))
            for (const auto& S : (*Sounds)->Values) C.Sounds.Add(FName(*FString(*S.Key)), FName(*S.Value->AsString()));
        Parsed.Look = C.Look;
        Work.Archetypes.Add(Id, Parsed);
        Creatures.Add(C);
    }
    Database = MoveTemp(Work);
    GCreatures = MoveTemp(Creatures);
    UE_LOG(LogCireExpansion, Display, TEXT("CIRE_BESTIARY_LOADED creatures=%d archetypes=%d"), GCreatures.Num(), Database.Archetypes.Num());
    return true;
}

const TArray<CireMonsterExpansion::FCreature>& CireMonsterExpansion::Creatures() { CireNPCArchetypes::Get(); return GCreatures; }
const CireMonsterExpansion::FCreature* CireMonsterExpansion::Find(FName Id)
{
    return Creatures().FindByPredicate([Id](const FCreature& C) { return C.Id == Id; });
}
FLinearColor CireMonsterExpansion::SpecialColor(uint8 Kind) { Creatures(); return Kind == 2 ? GBonusColor : GRareColor; }

FName CireMonsterExpansion::VariantFor(FName Id, TMap<FName, int32>& Counters)
{
    const FCireNPCArchetype* A = CireNPCArchetypes::Find(Id);
    if (!A || A->RaceId.IsNone() || Find(Id)) return Id;
    for (const FCreature& C : Creatures())
    {
        if (C.Every <= 0 || C.Race != A->RaceId || C.Slot != A->Slot || !CireNPCArchetypes::Find(C.Id)) continue;
        int32& Count = Counters.FindOrAdd(FName(*(C.Race.ToString() + TEXT(".") + C.Slot.ToString())));
        return ++Count % C.Every == 0 ? C.Id : Id;
    }
    return Id;
}

// ============================================================================================ server
float CireMonsterExpansion::ApplyRare(ACireMonster* M, const FCireRareSpawnRules& Rules, int32 GlobalWave)
{
    if (!IsValid(M)) return 1.f;
    (void)GlobalWave;
    M->SpecialSpawn = 1;
    M->MaxHealth = M->Health = FMath::Clamp(M->MaxHealth * Rules.Health, 1.f, 1.e8f);
    M->Damage = FMath::Clamp(M->Damage * Rules.Damage, 1.f, 100000.f);
    const FCireNPCArchetype* A = ArchetypeOf(M);
    M->MonsterName = FString::Printf(TEXT("Rare %s"), A ? *A->DisplayName : *M->MonsterName);
    M->ForceNetUpdate();
    return Rules.Size;
}

float CireMonsterExpansion::ApplyBonus(ACireMonster* M, const FCireBonusWaveRules& Rules)
{
    if (!IsValid(M)) return 1.f;
    M->SpecialSpawn = 2;
    const FCireNPCArchetype* A = ArchetypeOf(M);
    M->MonsterName = A ? A->DisplayName : M->MonsterName;
    M->SpecialEscapeAt = NowOf(M) + Rules.EscapeSeconds;
    M->LeakCostOverride = 0; M->bEngaged = false;
    CireNPCCombat::Interrupt(M); CireThreat::Clear(M);
    M->ForceNetUpdate();
    return 1.f;
}

float CireMonsterExpansion::BountyMobValues(const ACireMonster* M)
{
    if (!M || M->SpecialSpawn == 0) return 0.f;
    const FCireWaveConfig& C = CireWaveDirector::Config(M->GetWorld());
    return M->SpecialSpawn == 2 ? C.Bonus.Bounty : C.Rare.Bounty;
}

int32 CireMonsterExpansion::EscapedCount(const UWorld* World)
{
    return World ? GEscaped.FindRef(TWeakObjectPtr<UWorld>(const_cast<UWorld*>(World))) : 0;
}

namespace
{
void Escape(ACireMonster* M, ACireGameMode* Mode)
{
    ++GEscaped.FindOrAdd(M->GetWorld());
    UE_LOG(LogCireExpansion, Display, TEXT("CIRE_BONUS_ESCAPE %s lane=%d health=%.0f/%.0f"), *M->GetNPCDisplayName(), M->Lane, M->Health, M->MaxHealth);
    Mode->Monsters.Remove(M);
    CireWaveDirector::Forget(M);
    CireNPCCombat::Interrupt(M); CireThreat::Clear(M);
    M->Destroy();
}
}

bool CireMonsterExpansion::TickSpecial(ACireMonster* M, ACireGameMode* Mode, float Delta)
{
    if (!IsValid(M) || !Mode || M->SpecialSpawn != 2 || M->Health <= 0) return false;
    (void)Delta;
    UWorld* World = M->GetWorld();
    const FCireBonusWaveRules& B = CireWaveDirector::Config(World).Bonus;
    if (!M->CastingAbility.IsEmpty()) CireNPCCombat::Interrupt(M);
    if (!M->Threat.IsEmpty() || M->Victim) CireThreat::Clear(M);
    M->bEngaged = false;
    if (NowOf(M) >= M->SpecialEscapeAt || CireNPCCombat::ReachedGoal(M)) { Escape(M, Mode); return true; }
    // Greedy creatures bolt from the closest champion of their lane; otherwise they trot down the road toward the town.
    ACireHero* Near = nullptr; double Best = FMath::Square(static_cast<double>(B.FleeRadius));
    for (ACireHero* H : Mode->Heroes)
    {
        if (!IsValid(H) || H->bDead || H->TeamId != M->Lane) continue;
        const double D = FVector::DistSquared2D(H->GetActorLocation(), M->GetActorLocation());
        if (D < Best) { Best = D; Near = H; }
    }
    auto* Movement = M->GetCharacterMovement();
    const FVector From = M->GetActorLocation();
    const float Slowed = M->SlowUntil > NowOf(M) ? .65f : 1.f; // champions' slows and roots are how you catch a goblin
    if (Near)
    {
        Movement->MaxWalkSpeed = M->BaseMoveSpeed * 1.15f * Slowed;
        const FVector Away = (From - Near->GetActorLocation()).GetSafeNormal2D();
        const float Length = FMath::Max(1.f, CireLanePath::RouteLength(World, M->Lane));
        const float Progress = CireLanePath::RouteProgress(World, M->Lane, From);
        const FVector Back = (CireLanePath::PointAlongRoute(World, M->Lane, FMath::Max(0.f, Progress - 700.f / Length), From.Z) - From).GetSafeNormal2D();
        FVector Dir = (Away * .65f + Back * .35f).GetSafeNormal2D();
        if (Dir.IsNearlyZero()) Dir = Away.IsNearlyZero() ? FVector(1, 0, 0) : Away;
        const FVector Goal = CireLanePath::ClampToLane(World, M->Lane, From + Dir * 600.f, 120.f);
        M->AddMovementInput(CireNav::Steer(M, Goal));
    }
    else
    {
        Movement->MaxWalkSpeed = M->BaseMoveSpeed * Slowed;
        M->AddMovementInput(CireNav::Steer(M, CireNPCCombat::RouteDestination(M)));
    }
    return true;
}

// ============================================================================================ client presentation
bool UCireExpansionPresenter::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

bool UCireExpansionPresenter::IsTickable() const
{
    const UWorld* World = GetWorld();
    return World && World->GetNetMode() != NM_DedicatedServer && !IsTemplate();
}

void UCireExpansionPresenter::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_DedicatedServer) return;
    const APlayerController* PC = World->GetFirstPlayerController();
    const ACireHero* Me = PC ? Cast<ACireHero>(PC->GetPawn()) : nullptr;
    const float Time = World->GetTimeSeconds();
    auto Play = [&](const CireMonsterExpansion::FCreature* C, const TCHAR* Role, const FVector& At)
    {
        const FName* Cue = C ? C->Sounds.Find(Role) : nullptr;
        if (Cue && CireAudio::PlayCue(World, *Cue, At)) ++SoundsPlayed;
    };
    for (TActorIterator<ACireMonster> It(World); It; ++It)
    {
        ACireMonster* M = *It;
        if (!IsValid(M)) continue;
        const bool bNew = !Seen.Contains(M);
        FSeen& S = Seen.FindOrAdd(M);
        const FName Arch = M->NPCState ? M->NPCState->ArchetypeId : NAME_None;
        const CireMonsterExpansion::FCreature* C = CireMonsterExpansion::Find(Arch);
        if (bNew || S.Archetype != Arch)
        {
            S.Archetype = Arch; S.LastHealth = M->Health; S.LastCastStarted = M->CastStartedAt;
            S.SwingSerial = M->MonsterArt ? M->MonsterArt->SwingSerial : 0;
            if (C && M->Health > 0) Play(C, TEXT("spawn"), M->GetActorLocation());
        }
        S.Location = M->GetActorLocation();
        if (M->SpecialSpawn != S.Special)
        {
            S.Special = M->SpecialSpawn;
            if (S.Special != 0 && M->Health > 0)
            {
                CireRaces::ApplySkin(M); // the rare / bonus colour on reskinned and Tripo bodies (the rim follows RankColor)
                const FLinearColor Color = CireMonsterExpansion::SpecialColor(S.Special);
                float Scale = 1.f;
                if (UFXSystemAsset* System = CireFabVFX::ResolveSchool(S.Special == 2 ? ECireSchool::Holy : ECireSchool::Arcane, CireFabVFX::ERole::Aura, &Scale))
                    if (UFXSystemComponent* Aura = CireFabVFX::SpawnAttached(System, M->GetMesh(), FVector::ZeroVector, Scale * .8f, false))
                    { CireFabVFX::ApplyTint(Aura, Color); S.Aura = Aura; }
                const bool bMine = !Me || M->Lane == Me->TeamId;
                if (bMine && S.Special == 1)
                {
                    ++RareBanners;
                    CireBanners::Show(ECireBanner::Custom, M->GetNPCDisplayName(), TEXT("A rare creature joins the wave: tougher, glowing, and it drops a rich personal chest."), TEXT("RARE SPAWN"));
                    CireAudio::PlayCue2D(World, TEXT("sting.rare"));
                }
                if (bMine && S.Special == 2 && Time - LastBonusBannerAt > 15.f)
                {
                    ++BonusBanners; LastBonusBannerAt = Time;
                    CireBanners::Show(ECireBanner::Custom, TEXT("Bonus Loot Wave"), TEXT("Treasure creatures flee down your lane with gold. Catch them before they escape!"), TEXT("BONUS LOOT WAVE"));
                    CireAudio::PlayCue2D(World, TEXT("sting.bonus_wave"));
                }
            }
        }
        if (M->Health <= 0.f)
        {
            if (!S.bDead)
            {
                S.bDead = true;
                Play(C, TEXT("death"), S.Location);
                if (S.Special == 2)
                {
                    CireAudio::PlayCue(World, TEXT("bonus.caught"), S.Location);
                    float Scale = 1.f;
                    if (UFXSystemAsset* Burst = CireFabVFX::ResolveSchool(ECireSchool::Holy, CireFabVFX::ERole::Impact, &Scale))
                        if (UFXSystemComponent* Fx = CireFabVFX::SpawnAt(World, Burst, S.Location, FRotator::ZeroRotator, Scale)) CireFabVFX::ApplyTint(Fx, CireMonsterExpansion::SpecialColor(2));
                }
                if (S.Aura.IsValid()) S.Aura->Deactivate();
            }
            S.LastHealth = 0.f;
            continue;
        }
        if (C)
        {
            const uint8 Serial = M->MonsterArt ? M->MonsterArt->SwingSerial : 0;
            if (Serial != S.SwingSerial) { S.SwingSerial = Serial; Play(C, TEXT("attack"), S.Location); }
            if (M->CastStartedAt > 0.f && !FMath::IsNearlyEqual(M->CastStartedAt, S.LastCastStarted)) { S.LastCastStarted = M->CastStartedAt; Play(C, TEXT("attack"), S.Location); }
            if (S.LastHealth > 0.f && S.LastHealth - M->Health > .015f * FMath::Max(1.f, M->MaxHealth) && Time - S.LastHitAt > .6f)
            { S.LastHitAt = Time; Play(C, TEXT("hit"), S.Location); }
        }
        S.LastHealth = M->Health;
    }
    for (auto It = Seen.CreateIterator(); It; ++It)
    {
        if (It.Key().IsValid()) continue;
        const FSeen& S = It.Value();
        // A bonus creature that vanished alive escaped with its loot: a puff and a mocking exit jingle.
        if (S.Special == 2 && !S.bDead && S.LastHealth > 0.f)
        {
            ++Escapes;
            CireAudio::PlayCue(World, TEXT("bonus.escape"), S.Location);
            float Scale = 1.f;
            if (UFXSystemAsset* Puff = CireFabVFX::ResolveSchool(ECireSchool::Arcane, CireFabVFX::ERole::Impact, &Scale))
                if (UFXSystemComponent* Fx = CireFabVFX::SpawnAt(World, Puff, S.Location, FRotator::ZeroRotator, Scale)) CireFabVFX::ApplyTint(Fx, CireMonsterExpansion::SpecialColor(2));
        }
        It.RemoveCurrent();
    }
}
