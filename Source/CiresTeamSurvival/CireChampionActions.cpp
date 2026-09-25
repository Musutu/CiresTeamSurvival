#include "CireChampionActions.h"
#include "CireSignatureSkills.h" // new-champions

#include "CireChampionArt.h"
#include "CireWeaponPresentation.h"
#include "CireAbilityVFX.h" // ability-vfx: release timing shared with the spell cues
#include "CireGame.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireChampionActions, Log, All);

namespace
{
struct FData
{
    bool bLoaded = false;
    TMap<FString, FString> Bodies;                       // mesh object path -> clip folder
    TMap<FString, CireChampionActions::FWindow> Windows;
    TMap<FString, TMap<FString, FString>> Motions;       // motion -> kind -> clip
    TMap<FString, FString> ProfileMotion;                // profile id -> motion (WeaponLoadouts.json)
    TSet<FString> Shouts, Spells;
    float MaxWindupRate = 2.4f;
    struct FStyle { FString Clip; float Twist = 0.f; };
    TMap<FString, FStyle> Styles;                         // weapon preset -> basic attack clip + torso twist
};
FData GData;

bool ReadJson(const TCHAR* Name, TSharedPtr<FJsonObject>& Out)
{
    FString Text;
    return FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), Name)) && Text.Len() < 400000 &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}

const FData& Data()
{
    if (GData.bLoaded) return GData;
    GData.bLoaded = true;
    TSharedPtr<FJsonObject> Root, Loadouts;
    if (ReadJson(TEXT("ChampionAttacks02.json"), Root))
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (Root->TryGetObjectField(TEXT("bodies"), Object))
            for (const auto& Pair : (*Object)->Values) { FString Folder; if (Pair.Value->TryGetString(Folder)) GData.Bodies.Add(FString(Pair.Key.ToView()), Folder); }
        if (Root->TryGetObjectField(TEXT("windows"), Object))
            for (const auto& Pair : (*Object)->Values)
            {
                const TSharedPtr<FJsonObject>* W = nullptr; CireChampionActions::FWindow Window; double V = 0;
                if (!Pair.Value->TryGetObject(W)) continue;
                if ((*W)->TryGetNumberField(TEXT("start"), V)) Window.Start = V;
                if ((*W)->TryGetNumberField(TEXT("contact"), V)) Window.Contact = V;
                if ((*W)->TryGetNumberField(TEXT("end"), V)) Window.End = V;
                if ((*W)->TryGetNumberField(TEXT("recoverRate"), V)) Window.RecoverRate = FMath::Clamp(V, .2, 5.);
                GData.Windows.Add(FString(Pair.Key.ToView()), Window);
            }
        if (Root->TryGetObjectField(TEXT("motions"), Object))
            for (const auto& Pair : (*Object)->Values)
            {
                const TSharedPtr<FJsonObject>* Kinds = nullptr;
                if (!Pair.Value->TryGetObject(Kinds)) continue;
                auto& Entry = GData.Motions.Add(FString(Pair.Key.ToView()));
                for (const auto& Kind : (*Kinds)->Values) { FString Clip; if (Kind.Value->TryGetString(Clip)) Entry.Add(FString(Kind.Key.ToView()), Clip); }
            }
        const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
        if (Root->TryGetArrayField(TEXT("shoutSkills"), List)) for (const auto& V : *List) GData.Shouts.Add(V->AsString());
        if (Root->TryGetArrayField(TEXT("spellSkills"), List)) for (const auto& V : *List) GData.Spells.Add(V->AsString());
        double Rate = 0;
        if (Root->TryGetNumberField(TEXT("maxWindupRate"), Rate)) GData.MaxWindupRate = FMath::Clamp(Rate, 1., 6.);
        if (Root->TryGetObjectField(TEXT("styles"), Object))
            for (const auto& Pair : (*Object)->Values)
            {
                const TSharedPtr<FJsonObject>* Row = nullptr; FData::FStyle Style; double Twist = 0;
                if (!Pair.Value->TryGetObject(Row)) continue;
                (*Row)->TryGetStringField(TEXT("clip"), Style.Clip);
                if ((*Row)->TryGetNumberField(TEXT("twist"), Twist)) Style.Twist = FMath::Clamp(Twist, -80., 80.);
                GData.Styles.Add(FString(Pair.Key.ToView()), Style);
            }
    }
    if (ReadJson(TEXT("WeaponLoadouts.json"), Loadouts))
    {
        const TSharedPtr<FJsonObject>* Presets = nullptr; const TSharedPtr<FJsonObject>* Profiles = nullptr;
        if (Loadouts->TryGetObjectField(TEXT("presets"), Presets) && Loadouts->TryGetObjectField(TEXT("profiles"), Profiles))
            for (const auto& Pair : (*Profiles)->Values)
            {
                FString Preset, Motion; const TSharedPtr<FJsonObject>* Row = nullptr;
                if (Pair.Value->TryGetString(Preset) && (*Presets)->TryGetObjectField(Preset, Row) && (*Row)->TryGetStringField(TEXT("motion"), Motion))
                    GData.ProfileMotion.Add(FString(Pair.Key.ToView()), Motion);
            }
    }
    UE_LOG(LogCireChampionActions, Log, TEXT("CIRE_CHAMPION_ACTIONS_DATA bodies=%d windows=%d motions=%d profiles=%d"),
        GData.Bodies.Num(), GData.Windows.Num(), GData.Motions.Num(), GData.ProfileMotion.Num());
    return GData;
}

float Smooth01(float X) { X = FMath::Clamp(X, 0.f, 1.f); return X * X * (3.f - 2.f * X); }

double ServerNow(const UWorld* World)
{
    const AGameStateBase* State = World ? World->GetGameState() : nullptr;
    return State ? State->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0.0;
}

UCireChampionAction* StateFor(ACireHero& Hero)
{
    if (auto* Found = Hero.FindComponentByClass<UCireChampionAction>()) return Found;
    auto* Created = NewObject<UCireChampionAction>(&Hero, TEXT("ChampionAction"));
    Created->RegisterComponent();
    return Created;
}

void Start(UCireChampionAction& State, UAnimSequence* Clip, const FString& Name, double StartedAt, float Windup, float Settled = 0.f)
{
    if (!Clip) return;
    State.Sequence = Clip; State.Clip = Name; State.Window = CireChampionActions::Window(Name);
    State.StartedAt = StartedAt; State.Windup = FMath::Max(.05f, Windup); State.bBasic = Settled > 0.f;
    // A basic attack must be back at rest by Settled seconds so the next swing starts from locomotion.
    const float Follow = State.Window.End - State.Window.Contact;
    State.RecoverRate = Settled > State.Windup + .05f ? FMath::Max(State.Window.RecoverRate, Follow / (Settled - State.Windup)) : State.Window.RecoverRate;
}
}

FString CireChampionActions::MotionFor(const ACireHero& Hero)
{
    FString Profile = Hero.ChampionProfileId;
    if (Profile.IsEmpty())
    {
        const TCHAR* Legacy[] = {TEXT("knight"), TEXT("ranger"), TEXT("scholar"), TEXT("lancer"), TEXT("summoner")};
        Profile = Hero.Archetype >= 0 && Hero.Archetype < UE_ARRAY_COUNT(Legacy) ? Legacy[Hero.Archetype] : TEXT("knight");
    }
    const FString* Motion = Data().ProfileMotion.Find(Profile);
    return Motion ? *Motion : FString(TEXT("melee"));
}

CireChampionActions::FWindow CireChampionActions::Window(const FString& Clip)
{
    const FWindow* Found = Data().Windows.Find(Clip);
    return Found ? *Found : FWindow{0.f, .4f, 1.f, 1.f};
}

float CireChampionActions::SkillWindup(const UWorld* World, const FString& SkillId) { return FMath::Max(.05f, CireAbilityVFX::ReleaseLead(World, SkillId)); } // ability-vfx

FString CireChampionActions::SkillKind(const FString& SkillId)
{
    const FData& D = Data();
    if (D.Shouts.Contains(SkillId) || SkillId.Contains(TEXT("roar")) || SkillId.Contains(TEXT("shout")) || SkillId.EndsWith(TEXT("_cry"))) return TEXT("shout");
    if (D.Spells.Contains(SkillId)) return TEXT("spell");
    return TEXT("ability");
}

namespace
{
const FData::FStyle* StyleFor(const ACireHero& Hero)
{
    const auto* Weapons = Hero.FindComponentByClass<UCireWeaponPresentation>();
    return Weapons ? Data().Styles.Find(Weapons->GetEquippedLoadout()) : nullptr;
}
}

FString CireChampionActions::StyleName(const ACireHero& Hero)
{
    const auto* Weapons = Hero.FindComponentByClass<UCireWeaponPresentation>();
    return Weapons ? Weapons->GetEquippedLoadout() : FString();
}

FString CireChampionActions::ClipName(const ACireHero& Hero, const FString& Kind)
{
    if (Kind == TEXT("shout")) return TEXT("war_cry");
    const FData& D = Data();
    // Weapon classes read differently: thrusts for daggers/lances, sweeps for axes, overheads for hammers.
    // new-champions: the Gunblade's basic attack is a falchion slash up close and an aimed pistol shot at range.
    if (Kind == TEXT("attack") && Hero.BasicAttackStyle() == TEXT("gunblade"))
    {
        const AActor* Foe = Hero.PendingAttackTarget.IsValid() ? Hero.PendingAttackTarget.Get() : Hero.Target;
        return CireSignatureSkills::IsCloseQuarters(&Hero, Foe) ? FString(TEXT("slash")) : FString(TEXT("attack_crossbow"));
    }
    if (Kind == TEXT("attack") || Kind == TEXT("ability"))
        if (const FData::FStyle* Style = StyleFor(Hero); Style && !Style->Clip.IsEmpty()) return Style->Clip;
    const TMap<FString, FString>* Motion = D.Motions.Find(MotionFor(Hero));
    if (!Motion) Motion = D.Motions.Find(TEXT("melee"));
    const FString* Clip = Motion ? Motion->Find(Kind) : nullptr;
    return Clip ? *Clip : FString(TEXT("slash"));
}

UAnimSequence* CireChampionActions::ClipFor(const USkeletalMesh* Body, const FString& Clip)
{
    if (!Body) return nullptr;
    const FString* Folder = Data().Bodies.Find(Body->GetPathName());
    if (!Folder) return nullptr;
    const FString Name = FString::Printf(TEXT("A_%s_%s"), **Folder, *Clip);
    const FString Path = FString::Printf(TEXT("/Game/Art/Characters/ChampionAttacks02/%s/%s.%s"), **Folder, *Name, *Name);
    if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))) return nullptr;
    UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path);
    return Sequence && Sequence->GetSkeleton() == Body->GetSkeleton() ? Sequence : nullptr;
}

bool CireChampionActions::Apply(ACireHero& Hero, UCireCombatAnimInstance& Anim, float DeltaSeconds, float Speed)
{
    USkeletalMesh* Body = Hero.GetMesh() ? Hero.GetMesh()->GetSkeletalMeshAsset() : nullptr;
    if (!Body || !Data().Bodies.Contains(Body->GetPathName()) || !ClipFor(Body, ClipName(Hero, TEXT("attack")))) return false;
    UCireChampionAction* State = StateFor(Hero);
    const double Now = ServerNow(Hero.GetWorld());
    if (State->CachedBody != Body)
    {
        // New body: forget old clips and do not replay attacks/casts that happened before it appeared.
        State->CachedBody = Body; State->Sequence = nullptr;
        State->SeenAttackSerial = Hero.AttackSerial; State->LastCooldowns = Hero.Cooldowns;
    }
    if (Hero.AttackSerial != State->SeenAttackSerial)
    {
        State->SeenAttackSerial = Hero.AttackSerial;
        // Server releases the blow at 0.25 of the 0.65 s authored attack, scaled by attack speed.
        const FString Clip = ClipName(Hero, TEXT("attack"));
        const float Duration = FMath::Max(.05f, Hero.AttackDuration);
        Start(*State, ClipFor(Body, Clip), Clip, Hero.AttackStartedServerTime, Duration * (.25f / .65f), Duration * 1.5f);
    }
    for (int32 Slot = 0; Slot < Hero.Cooldowns.Num(); ++Slot)
    {
        const float Previous = State->LastCooldowns.IsValidIndex(Slot) ? State->LastCooldowns[Slot] : 0.f;
        // A cooldown starting means the server accepted a cast of that slot.
        if (Hero.Cooldowns[Slot] > Previous + .5f && Hero.Skills.IsValidIndex(Slot))
        {
            const FString Clip = ClipName(Hero, SkillKind(Hero.Skills[Slot]));
            // ability-vfx: the contact frame lands on the effect release (skillshot warning end, or the
            // short snap-in the spell cue waits for), instead of a fixed 0.28 s after the cast.
            Start(*State, ClipFor(Body, Clip), Clip, Now, SkillWindup(Hero.GetWorld(), Hero.Skills[Slot]));
        }
    }
    State->LastCooldowns = Hero.Cooldowns;
    if (State->bBasic && State->Sequence && Hero.AttackSerial == State->SeenAttackSerial) State->StartedAt = Hero.AttackStartedServerTime;
    Anim.AttackWeight = 0.f;
    if (Hero.bDead) State->Sequence = nullptr;
    // At rest the layer keeps the weapon class's attack clip at zero weight (same contract as the prototype).
    if (!State->Sequence) { Anim.AttackSequence = ClipFor(Body, ClipName(Hero, TEXT("attack"))); return true; }
    const FWindow& W = State->Window;
    const float MaxRate = Data().MaxWindupRate;
    const float Elapsed = static_cast<float>(Now - State->StartedAt);
    // Fast attacks skip the slow start of the raise instead of racing through it.
    const float From = FMath::Max(W.Start, W.Contact - State->Windup * MaxRate);
    float Time = Elapsed < State->Windup ? FMath::Lerp(From, W.Contact, FMath::Max(0.f, Elapsed) / State->Windup)
                                         : W.Contact + (Elapsed - State->Windup) * State->RecoverRate;
    if (State->bHold) Time = State->HoldTime;
    if (!State->bHold && Time >= W.End) { State->Sequence = nullptr; Anim.AttackSequence = ClipFor(Body, ClipName(Hero, TEXT("attack"))); return true; }
    const float Weight = State->bHold ? 1.f : FMath::Min(Smooth01(Elapsed / .09f), Smooth01((W.End - Time) / FMath::Max(.05f, .3f * State->RecoverRate)));
    Anim.AttackSequence = State->Sequence;
    Anim.AttackTime = FMath::Clamp(Time, 0.f, State->Sequence->GetPlayLength());
    Anim.AttackWeight = Weight;
    // Sweeping styles turn the torso away during the raise and through the target after contact.
    if (const FData::FStyle* Style = StyleFor(Hero); Style && Style->Twist != 0.f && State->Clip == Style->Clip)
    {
        const float T = Style->Twist;
        float Twist;
        if (Time <= W.Contact) Twist = -T * Smooth01((Time - From) / FMath::Max(.05f, W.Contact - From));
        else
        {
            const float Q = (Time - W.Contact) / FMath::Max(.05f, W.End - W.Contact);
            Twist = Q < .35f ? FMath::Lerp(-T, T, Smooth01(Q / .35f)) : T * (1.f - Smooth01((Q - .35f) / .65f));
        }
        Anim.SpineTwist = Twist * Weight;
    }
    // Upper body swings/casts; the legs keep the locomotion cycle while the champion moves.
    Anim.AttackLowerBody = 1.f - FMath::Clamp((Speed - 20.f) / 130.f, 0.f, 1.f);
    return true;
}

#if !UE_BUILD_SHIPPING
bool CireChampionActions::Hold(ACireHero& Hero, const FString& Clip, float Phase)
{
    USkeletalMesh* Body = Hero.GetMesh() ? Hero.GetMesh()->GetSkeletalMeshAsset() : nullptr;
    UAnimSequence* Sequence = ClipFor(Body, Clip);
    if (!Sequence) return false;
    UCireChampionAction* State = StateFor(Hero);
    State->CachedBody = Body; State->SeenAttackSerial = Hero.AttackSerial; State->LastCooldowns = Hero.Cooldowns;
    State->Sequence = Sequence; State->Clip = Clip; State->Window = Window(Clip); State->bHold = true; State->bBasic = false; State->Windup = .3f;
    const FWindow& W = State->Window;
    State->HoldTime = Phase <= 1.f ? FMath::Lerp(W.Start, W.Contact, FMath::Max(0.f, Phase)) : FMath::Lerp(W.Contact, W.End, FMath::Min(1.f, Phase - 1.f));
    return true;
}
#endif

#if !UE_BUILD_SHIPPING
#include "CireChampionRoster.h"
#include "Engine/World.h"

bool CireChampionActions::RunSmoke(ACireGameMode* Mode)
{
    if (!Mode) return false;
    bool bPass = true;
    int32 Checks = 0, Clips = 0;
    auto Check = [&](bool bValue, const FString& Why)
    {
        ++Checks;
        if (!bValue) { bPass = false; UE_LOG(LogCireChampionActions, Error, TEXT("CIRE_CHAMPION_ACTIONS_CHECK_FAIL %s"), *Why); }
    };
    const FData& D = Data();
    Check(D.Bodies.Num() >= 17 && D.Windows.Num() >= 5 && D.Motions.Num() >= 6, TEXT("ChampionAttacks02.json loads"));
    // Every body has its three common clips on its own skeleton, in place, root not animated away.
    for (const auto& Pair : D.Bodies)
    {
        USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr, *Pair.Key);
        Check(Body != nullptr, TEXT("champion body loads: ") + Pair.Value);
        if (!Body) continue;
        TArray<FString> Needed = {TEXT("slash"), TEXT("cast_a_spell"), TEXT("war_cry")};
        if (Pair.Value == TEXT("Ranger")) Needed.Append({TEXT("attack_bow"), TEXT("attack_crossbow")});
        for (const FString& Clip : Needed)
        {
            UAnimSequence* Sequence = ClipFor(Body, Clip);
            Check(Sequence != nullptr, FString::Printf(TEXT("%s has %s"), *Pair.Value, *Clip));
            if (!Sequence) continue;
            ++Clips;
            const FWindow W = Window(Clip);
            Check(W.Start <= W.Contact && W.Contact < W.End && W.End <= Sequence->GetPlayLength() + .05f,
                FString::Printf(TEXT("%s %s window inside the %.2fs clip"), *Pair.Value, *Clip, Sequence->GetPlayLength()));
            Check(!Sequence->bEnableRootMotion, FString::Printf(TEXT("%s %s has no root motion"), *Pair.Value, *Clip));
            if (Clip == TEXT("slash")) Check(Sequence->GetPlayLength() < 3.f, TEXT("slash trimmed to a single swing: ") + Pair.Value);
        }
    }
    // Every drafted profile resolves a weapon class, and every weapon class resolves clips.
    for (const auto& Profile : CireChampionRoster::All())
    {
        const FString* Motion = D.ProfileMotion.Find(Profile.Id);
        Check(Motion && D.Motions.Contains(*Motion), TEXT("weapon motion for profile ") + Profile.Id);
    }
    Check(SkillKind(TEXT("war_cry")) == TEXT("shout") && SkillKind(TEXT("bear_roar")) == TEXT("shout"), TEXT("shouts use war_cry"));
    Check(SkillKind(TEXT("restoring_light")) == TEXT("spell") && SkillKind(TEXT("cleaving_strike")) == TEXT("ability"), TEXT("spells cast, strikes swing"));
    UE_LOG(LogCireChampionActions, Display, TEXT("CIRE_CHAMPION_ACTIONS_%s checks=%d clips=%d bodies=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks, Clips, D.Bodies.Num());
    return bPass;
}
#endif
