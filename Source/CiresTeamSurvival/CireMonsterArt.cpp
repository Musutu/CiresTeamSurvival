#include "CireMonsterArt.h"
#include "CireFabAnimation.h" // fab-coverage

#include "CireMonsterAnim.h"
#include "CireGame.h"
#include "CireNPCArchetypes.h"
#include "CireNPCState.h"
#include "CireAttackSystem.h"
#include "CireCombatEvents.h"

#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h" // world-dressing
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "CireRaces.h" // monster-races
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireMonsterArt, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarSwingReadability(TEXT("cire.Monsters.SwingReadability"), 1,
    TEXT("monster-rig: 1 = melee wind-ups scale with rank (normal .3-.5 s, elite .38-.6 s, boss .55-.85 s); 0 = flat .22-.5 s."));
TAutoConsoleVariable<int32> CVarSwingWindup(TEXT("cire.Monsters.SwingWindup"), 1,
    TEXT("1: monster melee blows land on the swing's contact frame (default). 0: legacy instant hits."));
TAutoConsoleVariable<int32> CVarTripoBodies(TEXT("cire.Monsters.TripoBodies"), 1,
    TEXT("1: draw monsters with the animated Tripo bodies. 0: mannequin fallback (applies to newly configured monsters)."));

CireMonsterArt::FData GData;
bool GLoaded = false;
int32 GCorpses = 0;
// monster-rig: walk_b/walk_l/walk_r (strafe, back-pedal, turn steps), attack3 (third swing of the rotation), heavy/heavy2
// (telegraphed skill strikes), cast, shout, stagger come from the weapon-matched Fab sets (MonsterFabClips.json "roles").
const TCHAR* const Roles[] = {TEXT("idle"), TEXT("walk"), TEXT("run"), TEXT("attack"), TEXT("attackAlt"), TEXT("hit"), TEXT("death"),
    TEXT("walk_b"), TEXT("walk_l"), TEXT("walk_r"), TEXT("attack3"), TEXT("heavy"), TEXT("heavy2"), TEXT("cast"), TEXT("shout"), TEXT("stagger")};

template<class T> T* LoadIfPresent(const FString& Path)
{
    if (Path.IsEmpty() || !Path.StartsWith(TEXT("/Game/"))) return nullptr;
    const FString Package = FPackageName::ObjectPathToPackageName(Path);
    if (!FPackageName::DoesPackageExist(Package)) return nullptr;
    return LoadObject<T>(nullptr, *Path);
}

bool ReadFile(const TCHAR* Name, TSharedPtr<FJsonObject>& Out)
{
    FString Text;
    const FString File = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), Name);
    return FFileHelper::LoadFileToString(Text, *File) && Text.Len() < 400000 &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}

bool ParseBody(const TSharedPtr<FJsonObject>& O, CireMonsterArt::FBody& Body)
{
    double Scale = 1, Yaw = -90, Height = 180;
    if (!O->TryGetStringField(TEXT("variant"), Body.Variant) || !O->TryGetStringField(TEXT("mesh"), Body.MeshPath) ||
        !O->TryGetNumberField(TEXT("meshScale"), Scale) || !FMath::IsFinite(Scale) || Scale < .05 || Scale > 50) return false;
    O->TryGetNumberField(TEXT("yaw"), Yaw); O->TryGetNumberField(TEXT("heightCm"), Height);
    O->TryGetStringField(TEXT("bakedWeapon"), Body.BakedWeapon);
    // world-dressing: free creature bodies (RaceMeshes.free.json).
    {
        O->TryGetStringField(TEXT("rig"), Body.Rig);
        O->TryGetBoolField(TEXT("lockRoot"), Body.bLockRoot);
        double V = 0;
        if (O->TryGetNumberField(TEXT("walkSpeedCm"), V) && FMath::IsFinite(V)) Body.WalkSpeedCm = static_cast<float>(FMath::Clamp(V, 0., 2000.));
        if (O->TryGetNumberField(TEXT("runSpeedCm"), V) && FMath::IsFinite(V)) Body.RunSpeedCm = static_cast<float>(FMath::Clamp(V, 0., 3000.));
        if (O->TryGetNumberField(TEXT("reachCm"), V) && FMath::IsFinite(V)) Body.ReachCm = static_cast<float>(FMath::Clamp(V, 0., 2000.));
        if (O->TryGetNumberField(TEXT("soleCm"), V) && FMath::IsFinite(V)) Body.SoleCm = static_cast<float>(FMath::Clamp(V, 0., 60.)); // fab-integration
        if (O->TryGetNumberField(TEXT("airborneCm"), V) && FMath::IsFinite(V)) Body.AirborneCm = static_cast<float>(FMath::Clamp(V, 0., 60.));
        const TArray<TSharedPtr<FJsonValue>>* PartList = nullptr; // fab-integration
        if (O->TryGetArrayField(TEXT("parts"), PartList))
            for (const auto& Value : *PartList) { FString Part; if (Value->TryGetString(Part) && Part.StartsWith(TEXT("/Game/")) && Body.Parts.Num() < 8) Body.Parts.Add(Part); }
        const TArray<TSharedPtr<FJsonValue>>* Drops = nullptr;
        if (O->TryGetArrayField(TEXT("dropPropBones"), Drops))
            for (const auto& Value : *Drops) { FString Bone; if (Value->TryGetString(Bone)) Body.DropPropBones.Add(FName(*Bone)); }
        // monster-expansion: skeletal attachments and the Fab reskin (RaceMeshes.fabx.json).
        const TArray<TSharedPtr<FJsonValue>>* Attach = nullptr;
        if (O->TryGetArrayField(TEXT("attachments"), Attach))
            for (const auto& Value : *Attach)
            {
                const TSharedPtr<FJsonObject>* A = nullptr; FString Path, Socket;
                if (Value->TryGetObject(A) && (*A)->TryGetStringField(TEXT("mesh"), Path) && Path.StartsWith(TEXT("/Game/")) && (*A)->TryGetStringField(TEXT("socket"), Socket) && Body.Attachments.Num() < 4)
                    Body.Attachments.Emplace(Path, FName(*Socket));
            }
        O->TryGetBoolField(TEXT("spectral"), Body.bSpectral);
        const TSharedPtr<FJsonObject>* Reskin = nullptr;
        if (O->TryGetObjectField(TEXT("reskin"), Reskin))
        {
            auto ReadColor = [](const TSharedPtr<FJsonObject>& R, const TCHAR* Key, FLinearColor& Out)
            {
                const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
                if (R->TryGetArrayField(Key, V) && V->Num() >= 3) Out = FLinearColor((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber(), 1.f);
            };
            ReadColor(*Reskin, TEXT("tint"), Body.ReskinTint); ReadColor(*Reskin, TEXT("rim"), Body.ReskinRim);
            double N = 0;
            if ((*Reskin)->TryGetNumberField(TEXT("tintStrength"), N)) Body.ReskinTintStrength = FMath::Clamp(static_cast<float>(N), 0.f, 1.f);
            if ((*Reskin)->TryGetNumberField(TEXT("rimStrength"), N)) Body.ReskinRimStrength = FMath::Clamp(static_cast<float>(N), 0.f, 5.f);
            if ((*Reskin)->TryGetNumberField(TEXT("body"), N)) Body.ReskinBody = FMath::Clamp(static_cast<float>(N), 0.f, 1.f);
            const TSharedPtr<FJsonObject>* Slots = nullptr;
            if ((*Reskin)->TryGetObjectField(TEXT("slots"), Slots))
                for (const auto& Pair : (*Slots)->Values)
                {
                    const TSharedPtr<FJsonObject>* T = nullptr;
                    if (!Pair.Value->TryGetObject(T)) continue;
                    TMap<FName, FString>& Textures = Body.ReskinTextures.FindOrAdd(FCString::Atoi(*FString(Pair.Key.ToView())));
                    for (const auto& Tex : (*T)->Values) { FString Path; if (Tex.Value->TryGetString(Path) && Path.StartsWith(TEXT("/Game/"))) Textures.Add(FName(FString(Tex.Key.ToView())), Path); }
                }
        }
        const TSharedPtr<FJsonObject>* Sockets = nullptr;
        if (O->TryGetObjectField(TEXT("sockets"), Sockets))
            for (const auto& Pair : (*Sockets)->Values) { FString Bone; if (Pair.Value->TryGetString(Bone)) Body.Sockets.Add(FName(FString(Pair.Key.ToView())), FName(*Bone)); }
    }
    Body.MeshScale = static_cast<float>(Scale); Body.Yaw = static_cast<float>(FMath::Clamp(Yaw, -360., 360.));
    Body.HeightCm = static_cast<float>(FMath::Clamp(Height, 40., 600.));
    const TSharedPtr<FJsonObject>* Animations = nullptr;
    if (!O->TryGetObjectField(TEXT("animations"), Animations)) return false;
    for (const TCHAR* Role : Roles)
    {
        FString Path;
        if ((*Animations)->TryGetStringField(Role, Path) && Path.StartsWith(TEXT("/Game/"))) Body.Roles.Add(Role, Path);
    }
    const TSharedPtr<FJsonObject>* All = nullptr;
    if ((*Animations)->TryGetObjectField(TEXT("all"), All))
        for (const auto& Pair : (*All)->Values)
        {
            FString Path;
            if (Pair.Value->TryGetString(Path) && Path.StartsWith(TEXT("/Game/"))) Body.Clips.Add(FString(Pair.Key.ToView()), Path);
        }
    return Body.MeshPath.StartsWith(TEXT("/Game/")) && Body.Roles.Contains(TEXT("idle"));
}

void Load()
{
    CireMonsterArt::FData Candidate;
    TSharedPtr<FJsonObject> Meshes, Art;
    if (!ReadFile(TEXT("NPCMeshes.tripo.json"), Meshes))
    {
        UE_LOG(LogCireMonsterArt, Warning, TEXT("CIRE_MONSTER_ART_DATA missing NPCMeshes.tripo.json; mannequin fallback bodies stay active."));
        GData = MoveTemp(Candidate); return;
    }
    ReadFile(TEXT("MonsterArt.json"), Art);
    TMap<FName, TMap<FString, CireMonsterArt::FBody>> ByArchetype;
    TMap<FName, FString> Recommended;
    const TSharedPtr<FJsonObject>* Archetypes = nullptr;
    if (Meshes->TryGetObjectField(TEXT("archetypes"), Archetypes))
        for (const auto& Pair : (*Archetypes)->Values)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Pair.Value->TryGetObject(Entry)) continue;
            const FName Id(FString(Pair.Key.ToView()));
            CireMonsterArt::FBody Body;
            if (ParseBody(*Entry, Body)) { Recommended.Add(Id, Body.Variant); ByArchetype.FindOrAdd(Id).Add(Body.Variant, Body); }
            const TArray<TSharedPtr<FJsonValue>>* Alternates = nullptr;
            if ((*Entry)->TryGetArrayField(TEXT("alternates"), Alternates))
                for (const auto& Value : *Alternates)
                {
                    const TSharedPtr<FJsonObject>* Alt = nullptr; CireMonsterArt::FBody AltBody;
                    if (Value->TryGetObject(Alt) && ParseBody(*Alt, AltBody)) ByArchetype.FindOrAdd(Id).Add(AltBody.Variant, AltBody);
                }
        }
    // monster-races: RaceMeshes.tripo.json (tripo-races agent) overlays real race art by priority: a unit listed
    // there replaces any NPCMeshes body, and all of its alternates become variants.
    TSet<FName> RaceArt;
    {
        TSharedPtr<FJsonObject> RaceMeshes;
        const TSharedPtr<FJsonObject>* RaceUnits = nullptr;
        if (ReadFile(TEXT("RaceMeshes.tripo.json"), RaceMeshes) && (RaceMeshes->TryGetObjectField(TEXT("archetypes"), RaceUnits) || RaceMeshes->TryGetObjectField(TEXT("units"), RaceUnits)))
            for (const auto& Pair : (*RaceUnits)->Values)
            {
                const TSharedPtr<FJsonObject>* Entry = nullptr;
                if (!Pair.Value->TryGetObject(Entry)) continue;
                const FName Id(FString(Pair.Key.ToView()));
                TMap<FString, CireMonsterArt::FBody> Bodies;
                CireMonsterArt::FBody Body;
                if ((*Entry)->HasField(TEXT("variant")) || (*Entry)->TryGetStringField(TEXT("mesh"), Body.MeshPath))
                {
                    if (!(*Entry)->HasField(TEXT("variant"))) (*Entry)->SetStringField(TEXT("variant"), FString(Pair.Key.ToView()));
                    if (ParseBody(*Entry, Body)) Bodies.Add(Body.Variant, Body);
                }
                const TArray<TSharedPtr<FJsonValue>>* Alternates = nullptr;
                if ((*Entry)->TryGetArrayField(TEXT("alternates"), Alternates))
                    for (const auto& Value : *Alternates)
                    {
                        const TSharedPtr<FJsonObject>* Alt = nullptr; CireMonsterArt::FBody AltBody;
                        if (Value->TryGetObject(Alt) && ParseBody(*Alt, AltBody)) Bodies.Add(AltBody.Variant, AltBody);
                    }
                if (Bodies.IsEmpty()) continue;
                ByArchetype.Add(Id, Bodies); RaceArt.Add(Id);
                if (!Recommended.Contains(Id) || !Bodies.Contains(Recommended[Id])) { TArray<FString> Keys; Bodies.GetKeys(Keys); Recommended.Add(Id, Keys[0]); }
            }
    }
    // fab-integration: RaceMeshes.fab.json (purchased Fab creature packs: ROG Creatures, Quadruped Fantasy Creatures,
    // Undead Pack; Docs/FAB-PURCHASED.md) is the HIGHEST-priority overlay, but only for bodies whose mesh and idle clip
    // exist locally: the packs are licensed and never committed, so a clean clone keeps the Tripo/free art.
    {
        TSharedPtr<FJsonObject> FabMeshes;
        const TSharedPtr<FJsonObject>* FabUnits = nullptr;
        auto Present = [](const FString& Path) { const FString Package = FPackageName::ObjectPathToPackageName(Path);
            return FPackageName::IsValidLongPackageName(Package) && FPackageName::DoesPackageExist(Package); };
        // monster-expansion: RaceMeshes.fabx.json (the Bestiary.json creatures, Tools/BuildFabExpansionCreatures.py) is read the same way.
        for (const TCHAR* FabFile : {TEXT("RaceMeshes.fab.json"), TEXT("RaceMeshes.fabx.json")})
        if (!FParse::Param(FCommandLine::Get(), TEXT("CireNoFabCreatures")) && !FParse::Param(FCommandLine::Get(), TEXT("CireNoFab")) && ReadFile(FabFile, FabMeshes) && (FabMeshes->TryGetObjectField(TEXT("archetypes"), FabUnits) || FabMeshes->TryGetObjectField(TEXT("units"), FabUnits)))
            for (const auto& Pair : (*FabUnits)->Values)
            {
                const FName Id(FString(Pair.Key.ToView()));
                const TSharedPtr<FJsonObject>* Entry = nullptr;
                if (!Pair.Value->TryGetObject(Entry)) continue;
                TMap<FString, CireMonsterArt::FBody> Bodies;
                auto Add = [&](const TSharedPtr<FJsonObject>& O)
                {
                    CireMonsterArt::FBody Body;
                    if (!ParseBody(O, Body) || !Present(Body.MeshPath) || !Present(Body.Roles.FindRef(TEXT("idle")))) return;
                    Body.bFab = true;
                    Bodies.Add(Body.Variant, Body);
                };
                if ((*Entry)->HasField(TEXT("mesh"))) Add(*Entry);
                const TArray<TSharedPtr<FJsonValue>>* Alternates = nullptr;
                if ((*Entry)->TryGetArrayField(TEXT("alternates"), Alternates))
                    for (const auto& Value : *Alternates) { const TSharedPtr<FJsonObject>* Alt = nullptr; if (Value->TryGetObject(Alt)) Add(*Alt); }
                if (Bodies.IsEmpty()) continue;
                ByArchetype.Add(Id, Bodies); RaceArt.Add(Id);
                TArray<FString> Keys; Bodies.GetKeys(Keys); Recommended.Add(Id, Keys[0]);
                UE_LOG(LogCireMonsterArt, Log, TEXT("CIRE_MONSTER_ART_FAB unit=%s bodies=%d"), *Id.ToString(), Bodies.Num());
            }
    }
    // world-dressing: RaceMeshes.free.json (CC0 animated creatures, /Game/Free/Creatures) is the lowest-priority
    // overlay: a unit takes a free body only when neither NPCMeshes nor RaceMeshes.tripo.json gave it one, and only
    // if the mesh package is present (so a missing or local-only pack silently falls back).
    {
        TSharedPtr<FJsonObject> FreeMeshes;
        const TSharedPtr<FJsonObject>* FreeUnits = nullptr;
        if (ReadFile(TEXT("RaceMeshes.free.json"), FreeMeshes) && (FreeMeshes->TryGetObjectField(TEXT("archetypes"), FreeUnits) || FreeMeshes->TryGetObjectField(TEXT("units"), FreeUnits)))
            for (const auto& Pair : (*FreeUnits)->Values)
            {
                const FName Id(FString(Pair.Key.ToView()));
                const TSharedPtr<FJsonObject>* Entry = nullptr;
                if (ByArchetype.Contains(Id) || !Pair.Value->TryGetObject(Entry)) continue;
                TMap<FString, CireMonsterArt::FBody> Bodies;
                auto Add = [&](const TSharedPtr<FJsonObject>& O)
                {
                    CireMonsterArt::FBody Body;
                    if (!ParseBody(O, Body)) return;
                    if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Body.MeshPath))) return;
                    Bodies.Add(Body.Variant, Body);
                };
                if ((*Entry)->HasField(TEXT("mesh"))) Add(*Entry);
                const TArray<TSharedPtr<FJsonValue>>* Alternates = nullptr;
                if ((*Entry)->TryGetArrayField(TEXT("alternates"), Alternates))
                    for (const auto& Value : *Alternates) { const TSharedPtr<FJsonObject>* Alt = nullptr; if (Value->TryGetObject(Alt)) Add(*Alt); }
                if (Bodies.IsEmpty()) continue;
                ByArchetype.Add(Id, Bodies); RaceArt.Add(Id);
                TArray<FString> Keys; Bodies.GetKeys(Keys); Recommended.Add(Id, Keys[0]);
            }
    }
    // fab-coverage: MonsterFabClips.json (Tools/RetargetFabAnimations.py -CireFabAnimMonsters) adds a Fab shout
    // (Gun & Sword "Buff") and ground slam retargeted onto Tripo monster bodies that have no clip of that name, so
    // rallies, enrages, guards and self circles stop reusing the attack swing. Derived from licensed packs: local only;
    // a missing package or another skeleton is skipped when the clips load, and -CireNoFab / cire.FabAnim 0 turn it off.
    if (CireFabAnimation::Enabled())
    {
        TSharedPtr<FJsonObject> FabClips;
        const TSharedPtr<FJsonObject>* Variants = nullptr;
        if (ReadFile(TEXT("MonsterFabClips.json"), FabClips) && FabClips->TryGetObjectField(TEXT("variants"), Variants))
        {
            int32 Added = 0;
            for (auto& Pair : ByArchetype)
                for (auto& BodyPair : Pair.Value)
                {
                    CireMonsterArt::FBody& Body = BodyPair.Value;
                    const TSharedPtr<FJsonObject>* Clips = nullptr;
                    if (Body.bFab || !(*Variants)->TryGetObjectField(Body.Variant, Clips)) continue;
                    // monster-rig: a weapon-matched set replaces the generic Tripo roles (same skeleton: retargeted
                    // onto this body). In-place set locomotion carries its natural speeds (raw mesh cm/s).
                    const TSharedPtr<FJsonObject>* SetRoles = nullptr;
                    if ((*Clips)->TryGetObjectField(TEXT("roles"), SetRoles))
                    {
                        int32 Replaced = 0;
                        for (const auto& Role : (*SetRoles)->Values)
                        {
                            FString Path; const FString Name(Role.Key.ToView());
                            if (!Role.Value->TryGetString(Path) || !Path.StartsWith(TEXT("/Game/"))) continue;
                            if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))) continue;
                            Body.Roles.Add(Name, Path); ++Replaced; ++Added;
                        }
                        double Walk = 0, Run = 0;
                        if (Replaced && Body.Roles.Contains(TEXT("walk")) && (*Clips)->TryGetNumberField(TEXT("walkSpeedCm"), Walk) && Walk > 1.)
                        {
                            Body.WalkSpeedCm = static_cast<float>(Walk);
                            if ((*Clips)->TryGetNumberField(TEXT("runSpeedCm"), Run) && Run > Walk) Body.RunSpeedCm = static_cast<float>(Run);
                        }
                        Body.FabSet = (*Clips)->GetStringField(TEXT("set"));
                    }
                    for (const auto& Clip : (*Clips)->Values)
                    {
                        if (Clip.Value->Type != EJson::String) continue;
                        FString Path; const FString Name(Clip.Key.ToView());
                        if (Body.Clips.Contains(Name) || !Clip.Value->TryGetString(Path) || !Path.StartsWith(TEXT("/Game/"))) continue;
                        if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))) continue;
                        Body.Clips.Add(Name, Path); ++Added;
                    }
                }
            const TSharedPtr<FJsonObject>* Windows = nullptr;
            if (FabClips->TryGetObjectField(TEXT("windows"), Windows))
                for (const auto& Pair : (*Windows)->Values)
                {
                    const TSharedPtr<FJsonObject>* W = nullptr; CireMonsterArt::FClipWindow Window; double V = 0;
                    if (!Pair.Value->TryGetObject(W)) continue;
                    if ((*W)->TryGetNumberField(TEXT("start"), V)) Window.Start = V;
                    if ((*W)->TryGetNumberField(TEXT("contact"), V)) Window.Contact = V;
                    if ((*W)->TryGetNumberField(TEXT("end"), V)) Window.End = V;
                    if ((*W)->TryGetNumberField(TEXT("recoverRate"), V)) Window.RecoverRate = FMath::Clamp(V, .2, 5.);
                    if (Window.Start >= 0 && Window.Contact >= Window.Start && Window.End >= Window.Contact)
                        Candidate.Windows.Add(FString(Pair.Key.ToView()), Window);
                }
            UE_LOG(LogCireMonsterArt, Log, TEXT("CIRE_MONSTER_ART_FAB_CLIPS added=%d"), Added);
        }
    }
    const TSharedPtr<FJsonObject>* ArtArchetypes = nullptr;
    const TSharedPtr<FJsonObject>* ArtBodies = nullptr;
    if (Art)
    {
        Art->TryGetObjectField(TEXT("archetypes"), ArtArchetypes);
        Art->TryGetObjectField(TEXT("bodies"), ArtBodies);
        const TSharedPtr<FJsonObject>* Clips = nullptr;
        if (Art->TryGetObjectField(TEXT("clips"), Clips))
            for (const auto& Pair : (*Clips)->Values)
            {
                const TSharedPtr<FJsonObject>* W = nullptr; CireMonsterArt::FClipWindow Window; double V = 0;
                if (!Pair.Value->TryGetObject(W)) continue;
                if ((*W)->TryGetNumberField(TEXT("start"), V)) Window.Start = V;
                if ((*W)->TryGetNumberField(TEXT("contact"), V)) Window.Contact = V;
                if ((*W)->TryGetNumberField(TEXT("end"), V)) Window.End = V;
                if ((*W)->TryGetNumberField(TEXT("recoverRate"), V)) Window.RecoverRate = FMath::Clamp(V, .2, 5.);
                if (Window.Start >= 0 && Window.Contact >= Window.Start && Window.End >= Window.Contact)
                    Candidate.Windows.Add(FString(Pair.Key.ToView()), Window);
            }
        const auto Color = [&](const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FLinearColor& Out)
        {
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            if (Object->TryGetArrayField(Key, Values) && Values->Num() >= 3)
                Out = FLinearColor((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber(), 1.f);
        };
        const TSharedPtr<FJsonObject>* Rims = nullptr;
        if (Art->TryGetObjectField(TEXT("rims"), Rims))
        {
            Color(*Rims, TEXT("elite"), Candidate.EliteRim); Color(*Rims, TEXT("boss"), Candidate.BossRim); Color(*Rims, TEXT("enraged"), Candidate.EnragedRim);
        }
        const TSharedPtr<FJsonObject>* Death = nullptr; double V = 0;
        if (Art->TryGetObjectField(TEXT("death"), Death))
        {
            if ((*Death)->TryGetNumberField(TEXT("holdSeconds"), V)) Candidate.DeathHoldSeconds = FMath::Clamp(V, 0., 30.);
            if ((*Death)->TryGetNumberField(TEXT("sinkSeconds"), V)) Candidate.DeathSinkSeconds = FMath::Clamp(V, .1, 10.);
            if ((*Death)->TryGetNumberField(TEXT("sinkCm"), V)) Candidate.DeathSinkCm = FMath::Clamp(V, 0., 300.);
        }
    }
    for (auto& Pair : ByArchetype)
    {
        CireMonsterArt::FArchetypeArt Entry;
        TArray<FString> Variants;
        const TSharedPtr<FJsonObject>* Rule = nullptr;
        if (ArtArchetypes && (*ArtArchetypes)->TryGetObjectField(Pair.Key.ToString(), Rule))
        {
            const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
            if ((*Rule)->TryGetArrayField(TEXT("variants"), List))
                for (const auto& Value : *List) { FString Name; if (Value->TryGetString(Name) && Pair.Value.Contains(Name)) Variants.AddUnique(Name); }
            const TSharedPtr<FJsonObject>* Clips = nullptr;
            if ((*Rule)->TryGetObjectField(TEXT("abilityClips"), Clips))
                for (const auto& Clip : (*Clips)->Values)
                {
                    FString Name; if (Clip.Value->TryGetString(Name)) Entry.AbilityClips.Add(FName(FString(Clip.Key.ToView())), Name);
                }
        }
        if (Variants.IsEmpty() && Recommended.Contains(Pair.Key)) Variants.Add(Recommended[Pair.Key]);
        if (RaceArt.Contains(Pair.Key)) for (const auto& Body : Pair.Value) Variants.AddUnique(Body.Key); // monster-races
        for (const FString& Name : Variants)
        {
            CireMonsterArt::FBody Body = Pair.Value[Name];
            const TSharedPtr<FJsonObject>* BodyRule = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* Drops = nullptr;
            if (ArtBodies && (*ArtBodies)->TryGetObjectField(Name, BodyRule))
            {
                if ((*BodyRule)->TryGetArrayField(TEXT("dropPropBones"), Drops))
                    for (const auto& Value : *Drops) { FString Bone; if (Value->TryGetString(Bone)) Body.DropPropBones.Add(FName(*Bone)); }
                FString Override;
                if ((*BodyRule)->TryGetStringField(TEXT("mesh"), Override) && Override.StartsWith(TEXT("/Game/"))) Body.MeshOverride = Override;
                // monster-rig: skin sway regions (tentacles/vines Tripo's humanoid rig gave no bones).
                const TSharedPtr<FJsonObject>* Sway = nullptr;
                if ((*BodyRule)->TryGetObjectField(TEXT("sway"), Sway))
                {
                    double N = 0;
                    if ((*Sway)->TryGetNumberField(TEXT("speed"), N) && FMath::IsFinite(N)) Body.SwaySpeed = FMath::Clamp(static_cast<float>(N), 0.f, 10.f);
                    if ((*Sway)->TryGetNumberField(TEXT("wave"), N) && FMath::IsFinite(N)) Body.SwayWave = FMath::Clamp(static_cast<float>(N), 0.f, 10.f);
                    auto Vec = [](const TSharedPtr<FJsonObject>& R, const TCHAR* Key, FVector& Out)
                    {
                        const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
                        if (R->TryGetArrayField(Key, V) && V->Num() == 3) Out = FVector((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber());
                    };
                    const TArray<TSharedPtr<FJsonValue>>* Regions = nullptr;
                    if ((*Sway)->TryGetArrayField(TEXT("regions"), Regions))
                        for (const auto& Value : *Regions)
                        {
                            const TSharedPtr<FJsonObject>* R = nullptr; CireMonsterArt::FSwayRegion Region; double V = 0;
                            if (!Value->TryGetObject(R) || Body.Sway.Num() >= 2) continue;
                            Vec(*R, TEXT("center"), Region.Center); Vec(*R, TEXT("radii"), Region.Radii);
                            if ((*R)->TryGetNumberField(TEXT("rootZ"), V)) Region.Root = static_cast<float>(V);
                            if ((*R)->TryGetNumberField(TEXT("tipZ"), V)) Region.Tip = static_cast<float>(V);
                            if ((*R)->TryGetNumberField(TEXT("amount"), V)) Region.Amount = FMath::Clamp(static_cast<float>(V), 0.f, 40.f);
                            Region.Radii = Region.Radii.ComponentMax(FVector(.1));
                            if (Region.Amount > 0.f && FMath::Abs(Region.Root - Region.Tip) > .5f) Body.Sway.Add(Region);
                        }
                }
                const TSharedPtr<FJsonObject>* Adjust = nullptr;
                if ((*BodyRule)->TryGetObjectField(TEXT("props"), Adjust))
                    for (const auto& Prop : (*Adjust)->Values)
                    {
                        const TSharedPtr<FJsonObject>* Row = nullptr; double Scale = 1;
                        if (!Prop.Value->TryGetObject(Row)) continue;
                        const FName Bone(FString(Prop.Key.ToView()));
                        if ((*Row)->TryGetNumberField(TEXT("scale"), Scale) && Scale > .05 && Scale < 10) Body.PropScale.Add(Bone, Scale);
                        const TArray<TSharedPtr<FJsonValue>>* Offset = nullptr;
                        if ((*Row)->TryGetArrayField(TEXT("offsetCm"), Offset) && Offset->Num() == 3)
                            Body.PropOffset.Add(Bone, FVector((*Offset)[0]->AsNumber(), (*Offset)[1]->AsNumber(), (*Offset)[2]->AsNumber()));
                        const TArray<TSharedPtr<FJsonValue>>* Rotation = nullptr;
                        if ((*Row)->TryGetArrayField(TEXT("rotation"), Rotation) && Rotation->Num() == 3)
                            Body.PropRotation.Add(Bone, FRotator((*Rotation)[0]->AsNumber(), (*Rotation)[1]->AsNumber(), (*Rotation)[2]->AsNumber()));
                    }
            }
            Entry.Bodies.Add(MoveTemp(Body));
        }
        if (!Entry.Bodies.IsEmpty()) Candidate.Archetypes.Add(Pair.Key, MoveTemp(Entry));
    }
    Candidate.bValid = !Candidate.Archetypes.IsEmpty();
    UE_LOG(LogCireMonsterArt, Log, TEXT("CIRE_MONSTER_ART_DATA archetypes=%d windows=%d"), Candidate.Archetypes.Num(), Candidate.Windows.Num());
    GData = MoveTemp(Candidate);
}

FTransform ReferenceBone(const FReferenceSkeleton& Skeleton, FName Bone)
{
    int32 Index = Skeleton.FindBoneIndex(Bone);
    if (Index == INDEX_NONE) return FTransform::Identity;
    FTransform Result = Skeleton.GetRefBonePose()[Index];
    while ((Index = Skeleton.GetParentIndex(Index)) != INDEX_NONE) Result = Result * Skeleton.GetRefBonePose()[Index];
    return Result;
}

float Smooth01(float X) { X = FMath::Clamp(X, 0.f, 1.f); return X * X * (3.f - 2.f * X); }
}

const CireMonsterArt::FData& CireMonsterArt::Data(bool bReload)
{
    if (!GLoaded || bReload) { GLoaded = true; Load(); }
    return GData;
}

const CireMonsterArt::FArchetypeArt* CireMonsterArt::Find(FName ArchetypeId)
{
    if (const FArchetypeArt* Own = Data().Archetypes.Find(ArchetypeId)) return Own;
    // monster-races: a race unit without its own art borrows its fallback archetype's body (reskinned by CireRaces).
    const FCireNPCArchetype* Archetype = CireNPCArchetypes::Find(ArchetypeId);
    return Archetype && !Archetype->FallbackBody.IsNone() && Archetype->FallbackBody != ArchetypeId ? Data().Archetypes.Find(Archetype->FallbackBody) : nullptr;
}
bool CireMonsterArt::HasOwnBody(FName ArchetypeId) { return Data().Archetypes.Contains(ArchetypeId); }

CireMonsterArt::FClipWindow CireMonsterArt::Window(const UAnimSequence* Sequence)
{
    FClipWindow Result;
    if (!Sequence) return Result;
    const float Length = Sequence->GetPlayLength();
    Result.Contact = Length * .5f; Result.End = Length;
    const FString Name = Sequence->GetName();
    int32 Best = 0;
    for (const auto& Pair : Data().Windows)
        if (Pair.Key.Len() > Best && Name.EndsWith(TEXT("_") + Pair.Key)) { Best = Pair.Key.Len(); Result = Pair.Value; }
    Result.End = FMath::Min(Result.End, Length); Result.Contact = FMath::Min(Result.Contact, Result.End);
    Result.Start = FMath::Min(Result.Start, Result.Contact);
    return Result;
}

// ---------------------------------------------------------------------------------------------
UCireMonsterArt::UCireMonsterArt()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
    SetIsReplicatedByDefault(true);
}

void UCireMonsterArt::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UCireMonsterArt, BodySeed);
    DOREPLIFETIME(UCireMonsterArt, SwingSerial);
    DOREPLIFETIME(UCireMonsterArt, SwingStartedAt);
    DOREPLIFETIME(UCireMonsterArt, SwingWindup);
}

void UCireMonsterArt::BeginPlay()
{
    Super::BeginPlay();
    if (GetOwner() && GetOwner()->HasAuthority() && BodySeed == 0) BodySeed = static_cast<uint16>(FMath::RandRange(1, 65535));
}

double UCireMonsterArt::ServerNow() const
{
    const UWorld* World = GetWorld();
    if (!World) return 0.0;
    const AGameStateBase* State = World->GetGameState();
    return State ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

UCireMonsterAnimInstance* UCireMonsterArt::GetMonsterAnim() const
{
    const auto* Monster = Cast<ACireMonster>(GetOwner());
    return Monster && Monster->GetMesh() ? Cast<UCireMonsterAnimInstance>(Monster->GetMesh()->GetAnimInstance()) : nullptr;
}

// ---- server swing ---------------------------------------------------------------------------
bool UCireMonsterArt::StartSwing(ACireHero* Victim, float Amount, const FString& AttackName, float Reach, float Period)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Victim || CVarSwingWindup.GetValueOnGameThread() == 0) return false;
    const float Now = GetWorld()->GetTimeSeconds();
    // A quarter-to-a-third of the attack period, capped so fast (rallied/enraged) swings stay snappy.
    // monster-rig (readability): the wind-up grows with how hard the blow lands. Elites read a little longer and
    // bosses clearly longer, so a heavy hit is always visible before it connects (cire.Monsters.SwingReadability 0: old).
    SwingWindup = FMath::Clamp(Period * .3f, .22f, .5f);
    if (CVarSwingReadability.GetValueOnGameThread() != 0)
        if (const auto* Monster = Cast<ACireMonster>(GetOwner()))
            if (const FCireNPCArchetype* Archetype = Monster->NPCState ? Monster->NPCState->Archetype() : nullptr)
            {
                const bool bBoss = Archetype->Classification == ECireNPCClass::Boss, bElite = Archetype->Classification == ECireNPCClass::Elite;
                const float Min = bBoss ? .55f : bElite ? .38f : .3f, Max = bBoss ? .85f : bElite ? .6f : .5f;
                SwingWindup = FMath::Clamp(Period * (bBoss ? .4f : .33f), Min, Max);
            }
    SwingStartedAt = Now; ++SwingSerial;
    PendingVictim = Victim; PendingAmount = Amount; PendingReach = Reach; PendingName = AttackName;
    PendingReleaseAt = Now + SwingWindup; bSwingPending = true;
    GetOwner()->ForceNetUpdate();
    return true;
}

void UCireMonsterArt::PresentInstantStrike()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    SwingWindup = 0.f; SwingStartedAt = GetWorld()->GetTimeSeconds(); ++SwingSerial;
    GetOwner()->ForceNetUpdate();
}

void UCireMonsterArt::CancelSwing()
{
    PendingVictim.Reset(); PendingReleaseAt = 0.f; PendingAmount = 0.f; bSwingPending = false;
}

bool UCireMonsterArt::ReleaseSwing(float Now)
{
    if (!HasPendingSwing() || Now < PendingReleaseAt) return false;
    auto* Monster = Cast<ACireMonster>(GetOwner());
    ACireHero* Victim = PendingVictim.Get();
    const float Amount = PendingAmount, Reach = PendingReach; const FString Name = PendingName;
    CancelSwing();
    if (!Monster || Monster->Health <= 0 || !IsValid(Victim) || Victim->bDead || Victim->Health <= 0 || Victim->TeamId != Monster->Lane) return false;
    // The blow was committed: a victim who stepped just outside the reach during the windup is still hit.
    const float Slack = 120.f + Monster->GetCapsuleComponent()->GetScaledCapsuleRadius();
    if (FVector::Dist2D(Monster->GetActorLocation(), Victim->GetActorLocation()) > Reach + Slack) return false;
    CireAttacks::Resolve(Monster, Victim, Amount, CireAttacks::Roll(Monster, Victim, false), Name);
    CireCombat::PlayCue(Monster, Victim, TEXT("npc_melee"), Monster->GetActorLocation(), Victim->GetActorLocation(), ECireSpellCue::Impact, .8f, true);
    return true;
}

// ---- body -----------------------------------------------------------------------------------
void UCireMonsterArt::CaptureFallback()
{
    if (bFallbackCaptured) return;
    auto* Monster = Cast<ACireMonster>(GetOwner());
    if (!Monster) return;
    FallbackMesh = Monster->GetMesh()->GetSkeletalMeshAsset();
    FallbackAnimClass = Monster->GetMesh()->GetAnimClass();
    FallbackTransform = Monster->GetMesh()->GetRelativeTransform();
    bFallbackCaptured = true;
}

void UCireMonsterArt::RestoreFallback()
{
    auto* Monster = Cast<ACireMonster>(GetOwner());
    if (!Monster || !bTripoApplied) { bTripoApplied = false; return; }
    auto* Mesh = Monster->GetMesh();
    if (Rim && Mesh->GetOverlayMaterial() == Rim) Mesh->SetOverlayMaterial(nullptr);
    Rim = nullptr; AppliedRimColor = FLinearColor::Transparent;
    Mesh->SetAnimInstanceClass(nullptr);
    if (bFallbackCaptured && FallbackMesh)
    {
        Mesh->SetSkeletalMesh(FallbackMesh);
        Mesh->EmptyOverrideMaterials();
        Mesh->SetRelativeTransform(FallbackTransform);
        Monster->CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());
        if (FallbackAnimClass) Mesh->SetAnimInstanceClass(FallbackAnimClass);
    }
    RoleClips.Reset(); NamedClips.Reset(); Current = FAction();
    ClearBodyParts();
    bTripoApplied = false; AppliedVariant.Reset(); AppliedArchetype = NAME_None;
    SetComponentTickEnabled(false);
}

void UCireMonsterArt::ClearBodyParts()
{
    for (auto& Part : BodyParts) if (Part) Part->DestroyComponent();
    BodyParts.Reset();
}

UAnimSequence* UCireMonsterArt::RoleClip(const FString& Role) const
{
    const TObjectPtr<UAnimSequence>* Found = RoleClips.Find(Role);
    return Found ? Found->Get() : nullptr;
}

UAnimSequence* UCireMonsterArt::NamedClip(const FString& Name) const
{
    const TObjectPtr<UAnimSequence>* Found = NamedClips.Find(Name);
    return Found ? Found->Get() : nullptr;
}

bool UCireMonsterArt::ApplyBody(const FCireNPCArchetype& Archetype, TArray<TObjectPtr<UStaticMeshComponent>>& OutParts)
{
    auto* Monster = Cast<ACireMonster>(GetOwner());
    if (!Monster || Monster->GetNetMode() == NM_DedicatedServer) return false;
    const auto* Art = CVarTripoBodies.GetValueOnGameThread() ? CireMonsterArt::Find(Archetype.Id) : nullptr;
    if (!Art || Art->Bodies.IsEmpty()) { RestoreFallback(); return false; }
    const int32 Index = (ForcedVariant >= 0 ? ForcedVariant : static_cast<int32>(BodySeed)) % Art->Bodies.Num();
    const CireMonsterArt::FBody& Body = Art->Bodies[Index];
    USkeletalMesh* Original = LoadIfPresent<USkeletalMesh>(Body.MeshPath);
    USkeletalMesh* Asset = LoadIfPresent<USkeletalMesh>(Body.MeshOverride);
    // The override must share the original skeleton, or the original clips would not play on it.
    if (!Asset || !Original || Asset->GetSkeleton() != Original->GetSkeleton()) Asset = Original;
    TMap<FString, TObjectPtr<UAnimSequence>> Loaded, Named;
    if (Asset)
    {
        for (const auto& Pair : Body.Roles)
            if (auto* Clip = LoadIfPresent<UAnimSequence>(Pair.Value); Clip && Clip->GetSkeleton() == Asset->GetSkeleton() && Clip->GetPlayLength() > 0.f)
                Loaded.Add(Pair.Key, Clip);
        for (const auto& Pair : Body.Clips)
            if (auto* Clip = LoadIfPresent<UAnimSequence>(Pair.Value); Clip && Clip->GetSkeleton() == Asset->GetSkeleton() && Clip->GetPlayLength() > 0.f)
                Named.Add(Pair.Key, Clip);
    }
    if (!Asset || !Loaded.Contains(TEXT("idle")))
    {
        UE_LOG(LogCireMonsterArt, Warning, TEXT("CIRE_MONSTER_ART_FALLBACK archetype=%s variant=%s (missing mesh or idle clip)"), *Archetype.Id.ToString(), *Body.Variant);
        RestoreFallback();
        return false;
    }
    // world-dressing: animal bodies get humanoid-named sockets (aura, footsteps, hit points) on their own bones.
    if (!Body.Sockets.IsEmpty())
    {
        bool bAdded = false;
        for (const auto& Pair : Body.Sockets)
        {
            if (Asset->FindSocket(Pair.Key) || Asset->GetRefSkeleton().FindBoneIndex(Pair.Value) == INDEX_NONE) continue;
            USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Asset, NAME_None, RF_Transient);
            Socket->SocketName = Pair.Key; Socket->BoneName = Pair.Value;
            Asset->AddSocket(Socket, false); bAdded = true;
        }
        if (bAdded) Asset->RebuildSocketMap();
    }
    CaptureFallback();
    auto* Mesh = Monster->GetMesh();
    UMaterialInterface* Overlay = Mesh->GetOverlayMaterial();
    if (Rim && Overlay == Rim) Overlay = nullptr;
    // Clear the mannequin AnimBP before assigning an incompatible skeleton (it would collapse the body).
    Mesh->SetAnimInstanceClass(nullptr);
    Mesh->SetSkeletalMesh(Asset);
    Mesh->EmptyOverrideMaterials();
    Mesh->SetRelativeScale3D(FVector(Body.MeshScale));
    Mesh->SetRelativeRotation(FRotator(0, Body.Yaw, 0));
    VisualTurn = CireLocomotion::FVisualTurn(); LegIK = CireLocomotion::FLegIK(); bFeelTicksOrdered = false; // movement-feel: new rig, rest heading
    // Tripo pivots sit at the soles: the pivot goes on the capsule bottom. The actor scale (archetype
    // scale, elite tier, enrage) scales capsule and body together, so feet stay grounded at every size.
    // Some idle clips press the toes below the pivot: lift so the toe joints sit ~1.8% of the body above the floor.
    float Lift = 0.f;
    if (UAnimSequence* Stance = Loaded.FindRef(TEXT("idle")))
    {
        const float RawHeight = Body.HeightCm / FMath::Max(.01f, Body.MeshScale);
        Lift = FMath::Clamp(.018f * RawHeight - CireAnimClips::Analyze(Stance).StanceBallZ, -.02f * RawHeight, .05f * RawHeight);
    }
    Mesh->SetRelativeLocation(FVector(0, 0, -Monster->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + Lift * Body.MeshScale));
    Monster->CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());
    Mesh->SetAnimInstanceClass(UCireMonsterAnimInstance::StaticClass());
    Mesh->SetOverlayMaterial(Overlay);
    Mesh->SetVisibility(true, false);
    RoleClips = MoveTemp(Loaded); NamedClips = MoveTemp(Named);
    if (auto* Anim = GetMonsterAnim())
    {
        Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
        Anim->bLockRootToReference = Body.bLockRoot; // world-dressing
        Anim->Idle.Sequence = RoleClip(TEXT("idle")); Anim->Idle.Weight = 1.f;
        Anim->Walk.Sequence = RoleClip(TEXT("walk")); Anim->Walk.bRemoveDrift = true;
        Anim->Run.Sequence = RoleClip(TEXT("run")); Anim->Run.bRemoveDrift = true;
        // Walk/run carry the body forward on the pelvis; cycle it in place over the idle stance instead.
        const CireAnimClips::FClipInfo& Stance = CireAnimClips::Analyze(Anim->Idle.Sequence);
        Anim->Walk.PelvisTarget = Anim->Run.PelvisTarget = Stance.bValid ? Stance.DriftOffset : Stance.ReferencePelvis;
        Anim->Action = FCireAnimLayer(); Anim->Death = FCireAnimLayer();
        Anim->Side = FCireAnimLayer(); Anim->Side.bRemoveDrift = true; Anim->Side.PelvisTarget = Anim->Walk.PelvisTarget; Anim->SideAlpha = 0.f;
        Anim->MoveAlpha = Anim->RunAlpha = 0.f;
    }
    else { RestoreFallback(); return false; }
    GripHands = CireGrip::FHands();
    // Props: drop what the body already carries in its mesh, keep the rest on the right bones.
    const USkeletalMesh& Skeletal = *Asset;
    const FReferenceSkeleton& Reference = Skeletal.GetRefSkeleton();
    FVector Forward = ((ReferenceBone(Reference, TEXT("ball_l")).GetLocation() - ReferenceBone(Reference, TEXT("foot_l")).GetLocation()) +
        (ReferenceBone(Reference, TEXT("ball_r")).GetLocation() - ReferenceBone(Reference, TEXT("foot_r")).GetLocation())).GetSafeNormal2D();
    if (Forward.IsNearlyZero()) Forward = FVector(0, 1, 0);
    const FQuat Upright = FRotationMatrix::MakeFromXZ(Forward, FVector::UpVector).ToQuat();
    for (const FCireNPCProp& Prop : Archetype.Props)
    {
        if (Body.DropPropBones.Contains(Prop.Bone) || Reference.FindBoneIndex(Prop.Bone) == INDEX_NONE) continue;
        auto* PropMesh = LoadIfPresent<UStaticMesh>(Prop.Asset);
        if (!PropMesh) continue;
        auto* Part = NewObject<UStaticMeshComponent>(Monster);
        Monster->AddInstanceComponent(Part);
        Part->SetupAttachment(Mesh, Prop.Bone);
        Part->SetStaticMesh(PropMesh);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false); Part->SetCastShadow(true);
        const bool bHand = Prop.Bone == TEXT("hand_r") || Prop.Bone == TEXT("hand_l");
        if (bHand) Part->ComponentTags.AddUnique(TEXT("CireWeaponProp"));
        if (bHand)
            if (const CireGrip::FWeapon* Grip = CireGrip::FindWeapon(PropMesh))
            {
                // Handle inside the curled fist (CireGrip); shields strap onto the forearm.
                const float Size = Prop.Scale * (Body.PropScale.Contains(Prop.Bone) ? Body.PropScale[Prop.Bone] : 1.f);
                const CireGrip::FPlacement Placement = CireGrip::Place(Skeletal, Prop.Bone, *Grip, Size, Body.MeshScale);
                if (Placement.bValid)
                {
                    Part->SetupAttachment(Mesh, Placement.Bone);
                    Part->SetRelativeTransform(Placement.Relative);
                    Part->ComponentTags.AddUnique(FName(*(TEXT("CireGripHand_") + Prop.Bone.ToString())));
                    Part->RegisterComponent();
                    OutParts.Add(Part);
                    CireGrip::AddToHands(Skeletal, Placement, GripHands);
                    continue;
                }
            }
        const FTransform Bone = ReferenceBone(Reference, Prop.Bone);
        FQuat Frame = Upright;
        if (bHand)
        {
            // Grip frame from the bind-pose hand: the handle runs across the palm (pinky -> index), so the
            // blade / bow limb / shield top leaves on the thumb side (+Z) and the prop's face (+X) is the back
            // of the hand. Held that way, a sword points ahead of a lowered arm instead of out to the side.
            const TCHAR* Side = Prop.Bone == TEXT("hand_l") ? TEXT("_l") : TEXT("_r");
            const FVector Hand = Bone.GetLocation();
            const FVector Middle = ReferenceBone(Reference, FName(FString(TEXT("middle_01")) + Side)).GetLocation();
            const FVector IndexFinger = ReferenceBone(Reference, FName(FString(TEXT("index_01")) + Side)).GetLocation();
            const FVector Pinky = ReferenceBone(Reference, FName(FString(TEXT("pinky_01")) + Side)).GetLocation();
            const FVector Arm = (Middle - Hand).GetSafeNormal();
            FVector Across = IndexFinger - Pinky; Across = (Across - Arm * FVector::DotProduct(Across, Arm)).GetSafeNormal();
            FVector Back = FVector::UpVector - Arm * FVector::DotProduct(FVector::UpVector, Arm) - Across * FVector::DotProduct(FVector::UpVector, Across);
            Back = Back.GetSafeNormal();
            if (!Arm.IsNearlyZero() && !Across.IsNearlyZero() && !Back.IsNearlyZero()) Frame = FRotationMatrix::MakeFromXZ(Back, Across).ToQuat();
        }
        const FRotator* Turn = Body.PropRotation.Find(Prop.Bone);
        Part->SetRelativeRotation(Bone.GetRotation().Inverse() * Frame * (Turn ? *Turn : Prop.Rotation).Quaternion());
        FVector Grip = FVector::ZeroVector;
        if (bHand)
        {
            const FName Knuckle(Prop.Bone == TEXT("hand_l") ? TEXT("middle_01_l") : TEXT("middle_01_r"));
            if (Reference.FindBoneIndex(Knuckle) != INDEX_NONE)
                Grip = Bone.InverseTransformPosition((Bone.GetLocation() + ReferenceBone(Reference, Knuckle).GetLocation()) * .5);
        }
        const FVector* Offset = Body.PropOffset.Find(Prop.Bone);
        Grip += Bone.InverseTransformVector(Upright.RotateVector(Offset ? *Offset : Prop.Offset) / Body.MeshScale);
        Part->SetRelativeLocation(Grip);
        // Bone transforms inherit the imported root scale (100): props are authored in real centimetres.
        const float BoneScale = static_cast<float>(Bone.GetScale3D().GetAbsMax()) * Body.MeshScale;
        const float PropScale = Prop.Scale * (Body.PropScale.Contains(Prop.Bone) ? Body.PropScale[Prop.Bone] : 1.f);
        Part->SetRelativeScale3D(FVector(BoneScale > UE_SMALL_NUMBER ? PropScale / BoneScale : PropScale));
        Part->RegisterComponent();
        OutParts.Add(Part);
    }
    bFabApplied = Body.bFab; // fab-integration
    // fab-integration: leader-pose parts (the Centaur's armour, mane and bow are separate meshes on its skeleton).
    ClearBodyParts();
    for (const FString& PartPath : Body.Parts)
    {
        USkeletalMesh* PartMesh = LoadIfPresent<USkeletalMesh>(PartPath);
        if (!PartMesh || PartMesh->GetSkeleton() != Asset->GetSkeleton()) continue;
        auto* Part = NewObject<USkeletalMeshComponent>(Monster);
        Monster->AddInstanceComponent(Part);
        Part->SetupAttachment(Mesh);
        Part->SetSkeletalMesh(PartMesh);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false); Part->SetCastShadow(true);
        Part->RegisterComponent();
        Part->SetLeaderPoseComponent(Mesh); Part->SetVisibility(Mesh->IsVisible());
        BodyParts.Add(Part);
    }
    // monster-expansion: skeletal props on their own skeleton ride a socket/bone of the body (not leader pose).
    for (const auto& Attachment : Body.Attachments)
    {
        USkeletalMesh* PartMesh = LoadIfPresent<USkeletalMesh>(Attachment.Key);
        if (!PartMesh || (!Asset->FindSocket(Attachment.Value) && Asset->GetRefSkeleton().FindBoneIndex(Attachment.Value) == INDEX_NONE)) continue;
        auto* Part = NewObject<USkeletalMeshComponent>(Monster);
        Monster->AddInstanceComponent(Part);
        Part->SetupAttachment(Mesh, Attachment.Value);
        Part->SetSkeletalMesh(PartMesh);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false); Part->SetCastShadow(true);
        Part->ComponentTags.AddUnique(TEXT("CireAttachment"));
        Part->RegisterComponent();
        Part->SetVisibility(Mesh->IsVisible());
        BodyParts.Add(Part);
    }
    AppliedReskinBody = Body.ReskinTextures.IsEmpty() ? CireMonsterArt::FBody() : Body; // monster-expansion
    bAppliedSpectral = Body.bSpectral;
    AppliedSwayRegions = Body.Sway; AppliedSwaySpeed = Body.SwaySpeed; AppliedSwayWave = Body.SwayWave; // monster-rig
    bTripoApplied = true; AppliedArchetype = Archetype.Id; AppliedVariant = Body.Variant; AppliedMeshScale = Body.MeshScale;
    AppliedWalkRaw = Body.WalkSpeedCm / FMath::Max(.01f, Body.MeshScale); AppliedRunRaw = Body.RunSpeedCm / FMath::Max(.01f, Body.MeshScale); // world-dressing
    Current = FAction(); SeenSwingSerial = SwingSerial; SeenCastStartedAt = -1.f; // a cast already under way is picked up mid-bar
    LastHealth = Monster->Health; Phase = FMath::FRand(); IdleTime = FMath::FRand() * 5.f;
    CireRaces::ApplySkin(Monster); // monster-races: race palette + rank colours on the body's own textures
    UpdateRim();
    SetComponentTickEnabled(true);
    UE_LOG(LogCireMonsterArt, Verbose, TEXT("CIRE_MONSTER_ART_APPLIED archetype=%s variant=%s scale=%.3f"), *Archetype.Id.ToString(), *Body.Variant, Body.MeshScale);
    return true;
}

void UCireMonsterArt::ForceVariant(int32 Index)
{
    ForcedVariant = Index;
    if (auto* Monster = Cast<ACireMonster>(GetOwner()); Monster && Monster->NPCState)
    {
        Monster->NPCState->AppliedVisualArchetype = NAME_None;
        Monster->NPCState->ApplyVisuals();
    }
}

void UCireMonsterArt::OnRep_BodySeed()
{
    auto* Monster = Cast<ACireMonster>(GetOwner());
    if (!Monster || !Monster->NPCState || Monster->NPCState->ArchetypeId.IsNone() || ForcedVariant >= 0) return;
    const auto* Art = CireMonsterArt::Find(Monster->NPCState->ArchetypeId);
    if (!bTripoApplied || !Art || Art->Bodies.Num() < 2 || Art->Bodies[BodySeed % Art->Bodies.Num()].Variant == AppliedVariant) return;
    Monster->NPCState->AppliedVisualArchetype = NAME_None;
    Monster->NPCState->ApplyVisuals();
}

FVector2D UCireMonsterArt::GroundSpeeds() const
{
    const auto* Monster = Cast<ACireMonster>(GetOwner());
    const float Scale = Monster ? static_cast<float>(Monster->GetMesh()->GetComponentScale().X) : 1.f;
    const auto Speed = [&](const TCHAR* Role) { const UAnimSequence* Clip = RoleClip(Role); return Clip ? CireAnimClips::Analyze(Clip).GroundSpeed() * Scale : 0.f; };
    if (AppliedWalkRaw > 0.f) return FVector2D(AppliedWalkRaw * Scale, FMath::Max(AppliedRunRaw, AppliedWalkRaw * 1.5f) * Scale); // world-dressing
    return FVector2D(Speed(TEXT("walk")), Speed(TEXT("run")));
}

void UCireMonsterArt::UpdateRim()
{
    auto* Monster = Cast<ACireMonster>(GetOwner());
    if (!Monster || !bTripoApplied) return;
    const auto& D = CireMonsterArt::Data();
    const ECireNPCClass Class = Monster->GetNPCClassification();
    const bool bEnraged = Monster->NPCState && Monster->NPCState->HasStatus(CireNPCStatus::Enraged);
    // monster-races: the race skin carries the rank rim itself; without it the overlay rim shows the rank colour.
    const ECireNPCRank RankValue = CireRaces::RankOf(Monster);
    const FLinearColor Want = bEnraged ? D.EnragedRim : CireRaces::HasSkin(Monster) ? FLinearColor::Transparent :
        RankValue != ECireNPCRank::Normal ? CireRaces::RankColor(Monster) * 1.2f :
        Class == ECireNPCClass::Boss ? D.BossRim : Class == ECireNPCClass::Elite ? D.EliteRim : FLinearColor::Transparent;
    if (Want == AppliedRimColor) return;
    AppliedRimColor = Want;
    auto* Mesh = Monster->GetMesh();
    if (Want.A <= 0.f)
    {
        if (Rim && Mesh->GetOverlayMaterial() == Rim) Mesh->SetOverlayMaterial(nullptr);
        return;
    }
    if (!Rim)
        if (auto* Edge = LoadIfPresent<UMaterialInterface>(TEXT("/Game/Art/Materials/M_SelectionEdge.M_SelectionEdge")))
            Rim = UMaterialInstanceDynamic::Create(Edge, this);
    if (!Rim) return;
    // A dim fresnel rim: readable at gameplay distance without competing with the selection edge.
    // monster-expansion: rares and bonus creatures glow much brighter than the rank rim.
    Rim->SetVectorParameterValue(TEXT("SelectionTint"), Want * (Monster->SpecialSpawn != 0 ? 1.6f : .55f));
    // Never replace another overlay (the selection highlight stores and restores ours).
    if (!Mesh->GetOverlayMaterial()) Mesh->SetOverlayMaterial(Rim);
}

UAnimSequence* UCireMonsterArt::ClipForAbility(FName AbilityId) const
{
    const auto* Monster = Cast<ACireMonster>(GetOwner());
    const FCireNPCArchetype* Archetype = Monster && Monster->NPCState ? Monster->NPCState->Archetype() : nullptr;
    if (const auto* Art = CireMonsterArt::Find(AppliedArchetype))
        if (const FString* Name = Art->AbilityClips.Find(AbilityId))
            if (UAnimSequence* Clip = NamedClip(*Name)) return Clip;
    const FCireNPCAbility* Ability = Archetype ? Archetype->FindAbility(AbilityId) : nullptr;
    UAnimSequence* Attack = RoleClip(TEXT("attack"));
    // monster-rig: weapon-matched set clips first (a telegraphed heavy strike for cones/charges, the set's cast and
    // shout), then the Tripo library clips.
    UAnimSequence* Cast = RoleClip(TEXT("cast")) ? RoleClip(TEXT("cast")) : NamedClip(TEXT("cast_a_spell"));
    UAnimSequence* Shout = RoleClip(TEXT("shout")) ? RoleClip(TEXT("shout")) : NamedClip(TEXT("war_cry"));
    UAnimSequence* Heavy = RoleClip(TEXT("heavy")) ? RoleClip(TEXT("heavy")) : Attack;
    UAnimSequence* Heavy2 = RoleClip(TEXT("heavy2")) ? RoleClip(TEXT("heavy2")) : Heavy;
    if (!Ability) return Cast ? Cast : Attack;
    switch (Ability->Kind)
    {
    case ECireNPCAbilityKind::Projectile: return Attack;
    case ECireNPCAbilityKind::Melee: return Attack;
    case ECireNPCAbilityKind::Cone: return Heavy;
    case ECireNPCAbilityKind::Charge: case ECireNPCAbilityKind::Pull: return Heavy2;
    case ECireNPCAbilityKind::SelfCircle: if (auto* Slam = NamedClip(TEXT("ground_slam"))) return Slam; return Heavy2;
    case ECireNPCAbilityKind::Rally: case ECireNPCAbilityKind::Enrage: case ECireNPCAbilityKind::Provoke:
        return Shout ? Shout : Cast ? Cast : Attack;
    default: return Cast ? Cast : Shout ? Shout : Attack;
    }
}

void UCireMonsterArt::UpdateDirectionalGait(float DeltaTime, const ACireMonster& Monster, UCireMonsterAnimInstance& Anim, float WalkSpeed)
{
    // monster-rig: the set's strafe / back-pedal clips when the body moves off its facing, and short side steps
    // when it turns on the spot, so a monster circling or backing off never glides on its forward cycle.
    // movement-feel owns this when it is on (travel warp, reversed gait for backpedals, stepped turns): the set's
    // directional clips would double the lower-body turn, so this layer is only the fallback (cire.Locomotion 0).
    if (CireLocomotion::Enabled())
    {
        Anim.SideAlpha = 0.f; Anim.Side.Sequence = nullptr;
        LastYaw = static_cast<float>(Monster.GetActorRotation().Yaw); SmoothedYawRate = 0.f;
        return;
    }
    const float Yaw = static_cast<float>(Monster.GetActorRotation().Yaw);
    const float YawRate = DeltaTime > KINDA_SMALL_NUMBER ? FMath::FindDeltaAngleDegrees(LastYaw, Yaw) / DeltaTime : 0.f;
    LastYaw = Yaw;
    SmoothedYawRate = FMath::FInterpTo(SmoothedYawRate, YawRate, DeltaTime, 6.f);
    UAnimSequence* Target = nullptr;
    float Alpha = 0.f;
    const FVector Velocity = Monster.Health > 0 ? Monster.GetVelocity() : FVector::ZeroVector;
    if (Velocity.Size2D() > WalkSpeed * .2f)
    {
        const float Angle = FMath::FindDeltaAngleDegrees(Yaw, static_cast<float>(Velocity.Rotation().Yaw)); // + = moving to its right
        const float Abs = FMath::Abs(Angle);
        if (Abs > 115.f) { Target = RoleClip(TEXT("walk_b")); Alpha = FMath::Clamp((Abs - 115.f) / 30.f, 0.f, 1.f); }
        if (!Target) { Target = RoleClip(Angle > 0.f ? TEXT("walk_r") : TEXT("walk_l")); Alpha = FMath::Clamp((FMath::Min(Abs, 180.f - Abs) - 30.f) / 45.f, 0.f, 1.f); }
    }
    else if (Monster.Health > 0 && FMath::Abs(SmoothedYawRate) > 50.f && !Current.Sequence)
    {
        // Turning in place: side-step toward the turn, the step rate following the yaw rate.
        Target = RoleClip(SmoothedYawRate > 0.f ? TEXT("walk_r") : TEXT("walk_l"));
        Alpha = FMath::Clamp((FMath::Abs(SmoothedYawRate) - 50.f) / 90.f, 0.f, 1.f);
        if (Target) Anim.MoveAlpha = FMath::Max(Anim.MoveAlpha, .7f * Alpha);
        Phase = FMath::Frac(Phase + DeltaTime * FMath::Clamp(FMath::Abs(SmoothedYawRate) / 180.f, .4f, 1.4f) / FMath::Max(.1f, Target ? Target->GetPlayLength() : 1.f));
    }
    if (Target && Target != Anim.Side.Sequence && Anim.SideAlpha < .05f) Anim.Side.Sequence = Target;
    const float Goal = Target == Anim.Side.Sequence ? Alpha : 0.f; // change clips only after fading out
    Anim.SideAlpha = FMath::FInterpTo(Anim.SideAlpha, Goal, DeltaTime, 7.f);
    if (UAnimSequence* SideClip = Anim.Side.Sequence)
        Anim.Side.Time = FMath::Frac(Phase + CireAnimClips::Analyze(SideClip).LeftFootApexPhase) * SideClip->GetPlayLength();
}

void UCireMonsterArt::StartAction(UAnimSequence* Sequence, double StartedAt, float Windup, float Weight, float LowerBody, bool bCast)
{
    if (!Sequence) return;
    Current = FAction();
    Current.Sequence = Sequence; Current.Window = CireMonsterArt::Window(Sequence);
    Current.StartedAt = StartedAt; Current.Windup = Windup; Current.Weight = Weight; Current.LowerBody = LowerBody; Current.bCast = bCast;
}

bool UCireMonsterArt::PlayAction(const FString& Role, float WindupSeconds)
{
    UAnimSequence* Clip = RoleClip(Role);
    if (!Clip) Clip = NamedClip(Role);
    if (!Clip || !bTripoApplied) return false;
    StartAction(Clip, ServerNow(), WindupSeconds, 1.f, 1.f, false);
    return true;
}

void UCireMonsterArt::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (bTripoApplied && !bFrozen) UpdatePresentation(DeltaTime);
}

void UCireMonsterArt::UpdatePresentation(float DeltaTime)
{
    auto* Monster = Cast<ACireMonster>(GetOwner());
    UCireMonsterAnimInstance* Anim = GetMonsterAnim();
    if (!Monster || !Anim || !FMath::IsFinite(DeltaTime)) return;
    DeltaTime = FMath::Clamp(DeltaTime, 0.f, .25f);
    const double Now = ServerNow();
    const float Scale = static_cast<float>(Monster->GetMesh()->GetComponentScale().X);

    // ---- locomotion: idle/walk/run by speed, phase-locked, rate matched to ground speed ----
    const float Speed = Monster->Health > 0 ? static_cast<float>(Monster->GetVelocity().Size2D()) : 0.f;
    SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, Speed, DeltaTime, 10.f);
    UAnimSequence* WalkClip = Anim->Walk.Sequence;
    UAnimSequence* RunClip = Anim->Run.Sequence;
    const CireAnimClips::FClipInfo& WalkInfo = CireAnimClips::Analyze(WalkClip);
    const CireAnimClips::FClipInfo& RunInfo = CireAnimClips::Analyze(RunClip);
    float WalkSpeed = FMath::Max(20.f, WalkInfo.GroundSpeed() * Scale);
    float RunSpeed = FMath::Max(WalkSpeed + 50.f, RunInfo.GroundSpeed() * Scale);
    // world-dressing: in-place clips of free creatures carry their natural speeds in data.
    if (AppliedWalkRaw > 0.f) { WalkSpeed = AppliedWalkRaw * Scale; RunSpeed = FMath::Max(WalkSpeed + 50.f, AppliedRunRaw * Scale); }
    // movement-feel: the stride each clip was authored for, measured from its planted contacts (the data speeds of
    // several Fab creatures are placeholders, e.g. 60 cm/s, which made gaits play far too fast or slow).
    WalkSpeed = CireLocomotion::GaitSpeed(WalkClip, Scale, WalkSpeed);
    if (RunClip && RunClip != WalkClip) RunSpeed = FMath::Max(WalkSpeed + 50.f, CireLocomotion::GaitSpeed(RunClip, Scale, RunSpeed));
    else if (CireLocomotion::Enabled()) RunSpeed = WalkSpeed + 1.f; // one gait clip: its own stride sets the cadence
    const float RunAlpha = RunClip ? FMath::Clamp((SmoothedSpeed - WalkSpeed) / (RunSpeed - WalkSpeed), 0.f, 1.f) : 0.f;
    const float MoveTarget = FMath::Clamp(SmoothedSpeed / (WalkSpeed * .35f), 0.f, 1.f);
    Anim->MoveAlpha = FMath::FInterpTo(Anim->MoveAlpha, MoveTarget, DeltaTime, 8.f);
    Anim->RunAlpha = RunAlpha;
    const float WalkLength = WalkClip ? WalkClip->GetPlayLength() : 1.f, RunLength = RunClip ? RunClip->GetPlayLength() : WalkLength;
    const float Cycle = FMath::Lerp(WalkLength, RunLength, RunAlpha);
    const float NaturalSpeed = FMath::Lerp(WalkSpeed, RunSpeed, RunAlpha);
    // One cycle per the clip's own stride: planted feet move with the ground, not across it.
    // A body with a single gait clip (the Undead zombie's shuffling walk carries a 175 cm/s shambler) has no faster
    // stride to blend to, so its cadence may rise further before the feet are allowed to slide.
    const bool bSingleGait = !RunClip || RunClip == WalkClip;
    const float Rate = CireLocomotion::Enabled() ? FMath::Clamp(SmoothedSpeed / NaturalSpeed, .3f, bSingleGait ? 4.f : 2.5f) : FMath::Clamp(SmoothedSpeed / NaturalSpeed, .35f, 2.2f);
    float Direction = 1.f, StepCycles = 0.f;
    if (CireLocomotion::Enabled())
    {
        // movement-feel: the legs point along travel (kiting sidesteps, backpedals run the gait backwards), the body turns
        // smoothly (attack facing, steering corners), standing turns step, and humanoid feet follow the ground.
        const float ActorYaw = static_cast<float>(Monster->GetActorRotation().Yaw);
        const float Target = CireLocomotion::TravelWarp(ActorYaw, Monster->GetVelocity(), 70.f, VisualTurn, DeltaTime);
        if (!bFeelTicksOrdered)
        {   // evaluate after the AI (facing) and this presentation tick in the same frame: no one-frame stale parameters
            Monster->GetMesh()->PrimaryComponentTick.AddPrerequisite(Monster, Monster->PrimaryActorTick);
            Monster->GetMesh()->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
            bFeelTicksOrdered = true;
        }
        VisualTurn.Update(ActorYaw, Target, Monster->GetActorLocation(), Speed, DeltaTime, 45.f);
        Direction = VisualTurn.bReverse ? -1.f : 1.f;
        StepCycles = VisualTurn.StepDelta / 180.f;
        Anim->MoveAlpha = FMath::Max(Anim->MoveAlpha, VisualTurn.StepWeight * .8f);
        LegIK.Update(*Monster, *Monster->GetMesh(), DeltaTime, Monster->Health > 0 && Monster->GetCharacterMovement()->IsMovingOnGround());
        Anim->Feel.Set(VisualTurn, LegIK, Scale, .85f);
    }
    else Anim->Feel = CireLocomotion::FPoseFeel();
    if (Anim->MoveAlpha > .01f) Phase = FMath::Frac(Phase + Direction * DeltaTime * Rate / FMath::Max(.1f, Cycle) + StepCycles);
    if (WalkClip) Anim->Walk.Time = FMath::Frac(Phase + WalkInfo.LeftFootApexPhase) * WalkLength;
    if (RunClip) Anim->Run.Time = FMath::Frac(Phase + RunInfo.LeftFootApexPhase) * RunLength;
    if (UAnimSequence* Idle = Anim->Idle.Sequence) { IdleTime = FMath::Fmod(IdleTime + DeltaTime, Idle->GetPlayLength()); Anim->Idle.Time = IdleTime; }
    UpdateDirectionalGait(DeltaTime, *Monster, *Anim, WalkSpeed);

    // ---- triggers: melee swing, cast bar, hit reaction ----
    if (SwingSerial != SeenSwingSerial)
    {
        SeenSwingSerial = SwingSerial;
        // monster-rig: basic swings rotate through the set's attacks (same order on every client: the serial replicates).
        UAnimSequence* Swings[3] = {RoleClip(TEXT("attack")), RoleClip(TEXT("attackAlt")), RoleClip(TEXT("attack3"))};
        int32 NumSwings = 0;
        for (UAnimSequence* Swing : Swings) if (Swing) Swings[NumSwings++] = Swing;
        if (UAnimSequence* Attack = NumSwings ? Swings[SwingSerial % NumSwings] : nullptr)
        {
            const bool bRanged = CireNPCArchetypes::IsRangedRole(Monster->GetNPCRole()); // monster-races: supports shoot too
            if (!bRanged) StartAction(Attack, SwingStartedAt, SwingWindup, 1.f, 1.f, false);
        }
    }
    if (!Monster->CastingAbility.IsEmpty() && Monster->CastStartedAt != SeenCastStartedAt)
    {
        SeenCastStartedAt = Monster->CastStartedAt;
        const FName Ability = Monster->NPCState && !Monster->NPCState->CastAbilityId.IsNone() ? Monster->NPCState->CastAbilityId : FName(*Monster->CastingAbility);
        StartAction(ClipForAbility(Ability), Monster->CastStartedAt, FMath::Max(.05f, Monster->CastEndsAt - Monster->CastStartedAt), 1.f, 1.f, true);
        CireRaces::OnCastPresented(Monster, Ability); // monster-races: the ability's audio cue
    }
    if (Current.bCast && !Current.bInterrupted && Monster->CastingAbility.IsEmpty() && Now < Current.StartedAt + Current.Windup - .1)
    {
        Current.bInterrupted = true; Current.InterruptedAt = Now; // kicked or stunned: let go of the pose
    }
    if (LastHealth >= 0.f && Monster->Health < LastHealth - .5f && Monster->Health > 0.f && !Current.Sequence && Now - LastHitAt > 1.1)
    {
        LastHitAt = Now;
        if (UAnimSequence* Hit = RoleClip(TEXT("hit")))
        {
            const CireMonsterArt::FClipWindow W = CireMonsterArt::Window(Hit);
            StartAction(Hit, Now, FMath::Max(.05f, (W.Contact - W.Start) / 1.4f), .75f, 0.f, false);
        }
    }
    LastHealth = Monster->Health;

    // ---- action layer ----
    Anim->Action.Weight = 0.f;
    if (Current.Sequence)
    {
        const CireMonsterArt::FClipWindow& W = Current.Window;
        const double Elapsed = Now - Current.StartedAt;
        float Time;
        if (Current.Windup <= KINDA_SMALL_NUMBER)
            Time = W.Contact - .12f * W.RecoverRate + static_cast<float>(Elapsed) * W.RecoverRate;
        else if (Elapsed < Current.Windup)
        {
            // Short windups skip the slow start of the raise rather than racing through it (at most 2.6x).
            const float From = FMath::Max(W.Start, W.Contact - Current.Windup * 2.6f);
            Time = From + (W.Contact - From) * static_cast<float>(FMath::Max(0.0, Elapsed) / Current.Windup);
        }
        else
            Time = W.Contact + static_cast<float>(Elapsed - Current.Windup) * W.RecoverRate;
        const float FadeIn = Smooth01(static_cast<float>(Elapsed) / .12f);
        const float FadeOut = Smooth01((W.End - Time) / FMath::Max(.05f, .3f * W.RecoverRate));
        float Weight = Current.Weight * FMath::Min(FadeIn, FadeOut);
        if (Current.bInterrupted) Weight *= Smooth01(1.f - static_cast<float>(Now - Current.InterruptedAt) / .2f);
        if (Time >= W.End || Weight <= 0.f && Elapsed > .2) Current = FAction();
        else
        {
            Anim->Action.Sequence = Current.Sequence;
            Anim->Action.Time = FMath::Clamp(Time, 0.f, Current.Sequence->GetPlayLength());
            Anim->Action.Weight = Weight;
            // Legs keep the locomotion cycle whenever the body is travelling.
            Anim->Action.LowerBody = Current.LowerBody * (1.f - Anim->MoveAlpha);
        }
    }
    if (Anim->Action.Weight <= 0.f) Anim->Action.Sequence = nullptr;
    Anim->Hands = GripHands;
    // The off hand lets go of a two-handed weapon while an action clip drives the arms.
    Anim->Hands.TwoHandWeight = GripHands.bTwoHand || GripHands.bCarry ? 1.f - Anim->Action.Weight : 0.f;
    UpdateRim();
    for (auto& Part : BodyParts) if (Part) Part->SetOverlayMaterial(Monster->GetMesh()->GetOverlayMaterial()); // fab-integration
}

CireMonsterArt::FClipWindow UCireMonsterArt::WindowOf(const FString& RoleOrName) const
{
    const UAnimSequence* Clip = RoleClip(RoleOrName);
    if (!Clip) Clip = NamedClip(RoleOrName);
    return Clip ? CireMonsterArt::Window(Clip) : CireMonsterArt::FClipWindow();
}

bool UCireMonsterArt::PoseClip(const FString& RoleOrName, float ClipSeconds)
{
    UAnimSequence* Clip = RoleClip(RoleOrName);
    if (!Clip) Clip = NamedClip(RoleOrName);
    if (!Clip || RoleOrName == TEXT("idle") || RoleOrName == TEXT("walk") || RoleOrName == TEXT("run"))
        return PoseForTest(RoleOrName, Clip ? ClipSeconds / FMath::Max(.01f, Clip->GetPlayLength()) : 0.f);
    const CireMonsterArt::FClipWindow W = CireMonsterArt::Window(Clip);
    const bool bDeath = RoleOrName == TEXT("death");
    return PoseForTest(RoleOrName, bDeath ? ClipSeconds / FMath::Max(.01f, Clip->GetPlayLength()) : (ClipSeconds - W.Start) / FMath::Max(.01f, W.End - W.Start));
}

bool UCireMonsterArt::PoseForTest(const FString& Role, float Normalized, float MoveSpeed)
{
    auto* Monster = Cast<ACireMonster>(GetOwner());
    UCireMonsterAnimInstance* Anim = GetMonsterAnim();
    if (!Monster || !Anim || !bTripoApplied) return false;
    bFrozen = true;
    Normalized = FMath::Clamp(Normalized, 0.f, 1.f);
    Anim->MoveAlpha = 0.f; Anim->RunAlpha = 0.f; Anim->SideAlpha = 0.f;
    Anim->Action = FCireAnimLayer(); Anim->Death = FCireAnimLayer();
    if (Role == TEXT("idle")) Anim->Idle.Time = Normalized * Anim->Idle.Sequence->GetPlayLength();
    else if (Role == TEXT("walk") || Role == TEXT("run"))
    {
        FCireAnimLayer& Layer = Role == TEXT("walk") ? Anim->Walk : Anim->Run;
        if (!Layer.Sequence) return false;
        Anim->MoveAlpha = 1.f; Anim->RunAlpha = Role == TEXT("run") ? 1.f : 0.f;
        Layer.Time = Normalized * Layer.Sequence->GetPlayLength();
    }
    else
    {
        UAnimSequence* Clip = RoleClip(Role);
        if (!Clip) Clip = NamedClip(Role);
        if (!Clip) return false;
        const CireMonsterArt::FClipWindow W = CireMonsterArt::Window(Clip);
        FCireAnimLayer& Layer = Role == TEXT("death") ? Anim->Death : Anim->Action;
        Layer.Sequence = Clip; Layer.Weight = 1.f; Layer.LowerBody = 1.f;
        Layer.Time = Role == TEXT("death") ? Normalized * Clip->GetPlayLength() : FMath::Lerp(W.Start, W.End, Normalized);
    }
    (void)MoveSpeed;
    Anim->Hands = GripHands;
    Anim->Hands.TwoHandWeight = (GripHands.bTwoHand || GripHands.bCarry) && Role != TEXT("death") && Anim->Action.Weight <= 0.f ? 1.f : 0.f;
    USkeletalMeshComponent* Mesh = Monster->GetMesh();
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Mesh->TickAnimation(0.f, false);
    Mesh->RefreshBoneTransforms();
    return !Anim->bLastPoseRejected;
}

// ---- death ------------------------------------------------------------------------------------
void UCireMonsterArt::MulticastDeath_Implementation()
{
    if (GetNetMode() == NM_DedicatedServer || bDeathPresented) return;
    bDeathPresented = true;
    SpawnCorpse();
}

void UCireMonsterArt::SpawnCorpse()
{
    auto* Monster = Cast<ACireMonster>(GetOwner());
    if (!Monster || !bTripoApplied || !Monster->GetWorld()) return;
    USkeletalMeshComponent* Mesh = Monster->GetMesh();
    UAnimSequence* Fall = RoleClip(TEXT("death"));
    if (!Mesh || !Fall || !Mesh->IsVisible()) return;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags |= RF_Transient;
    auto* Corpse = Monster->GetWorld()->SpawnActor<ACireMonsterCorpse>(Mesh->GetComponentLocation(), Mesh->GetComponentRotation(), Params);
    if (!Corpse) return;
    TArray<TObjectPtr<UStaticMeshComponent>> Props;
    if (Monster->NPCState) Props = Monster->NPCState->VisualParts;
    if (!Corpse->Initialize(*Mesh, Fall, RoleClip(TEXT("idle")), Props, &GripHands, &BodyParts)) { Corpse->Destroy(); return; }
    if (bAppliedSpectral) { Corpse->HoldSeconds = .4f; Corpse->SinkSeconds = 1.6f; Corpse->SinkCm = 320.f; } // monster-expansion: a spirit sinks away
    // The live actor is destroyed this frame; hide it now so the body is never drawn twice.
    Mesh->SetVisibility(false, true);
}

ACireMonsterCorpse::ACireMonsterCorpse()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = false;
    SetCanBeDamaged(false);
    Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetGenerateOverlapEvents(false);
    Body->SetCanEverAffectNavigation(false);
    RootComponent = Body;
}

void ACireMonsterCorpse::BeginPlay() { Super::BeginPlay(); ++GCorpses; }
void ACireMonsterCorpse::EndPlay(const EEndPlayReason::Type Reason) { --GCorpses; Super::EndPlay(Reason); }

int32 ACireMonsterCorpse::LiveCount() { return GCorpses; }

bool ACireMonsterCorpse::Initialize(const USkeletalMeshComponent& Source, UAnimSequence* Fall, UAnimSequence* Idle, const TArray<TObjectPtr<UStaticMeshComponent>>& Props, const CireGrip::FHands* Hands,
    const TArray<TObjectPtr<USkeletalMeshComponent>>* Parts)
{
    if (!Source.GetSkeletalMeshAsset() || !Fall) return false;
    SetActorTransform(Source.GetComponentTransform());
    Body->SetSkeletalMesh(Source.GetSkeletalMeshAsset());
    for (int32 Slot = 0; Slot < Source.GetNumMaterials(); ++Slot) Body->SetMaterial(Slot, Source.GetMaterial(Slot));
    Body->SetAnimInstanceClass(UCireMonsterAnimInstance::StaticClass());
    auto* Anim = Cast<UCireMonsterAnimInstance>(Body->GetAnimInstance());
    if (!Anim) return false;
    Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
    if (const auto* SourceAnim = Cast<UCireMonsterAnimInstance>(Source.GetAnimInstance())) Anim->bLockRootToReference = SourceAnim->bLockRootToReference; // world-dressing
    Anim->Idle.Sequence = Idle; Anim->Idle.Weight = 1.f;
    Anim->Death.Sequence = Fall; Anim->Death.Weight = 0.f; Anim->Death.LowerBody = 1.f;
    if (Hands) { Anim->Hands = *Hands; Anim->Hands.TwoHandWeight = 0.f; }
    const CireMonsterArt::FClipWindow Window = CireMonsterArt::Window(Fall);
    FallSeconds = FMath::Max(.2f, Window.End - Window.Start);
    const auto& D = CireMonsterArt::Data();
    HoldSeconds = D.DeathHoldSeconds; SinkSeconds = D.DeathSinkSeconds; SinkCm = D.DeathSinkCm;
    for (const UStaticMeshComponent* Prop : Props)
    {
        if (!Prop || !Prop->GetStaticMesh()) continue;
        auto* Copy = NewObject<UStaticMeshComponent>(this);
        AddInstanceComponent(Copy);
        Copy->SetupAttachment(Body, Prop->GetAttachSocketName());
        Copy->SetStaticMesh(Prop->GetStaticMesh());
        Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Copy->SetRelativeTransform(Prop->GetRelativeTransform());
        Copy->RegisterComponent();
    }
    // fab-integration: the body's leader-pose parts fall with it.
    if (Parts)
        for (const USkeletalMeshComponent* Part : *Parts)
        {
            if (!Part || !Part->GetSkeletalMeshAsset()) continue;
            auto* Copy = NewObject<USkeletalMeshComponent>(this);
            AddInstanceComponent(Copy);
            const bool bAttachment = Part->ComponentHasTag(TEXT("CireAttachment")); // monster-expansion: socketed prop, own skeleton
            Copy->SetupAttachment(Body, bAttachment ? Part->GetAttachSocketName() : NAME_None);
            Copy->SetSkeletalMesh(Part->GetSkeletalMeshAsset());
            Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot) Copy->SetMaterial(Slot, Part->GetMaterial(Slot));
            if (bAttachment) Copy->SetRelativeTransform(Part->GetRelativeTransform());
            Copy->RegisterComponent();
            if (!bAttachment) Copy->SetLeaderPoseComponent(Body);
        }
    StartLocation = GetActorLocation();
    return true;
}

void ACireMonsterCorpse::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += FMath::Clamp(DeltaSeconds, 0.f, .5f);
    if (auto* Anim = Cast<UCireMonsterAnimInstance>(Body->GetAnimInstance()))
    {
        const CireMonsterArt::FClipWindow Window = CireMonsterArt::Window(Anim->Death.Sequence);
        Anim->Death.Time = FMath::Min(Window.Start + Age * Window.RecoverRate, Window.End);
        Anim->Death.Weight = Smooth01(Age / .15f);
    }
    const float SinkStart = FallSeconds + HoldSeconds;
    if (Age > SinkStart) SetActorLocation(StartLocation - FVector(0, 0, SinkCm * Smooth01((Age - SinkStart) / SinkSeconds)));
    if (Age > SinkStart + SinkSeconds) Destroy();
}

