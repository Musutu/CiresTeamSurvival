// town-perf: CireNavCache.h
#include "CireNavCache.h"
#include "CireTownMap.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/RecastNavMeshDataChunk.h"
#include "NavMesh/RecastVersion.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireNavCache, Log, All);

namespace
{
constexpr uint32 Magic = 0x43524E43; // "CRNC"
constexpr int32 FormatVersion = 1;
constexpr int32 NavRulesVersion = 1;
bool bLocked = false;

// Two engine helpers the cache needs are protected. Naming them through a derived type yields ordinary member pointers
// (never instantiated; no engine change).
struct FNavSysAccess : public UNavigationSystemV1
{
    static void ResetDirtyAreas(UNavigationSystemV1* NS) { (NS->*(&FNavSysAccess::ResetDefaultDirtyAreasController))(); }
};
struct FRecastAccess : public ARecastNavMesh
{
    static FPImplRecastNavMesh* Impl(ARecastNavMesh* R)
    {
        FPImplRecastNavMesh* (ARecastNavMesh::*Get)() = &FRecastAccess::GetRecastNavMeshImpl;
        return (R->*Get)();
    }
};

FString CacheKey()
{
    // Everything that shapes the town navmesh. Any difference is a miss (a rebuild), never a stale mesh.
    // NavRulesVersion: bump when code that shapes the town navmesh changes (which pack pieces affect navigation, the
    // bounds volumes, the landscape load order).
    FString Text = FString::Printf(TEXT("fmt=%d rules=%d navver=%d\n"), FormatVersion, NavRulesVersion, NAVMESHVER_LATEST);
    for (const TCHAR* File : {TEXT("Data/CastleTown.json"), TEXT("Data/CastleTownRoutes.json"), TEXT("Data/TownVendors.json")})
    {
        FString Body; FFileHelper::LoadFileToString(Body, *(FPaths::ProjectContentDir() / File)); Text += Body;
    }
    { FString Ini; FFileHelper::LoadFileToString(Ini, *(FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini"))); Text += Ini; }
    for (const FString& Level : CireTownMap::Def().Levels)
    {
        FString File;
        if (FPackageName::TryConvertLongPackageNameToFilename(Level, File, FPackageName::GetMapPackageExtension()))
            Text += FString::Printf(TEXT("%s %lld %s\n"), *Level, IFileManager::Get().FileSize(*File), *IFileManager::Get().GetTimeStamp(*File).ToString());
    }
    return FMD5::HashAnsiString(*Text);
}
FString CachePath() { return FPaths::ProjectSavedDir() / TEXT("NavCache") / (TEXT("CastleTown-") + CacheKey() + TEXT(".navcache")); }
TArray<ARecastNavMesh*> Meshes(UNavigationSystemV1* NS)
{
    TArray<ARecastNavMesh*> Out;
    for (ANavigationData* D : NS->NavDataSet) if (auto* R = Cast<ARecastNavMesh>(D)) Out.Add(R);
    Out.Sort([](const ARecastNavMesh& A, const ARecastNavMesh& B) { return A.GetConfig().AgentRadius < B.GetConfig().AgentRadius; });
    return Out;
}
void Unlock(UNavigationSystemV1* NS, bool bRebuild)
{
    if (!bLocked || !NS) return;
    bLocked = false;
    NS->RemoveNavigationBuildLock(ENavigationBuildLock::Custom, bRebuild ? UNavigationSystemV1::ELockRemovalRebuildAction::Rebuild : UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
}
}

bool CireNavCache::Enabled() { return CireTownMap::IsActive() && !FParse::Param(FCommandLine::Get(), TEXT("CireNoNavCache")); }

void CireNavCache::BeginLoad(UNavigationSystemV1* NS)
{
    if (!NS || !Enabled() || !IFileManager::Get().FileExists(*CachePath())) return;
    NS->AddNavigationBuildLock(ENavigationBuildLock::Custom);
    bLocked = true;
}

bool CireNavCache::FinishLoad(UWorld* World, UNavigationSystemV1* NS, double& OutMs, int32& OutTiles)
{
    OutMs = 0; OutTiles = 0;
    if (!bLocked || !NS || !World) return false;
    const double Started = FPlatformTime::Seconds();
    const FString Path = CachePath();
    TArray<uint8> Packed;
    auto Miss = [&](const TCHAR* Why)
    {
        UE_LOG(LogCireNavCache, Warning, TEXT("CIRE_NAV_CACHE_MISS %s file=%s"), Why, *Path);
        // Tiles already attached (a partial load) are replaced by the full build the caller starts next.
        for (ARecastNavMesh* R : Meshes(NS)) R->CancelBuild();
        Unlock(NS, false);
        return false;
    };
    if (!FFileHelper::LoadFileToArray(Packed, *Path)) return Miss(TEXT("unreadable"));
    FMemoryReader Head(Packed);
    uint32 FileMagic = 0; int32 Version = 0, Meshes0 = 0, RawSize = 0;
    Head << FileMagic << Version << Meshes0 << RawSize;
    if (FileMagic != Magic || Version != FormatVersion || RawSize <= 0) return Miss(TEXT("bad header"));
    TArray<uint8> Raw; Raw.SetNumUninitialized(RawSize);
    const int32 HeaderBytes = int32(Head.Tell());
    if (!FCompression::UncompressMemory(NAME_Oodle, Raw.GetData(), RawSize, Packed.GetData() + HeaderBytes, Packed.Num() - HeaderBytes)) return Miss(TEXT("corrupt"));
    const TArray<ARecastNavMesh*> Navs = Meshes(NS);
    if (Navs.Num() != Meshes0) return Miss(TEXT("agent count changed"));
    FMemoryReader Body(Raw);
    for (ARecastNavMesh* R : Navs)
    {
        float Radius = 0; Body << Radius;
        if (!FMath::IsNearlyEqual(Radius, R->GetConfig().AgentRadius, .5f)) return Miss(TEXT("agent radius changed"));
        if (!R->GetRecastMesh()) return Miss(TEXT("navmesh not allocated"));
        URecastNavMeshDataChunk* Chunk = NewObject<URecastNavMeshDataChunk>(GetTransientPackage(), NAME_None, RF_Transient);
        FObjectAndNameAsStringProxyArchive Proxy(Body, false);
        Chunk->Serialize(Proxy);
        if (Body.IsError()) return Miss(TEXT("truncated"));
        const TArray<FNavTileRef> Attached = Chunk->AttachTiles(*R);
        OutTiles += Attached.Num();
        Chunk->MarkAsGarbage();
    }
    // Registering the bounds and the town's geometry queued dirty areas, and each generator's Init marked its whole bounds
    // dirty (MarkNavBoundsDirty ignores build locks): the attached tiles already cover all of it. Nothing is running yet
    // (the build was locked), so discarding the pending tiles is free.
    for (ARecastNavMesh* R : Navs) R->CancelBuild();
    FNavSysAccess::ResetDirtyAreas(NS);
    Unlock(NS, false);
    NS->Tick(0.f);
    bool bBusy = NS->IsNavigationBuildInProgress();
    if (bBusy) { for (ARecastNavMesh* R : Navs) R->CancelBuild(); bBusy = NS->IsNavigationBuildInProgress(); }
    for (ARecastNavMesh* R : Navs) R->RequestDrawingUpdate();
    OutMs = (FPlatformTime::Seconds() - Started) * 1000.0;
    UE_LOG(LogCireNavCache, Display, TEXT("CIRE_NAV_CACHE_HIT tiles=%d ms=%.0f building=%d file=%s"), OutTiles, OutMs, bBusy ? 1 : 0, *Path);
    return OutTiles > 0;
}

void CireNavCache::Save(UWorld* World, UNavigationSystemV1* NS)
{
    if (!NS || !World || !Enabled()) return;
    const double Started = FPlatformTime::Seconds();
    const TArray<ARecastNavMesh*> Navs = Meshes(NS);
    TArray<uint8> Raw;
    FMemoryWriter Body(Raw);
    int32 Tiles = 0;
    for (ARecastNavMesh* R : Navs)
    {
        float Radius = R->GetConfig().AgentRadius; Body << Radius;
        TArray<FNavTileRef> Refs; R->GetAllNavMeshTiles(Refs);
        URecastNavMeshDataChunk* Chunk = NewObject<URecastNavMeshDataChunk>(GetTransientPackage(), NAME_None, RF_Transient);
        Chunk->GetTiles(FRecastAccess::Impl(R), Refs, EGatherTilesCopyMode::CopyDataAndCacheData, false);
        Tiles += Chunk->GetNumTiles();
        FObjectAndNameAsStringProxyArchive Proxy(Body, false);
        Chunk->Serialize(Proxy);
        Chunk->ReleaseTiles(); Chunk->MarkAsGarbage();
    }
    if (Tiles == 0) return;
    int32 PackedSize = FCompression::CompressMemoryBound(NAME_Oodle, Raw.Num());
    TArray<uint8> Packed; Packed.SetNumUninitialized(PackedSize);
    if (!FCompression::CompressMemory(NAME_Oodle, Packed.GetData(), PackedSize, Raw.GetData(), Raw.Num())) return;
    Packed.SetNum(PackedSize);
    TArray<uint8> File; FMemoryWriter Out(File);
    uint32 FileMagic = Magic; int32 Version = FormatVersion, Count = Navs.Num(), RawSize = Raw.Num();
    Out << FileMagic << Version << Count << RawSize;
    File.Append(Packed);
    const FString Path = CachePath();
    // One cache per key; older keys (other layouts, older builds) are removed so the folder never grows.
    TArray<FString> Old; IFileManager::Get().FindFiles(Old, *(FPaths::GetPath(Path) / TEXT("CastleTown-*.navcache")), true, false);
    for (const FString& Name : Old) IFileManager::Get().Delete(*(FPaths::GetPath(Path) / Name));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    const bool bOk = FFileHelper::SaveArrayToFile(File, *Path);
    UE_LOG(LogCireNavCache, Display, TEXT("CIRE_NAV_CACHE_SAVED ok=%d tiles=%d raw_mb=%.1f file_mb=%.1f ms=%.0f file=%s"), bOk ? 1 : 0, Tiles, Raw.Num() / 1048576.0, File.Num() / 1048576.0,
        (FPlatformTime::Seconds() - Started) * 1000.0, *Path);
}
