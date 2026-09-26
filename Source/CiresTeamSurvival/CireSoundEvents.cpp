#include "CireSoundEvents.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#include "CireAudio.h"
#include "CireAbilityShapes.h"
#include "CireAbilityDB.h"
#include "CireNPCArchetypes.h"
#include "CireFootsteps.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireNPCState.h"
#include "CireRealm.h"
#include "CireSpellPresentation.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireSoundEvents, Log, All);

namespace
{
CireSoundEvents::FData GData;
bool GLoaded = false;

FName ReadName(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
{
    FString S; return O && O->TryGetStringField(Field, S) && !S.IsEmpty() ? FName(*S) : NAME_None;
}
void ReadNameMap(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field, TMap<FName, FName>& Out, bool bNormalizeKeys = false)
{
    const TSharedPtr<FJsonObject>* O = nullptr;
    if(!Root->TryGetObjectField(Field, O)) return;
    for(const auto& Pair : (*O)->Values)
    {
        if(Pair.Key == TEXT("notes")) continue;
        FString V; if(!Pair.Value->TryGetString(V)) continue;
        Out.Add(bNormalizeKeys ? CireSoundEvents::Normalize(FString(Pair.Key)) : FName(*Pair.Key), FName(*V));
    }
}
void ReadAbility(const TSharedPtr<FJsonObject>& O, CireSoundEvents::FAbilitySound& Row)
{
    Row.Element = ReadName(O, TEXT("element"));
    Row.Kind = ReadName(O, TEXT("kind"));
    Row.Weapon = ReadName(O, TEXT("weapon"));
    const TSharedPtr<FJsonObject>* Cues = nullptr;
    if(O->TryGetObjectField(TEXT("cues"), Cues))
        for(const auto& Pair : (*Cues)->Values) { FString V; if(Pair.Value->TryGetString(V)) Row.Overrides.Add(FName(*Pair.Key), FName(*V)); }
}

FString Fill(const FString& Template, FName Element, FName Weapon, FName Armor)
{
    FString S = Template;
    S.ReplaceInline(TEXT("{element}"), *Element.ToString());
    S.ReplaceInline(TEXT("{weapon}"), *Weapon.ToString());
    S.ReplaceInline(TEXT("{armor}"), *Armor.ToString());
    return S;
}

const APawn* LocalPawn(UWorld* World)
{
    const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    return PC ? PC->GetPawn() : nullptr;
}

bool Alive(const ACharacter* C)
{
    if(const ACireHero* H = Cast<ACireHero>(C)) return H->bDrafted && !H->bDead;
    if(const ACireMonster* M = Cast<ACireMonster>(C)) return M->Health > 0.f;
    return IsValid(C);
}
}

// ---------------------------------------------------------------------------------------------
FName CireSoundEvents::Normalize(const FString& Id)
{
    FString S = Id.ToLower();
    S.TrimStartAndEndInline();
    S.ReplaceInline(TEXT(" "), TEXT("_")); S.ReplaceInline(TEXT("'"), TEXT("")); S.ReplaceInline(TEXT("-"), TEXT("_"));
    return FName(*S);
}

const CireSoundEvents::FData& CireSoundEvents::Data(bool bReload)
{
    if(GLoaded && !bReload) return GData;
    GLoaded = true; GData = FData();
    FString Text; TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/AudioEvents.json"))) ||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
    {
        UE_LOG(LogCireSoundEvents, Warning, TEXT("AudioEvents.json missing or invalid; spells keep the legacy sounds"));
        return GData;
    }
    const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
    if(Root->TryGetArrayField(TEXT("elements"), Elements)) for(const auto& V : *Elements) GData.Elements.Add(FName(*V->AsString()));
    ReadNameMap(Root, TEXT("schools"), GData.SchoolElements, true);
    ReadNameMap(Root, TEXT("weaponAliases"), GData.WeaponAliases, true);
    ReadNameMap(Root, TEXT("armorLayers"), GData.ArmorLayers);
    ReadNameMap(Root, TEXT("armorWeapons"), GData.ArmorWeapons);
    ReadNameMap(Root, TEXT("outcomes"), GData.Outcomes);
    ReadNameMap(Root, TEXT("deaths"), GData.Deaths);
    ReadNameMap(Root, TEXT("deathClasses"), GData.DeathClasses);
    ReadNameMap(Root, TEXT("ui"), GData.Ui);
    const TSharedPtr<FJsonObject>* Kinds = nullptr;
    if(Root->TryGetObjectField(TEXT("kinds"), Kinds))
        for(const auto& Pair : (*Kinds)->Values)
            if(const TSharedPtr<FJsonObject> O = Pair.Value->AsObject())
            {
                TMap<FName, FString>& Slots = GData.Kinds.Add(FName(*Pair.Key));
                for(const auto& Slot : O->Values) { FString V; if(Slot.Value->TryGetString(V)) Slots.Add(FName(*Slot.Key), V); }
            }
    const TSharedPtr<FJsonObject>* Weapons = nullptr;
    if(Root->TryGetObjectField(TEXT("weapons"), Weapons))
        for(const auto& Pair : (*Weapons)->Values)
            if(const TSharedPtr<FJsonObject> O = Pair.Value->AsObject())
            {
                FWeapon W;
                for(const auto& Slot : O->Values)
                {
                    FString V;
                    if(Slot.Key == TEXT("kind")) { if(Slot.Value->TryGetString(V)) W.Kind = FName(*V); }
                    else if(Slot.Key == TEXT("ranged")) { if(Slot.Value->TryGetString(V)) W.Ranged = FName(*V); }
                    else if(Slot.Key == TEXT("rangedAbove")) W.RangedAbove = static_cast<float>(Slot.Value->AsNumber());
                    else if(Slot.Value->TryGetString(V)) W.Slots.Add(FName(*Slot.Key), FName(*V));
                }
                GData.Weapons.Add(FName(*Pair.Key), W);
            }
    for(const TCHAR* Table : {TEXT("abilities"), TEXT("extraIds")})
    {
        const TSharedPtr<FJsonObject>* Abilities = nullptr;
        if(!Root->TryGetObjectField(Table, Abilities)) continue;
        for(const auto& Pair : (*Abilities)->Values)
            if(const TSharedPtr<FJsonObject> O = Pair.Value->AsObject())
            {
                FAbilitySound Row; ReadAbility(O, Row);
                GData.Abilities.Add(Normalize(FString(Pair.Key)), Row);
                FString Name; if(O->TryGetStringField(TEXT("name"), Name)) GData.Abilities.FindOrAdd(Normalize(Name), Row);
            }
    }
    const TSharedPtr<FJsonObject>* Layers = nullptr;
    if(Root->TryGetObjectField(TEXT("layers"), Layers))
    {
        FString V; if((*Layers)->TryGetStringField(TEXT("impact"), V)) GData.ImpactLayerTemplate = V;
        const TArray<TSharedPtr<FJsonValue>>* Crit = nullptr;
        if((*Layers)->TryGetArrayField(TEXT("critical"), Crit)) for(const auto& C : *Crit) GData.CriticalLayers.Add(FName(*C->AsString()));
    }
    const TSharedPtr<FJsonObject>* Reactions = nullptr;
    if(Root->TryGetObjectField(TEXT("reactions"), Reactions))
    {
        GData.PlayerHit = ReadName(*Reactions, TEXT("playerHit"));
        GData.PlayerHitHeavy = ReadName(*Reactions, TEXT("playerHitHeavy"));
        double N = 0; if((*Reactions)->TryGetNumberField(TEXT("heavyFraction"), N)) GData.HeavyHitFraction = FMath::Clamp(static_cast<float>(N), .01f, 1.f);
    }
    const TSharedPtr<FJsonObject>* Mix = nullptr;
    if(Root->TryGetObjectField(TEXT("mix"), Mix))
    {
        double N = 0;
        if((*Mix)->TryGetNumberField(TEXT("otherVolume"), N)) GData.OtherVolume = FMath::Clamp(static_cast<float>(N), 0.f, 2.f);
        if((*Mix)->TryGetNumberField(TEXT("localPriorityBoost"), N)) GData.LocalPriorityBoost = static_cast<float>(N);
        if((*Mix)->TryGetNumberField(TEXT("incomingPriorityBoost"), N)) GData.IncomingPriorityBoost = static_cast<float>(N);
        if((*Mix)->TryGetNumberField(TEXT("castLoopRadius"), N)) GData.CastLoopRadius = static_cast<float>(N);
        if((*Mix)->TryGetNumberField(TEXT("deathRadius"), N)) GData.DeathRadius = static_cast<float>(N);
        if((*Mix)->TryGetNumberField(TEXT("areaLoopVolume"), N)) GData.AreaLoopVolume = static_cast<float>(N);
        if((*Mix)->TryGetNumberField(TEXT("maxCastLoops"), N)) GData.MaxCastLoops = FMath::Clamp(static_cast<int32>(N), 0, 32);
        GData.CastLoopDefault = ReadName(*Mix, TEXT("castLoopSlot"));
    }
    GData.bValid = !GData.Kinds.IsEmpty() && !GData.Elements.IsEmpty();
    return GData;
}

FName CireSoundEvents::SlotName(ECireSpellCue Cue)
{
    switch(Cue)
    {
    case ECireSpellCue::Cast: return TEXT("cast");
    case ECireSpellCue::Launch: return TEXT("launch");
    case ECireSpellCue::Impact: return TEXT("impact");
    case ECireSpellCue::Critical: return TEXT("critical");
    case ECireSpellCue::Projectile: return TEXT("projectile");
    case ECireSpellCue::Wall: return TEXT("wall");
    case ECireSpellCue::Protection: return TEXT("protection");
    }
    return TEXT("cast");
}

FName CireSoundEvents::ElementForSchool(int32 School)
{
    const FData& D = Data();
    const FName Key = Normalize(CireAbilityShapes::SchoolName(static_cast<ECireSchool>(FMath::Clamp(School, 0, static_cast<int32>(ECireSchool::Count) - 1))));
    if(const FName* E = D.SchoolElements.Find(Key)) return *E;
    return TEXT("physical");
}

const CireSoundEvents::FAbilitySound* CireSoundEvents::FindAbility(FName Skill)
{
    const FData& D = Data();
    const FName Key = Normalize(Skill.ToString());
    if(const FAbilitySound* Row = D.Abilities.Find(Key)) return Row;
    // "Frost Bind (rank 2)", "ember_lance_proc": strip a trailing qualifier once.
    FString S = Key.ToString(), Left, Right;
    if(S.Split(TEXT("_("), &Left, &Right)) if(const FAbilitySound* Row = D.Abilities.Find(FName(*Left))) return Row;
    return nullptr;
}

FName CireSoundEvents::WeaponFor(FName SkillOrStyle)
{
    const FData& D = Data();
    const FName Key = Normalize(SkillOrStyle.ToString());
    if(const FName* W = D.WeaponAliases.Find(Key)) return *W;
    if(D.Weapons.Contains(Key)) return Key;
    return NAME_None;
}

CireSoundEvents::FResolved CireSoundEvents::Resolve(const FQuery& Q)
{
    const FData& D = Data();
    FResolved R;
    if(!D.bValid) return R;
    const FAbilitySound* Row = FindAbility(Q.Skill);
    const FName AliasWeapon = WeaponFor(Q.Skill);
    if(Row) { R.Element = Row->Element; R.Kind = Row->Kind; R.Weapon = Row->Weapon; R.Source = TEXT("table"); }
    else if(!AliasWeapon.IsNone())
    {
        R.Weapon = AliasWeapon; R.Element = TEXT("physical");
        const FWeapon* W = D.Weapons.Find(AliasWeapon);
        R.Kind = W ? W->Kind : FName(TEXT("melee")); R.Source = TEXT("alias");
    }
    else
    {
        const int32 School = Q.SchoolOverride >= 0 ? Q.SchoolOverride : static_cast<int32>(CireAbilityShapes::SchoolFor(Q.Skill));
        R.Element = ElementForSchool(School); R.Source = TEXT("school");
        R.Kind = R.Element == TEXT("physical") ? (Q.Distance > 450.f ? FName(TEXT("shot")) : FName(TEXT("melee"))) : FName(TEXT("spell"));
    }
    if(R.Element.IsNone()) R.Element = TEXT("physical");
    if(R.Kind.IsNone()) R.Kind = TEXT("spell");
    // Weapon: the row's, else the caster's basic attack, else the kind's default.
    if(R.Weapon.IsNone() || R.Weapon == TEXT("caster")) R.Weapon = Q.CasterWeapon;
    if(R.Weapon.IsNone()) R.Weapon = R.Kind == TEXT("shot") ? FName(TEXT("bow")) : FName(TEXT("sword"));
    if(const FWeapon* W = D.Weapons.Find(R.Weapon); W && !W->Ranged.IsNone() && W->RangedAbove > 0.f && Q.Distance > W->RangedAbove) R.Weapon = W->Ranged;
    // A ranged weapon on a melee-kind ability (a bow champion's physical strike) plays as a shot.
    if(R.Kind == TEXT("melee")) if(const FWeapon* W = D.Weapons.Find(R.Weapon); W && W->Kind == TEXT("shot") && Q.Distance > 450.f) R.Kind = TEXT("shot");

    // Primary cue: explicit override, else kind template (weapon templates read "weapon:<slot>").
    auto Template = [&](FName Kind, FName Slot) -> FName
    {
        const TMap<FName, FString>* Slots = D.Kinds.Find(Kind);
        const FString* T = Slots ? Slots->Find(Slot) : nullptr;
        if(!T || T->IsEmpty() || *T == TEXT("none")) return NAME_None;
        if(T->StartsWith(TEXT("weapon:")))
        {
            const FWeapon* W = D.Weapons.Find(R.Weapon);
            const FName* Cue = W ? W->Slots.Find(FName(*T->Mid(7))) : nullptr;
            if(!Cue) if(const FWeapon* Sword = D.Weapons.Find(TEXT("sword"))) Cue = Sword->Slots.Find(FName(*T->Mid(7)));
            return Cue ? *Cue : NAME_None;
        }
        return FName(*Fill(*T, R.Element, R.Weapon, NAME_None));
    };
    FName Primary = NAME_None;
    if(Row) if(const FName* O = Row->Overrides.Find(Q.Slot)) Primary = *O;
    if(Primary.IsNone()) Primary = Template(R.Kind, Q.Slot);
    if(!Primary.IsNone() && !CireAudio::HasCue(Primary)) Primary = Template(TEXT("spell"), Q.Slot); // never silent on a data gap
    if(!Primary.IsNone() && Primary != TEXT("none") && CireAudio::HasCue(Primary)) R.Cues.Add(Primary);

    // Layers: critical ring; physical hits add the flesh/armour/stone body layer of the target.
    const bool bHit = Q.Slot == TEXT("impact") || Q.Slot == TEXT("critical");
    if(Q.Slot == TEXT("critical")) for(const FName& L : D.CriticalLayers) if(CireAudio::HasCue(L)) R.Cues.AddUnique(L);
    const bool bBody = R.Element == TEXT("physical") && R.Kind != TEXT("heal") && R.Kind != TEXT("buff") && R.Kind != TEXT("guard");
    if(bHit && bBody && !Q.TargetArmor.IsNone())
        if(const FName* Layer = D.ArmorLayers.Find(Q.TargetArmor); Layer && *Layer != TEXT("none"))
        {
            const FName Cue(*Fill(D.ImpactLayerTemplate, R.Element, R.Weapon, *Layer));
            if(CireAudio::HasCue(Cue)) R.Cues.AddUnique(Cue);
        }
    return R;
}

ACharacter* CireSoundEvents::CharacterNear(UWorld* World, const FVector& At, float Radius)
{
    if(!World) return nullptr;
    ACharacter* Best = nullptr; float BestD = Radius * Radius;
    for(TCireActorIterator<ACharacter> It(World); It; ++It)
    {
        ACharacter* C = *It;
        if(!IsValid(C) || C->IsHidden()) continue;
        const float D = FVector::DistSquared(C->GetActorLocation(), At);
        if(D < BestD) { BestD = D; Best = C; }
    }
    return Best;
}

FName CireSoundEvents::WeaponOf(const ACharacter* Character)
{
    if(const ACireHero* Hero = Cast<ACireHero>(Character)) return WeaponFor(FName(*Hero->BasicAttackStyle()));
    if(Character)
    {
        const FName Class = CireFootsteps::ForCharacter(Character).Class;
        if(const FName* W = Data().ArmorWeapons.Find(Class)) return *W;
    }
    return NAME_None;
}

bool CireSoundEvents::PlaySpellCue(UWorld* World, FName Skill, ECireSpellCue Cue, const FVector& From, const FVector& To, float Scale)
{
    const FData& D = Data();
    if(!World || !D.bValid) return false;
    const bool bOrigin = Cue == ECireSpellCue::Cast || Cue == ECireSpellCue::Launch;
    ACharacter* Caster = CharacterNear(World, From, 140.f);
    ACharacter* Target = bOrigin ? nullptr : CharacterNear(World, To, 170.f);
    FQuery Q; Q.Skill = Skill; Q.Slot = SlotName(Cue); Q.Distance = FVector::Dist(From, To);
    Q.CasterWeapon = WeaponOf(Caster);
    if(Target) Q.TargetArmor = CireFootsteps::ForCharacter(Target).Class;
    const FResolved R = Resolve(Q);
    if(R.Cues.IsEmpty()) return false;
    const APawn* Me = LocalPawn(World);
    const bool bMine = Me && Caster == Me, bIncoming = Me && Target == Me && !bMine;
    FCirePlayParams P;
    const FVector At = bOrigin ? From : To;
    P.Location = &At;
    P.bLocal = bMine || bIncoming;
    P.PriorityBoost = bMine ? D.LocalPriorityBoost : bIncoming ? D.IncomingPriorityBoost : 0.f;
    P.Volume = (P.bLocal ? 1.f : D.OtherVolume) * FMath::Clamp(.75f + .25f * Scale, .6f, 1.4f);
    bool bAny = false;
    for(const FName& Id : R.Cues) bAny |= CireAudio::PlayCueEx(World, Id, P) != nullptr;
    if(UCireAudioSubsystem* Audio = UCireAudioSubsystem::Get(World)) ++Audio->Events.Spells;
    return true; // handled (even when rate-limited) so the legacy sound never doubles it
}

bool CireSoundEvents::StartLoop(UAudioComponent* Component, FName Skill, FName Slot)
{
    if(!Component || !Data().bValid) return false;
    FQuery Q; Q.Skill = Skill; Q.Slot = Slot;
    if(const AActor* Owner = Component->GetOwner()) if(const AActor* Followed = Owner->GetOwner()) if(const ACharacter* C = Cast<ACharacter>(Followed)) Q.CasterWeapon = WeaponOf(C);
    const FResolved R = Resolve(Q);
    for(const FName& Id : R.Cues)
        if(CireAudio::CueIsLoop(Id) && CireAudio::ConfigureComponent(Component, Id, Slot == TEXT("area") ? Data().AreaLoopVolume : 1.f))
        {
            Component->FadeIn(.2f, 1.f);
            return true;
        }
    return false;
}

// ---------------------------------------------------------------------------------------------
void FCireSoundEventTracker::Reset()
{
    for(auto& Pair : Casting) if(Pair.Value.IsValid()) Pair.Value->Stop();
    LastSequence = 0; bPrimed = false; LastHealth.Reset(); LastLocation.Reset(); DeathKey.Reset(); HeroDead.Reset();
    Casting.Reset(); CastingId.Reset(); LastNotice.Reset(); bWasReady = false; ReadyWave = -1; ScanTimer = 0.f;
}

FString FCireSoundEventTracker::StatsLine() const
{
    return FString::Printf(TEXT("spells=%d blocks=%d deflects=%d avoids=%d reactions=%d deaths=%d cast_loops=%d ui=%d"),
        Spells, Blocks, Deflects, Avoids, Reactions, Deaths, CastLoops, UiEvents);
}

void FCireSoundEventTracker::Tick(UCireAudioSubsystem& Audio, float DeltaSeconds)
{
    if(!CireSoundEvents::Data().bValid) return;
    TickCombatEvents(Audio);
    TickUnits(Audio, DeltaSeconds);
    TickUi(Audio);
}

void FCireSoundEventTracker::TickCombatEvents(UCireAudioSubsystem& Audio)
{
    using namespace CireSoundEvents;
    UWorld* World = Audio.GetWorld();
    ACireController* PC = World ? Cast<ACireController>(World->GetFirstPlayerController()) : nullptr;
    if(!PC) return;
    const FData& D = Data();
    const ACireHero* Me = Cast<ACireHero>(PC->GetPawn());
    if(!bPrimed) { bPrimed = true; LastSequence = PC->CombatEventSequence; return; } // never replay history
    for(const FCireCombatEvent& E : PC->CombatEvents)
    {
        if(E.Sequence <= LastSequence) continue;
        LastSequence = FMath::Max(LastSequence, E.Sequence);
        const bool bMine = E.bLocalSource, bIncoming = E.bLocalTarget;
        const FVector At = E.Location - FVector(0, 0, 60);
        FCirePlayParams P; P.Location = &At; P.bLocal = bMine || bIncoming;
        P.PriorityBoost = bIncoming ? D.IncomingPriorityBoost + 10.f : bMine ? D.LocalPriorityBoost : 0.f;
        P.Volume = P.bLocal ? 1.f : D.OtherVolume;
        const uint8 Outcome = static_cast<uint8>(E.Outcome);
        if(Outcome == static_cast<uint8>(ECireHitOutcome::Block))
        {
            // Shield block (30% of physical hits on shield tanks): always a distinct clang; ranged hits ricochet.
            const FName Weapon = WeaponFor(FName(*E.AbilityName));
            const FWeapon* W = D.Weapons.Find(Weapon);
            const bool bRanged = W && W->Kind == TEXT("shot");
            const FName* Cue = D.Outcomes.Find(bRanged ? FName(TEXT("deflect")) : FName(TEXT("block")));
            if(!Cue) Cue = D.Outcomes.Find(TEXT("block"));
            P.PriorityBoost += 25.f; // blocks must always be heard
            if(Cue) CireAudio::PlayCueEx(World, *Cue, P);
            ++(bRanged ? Deflects : Blocks);
            continue;
        }
        if(Outcome == static_cast<uint8>(ECireHitOutcome::Miss) || Outcome == static_cast<uint8>(ECireHitOutcome::Dodge) || Outcome > static_cast<uint8>(ECireHitOutcome::Dodge))
        {
            if(!P.bLocal) continue; // other units' misses are noise in a 5v5
            const FName Key = Outcome == static_cast<uint8>(ECireHitOutcome::Miss) ? FName(TEXT("miss")) : Outcome == static_cast<uint8>(ECireHitOutcome::Dodge) ? FName(TEXT("dodge")) : FName(TEXT("resist"));
            if(const FName* Cue = D.Outcomes.Find(Key)) { CireAudio::PlayCueEx(World, *Cue, P); ++Avoids; }
            continue;
        }
        // Hit reactions: the local player taking damage (heavier hits are louder and duck the score).
        if(bIncoming && !E.bHealing && E.Amount > 0.f && Me && Me->MaxHealth > 0.f)
        {
            const float Fraction = E.Amount / Me->MaxHealth;
            const bool bHeavy = Fraction >= D.HeavyHitFraction;
            const FName Cue = bHeavy && !D.PlayerHitHeavy.IsNone() ? D.PlayerHitHeavy : D.PlayerHit;
            FCirePlayParams R; R.bLocal = true; R.PriorityBoost = D.IncomingPriorityBoost;
            R.Volume = FMath::Clamp(.55f + Fraction * 3.f, .55f, 1.2f);
            if(!Cue.IsNone() && CireAudio::PlayCueEx(World, Cue, R)) ++Reactions;
        }
    }
}

void FCireSoundEventTracker::TickUnits(UCireAudioSubsystem& Audio, float DeltaSeconds)
{
    using namespace CireSoundEvents;
    UWorld* World = Audio.GetWorld();
    const FData& D = Data();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    const APawn* Me = PC ? PC->GetPawn() : nullptr;
    const FVector Ear = CireAudio::ListenerTransform(World).GetLocation();
    auto DeathCue = [&](FName Key) -> FName { const FName* C = D.Deaths.Find(Key); return C ? *C : NAME_None; };
    auto Observable = [&](const AActor* A) { return !PC || CireRealm::CanObserve(PC, A); };

    // ---- heroes: deaths and timed-cast channel loops ----
    for(TCireActorIterator<ACireHero> It(World); It; ++It)
    {
        ACireHero* H = *It;
        if(!IsValid(H) || !H->bDrafted) continue;
        const TWeakObjectPtr<AActor> Key(H);
        bool& WasDead = HeroDead.FindOrAdd(Key, H->bDead);
        const float Distance = FVector::Dist(H->GetActorLocation(), Ear);
        if(H->bDead && !WasDead && Observable(H) && (H == Me || Distance < D.DeathRadius))
        {
            FVector At = H->GetActorLocation();
            FCirePlayParams P; P.Location = &At; P.bLocal = H == Me; P.PriorityBoost = H == Me ? 60.f : 15.f;
            if(const FName Cue = DeathCue(TEXT("hero")); !Cue.IsNone()) CireAudio::PlayCueEx(World, Cue, P);
            if(H == Me) if(const FName Cue = DeathCue(TEXT("player")); !Cue.IsNone()) { FCirePlayParams S; S.bLocal = true; CireAudio::PlayCueEx(World, Cue, S); }
            ++Deaths;
        }
        WasDead = H->bDead;
        // Channel loop while a timed cast runs (the caster's element; the local player's always wins a slot).
        const FString CastId = H->bDead ? FString() : H->CastSkill.IsNone() ? FString() : H->CastSkill.ToString();
        FString& Last = CastingId.FindOrAdd(Key);
        if(CastId != Last)
        {
            if(TWeakObjectPtr<UAudioComponent>* Loop = Casting.Find(Key); Loop && Loop->IsValid()) (*Loop)->FadeOut(.25f, 0.f);
            Casting.Remove(Key);
            if(!CastId.IsEmpty() && Observable(H) && (H == Me || Distance < D.CastLoopRadius) && (H == Me || Casting.Num() < D.MaxCastLoops))
            {
                FQuery Q; Q.Skill = FName(*CastId); Q.Slot = TEXT("channel"); Q.CasterWeapon = WeaponOf(H);
                const TArray<FName> Cues = Resolve(Q).Cues;
                if(Cues.Num() > 0)
                {
                    // A loop runs for the whole cast; a one-shot (a bow draw, a pistol cock) marks its start.
                    FCirePlayParams P; P.Attach = H->GetRootComponent(); P.bLocal = H == Me; P.PriorityBoost = H == Me ? D.LocalPriorityBoost : 0.f;
                    P.Volume = H == Me ? 1.f : D.OtherVolume;
                    if(UAudioComponent* C = CireAudio::PlayCueEx(World, Cues[0], P))
                    {
                        ++CastLoops;
                        if(CireAudio::CueIsLoop(Cues[0])) { C->FadeIn(.15f, 1.f); Casting.Add(Key, C); }
                    }
                }
            }
            Last = CastId;
        }
    }

    // ---- monsters: deaths by body type (and bosses duck the score) ----
    for(TCireActorIterator<ACireMonster> It(World); It; ++It)
    {
        ACireMonster* M = *It;
        if(!IsValid(M)) continue;
        const TWeakObjectPtr<AActor> Key(M);
        const float Fraction = M->MaxHealth > 0.f ? FMath::Max(0.f, M->Health) / M->MaxHealth : 0.f;
        const float* Prev = LastHealth.Find(Key);
        LastLocation.Add(Key, M->GetActorLocation());
        if(!DeathKey.Contains(Key))
        {
            const FName Class = CireFootsteps::ForCharacter(M).Class;
            const FName* Death = D.DeathClasses.Find(Class);
            DeathKey.Add(Key, M->GetNPCClassification() == ECireNPCClass::Boss ? FName(TEXT("boss")) : Death ? *Death : FName(TEXT("humanoid")));
        }
        if(Prev && *Prev > 0.f && Fraction <= 0.f && !M->IsHidden() && Observable(M) && FVector::Dist(M->GetActorLocation(), Ear) < D.DeathRadius)
        {
            FVector At = M->GetActorLocation();
            FCirePlayParams P; P.Location = &At; P.PriorityBoost = DeathKey[Key] == TEXT("boss") ? 50.f : 0.f; P.Volume = D.OtherVolume;
            if(const FName Cue = DeathCue(DeathKey[Key]); !Cue.IsNone() && CireAudio::PlayCueEx(World, Cue, P)) ++Deaths;
        }
        LastHealth.Add(Key, Fraction);
        // Monster timed casts: channel loop for elite/boss casters near the listener.
        const FString CastId = M->Health > 0.f ? M->CastingAbility : FString();
        FString& Last = CastingId.FindOrAdd(Key);
        if(CastId != Last)
        {
            if(TWeakObjectPtr<UAudioComponent>* Loop = Casting.Find(Key); Loop && Loop->IsValid()) (*Loop)->FadeOut(.25f, 0.f);
            Casting.Remove(Key);
            if(!CastId.IsEmpty() && Observable(M) && Casting.Num() < D.MaxCastLoops && FVector::Dist(M->GetActorLocation(), Ear) < D.CastLoopRadius)
            {
                FQuery Q; Q.Skill = FName(*CastId); Q.Slot = TEXT("channel");
                for(const FName& Cue : Resolve(Q).Cues)
                    if(CireAudio::CueIsLoop(Cue))
                    {
                        FCirePlayParams P; P.Attach = M->GetRootComponent(); P.Volume = D.OtherVolume;
                        if(UAudioComponent* C = CireAudio::PlayCueEx(World, Cue, P)) { C->FadeIn(.15f, 1.f); Casting.Add(Key, C); ++CastLoops; }
                        break;
                    }
            }
            Last = CastId;
        }
    }
    // Forget destroyed units; a unit that vanished while nearly dead also counts as a death (no final replication).
    for(auto It = LastHealth.CreateIterator(); It; ++It)
    {
        if(It->Key.IsValid()) continue;
        if(It->Value > 0.f && It->Value < .3f)
            if(const FVector* At = LastLocation.Find(It->Key); At && FVector::Dist(*At, Ear) < D.DeathRadius)
                if(const FName* K = DeathKey.Find(It->Key))
                    if(const FName Cue = DeathCue(*K); !Cue.IsNone())
                    {
                        const FVector Where = *At; FCirePlayParams P; P.Location = &Where; P.Volume = D.OtherVolume;
                        if(CireAudio::PlayCueEx(World, Cue, P)) ++Deaths;
                    }
        LastLocation.Remove(It->Key); DeathKey.Remove(It->Key); It.RemoveCurrent();
    }
    for(auto It = Casting.CreateIterator(); It; ++It) if(!It->Key.IsValid()) { if(It->Value.IsValid()) It->Value->FadeOut(.2f, 0.f); It.RemoveCurrent(); }
    for(auto It = CastingId.CreateIterator(); It; ++It) if(!It->Key.IsValid()) It.RemoveCurrent();
    for(auto It = HeroDead.CreateIterator(); It; ++It) if(!It->Key.IsValid()) It.RemoveCurrent();
}

void FCireSoundEventTracker::TickUi(UCireAudioSubsystem& Audio)
{
    using namespace CireSoundEvents;
    UWorld* World = Audio.GetWorld();
    const FData& D = Data();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    const ACireHero* Me = PC ? Cast<ACireHero>(PC->GetPawn()) : nullptr;
    const ACireHUD* HUD = PC ? Cast<ACireHUD>(PC->GetHUD()) : nullptr;
    const ACireGameState* State = World ? World->GetGameState<ACireGameState>() : nullptr;
    auto Ui = [&](const TCHAR* Key) { if(const FName* Cue = D.Ui.Find(Key)) { FCirePlayParams P; P.bLocal = true; if(CireAudio::PlayCueEx(World, *Cue, P)) ++UiEvents; } };
    // READY TO CONTINUE / READY UP (HUD button or the shop's): a confirm chime when the local player readies.
    if(HUD && State)
    {
        const bool bReady = HUD->IsBreatherReadyLocal(State->Wave);
        if(State->Wave != ReadyWave) { ReadyWave = State->Wave; bWasReady = bReady; }
        else if(bReady != bWasReady) { Ui(bReady ? TEXT("ready") : TEXT("unready")); bWasReady = bReady; }
    }
    // Failed actions announced through the hero notice (not enough mana/energy, cooldown, range...).
    if(Me)
    {
        if(Me->Notice != LastNotice)
        {
            const FString N = Me->Notice.ToLower();
            if(!N.IsEmpty())
            {
                if(N.Contains(TEXT("not enough mana")) || N.Contains(TEXT("mana or energy"))) Ui(TEXT("errorMana"));
                else if(N.Contains(TEXT("not enough energy"))) Ui(TEXT("errorEnergy"));
                else if(N.Contains(TEXT("gold"))) Ui(TEXT("errorGold"));
                else if(N.Contains(TEXT("not ")) || N.Contains(TEXT("cannot")) || N.Contains(TEXT("can't")) || N.Contains(TEXT("out of range")) || N.Contains(TEXT("cooldown")) || N.Contains(TEXT("no target")))
                    Ui(TEXT("error"));
            }
            LastNotice = Me->Notice;
        }
    }
}

// ---------------------------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING
bool CireSoundEvents::RunSmoke(UWorld* World, int32& OutChecks)
{
    bool Pass = true;
    auto Check = [&](bool bValue, const FString& Name) { ++OutChecks; Pass &= bValue; if(!bValue) UE_LOG(LogCireSoundEvents, Error, TEXT("CIRE_AUDIO_ASSERT %s"), *Name); };
    const FData& D = Data(true);
    Check(D.bValid, TEXT("AudioEvents.json parses (elements + kinds)"));
    static const TCHAR* Required[] = {TEXT("physical"), TEXT("fire"), TEXT("frost"), TEXT("nature"), TEXT("shadow"), TEXT("arcane"), TEXT("holy"), TEXT("earth"), TEXT("water"), TEXT("lightning")};
    static const TCHAR* Slots[] = {TEXT("cast"), TEXT("channel"), TEXT("projectile"), TEXT("impact"), TEXT("heal")};
    for(const TCHAR* E : Required)
    {
        Check(D.Elements.Contains(FName(E)), FString::Printf(TEXT("element %s listed"), E));
        for(const TCHAR* S : Slots)
        {
            const FName Cue(*FString::Printf(TEXT("spell.%s.%s"), E, S));
            Check(CireAudio::HasCue(Cue), TEXT("element cue exists: ") + Cue.ToString());
            Check(CireAudio::CueIsLoop(Cue) == (FString(S) == TEXT("channel") || FString(S) == TEXT("projectile")), TEXT("loop flag matches slot: ") + Cue.ToString());
        }
    }
    for(int32 S = 0; S < static_cast<int32>(ECireSchool::Count); ++S)
        Check(D.Elements.Contains(ElementForSchool(S)), TEXT("school maps to an element: ") + CireAbilityShapes::SchoolName(static_cast<ECireSchool>(S)));
    // Weapons: every weapon has its slots and they exist.
    static const TCHAR* Weapons[] = {TEXT("sword"), TEXT("axe"), TEXT("mace"), TEXT("dagger"), TEXT("glaive"), TEXT("spear"), TEXT("claws"), TEXT("staff"), TEXT("bow"), TEXT("crossbow"), TEXT("pistol"), TEXT("gunblade"), TEXT("blunderbuss"), TEXT("shield")};
    for(const TCHAR* W : Weapons)
    {
        const FWeapon* Def = D.Weapons.Find(W);
        Check(Def != nullptr, FString::Printf(TEXT("weapon %s defined"), W));
        if(!Def) continue;
        Check(Def->Slots.Contains(TEXT("impact")), FString::Printf(TEXT("weapon %s has an impact"), W));
        for(const auto& Pair : Def->Slots) Check(CireAudio::HasCue(Pair.Value), FString::Printf(TEXT("weapon %s %s cue %s exists"), W, *Pair.Key.ToString(), *Pair.Value.ToString()));
    }
    for(const TCHAR* W : {TEXT("bow"), TEXT("crossbow")})
        if(const FWeapon* Def = D.Weapons.Find(W)) Check(Def->Slots.Contains(TEXT("draw")) && Def->Slots.Contains(TEXT("release")), FString::Printf(TEXT("%s has draw + release"), W));
    // Every roster attack style resolves to a weapon or an element.
    for(const TCHAR* Style : {TEXT("sword"), TEXT("bow"), TEXT("arcane"), TEXT("lance"), TEXT("claws"), TEXT("flail"), TEXT("axes"), TEXT("totem"), TEXT("staff"), TEXT("gunblade"), TEXT("blunderbuss"), TEXT("glaive")})
    {
        FQuery Q; Q.Skill = Style; Q.Slot = TEXT("launch"); Q.Distance = 200.f;
        const FResolved R = Resolve(Q);
        Check(!R.Cues.IsEmpty(), FString::Printf(TEXT("attack style %s launch resolves (%s)"), Style, *R.Source));
        Q.Slot = TEXT("impact");
        Check(!Resolve(Q).Cues.IsEmpty(), FString::Printf(TEXT("attack style %s impact resolves"), Style));
    }
    {
        FQuery Q; Q.Skill = TEXT("gunblade"); Q.Slot = TEXT("launch"); Q.Distance = 900.f;
        Check(Resolve(Q).Weapon == TEXT("pistol"), TEXT("gunblade at range fires the pistol"));
        Q.Distance = 150.f; Check(Resolve(Q).Weapon == TEXT("gunblade"), TEXT("gunblade up close swings the falchion"));
        Q.Skill = TEXT("Pistol shot"); Q.Slot = TEXT("impact"); Check(Resolve(Q).Weapon == TEXT("pistol"), TEXT("Pistol shot impact is a bullet"));
        Q.Skill = TEXT("sword strike"); Q.TargetArmor = TEXT("plate"); Q.Slot = TEXT("critical");
        const FResolved R = Resolve(Q);
        Check(R.Cues.Num() >= 3, TEXT("critical sword strike on plate layers weapon + crit + armour"));
        Q.TargetArmor = TEXT("leather"); Q.Slot = TEXT("impact");
        Check(Resolve(Q).Cues.ContainsByPredicate([](FName C) { return C.ToString().Contains(TEXT("flesh")); }), TEXT("sword on leather adds the flesh layer"));
    }
    // Outcomes, reactions, deaths, UI.
    for(const TCHAR* K : {TEXT("block"), TEXT("deflect"), TEXT("dodge"), TEXT("miss")})
        Check(D.Outcomes.Contains(K) && CireAudio::HasCue(D.Outcomes[K]), FString::Printf(TEXT("outcome %s cue"), K));
    Check(D.Outcomes.Contains(TEXT("block")) && D.Outcomes[TEXT("block")] != D.Outcomes.FindRef(TEXT("dodge")), TEXT("block is distinct from dodge"));
    for(const TCHAR* K : {TEXT("hero"), TEXT("player"), TEXT("humanoid"), TEXT("creature"), TEXT("boss")})
        Check(D.Deaths.Contains(K) && CireAudio::HasCue(D.Deaths[K]), FString::Printf(TEXT("death %s cue"), K));
    for(const TCHAR* K : {TEXT("ready"), TEXT("error"), TEXT("errorMana"), TEXT("errorGold")})
        Check(D.Ui.Contains(K) && CireAudio::HasCue(D.Ui[K]), FString::Printf(TEXT("ui %s cue"), K));
    Check(CireAudio::HasCue(D.PlayerHit) && CireAudio::HasCue(D.PlayerHitHeavy), TEXT("player hit reactions"));
    {
        // Shield block must always sound, even on a clean clone: its fallback is a metal clang.
        const bool bPacks0 = CireAudio::PacksEnabled(); CireAudio::SetPacksEnabled(false);
        Check(D.Outcomes.Contains(TEXT("block")) && !CireAudio::CueMembers(D.Outcomes[TEXT("block")]).IsEmpty(), TEXT("block has a shipped clang"));
        for(const TCHAR* E : Required)
            for(const TCHAR* S : {TEXT("cast"), TEXT("impact"), TEXT("heal"), TEXT("channel")})
                Check(!CireAudio::CueMembers(FName(*FString::Printf(TEXT("spell.%s.%s"), E, S))).IsEmpty(), FString::Printf(TEXT("spell.%s.%s has a shipped fallback"), E, S));
        CireAudio::SetPacksEnabled(bPacks0);
    }

    // Coverage (fallback path: packs off): every Ability DB entry is in the table and resolves to a playable sound for its cast and its hit.
    const bool bPacks = CireAudio::PacksEnabled();
    CireAudio::SetPacksEnabled(false);
    int32 Abilities = 0, Covered = 0;
    for(const FCireAbilityDef& Def : CireAbilityDB::All())
    {
        const FName Id(*Def.Id);
        ++Abilities;
        const FAbilitySound* Row = FindAbility(Id);
        bool bOk = Row != nullptr && D.Elements.Contains(Row->Element);
        for(const TCHAR* Slot : {TEXT("cast"), TEXT("impact")})
        {
            FQuery Q; Q.Skill = Id; Q.Slot = Slot; Q.CasterWeapon = TEXT("sword");
            const FResolved R = Resolve(Q);
            bOk &= !R.Cues.IsEmpty();
            for(const FName& Cue : R.Cues) bOk &= !CireAudio::CueMembers(Cue).IsEmpty();
        }
        Covered += bOk ? 1 : 0;
        Check(bOk, TEXT("ability has a sound set: ") + Id.ToString());
    }
    Check(Abilities >= 100, FString::Printf(TEXT("Ability DB loaded (%d)"), Abilities));
    UE_LOG(LogCireSoundEvents, Display, TEXT("CIRE_AUDIO_COVERAGE abilities=%d covered=%d"), Abilities, Covered);

    // Fallback path: with packs disabled every cue plays shipped sounds that exist (pack-only layers may be empty).
    int32 PackCues = 0;
    for(const FName& Cue : CireAudio::CueIds())
    {
        const TArray<FString> Members = CireAudio::CueMembers(Cue);
        bool bExists = true;
        for(const FString& M : Members) bExists &= !CireAudio::SoundObjectPath(M).IsEmpty();
        Check(bExists, TEXT("fallback members exist: ") + Cue.ToString());
        Check(!CireAudio::CueUsesPack(Cue), TEXT("packs disabled -> fallback: ") + Cue.ToString());
    }
    CireAudio::SetPacksEnabled(bPacks);
    for(const FName& Cue : CireAudio::CueIds()) PackCues += CireAudio::CueUsesPack(Cue) ? 1 : 0;
    UE_LOG(LogCireSoundEvents, Display, TEXT("CIRE_AUDIO_PACKS enabled=%d cues_on_pack=%d cues=%d"), bPacks ? 1 : 0, PackCues, CireAudio::CueIds().Num());

    // Runtime: a spell cue plays through the resolver in a hearing world.
    if(World && World->GetNetMode() != NM_DedicatedServer && UCireAudioSubsystem::Get(World))
    {
        const FVector At = CireAudio::ListenerTransform(World).GetLocation() + FVector(200, 0, 0);
        Check(PlaySpellCue(World, TEXT("sword"), ECireSpellCue::Launch, At, At + FVector(100, 0, 0), 1.f), TEXT("resolver handles a basic sword swing"));
        Check(PlaySpellCue(World, TEXT("fireball"), ECireSpellCue::Impact, At, At, 1.f) || !FindAbility(TEXT("fireball")), TEXT("resolver handles a fire impact"));
        // A shield block reaching the local client always produces the clang (and ranged blocks the deflect).
        if(ACireController* PC = Cast<ACireController>(World->GetFirstPlayerController()))
        {
            UCireAudioSubsystem* Audio = UCireAudioSubsystem::Get(World);
            Audio->Events.Tick(*Audio, 0.f); // prime: history is never replayed
            // Both paths: the Shield Blocks & Deflects pack when installed (never committed), then the shipped clang.
            const bool bCanHear = World->GetAudioDevice().IsValid() && !CireAudio::LocalSettings(World).bMuteAudio;
            const bool bPacksWere = CireAudio::PacksEnabled();
            for(const bool bUsePacks : {true, false})
            {
                CireAudio::SetPacksEnabled(bUsePacks);
                FPlatformProcess::Sleep(.05f); // clear the 20 ms block/deflect cooldown between passes
                const TCHAR* Path = bUsePacks ? TEXT("packs-on") : TEXT("packs-off");
                const int32 ExpectPack = (CireAudio::CueUsesPack(D.Outcomes.FindRef(TEXT("block"))) ? 1 : 0) + (CireAudio::CueUsesPack(D.Outcomes.FindRef(TEXT("deflect"))) ? 1 : 0);
                const CireAudio::FStats S0 = CireAudio::Stats();
                const int32 Blocks0 = Audio->Events.Blocks, Deflects0 = Audio->Events.Deflects;
                FCireCombatEvent E; E.Outcome = ECireHitOutcome::Block; E.bLocalTarget = true; E.AbilityName = TEXT("sword strike"); E.Location = At;
                CireCombat::AppendReceivedEvent(PC->CombatEvents, PC->CombatEventSequence, E, World->GetTimeSeconds());
                E.AbilityName = TEXT("Bow shot");
                CireCombat::AppendReceivedEvent(PC->CombatEvents, PC->CombatEventSequence, E, World->GetTimeSeconds());
                Audio->Events.Tick(*Audio, 0.f);
                const CireAudio::FStats& S1 = CireAudio::Stats();
                const int32 Played = S1.Played - S0.Played, PackPlays = S1.PackPlays - S0.PackPlays, FallbackPlays = S1.FallbackPlays - S0.FallbackPlays;
                Check(Audio->Events.Blocks == Blocks0 + 1 && Audio->Events.Deflects == Deflects0 + 1, FString::Printf(TEXT("block event -> clang, ranged block -> deflect (%s)"), Path));
                if(!bUsePacks) Check(ExpectPack == 0, TEXT("packs off -> block and deflect fall back to the shipped clang"));
                if(bCanHear)
                {
                    Check(Played == 2, FString::Printf(TEXT("received block + deflect both sound (%s, played=%d)"), Path, Played));
                    Check(PackPlays == ExpectPack && FallbackPlays == 2 - ExpectPack, FString::Printf(TEXT("block sounds use %s (%s, pack=%d fallback=%d)"),
                        ExpectPack ? TEXT("the installed pack") : TEXT("the shipped clang"), Path, PackPlays, FallbackPlays));
                }
                UE_LOG(LogCireSoundEvents, Display, TEXT("CIRE_AUDIO_BLOCK path=%s cues_on_pack=%d played=%d pack=%d fallback=%d"), Path, ExpectPack, Played, PackPlays, FallbackPlays);
            }
            CireAudio::SetPacksEnabled(bPacksWere);
        }
    }
    return Pass;
}
#endif
