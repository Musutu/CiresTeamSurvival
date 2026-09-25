#include "CireFabAnimation.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Engine/SkeletalMesh.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireFabAnim, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarFabAnim(TEXT("cire.FabAnim"), 1,
    TEXT("1: champions use clips retargeted from the local Fab animation packs when present (Content/Data/FabAnimations.json). 0: Tripo/prototype clips only."));

struct FFabData
{
    bool bLoaded = false;
    FString Root = TEXT("/Game/FabDerived/Anim");
    TMap<FString, CireChampionActions::FWindow> Windows;
    TMap<FString, TMap<FString, TArray<FString>>> Replace; // "style:x" / "motion:y" -> kind -> clips
    TMap<FString, FString> Bodies;                          // mesh object path -> folder (ChampionAttacks02.json)
    bool bLocomotion = true;
};
FFabData GFab;

bool ReadJson(const TCHAR* Name, TSharedPtr<FJsonObject>& Out)
{
    FString Text;
    return FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), Name)) && Text.Len() < 400000 &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}

const FFabData& Data()
{
    if (GFab.bLoaded) return GFab;
    GFab = FFabData(); GFab.bLoaded = true;
    TSharedPtr<FJsonObject> Attacks, Root;
    if (ReadJson(TEXT("ChampionAttacks02.json"), Attacks))
    {
        const TSharedPtr<FJsonObject>* Bodies = nullptr;
        if (Attacks->TryGetObjectField(TEXT("bodies"), Bodies))
            for (const auto& Pair : (*Bodies)->Values) { FString Folder; if (Pair.Value->TryGetString(Folder)) GFab.Bodies.Add(FString(Pair.Key.ToView()), Folder); }
    }
    if (!ReadJson(TEXT("FabAnimations.json"), Root)) return GFab;
    FString R; if (Root->TryGetStringField(TEXT("root"), R) && R.StartsWith(TEXT("/Game/")) && !R.Contains(TEXT(".."))) GFab.Root = R;
    Root->TryGetBoolField(TEXT("locomotion"), GFab.bLocomotion);
    const TSharedPtr<FJsonObject>* Clips = nullptr;
    if (Root->TryGetObjectField(TEXT("clips"), Clips))
        for (const auto& Pair : (*Clips)->Values)
        {
            const TSharedPtr<FJsonObject>* W = nullptr; CireChampionActions::FWindow Window; double V = 0;
            if (!Pair.Value->TryGetObject(W)) continue;
            if ((*W)->TryGetNumberField(TEXT("start"), V)) Window.Start = V;
            if ((*W)->TryGetNumberField(TEXT("contact"), V)) Window.Contact = V;
            if ((*W)->TryGetNumberField(TEXT("end"), V)) Window.End = V;
            if ((*W)->TryGetNumberField(TEXT("recoverRate"), V)) Window.RecoverRate = FMath::Clamp(V, .2, 5.);
            if (Window.Start <= Window.Contact && Window.Contact < Window.End) GFab.Windows.Add(FString(Pair.Key.ToView()), Window);
        }
    const TSharedPtr<FJsonObject>* Replace = nullptr;
    if (Root->TryGetObjectField(TEXT("replace"), Replace))
        for (const auto& Row : (*Replace)->Values)
        {
            const TSharedPtr<FJsonObject>* Kinds = nullptr;
            if (!Row.Value->TryGetObject(Kinds)) continue;
            auto& Entry = GFab.Replace.Add(FString(Row.Key.ToView()));
            for (const auto& Kind : (*Kinds)->Values)
            {
                TArray<FString>& List = Entry.Add(FString(Kind.Key.ToView()));
                if (Kind.Value->Type == EJson::String) List.Add(Kind.Value->AsString());
                else if (Kind.Value->Type == EJson::Array) for (const auto& V : Kind.Value->AsArray()) List.Add(V->AsString());
            }
        }
    UE_LOG(LogCireFabAnim, Log, TEXT("CIRE_FAB_ANIM_DATA clips=%d rows=%d root=%s"), GFab.Windows.Num(), GFab.Replace.Num(), *GFab.Root);
    return GFab;
}

template<typename T> T* LoadIfPresent(const FString& Path)
{
    const FString Package = FPackageName::ObjectPathToPackageName(Path);
    if (!FPackageName::IsValidLongPackageName(Package) || !FPackageName::DoesPackageExist(Package)) return nullptr;
    return LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}
}

bool CireFabAnimation::Enabled()
{
    static const bool bOff = (FParse::Param(FCommandLine::Get(), TEXT("CireNoFabAnim")) || FParse::Param(FCommandLine::Get(), TEXT("CireNoFab")));
    return !bOff && CVarFabAnim.GetValueOnAnyThread() != 0;
}

void CireFabAnimation::Reload() { GFab.bLoaded = false; Data(); }

FString CireFabAnimation::FolderFor(const USkeletalMesh* Body)
{
    const FString* Folder = Body ? Data().Bodies.Find(Body->GetPathName()) : nullptr;
    return Folder ? *Folder : FString();
}

UAnimSequence* CireFabAnimation::Find(const USkeletalMesh* Body, const FString& Folder, const FString& Clip)
{
    if (!Body || Folder.IsEmpty() || Clip.IsEmpty() || !Enabled()) return nullptr;
    const FString Name = FString::Printf(TEXT("A_%s_%s"), *Folder, *Clip);
    UAnimSequence* Sequence = LoadIfPresent<UAnimSequence>(FString::Printf(TEXT("%s/%s/%s.%s"), *Data().Root, *Folder, *Name, *Name));
    return Sequence && Sequence->GetSkeleton() == Body->GetSkeleton() ? Sequence : nullptr;
}

bool CireFabAnimation::Window(const FString& Clip, CireChampionActions::FWindow& Out)
{
    const auto* W = Data().Windows.Find(Clip);
    if (!W) return false;
    Out = *W; return true;
}

bool CireFabAnimation::Pick(const USkeletalMesh* Body, const FString& Folder, const FString& Style, const FString& Motion,
    const FString& Kind, uint32 Seed, UAnimSequence*& OutClip, FString& OutName)
{
    OutClip = nullptr;
    if (!Enabled()) return false;
    const FFabData& D = Data();
    for (const FString& Key : {TEXT("style:") + Style, TEXT("motion:") + Motion, FString(TEXT("default"))})
    {
        const auto* Row = D.Replace.Find(Key);
        const TArray<FString>* List = Row ? Row->Find(Kind) : nullptr;
        if (!List || List->IsEmpty()) continue;
        // Rotate through the alternatives (combo strings); skip ones this body failed to retarget.
        for (int32 I = 0; I < List->Num(); ++I)
        {
            const FString& Name = (*List)[(Seed + I) % List->Num()];
            if (!D.Windows.Contains(Name)) continue;
            if (UAnimSequence* Clip = Find(Body, Folder, Name)) { OutClip = Clip; OutName = Name; return true; }
        }
    }
    return false;
}

UBlendSpace* CireFabAnimation::Locomotion(const USkeletalMesh* Body, const FString& Folder)
{
    if (!Body || Folder.IsEmpty() || !Enabled() || !Data().bLocomotion) return nullptr;
    const FString Name = TEXT("BS_Fab_Locomotion_") + Folder;
    UBlendSpace* Blend = LoadIfPresent<UBlendSpace>(FString::Printf(TEXT("%s/%s/%s.%s"), *Data().Root, *Folder, *Name, *Name));
    return Blend && Blend->GetSkeleton() == Body->GetSkeleton() ? Blend : nullptr;
}

CireFabAnimation::FCoverage CireFabAnimation::Coverage()
{
    FCoverage C; const FFabData& D = Data();
    for (const auto& Pair : D.Bodies)
    {
        ++C.Bodies;
        USkeletalMesh* Body = LoadIfPresent<USkeletalMesh>(Pair.Key);
        if (!Body) continue;
        int32 Here = 0;
        for (const auto& W : D.Windows) if (Find(Body, Pair.Value, W.Key)) ++Here;
        C.Clips += Here; C.BodiesWithClips += Here > 0;
        C.Locomotion += Locomotion(Body, Pair.Value) != nullptr;
    }
    return C;
}

#if !UE_BUILD_SHIPPING
bool CireFabAnimation::RunTests()
{
    bool bOk = true; int32 Checks = 0;
    auto Check = [&](bool b, const FString& What) { ++Checks; if (!b) { bOk = false; UE_LOG(LogCireFabAnim, Error, TEXT("CIRE_FAB_ANIM_TEST_FAIL %s"), *What); } };
    Reload();
    const FFabData& D = Data();
    Check(D.Bodies.Num() > 0, TEXT("champion body folders load"));
    // Every replacement names a clip with timing, so a present clip can never play with a default window.
    for (const auto& Row : D.Replace)
        for (const auto& Kind : Row.Value)
            for (const FString& Name : Kind.Value)
                Check(D.Windows.Contains(Name), FString::Printf(TEXT("%s.%s -> %s has a window"), *Row.Key, *Kind.Key, *Name));
    // Missing content is quiet and null (clean clone).
    Check(Find(nullptr, TEXT("Warden"), TEXT("slash")) == nullptr, TEXT("null body"));
    for (const auto& Pair : D.Bodies)
    {
        USkeletalMesh* Body = LoadIfPresent<USkeletalMesh>(Pair.Key);
        if (!Body) continue;
        Check(Find(Body, Pair.Value, TEXT("__no_such_clip__")) == nullptr, TEXT("absent clip is null"));
        for (const auto& W : D.Windows)
            if (UAnimSequence* Clip = Find(Body, Pair.Value, W.Key))
            {
                Check(W.Value.End <= Clip->GetPlayLength() + .05f, FString::Printf(TEXT("%s %s window inside %.2fs"), *Pair.Value, *W.Key, Clip->GetPlayLength()));
                Check(!Clip->bEnableRootMotion, FString::Printf(TEXT("%s %s has no root motion"), *Pair.Value, *W.Key));
            }
        if (UBlendSpace* Blend = Locomotion(Body, Pair.Value))
            Check(Blend->GetBlendParameter(0).Min <= -179.f && Blend->GetBlendParameter(0).Max >= 179.f && Blend->GetBlendParameter(1).Max >= 300.f,
                TEXT("Fab locomotion axes match Direction/Speed: ") + Pair.Value);
    }
    const FCoverage C = Coverage();
    UE_LOG(LogCireFabAnim, Display, TEXT("CIRE_FAB_ANIM coverage bodies=%d withClips=%d clips=%d locomotion=%d"), C.Bodies, C.BodiesWithClips, C.Clips, C.Locomotion);
    UE_LOG(LogCireFabAnim, Display, TEXT("%s checks=%d"), bOk ? TEXT("CIRE_FAB_ANIM_TESTS_PASS") : TEXT("CIRE_FAB_ANIM_TESTS_FAIL"), Checks);
    return bOk;
}
#endif
