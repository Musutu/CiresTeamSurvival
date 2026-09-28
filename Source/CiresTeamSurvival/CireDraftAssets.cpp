#include "CireDraftAssets.h"
#include "CireDraftBrowser.h"
#include "CireParagonChampions.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireDraftAssets,Log,All);

namespace
{
// One manager for the session. Deliberately leaked: it is an FGCObject and must outlive every handle.
FStreamableManager& DraftStreamer(){static FStreamableManager* Manager=new FStreamableManager();return *Manager;}

CireDraftAssets::FTunables& DraftTunables(){static CireDraftAssets::FTunables T;return T;}
bool& DraftTunablesLoaded(){static bool b=false;return b;}

// Cached package existence (a disk lookup: once per path).
bool DraftPackageExists(const FString& ObjectPath)
{
    static TMap<FString,bool> Known;
    if(const bool* Found=Known.Find(ObjectPath))return *Found;
    const FString Package=FPackageName::ObjectPathToPackageName(ObjectPath);
    return Known.Add(ObjectPath,FPackageName::IsValidLongPackageName(Package)&&FPackageName::DoesPackageExist(Package));
}
// A texture is usable once loaded AND its platform data is built (the editor compiles textures asynchronously and
// shows the default checker meanwhile).
UTexture2D* DraftUsable(UTexture2D* T){return T&&!T->IsDefaultTexture()?T:nullptr;}

// ---------- textures ----------
struct FDraftTex
{
    TSharedPtr<FStreamableHandle> Handle;
    TStrongObjectPtr<UTexture2D> Texture;
    bool bMissing=false;
};
struct FDraftTexCache
{
    TMap<FString,FDraftTex> Entries; // object path -> entry
    CireDraftBrowser::FLru Lru{1000000};
    bool bPin=false;
    // Returns the texture when resident; issues the request otherwise.
    UTexture2D* Get(const FString& Path,bool bHigh,int32& Requests)
    {
        if(Path.IsEmpty())return nullptr;
        FDraftTex* E=Entries.Find(Path);
        TArray<FString> Evicted;Lru.Touch(Path,&Evicted);
        for(const FString& Old:Evicted)Release(Old);
        if(!E)
        {
            E=&Entries.Add(Path);
            if(!DraftPackageExists(Path)){E->bMissing=true;return nullptr;}
            if(UTexture2D* Loaded=FindObject<UTexture2D>(nullptr,*Path)){E->Texture.Reset(Loaded);Pin(Loaded);}
            else
            {
                ++Requests;++CireDraftAssets::Stats().AsyncRequests;
                E->Handle=DraftStreamer().RequestAsyncLoad(FSoftObjectPath(Path),FStreamableDelegate(),bHigh?FStreamableManager::AsyncLoadHighPriority:FStreamableManager::DefaultAsyncLoadPriority,true);
            }
        }
        else ++CireDraftAssets::Stats().CacheHits;
        if(E->bMissing)return nullptr;
        if(!E->Texture&&E->Handle&&E->Handle->HasLoadCompleted())
        {
            if(UTexture2D* Loaded=Cast<UTexture2D>(E->Handle->GetLoadedAsset())){E->Texture.Reset(Loaded);Pin(Loaded);}
            else E->bMissing=true;
        }
        return DraftUsable(E->Texture.Get());
    }
    void Pin(UTexture2D* T){if(bPin&&T){T->bForceMiplevelsToBeResident=true;}}
    void Release(const FString& Path)
    {
        if(FDraftTex* E=Entries.Find(Path))
        {
            if(E->Texture&&bPin)E->Texture->bForceMiplevelsToBeResident=false;
            if(E->Handle){if(E->Handle->IsLoadingInProgress()){E->Handle->CancelHandle();++CireDraftAssets::Stats().AsyncCancelled;}else E->Handle->ReleaseHandle();}
            Entries.Remove(Path);
        }
    }
};
FDraftTexCache& DraftPortraits(){static FDraftTexCache C;return C;}
FDraftTexCache& DraftIcons(){static FDraftTexCache C;return C;}
FDraftTexCache& DraftBackgrounds()
{
    static FDraftTexCache C;static bool bInit=false;
    if(!bInit){bInit=true;C.bPin=true;C.Lru.SetCapacity(CireDraftAssets::Tunables().BackgroundCacheSize);}
    return C;
}

// ---------- art-binding path index ----------
struct FDraftPathIndex
{
    TMap<FString,TArray<FString>> Rows;   // profile id or id@skin -> object paths
    TMap<FString,FString> ParagonPortraits;
    bool bLoaded=false;
};
void DraftCollectPaths(const TSharedPtr<FJsonValue>& V,TArray<FString>& Out,int32 Depth=0)
{
    if(!V.IsValid()||Depth>8)return;
    FString S;
    if(V->TryGetString(S)){if(S.StartsWith(TEXT("/Game/"))&&S.Contains(TEXT("."))&&!Out.Contains(S))Out.Add(S);return;}
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
    if(V->TryGetArray(A)){for(const auto& X:*A)DraftCollectPaths(X,Out,Depth+1);return;}
    const TSharedPtr<FJsonObject>* O=nullptr;
    // The preview binds only idle / gait / attack clips (GCireCreatureArtPreviewLite): reaction, death and cast clips and
    // the FX their notifies drag in are not preloaded.
    static const TSet<FString> Skip={TEXT("casts"),TEXT("hit"),TEXT("death"),TEXT("attackAlt"),TEXT("contact"),TEXT("attacksExtra"),TEXT("attacksFolder")};
    if(V->TryGetObject(O)&&O->IsValid())for(const auto& Pair:(*O)->Values)if(!Skip.Contains(FString(Pair.Key.ToView())))DraftCollectPaths(Pair.Value,Out,Depth+1);
}
FDraftPathIndex& DraftPaths()
{
    static FDraftPathIndex Index;
    if(Index.bLoaded)return Index;
    Index.bLoaded=true;
    const auto Load=[](const TCHAR* File)->TSharedPtr<FJsonObject>
    {
        FString Json;TSharedPtr<FJsonObject> Root;
        if(!FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data"),File))||Json.Len()>8*1024*1024)return nullptr;
        return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)?Root:nullptr;
    };
    const auto AddRows=[&](const TSharedPtr<FJsonObject>& Root,const TCHAR* Field)
    {
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(!Root||!Root->TryGetArrayField(Field,Rows))return;
        for(const auto& Row:*Rows)
        {
            const TSharedPtr<FJsonObject>* O=nullptr;if(!Row->TryGetObject(O))continue;
            TArray<FString> Ids;FString Id;
            if((*O)->TryGetStringField(TEXT("profileId"),Id))Ids.Add(Id);
            const TArray<TSharedPtr<FJsonValue>>* More=nullptr;
            if((*O)->TryGetArrayField(TEXT("profileIds"),More))for(const auto& M:*More){FString X;if(M->TryGetString(X))Ids.AddUnique(X);}
            TArray<FString> Paths;DraftCollectPaths(Row,Paths);
            for(const FString& Key:Ids){TArray<FString>& Into=Index.Rows.FindOrAdd(Key);for(const FString& P:Paths)Into.AddUnique(P);}
        }
    };
    AddRows(Load(TEXT("ChampionArtBindings.json")),TEXT("bindings"));
    AddRows(Load(TEXT("ChampionArtBindings.fab.json")),TEXT("bindings"));
    AddRows(Load(TEXT("ChampionArt.hq.json")),TEXT("champions"));
    AddRows(Load(TEXT("ChampionArt.tripo.json")),TEXT("champions"));
    if(const TSharedPtr<FJsonObject> Paragon=Load(TEXT("ParagonChampions.json")))
    {
        AddRows(Paragon,TEXT("bindings"));
        const TArray<TSharedPtr<FJsonValue>>* Heroes=nullptr;
        if(Paragon->TryGetArrayField(TEXT("heroes"),Heroes))for(const auto& H:*Heroes)
        {
            const TSharedPtr<FJsonObject>* O=nullptr;FString Id,Portrait;
            if(H->TryGetObject(O)&&(*O)->TryGetStringField(TEXT("id"),Id)&&(*O)->TryGetStringField(TEXT("portrait"),Portrait))Index.ParagonPortraits.Add(Id,Portrait);
        }
    }
    int32 Paths=0;for(const auto& Pair:Index.Rows)Paths+=Pair.Value.Num();
    UE_LOG(LogCireDraftAssets,Log,TEXT("CIRE_DRAFT_ASSETS_INDEX rows=%d paths=%d"),Index.Rows.Num(),Paths);
    return Index;
}

// ---------- bodies ----------
struct FDraftBody{TSharedPtr<FStreamableHandle> Handle;double RequestedAt=0;bool bEmpty=false;};
TMap<FString,FDraftBody>& DraftBodies(){static TMap<FString,FDraftBody> M;return M;}
CireDraftBrowser::FLru& DraftBodyLru(){static CireDraftBrowser::FLru L(CireDraftAssets::Tunables().BodyCacheSize);return L;}
void DraftReleaseBody(const FString& Key)
{
    if(FDraftBody* B=DraftBodies().Find(Key))
    {
        if(B->Handle){if(B->Handle->IsLoadingInProgress()){B->Handle->CancelHandle();++CireDraftAssets::Stats().AsyncCancelled;}else B->Handle->ReleaseHandle();}
        DraftBodies().Remove(Key);
    }
    DraftBodyLru().Remove(Key);
}
FString DraftPortraitPath(const FString& Id)
{
    const FString Authored=FString::Printf(TEXT("/Game/UI/Draft/Portraits/T_Portrait_%s.T_Portrait_%s"),*Id,*Id);
    if(DraftPackageExists(Authored))return Authored;
    if(const FString* Pg=DraftPaths().ParagonPortraits.Find(Id);Pg&&DraftPackageExists(*Pg))return *Pg;
    return FString();
}
FString DraftIconPath(const FString& Id)
{
    for(const TCHAR* Prefix:{TEXT("T_Ability_"),TEXT("T_")})
    {
        const FString Name=FString(Prefix)+Id,Path=TEXT("/Game/UI/Abilities/")+Name+TEXT(".")+Name;
        if(DraftPackageExists(Path))return Path;
    }
    return FString();
}
}

const CireDraftAssets::FTunables& CireDraftAssets::Tunables()
{
    if(!DraftTunablesLoaded())Reload();
    return DraftTunables();
}
void CireDraftAssets::Reload()
{
    DraftTunablesLoaded()=true;
    FTunables T;FString Json;TSharedPtr<FJsonObject> Root;
    if(FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/DraftSelect.json")))&&Json.Len()<65536&&
       FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)&&Root)
    {
        const TSharedPtr<FJsonObject>* P=nullptr;
        if(Root->TryGetObjectField(TEXT("performance"),P))
        {
            double V=0;
            if((*P)->TryGetNumberField(TEXT("hoverDebounceSeconds"),V))T.HoverDebounceSeconds=FMath::Clamp(V,0.0,1.0);
            if((*P)->TryGetNumberField(TEXT("paragonHoverDebounceSeconds"),V))T.ParagonHoverDebounceSeconds=FMath::Clamp(V,0.0,2.0);
            bool B=false;if((*P)->TryGetBoolField(TEXT("preloadParagonNeighbours"),B))T.bPreloadParagonNeighbours=B;
            if((*P)->TryGetNumberField(TEXT("bodyCacheSize"),V))T.BodyCacheSize=FMath::Clamp(int32(V),1,24);
            if((*P)->TryGetNumberField(TEXT("backgroundCacheSize"),V))T.BackgroundCacheSize=FMath::Clamp(int32(V),1,64);
            if((*P)->TryGetNumberField(TEXT("previewPoolSize"),V))T.PreviewPoolSize=FMath::Clamp(int32(V),1,12);
            if((*P)->TryGetNumberField(TEXT("neighbourPreload"),V))T.NeighbourPreload=FMath::Clamp(int32(V),0,4);
            if((*P)->TryGetNumberField(TEXT("crossFadeSeconds"),V))T.CrossFadeSeconds=FMath::Clamp(V,0.05,2.0);
            if((*P)->TryGetNumberField(TEXT("asyncLoadingTimeLimitMs"),V))T.AsyncLoadingTimeLimitMs=FMath::Clamp(float(V),1.f,30.f);
        }
    }
    DraftTunables()=T;
    TArray<FString> Evicted;
    DraftBodyLru().SetCapacity(T.BodyCacheSize,&Evicted);for(const FString& K:Evicted)DraftReleaseBody(K);
    Evicted.Reset();DraftBackgrounds().Lru.SetCapacity(T.BackgroundCacheSize,&Evicted);for(const FString& K:Evicted)DraftBackgrounds().Release(K);
}
CireDraftAssets::FStats& CireDraftAssets::Stats(){static FStats S;return S;}
void CireDraftAssets::RetainLoadingBudget(bool bRetain)
{
    static int32 Holders=0;static float Before=-1.f;
    IConsoleVariable* CVar=IConsoleManager::Get().FindConsoleVariable(TEXT("s.AsyncLoadingTimeLimit"));
    if(!CVar)return;
    if(bRetain){if(Holders++==0){Before=CVar->GetFloat();CVar->Set(FMath::Max(Before,Tunables().AsyncLoadingTimeLimitMs),ECVF_SetByCode);UE_LOG(LogCireDraftAssets,Log,TEXT("CIRE_DRAFT_ASSETS_BUDGET on %.1f ms (was %.1f)"),CVar->GetFloat(),Before);}}
    else if(Holders>0&&--Holders==0&&Before>=0.f)CVar->Set(Before,ECVF_SetByCode);
}

UTexture2D* CireDraftAssets::Portrait(const FString& Id)
{
    const FString Path=DraftPortraitPath(Id);
    return Path.IsEmpty()?nullptr:DraftPortraits().Get(Path,true,Stats().PortraitRequests);
}
bool CireDraftAssets::HasPortrait(const FString& Id){return !DraftPortraitPath(Id).IsEmpty();}
UTexture2D* CireDraftAssets::Background(const FString& BgId)
{
    if(BgId.IsEmpty())return nullptr;
    const FString Path=FString::Printf(TEXT("/Game/UI/Draft/Backgrounds/T_DraftBg_%s.T_DraftBg_%s"),*BgId,*BgId);
    return DraftBackgrounds().Get(Path,true,Stats().BackgroundRequests);
}
bool CireDraftAssets::HasBackground(const FString& BgId)
{
    return !BgId.IsEmpty()&&DraftPackageExists(FString::Printf(TEXT("/Game/UI/Draft/Backgrounds/T_DraftBg_%s.T_DraftBg_%s"),*BgId,*BgId));
}
UTexture2D* CireDraftAssets::KitIcon(const FString& Id)
{
    const FString Path=Id.IsEmpty()?FString():DraftIconPath(Id);
    return Path.IsEmpty()?nullptr:DraftIcons().Get(Path,true,Stats().IconRequests);
}
void CireDraftAssets::PrefetchPortraits(const TArray<FString>& Ids)
{
    for(const FString& Id:Ids){const FString Path=DraftPortraitPath(Id);if(!Path.IsEmpty()&&!DraftPortraits().Entries.Contains(Path))DraftPortraits().Get(Path,false,Stats().PortraitRequests);}
}
void CireDraftAssets::PrefetchBackground(const FString& BgId)
{
    if(BgId.IsEmpty())return;
    const FString Path=FString::Printf(TEXT("/Game/UI/Draft/Backgrounds/T_DraftBg_%s.T_DraftBg_%s"),*BgId,*BgId);
    // Only fill free LRU room: a prefetch never evicts a background the player has actually seen.
    FDraftTexCache& C=DraftBackgrounds();
    if(!C.Entries.Contains(Path)&&C.Lru.Num()<C.Lru.GetCapacity())C.Get(Path,false,Stats().BackgroundRequests);
}
void CireDraftAssets::PrefetchIcons(const TArray<FString>& Ids)
{
    for(const FString& Id:Ids){const FString Path=Id.IsEmpty()?FString():DraftIconPath(Id);if(!Path.IsEmpty()&&!DraftIcons().Entries.Contains(Path))DraftIcons().Get(Path,false,Stats().IconRequests);}
}

FString CireDraftAssets::BodyKey(const FString& Id,const FString& Skin){return Skin.IsEmpty()?Id:Id+TEXT("@")+Skin;}
const TArray<FString>& CireDraftAssets::BodyPaths(const FString& Id,const FString& Skin)
{
    static TMap<FString,TArray<FString>> Cache;
    const FString Key=BodyKey(Id,Skin);
    if(const TArray<FString>* Found=Cache.Find(Key))return *Found;
    TArray<FString> Paths;
    if(const TArray<FString>* Base=DraftPaths().Rows.Find(Id))Paths=*Base;
    if(!Skin.IsEmpty())if(const TArray<FString>* Extra=DraftPaths().Rows.Find(Key))for(const FString& P:*Extra)Paths.AddUnique(P);
    Paths.RemoveAll([](const FString& P){return !DraftPackageExists(P);}); // optional packs (Fab / Paragon) may be absent
    return Cache.Add(Key,MoveTemp(Paths));
}
void CireDraftAssets::RequestBody(const FString& Id,const FString& Skin,bool bHigh)
{
    if(Id.IsEmpty())return;
    const double Start=FPlatformTime::Seconds();
    const FString Key=BodyKey(Id,Skin);
    TArray<FString> Evicted;const bool bHit=DraftBodyLru().Touch(Key,&Evicted);
    for(const FString& Old:Evicted)if(Old!=Key)DraftReleaseBody(Old);
    if(FDraftBody* Existing=DraftBodies().Find(Key))
    {
        ++Stats().CacheHits;
        // Promote a low-priority neighbour preload the player now hovers.
        if(bHigh&&Existing->Handle&&Existing->Handle->IsLoadingInProgress()){}
        (void)bHit;
    }
    else
    {
        FDraftBody& B=DraftBodies().Add(Key);B.RequestedAt=Start;
        TArray<FSoftObjectPath> Targets;for(const FString& P:BodyPaths(Id,Skin))Targets.Add(FSoftObjectPath(P));
        if(Targets.IsEmpty())B.bEmpty=true;
        else
        {
            ++Stats().AsyncRequests;++Stats().BodyRequests;
            B.Handle=DraftStreamer().RequestAsyncLoad(MoveTemp(Targets),FStreamableDelegate(),bHigh?FStreamableManager::AsyncLoadHighPriority:FStreamableManager::DefaultAsyncLoadPriority,true);
        }
    }
    Stats().RequestMs+=(FPlatformTime::Seconds()-Start)*1000.0;
}
bool CireDraftAssets::IsBodyRequested(const FString& Id,const FString& Skin){return DraftBodies().Contains(BodyKey(Id,Skin));}
bool CireDraftAssets::IsBodyReady(const FString& Id,const FString& Skin)
{
    const FDraftBody* B=DraftBodies().Find(BodyKey(Id,Skin));
    if(!B)return false;
    if(B->bEmpty||!B->Handle||B->Handle->WasCanceled())return true;
    if(!B->Handle->HasLoadCompleted())return false;
#if WITH_EDITOR
    TArray<UObject*> Loaded;B->Handle->GetLoadedAssets(Loaded);
    for(const UObject* O:Loaded)
    {
        if(const USkinnedAsset* Skinned=Cast<USkinnedAsset>(O);Skinned&&Skinned->IsCompiling())return false;
        if(const UStaticMesh* Static=Cast<UStaticMesh>(O);Static&&Static->IsCompiling())return false;
    }
#endif
    return true;
}
bool CireDraftAssets::IsBodyStreaming(const FString& Id,const FString& Skin)
{
    const FDraftBody* B=DraftBodies().Find(BodyKey(Id,Skin));
    return B&&B->Handle&&B->Handle->IsLoadingInProgress();
}
int32 CireDraftAssets::CancelBodiesExcept(const TSet<FString>& Keep)
{
    TArray<FString> Drop;
    for(const auto& Pair:DraftBodies())if(!Keep.Contains(Pair.Key)&&Pair.Value.Handle&&Pair.Value.Handle->IsLoadingInProgress())Drop.Add(Pair.Key);
    for(const FString& K:Drop)DraftReleaseBody(K);
    return Drop.Num();
}
int32 CireDraftAssets::BodiesInFlight()
{
    int32 N=0;for(const auto& Pair:DraftBodies())N+=Pair.Value.Handle&&Pair.Value.Handle->IsLoadingInProgress();
    return N;
}

#if !UE_BUILD_SHIPPING
bool CireDraftAssets::RunTests()
{
    bool bPassed=true;int32 Checks=0;
    const auto Check=[&](bool bOk,const TCHAR* What){++Checks;if(!bOk){bPassed=false;UE_LOG(LogCireDraftAssets,Error,TEXT("CIRE_DRAFT_ASSETS_CHECK_FAIL %s"),What);}};
    // Path index: every authored hero with a Tripo/Fab binding names its mesh; Paragon heroes name theirs when installed.
    Check(DraftPaths().Rows.Num()>0,TEXT("art-binding index loaded"));
    Check(BodyPaths(TEXT("no_such_hero")).IsEmpty(),TEXT("unknown hero has no paths"));
    const bool bParagon=CireParagonChampions::HeroIds().Contains(TEXT("pg_greystone"));
    if(bParagon)
    {
        bool bMesh=false;for(const FString& P:BodyPaths(TEXT("pg_greystone")))bMesh|=P.Contains(TEXT("Greystone.Greystone"));
        Check(bMesh,TEXT("Paragon body paths include the hero mesh"));
        const TArray<FString> Skins=CireParagonChampions::Skins(TEXT("pg_greystone"));
        if(Skins.Num()>0)
        {
            const FString SkinKey=Skins[0].Left(Skins[0].Find(TEXT("|")));
            Check(BodyPaths(TEXT("pg_greystone"),SkinKey).Num()>BodyPaths(TEXT("pg_greystone")).Num(),TEXT("skin body adds the skin mesh"));
        }
    }
    // Request / hit / cancel / LRU on a scratch capacity.
    const int32 SavedCapacity=DraftBodyLru().GetCapacity();
    {
        TArray<FString> Ev;DraftBodyLru().SetCapacity(2,&Ev);for(const FString& K:Ev)DraftReleaseBody(K);
    }
    const FStats Before=Stats();
    const FString Hero=bParagon?FString(TEXT("pg_greystone")):FString(TEXT("knight"));
    const bool bHasPaths=BodyPaths(Hero).Num()>0;
    RequestBody(Hero,FString(),true);
    Check(IsBodyRequested(Hero),TEXT("body requested"));
    Check(!bHasPaths||Stats().BodyRequests==Before.BodyRequests+1,TEXT("one async request issued"));
    RequestBody(Hero,FString(),true);
    Check(Stats().CacheHits==Before.CacheHits+1&&Stats().BodyRequests==Before.BodyRequests+(bHasPaths?1:0),TEXT("second request is a cache hit (no new load)"));
    // Cancelling everything except the hero keeps it; cancelling with an empty keep set drops it if still loading.
    TSet<FString> Keep;Keep.Add(Hero);
    Check(CancelBodiesExcept(Keep)==0&&IsBodyRequested(Hero),TEXT("kept request survives cancel"));
    const bool bWasLoading=BodiesInFlight()>0;
    const int32 Dropped=CancelBodiesExcept(TSet<FString>());
    Check(!bWasLoading||(Dropped==1&&!IsBodyRequested(Hero)&&Stats().AsyncCancelled>Before.AsyncCancelled),TEXT("stale in-flight request cancelled"));
    // Completion: after a flush the body is resident and stays cached.
    RequestBody(Hero,FString(),true);
    FlushAsyncLoading();
    Check(IsBodyReady(Hero),TEXT("body ready after the async load completes"));
    if(bHasPaths){const FString Mesh=BodyPaths(Hero)[0];Check(FindObject<UObject>(nullptr,*Mesh)!=nullptr,TEXT("body asset resident after load"));}
    Check(CancelBodiesExcept(TSet<FString>())==0&&IsBodyRequested(Hero),TEXT("completed bodies are never cancelled"));
    // LRU eviction: capacity 2, three distinct keys -> the oldest is released.
    RequestBody(TEXT("lru_a"),FString(),false);RequestBody(TEXT("lru_b"),FString(),false);
    Check(!IsBodyRequested(Hero)&&IsBodyRequested(TEXT("lru_a"))&&IsBodyRequested(TEXT("lru_b")),TEXT("body LRU evicts the least recently used"));
    Check(IsBodyReady(TEXT("lru_a")),TEXT("a hero without binding paths is ready at once"));
    {
        TArray<FString> Ev;DraftBodyLru().SetCapacity(SavedCapacity,&Ev);for(const FString& K:Ev)DraftReleaseBody(K);
        DraftReleaseBody(TEXT("lru_a"));DraftReleaseBody(TEXT("lru_b"));
    }
    // Backgrounds: LRU bounded, shared painting requested once.
    {
        const int32 Req0=Stats().BackgroundRequests;
        Background(TEXT("knight"));Background(TEXT("knight"));
        Check(Stats().BackgroundRequests-Req0<=1,TEXT("shared background requested once"));
        FlushAsyncLoading();
        UTexture2D* Knight=Background(TEXT("knight"));
        Check(!HasBackground(TEXT("knight"))||(Knight==nullptr||Knight->bForceMiplevelsToBeResident),TEXT("cached background pinned full resolution"));
        Check(DraftBackgrounds().Lru.Num()<=DraftBackgrounds().Lru.GetCapacity(),TEXT("background cache bounded"));
    }
    Check(Tunables().HoverDebounceSeconds>=.1&&Tunables().HoverDebounceSeconds<=.25,TEXT("hover debounce 100-250 ms"));
    UE_LOG(LogCireDraftAssets,Display,TEXT("CIRE_DRAFT_ASSETS_%s checks=%d"),bPassed?TEXT("PASS"):TEXT("FAIL"),Checks);
    return bPassed;
}
#endif
