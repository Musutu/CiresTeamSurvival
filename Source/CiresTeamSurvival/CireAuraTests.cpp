#include "CireAuraVisuals.h"
#if !UE_BUILD_SHIPPING
#include "CireAuraShapes.h"
#include "CireBuffs.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireAudio.h"
#include "CireNPCState.h"
#include "CireSkillRuntime.h"
#include "CireUISettings.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "ProceduralMeshComponent.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// aura-vfx native checks: data coverage, distinct signatures, lifecycle start/loop/expire,
// cleanup after death and phase changes, realm privacy and the concurrency caps.
bool CireAuraVisuals::RunSmoke(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->GetWorld())return false;
    UWorld* World=Mode->GetWorld();
    bool Pass=true;int32 Checks=0;
    auto Check=[&](bool Value,const FString& Why){++Checks;if(!Value){Pass=false;UE_LOG(LogTemp,Error,TEXT("CIRE_AURA_FAIL %s"),*Why);}};
    // ---- Data -------------------------------------------------------------
    FString Error;Check(CireAuraData::Reload(Error),TEXT("BuffVisuals.json loads: ")+Error);
    for(const FName Id:CireBuffs::KnownIds())
    {
        const FCireAuraDef* Def=CireAuraData::Find(Id);
        Check(Def!=nullptr,TEXT("every producible buff id has a visual entry: ")+Id.ToString());
        if(!Def)continue;
        Check(!Def->Layers.IsEmpty()&&Def->Fade>0,TEXT("entry has layers and a fade-out: ")+Id.ToString());
        Check(Def->Kind==TEXT("passive")||!Def->SoundStart.IsEmpty(),TEXT("non-passive entry exposes a start sound cue id: ")+Id.ToString());
        if(Def->Attack.bValid)Check(!Def->SoundHit.IsEmpty(),TEXT("attack modifier exposes a hit sound cue id: ")+Id.ToString());
    }
    {
        // Distinct silhouettes: no two effects may share the same set of shape/style layers.
        TMap<FString,FName> Signatures;
        for(const auto& Pair:CireAuraData::All())
        {
            TArray<FString> Parts;for(const auto& L:Pair.Value.Layers)Parts.Add(FString(CireAuraShapes::ShapeName(L.Shape))+TEXT(":")+L.Style.ToString());
            Parts.Sort();const FString Key=FString::Join(Parts,TEXT("|"));
            Check(!Signatures.Contains(Key),TEXT("distinct layer signature: ")+Pair.Key.ToString()+TEXT(" vs ")+(Signatures.Contains(Key)?Signatures[Key].ToString():FString()));
            Signatures.Add(Key,Pair.Key);
        }
    }
    {
        TMap<FName,FCireAuraDef> Scratch;FCireAuraLimits Limits;FString Why;
        Check(!CireAuraData::Parse(TEXT("{\"schemaVersion\":2,\"buffs\":{}}"),Scratch,Limits,Why),TEXT("rejects wrong schema"));
        Check(!CireAuraData::Parse(TEXT("{\"schemaVersion\":1,\"buffs\":{\"x\":{\"name\":\"X\",\"kind\":\"buff\",\"palette\":{\"primary\":[1,0,0],\"secondary\":[1,0,0],\"core\":[1,1,1]},\"layers\":[{\"shape\":\"laser\",\"attach\":\"body\"}]}}}"),Scratch,Limits,Why),TEXT("rejects unknown shape"));
        Check(!CireAuraData::Parse(TEXT("{\"schemaVersion\":1,\"buffs\":{\"x\":{\"name\":\"X\",\"kind\":\"buff\",\"palette\":{\"primary\":[3,0,0],\"secondary\":[1,0,0],\"core\":[1,1,1]},\"layers\":[{\"shape\":\"ring\",\"attach\":\"ground\"}]}}}"),Scratch,Limits,Why),TEXT("rejects out-of-range colour"));
        Check(!CireAuraData::Parse(TEXT("{\"schemaVersion\":1,\"buffs\":{\"x\":{\"name\":\"X\",\"kind\":\"buff\",\"palette\":{\"primary\":[1,0,0],\"secondary\":[1,0,0],\"core\":[1,1,1]},\"layers\":[{\"shape\":\"ring\",\"attach\":\"ground\"}],\"sound\":{\"start\":\"Bad Cue!\"}}}}"),Scratch,Limits,Why),TEXT("rejects unsafe sound cue id"));
        Check(CireAuraData::Find(TEXT("blood_rage"))!=nullptr,TEXT("rejected parses keep the loaded data"));
        Check(CireAudio::HasCue(TEXT("aura_apply"))&&CireAudio::HasCue(TEXT("aura_heal"))&&CireAudio::HasCue(TEXT("aura_swing")),TEXT("aura fallback and swing cues exist in AudioCues.json"));
        int32 Cues=0;
        for(const auto& Pair:CireAuraData::All())
            for(const FString* Cue:{&Pair.Value.SoundStart,&Pair.Value.SoundLoop,&Pair.Value.SoundEnd,&Pair.Value.SoundHit})
                if(!Cue->IsEmpty()){++Cues;Check(CireAudio::HasCue(FName(**Cue)),TEXT("buff cue id resolves to an AudioCues.json entry: ")+*Cue);}
        Check(Cues>=50,FString::Printf(TEXT("per-buff cues declared (%d)"),Cues));
        for(const TCHAR* Id:{TEXT("boss_leader_frenzy"),TEXT("blood_rage"),TEXT("frost_weapon"),TEXT("blessing"),TEXT("npc_tank_wall"),TEXT("poisoned")})
        {const auto* Def=CireAuraData::Find(Id);Check(Def&&!Def->SoundStart.IsEmpty()&&Def->SoundStart!=TEXT("aura_apply"),FString(TEXT("signature buff has its own start sound: "))+Id);}
    }
    // ---- World fixtures -----------------------------------------------------
    auto* Auras=CireAuraVisuals::Get(World);Check(Auras!=nullptr,TEXT("aura subsystem exists in game worlds"));
    if(!Auras){UE_LOG(LogTemp,Display,TEXT("CIRE_AURA_SMOKE_FAIL checks=%d"),Checks);return false;}
    TArray<AActor*> Actors;
    const TWeakObjectPtr<AActor> OldObserver=Auras->ObserverOverride;const bool bOldCamera=Auras->bCameraOverride;
    const FVector OldCamLoc=Auras->CameraOverrideLocation;const FRotator OldCamRot=Auras->CameraOverrideRotation;
    ON_SCOPE_EXIT
    {
        for(auto* Actor:Actors)if(IsValid(Actor))Actor->Destroy();
        Auras->ObserverOverride=OldObserver;Auras->bCameraOverride=bOldCamera;Auras->CameraOverrideLocation=OldCamLoc;Auras->CameraOverrideRotation=OldCamRot;
        for(TActorIterator<ACireAuraStrike> It(World);It;++It)It->Destroy();
    };
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Origin(0,52000,21000);
    auto Make=[&](FVector At,int32 Team)->ACireHero*
    {
        auto* H=World->SpawnActor<ACireHero>(At,FRotator::ZeroRotator,P);if(!H)return nullptr;Actors.Add(H);
        H->TeamId=Team;H->Draft(0);H->SetActorTickEnabled(false);H->SetActorEnableCollision(false);H->GetCharacterMovement()->DisableMovement();
        return H;
    };
    ACireHero* Observer=Make(Origin+FVector(-900,0,0),0);ACireHero* Hero=Make(Origin,0);
    Check(Observer&&Hero,TEXT("fixture heroes spawn"));if(!Observer||!Hero){UE_LOG(LogTemp,Display,TEXT("CIRE_AURA_SMOKE_FAIL checks=%d"),Checks);return false;}
    Auras->ObserverOverride=Observer;Auras->bCameraOverride=true;
    Auras->CameraOverrideLocation=Origin+FVector(-700,0,500);Auras->CameraOverrideRotation=(Origin-Auras->CameraOverrideLocation).Rotation();
    Check(CireBuffs::Get(Hero)&&CireBuffs::Get(Hero)->GetIsReplicated(),TEXT("hero carries a replicated buff record component"));
    auto* Aura=Hero->FindComponentByClass<UCireAuraComponent>();
    Check(Aura!=nullptr,TEXT("hero has an aura renderer"));
    {
        auto* Monster=World->SpawnActor<ACireMonster>(Origin+FVector(0,600,0),FRotator::ZeroRotator,P);
        if(Monster){Actors.Add(Monster);Monster->SetActorTickEnabled(false);}
        Check(Monster&&CireBuffs::Get(Monster)&&CireBuffs::Get(Monster)->GetIsReplicated()&&Monster->FindComponentByClass<UCireAuraComponent>(),TEXT("monster carries replicated records and a renderer"));
    }
    if(!Aura){UE_LOG(LogTemp,Display,TEXT("CIRE_AURA_SMOKE_FAIL checks=%d"),Checks);return false;}
    const auto Has=[&](FName Id){return Aura->Instances.ContainsByPredicate([Id](const FCireAuraInstance& I){return I.Id==Id&&I.FadeLocal<0;});};
    const auto Fading=[&](FName Id){return Aura->Instances.ContainsByPredicate([Id](const FCireAuraInstance& I){return I.Id==Id&&I.FadeLocal>=0;});};
    float Clock=1000.f;const float Now=World->GetTimeSeconds();
    // ---- Lifecycle: start -> loop -> expire -> removed ---------------------
    Hero->ShieldUntil=Now+30;Check(CireBuffs::Apply(Hero,TEXT("iron_guard"),30,Hero),TEXT("server records iron guard"));
    Auras->UpdateNow(Clock);
    Check(Has(TEXT("iron_guard"))&&!Has(TEXT("guarded")),TEXT("named guard shows its own signature, not the generic guard"));
    Check(Aura->CountVertices()>0&&Aura->CountLayers()>0,TEXT("start renders geometry"));
    Check(Aura->AreMeshesCollisionFree()&&Aura->Core&&Aura->Core->GetMaterial(0)&&Aura->Soft&&Aura->Soft->GetMaterial(0),TEXT("aura meshes are collision free and have materials"));
    const int32 BurstVertices=Aura->CountVertices();
    Clock+=2;Auras->UpdateNow(Clock);Check(Has(TEXT("iron_guard"))&&Aura->CountVertices()>0,TEXT("loop keeps rendering after the burst"));
    const float Start=CireBuffs::Get(Hero)->Find(TEXT("iron_guard"))->StartTime;
    CireBuffs::Apply(Hero,TEXT("iron_guard"),30,Hero);
    Check(CireBuffs::Get(Hero)->Buffs.Num()==1&&CireBuffs::Get(Hero)->Find(TEXT("iron_guard"))->StartTime==Start,TEXT("refresh keeps one record and its start (no replayed burst)"));
    Hero->ShieldUntil=0;Clock+=.05f;Auras->UpdateNow(Clock);
    Check(Fading(TEXT("iron_guard")),TEXT("a cleansed guard fades instead of popping"));
    Clock+=2;Auras->UpdateNow(Clock);
    Check(Aura->Instances.IsEmpty()&&Aura->CountVertices()==0,TEXT("expired effect is removed and its geometry cleared"));
    Check(BurstVertices>0,TEXT("burst frame produced geometry"));
    // ---- Every signature renders on a unit ---------------------------------
    for(const auto& Pair:CireAuraData::All())
    {
        CireBuffs::ClearAll(Hero);Hero->ShieldUntil=Hero->TauntUntil=Hero->SlowUntil=Now+30;
        Clock+=3;Auras->UpdateNow(Clock);Aura->Instances.Reset();
        CireBuffs::Apply(Hero,Pair.Key,30,Observer);Clock+=.01f;Auras->UpdateNow(Clock);
        const bool bPresent=Has(Pair.Key)||Pair.Key==TEXT("guarded")||Pair.Key==TEXT("taunting")||Pair.Key==TEXT("slowed");
        Check(bPresent&&Aura->CountVertices()>0&&Aura->CountVertices()<=CireAuraData::Limits().MaxVerticesPerUnit*6/5,TEXT("signature renders within budget: ")+Pair.Key.ToString());
    }
    CireBuffs::ClearAll(Hero);Hero->ShieldUntil=Hero->TauntUntil=Hero->SlowUntil=0;Clock+=3;Auras->UpdateNow(Clock);
    // ---- Derived states ----------------------------------------------------
    Hero->PoisonAreaCount=3;Hero->SlowUntil=Now+10;Clock+=.1f;Auras->UpdateNow(Clock);
    const FCireAuraInstance* Poison=Aura->Instances.FindByPredicate([](const FCireAuraInstance& I){return I.Id==TEXT("poisoned");});
    Check(Poison&&Poison->Stacks==3&&Has(TEXT("slowed")),TEXT("poison stacks and generic slow come from replicated state"));
    CireBuffs::Apply(Hero,TEXT("frost_bind"),10,Observer);Clock+=.1f;Auras->UpdateNow(Clock);
    Check(Has(TEXT("frost_bind"))&&Fading(TEXT("slowed")),TEXT("a named slow replaces the generic slow"));
    // ---- Item actives and consumables (replicated inventory timed buffs) ----
    for(const TCHAR* Item:{TEXT("vial_of_crimson"),TEXT("aether_phial"),TEXT("hourglass_of_ages"),TEXT("ravenfeather_mantle")})
        Check(CireAuraData::ItemBuffs().Contains(FName(Item)),FString(TEXT("timed item buff has a signature: "))+Item);
    if(Hero->Inventory)
    {
        FCireTimedBuff Timed;Timed.Id=TEXT("hourglass_of_ages");Timed.Duration=4;Timed.EndsAt=CireBuffs::ServerNow(World)+4;
        Hero->Inventory->Buffs.Add(Timed);Clock+=.1f;Auras->UpdateNow(Clock);
        Check(Has(TEXT("borrowed_time")),TEXT("Hourglass of Ages shows Borrowed Time from the replicated inventory"));
        Hero->Inventory->Buffs.Reset();Clock+=.1f;Auras->UpdateNow(Clock);
        Check(Fading(TEXT("borrowed_time")),TEXT("expired item buff fades"));
    }
    else Check(false,TEXT("hero has an inventory"));
    // ---- Death cleanup -----------------------------------------------------
    CireBuffs::Apply(Hero,TEXT("blood_rage"),30,Hero);Clock+=.1f;Auras->UpdateNow(Clock);Check(Has(TEXT("blood_rage")),TEXT("blood rage visible while alive"));
    Check(Aura->LoopIds.Contains(TEXT("blood_rage")),TEXT("blood rage heartbeat loop runs while the buff is active"));
    Hero->bDead=true;Clock+=.1f;Auras->UpdateNow(Clock);
    Check(!Aura->Instances.ContainsByPredicate([](const FCireAuraInstance& I){return I.FadeLocal<0;}),TEXT("death fades every effect"));
    Clock+=3;Auras->UpdateNow(Clock);Check(Aura->Instances.IsEmpty()&&Aura->CountVertices()==0&&!Aura->IsLightOn(),TEXT("no geometry or light leaks after death"));
    Check(Aura->LoopIds.IsEmpty()&&Aura->LoopAudio.IsEmpty(),TEXT("death stops attached loop sounds"));
    Check(CireBuffs::Get(Hero)->Prune(World->GetTimeSeconds(),CireSkillRuntime::Phase(World),false)>0&&CireBuffs::Get(Hero)->Buffs.IsEmpty(),TEXT("server drops records of a dead unit"));
    Hero->bDead=false;Hero->PoisonAreaCount=0;Hero->SlowUntil=0;
    // ---- Phase change ------------------------------------------------------
    CireBuffs::Apply(Hero,TEXT("war_cry"),30,Hero);
    const int32 Phase=CireSkillRuntime::Phase(World);
    Check(CireBuffs::Get(Hero)->Prune(World->GetTimeSeconds(),Phase,true)==0,TEXT("same phase keeps records"));
    Check(CireBuffs::Get(Hero)->Prune(World->GetTimeSeconds(),Phase+1,true)==1&&CireBuffs::Get(Hero)->Buffs.IsEmpty(),TEXT("phase change drops records"));
    Hero->TauntUntil=0;Clock+=3;Auras->UpdateNow(Clock);Check(Aura->Instances.IsEmpty(),TEXT("nothing survives the phase change"));
    // ---- Entry cap ---------------------------------------------------------
    for(int32 I=0;I<20;++I)CireBuffs::Apply(Hero,FName(*FString::Printf(TEXT("test_%d"),I)),5+I);
    Check(CireBuffs::Get(Hero)->Buffs.Num()<=CireBuffs::MaxEntriesPerUnit,TEXT("per-unit record cap holds"));
    CireBuffs::ClearAll(Hero);Clock+=3;Auras->UpdateNow(Clock);
    // ---- Realm privacy -----------------------------------------------------
    {
        ACireHero* Opponent=Make(Origin+FVector(0,-300,0),1);
        if(Opponent)
        {
            CireBuffs::Apply(Opponent,TEXT("blood_rage"),30,Opponent);Clock+=.1f;Auras->UpdateNow(Clock);
            auto* OpponentAura=Opponent->FindComponentByClass<UCireAuraComponent>();
            const bool bArena=CireSkillRuntime::Phase(World)==static_cast<int32>(Cires::MatchPhase::Arena);
            Check(OpponentAura&&(OpponentAura->CountVertices()>0)==bArena,TEXT("opponent's aura is drawn only when the realm rule allows observing it"));
            Opponent->SetActorHiddenInGame(true);Clock+=.1f;Auras->UpdateNow(Clock);
            Check(OpponentAura&&OpponentAura->CountVertices()==0,TEXT("hidden units never show auras"));
        }
    }
    // ---- Concurrency caps --------------------------------------------------
    {
        const FCireAuraLimits& Limits=CireAuraData::Limits();TArray<ACireHero*> Crowd;
        for(int32 I=0;I<Limits.MaxUnits+12;++I)if(auto* H=Make(Origin+FVector(200+(I%8)*120,-500+(I/8)*140,0),0)){CireBuffs::Apply(H,TEXT("bastion_of_dawn"),30,H);CireBuffs::Apply(H,TEXT("blood_rage"),30,H);H->ShieldUntil=Now+30;Crowd.Add(H);}
        Clock+=.1f;Auras->UpdateNow(Clock);
        Check(Auras->RenderedUnits<=Limits.MaxUnits&&Auras->CulledUnits>0,FString::Printf(TEXT("unit cap holds (%d rendered, %d culled)"),Auras->RenderedUnits,Auras->CulledUnits));
        Check(Auras->LitUnits<=Limits.MaxLights,FString::Printf(TEXT("light cap holds (%d)"),Auras->LitUnits));
        bool bLayers=true;int32 Lights=0;
        for(const auto& Weak:Auras->Components)if(auto* C=Weak.Get()){bLayers&=C->CountLayers()<=Limits.MaxLayersPerUnit;Lights+=C->IsLightOn();}
        Check(bLayers&&Lights<=Limits.MaxLights,TEXT("per-unit layer cap and world light count hold"));
        int32 Loops=0;for(const auto& Weak:Auras->Components)if(auto* C=Weak.Get())Loops+=C->LoopIds.Num();
        Check(Loops>0&&Loops<=CireAuraVisuals::MaxLoops,FString::Printf(TEXT("attached loop sound cap holds (%d)"),Loops));
        FCireAuraAttack Attack=CireAuraData::Find(TEXT("blood_rage"))->Attack;int32 Spawned=0;
        for(int32 I=0;I<Limits.MaxStrikes+10;++I)Spawned+=Auras->SpawnStrike(ACireAuraStrike::EMode::Swipe,Attack,Origin,Origin+FVector(100,0,0),1)!=nullptr;
        Check(Spawned==Limits.MaxStrikes,FString::Printf(TEXT("strike cap holds (%d)"),Spawned));
        bool bStrikes=true;for(TActorIterator<ACireAuraStrike> It(World);It;++It){bStrikes&=It->IsCollisionFree()&&It->VertexCount()>0;}
        Check(bStrikes,TEXT("strikes render and never collide"));
        for(TActorIterator<ACireAuraStrike> It(World);It;++It)It->Destroy();
        for(auto* H:Crowd){H->Destroy();}
        Clock+=.1f;Auras->UpdateNow(Clock);
    }
    {
        // Ranged empowered attacks draw a trail along the projectile path and a muzzle burst.
        const FCireAuraAttack Attack=CireAuraData::Find(TEXT("frost_weapon"))->Attack;
        ACireAuraStrike* Trail=Auras->SpawnStrike(ACireAuraStrike::EMode::Trail,Attack,Origin,Origin+FVector(300,0,0),1);
        ACireAuraStrike* Muzzle=Auras->SpawnStrike(ACireAuraStrike::EMode::Muzzle,Attack,Origin,Origin+FVector(300,0,0),1);
        if(Trail)Trail->SetPreviewTrail({Origin,Origin+FVector(100,0,20),Origin+FVector(200,0,25),Origin+FVector(300,0,10)});
        if(Muzzle)Muzzle->SetPreviewAge(.05f);
        Check(Trail&&Muzzle&&Trail->VertexCount()>0&&Muzzle->VertexCount()>0&&Trail->IsCollisionFree(),TEXT("ranged trail and muzzle burst render collision-free"));
        if(Trail)Trail->Destroy();if(Muzzle)Muzzle->Destroy();
    }
    // ---- Attack modifier selection -----------------------------------------
    CireBuffs::Apply(Hero,TEXT("battle_rhythm"),30,Hero);CireBuffs::Apply(Hero,TEXT("blood_rage"),30,Hero);Clock+=.1f;Auras->UpdateNow(Clock);
    const FCireAuraDef* Mod=Aura->AttackModifier(CireBuffs::ServerNow(World));
    Check(Mod&&Mod->Id==TEXT("blood_rage")&&Mod->Attack.Swipe==TEXT("blood")&&Mod->Attack.OnHit==TEXT("splash"),TEXT("highest-priority buff owns the attack modifier (bloody swipe and splash)"));
    {
        // End to end: a replicated attack serial released while empowered spawns exactly one swipe; an unbuffed swing spawns none.
        Auras->UpdateNow();const int32 Before=Auras->SpawnedStrikes;const float ServerNow=CireBuffs::ServerNow(World);
        Hero->AttackAimLocation=Hero->GetActorLocation()+FVector(150,0,0);Hero->AttackDuration=.65f;Hero->AttackStartedServerTime=ServerNow-.5f;
        if(++Hero->AttackSerial==0)++Hero->AttackSerial;
        Auras->UpdateNow();Auras->UpdateNow();
        Check(Auras->SpawnedStrikes==Before+1,TEXT("empowered basic attack spawns its signature swipe once at release"));
        CireBuffs::ClearAll(Hero);Auras->UpdateNow();Auras->UpdateNow(Auras->LastLocalNow+5);
        const int32 Plain=Auras->SpawnedStrikes;Hero->AttackStartedServerTime=CireBuffs::ServerNow(World)-.5f;if(++Hero->AttackSerial==0)++Hero->AttackSerial;
        Auras->UpdateNow();Auras->UpdateNow();
        Check(Auras->SpawnedStrikes==Plain,TEXT("an unbuffed attack has no empowered swipe"));
        for(TActorIterator<ACireAuraStrike> It(World);It;++It)It->Destroy();
    }
    CireBuffs::ClearAll(Hero);
    // ---- Other-player intensity setting -------------------------------------
    {
        const FString File=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("AuraChecks"),TEXT("AuraSettingsSmoke.ini"));
        FCireUISettings S;S.Load(File);S.OtherEffectsIntensity=3;S.Save();
        FCireUISettings T;T.Load(File);
        Check(T.OtherEffectsIntensity==1.f,TEXT("other-units intensity is clamped and persisted"));
        T.OtherEffectsIntensity=.25f;T.Save();FCireUISettings U;U.Load(File);
        Check(FMath::IsNearlyEqual(U.OtherEffectsIntensity,.25f),TEXT("other-units intensity round-trips"));
        IFileManager::Get().Delete(*File);
    }
    // ---- Teardown leaves nothing behind ------------------------------------
    for(auto* Actor:Actors)if(IsValid(Actor))Actor->Destroy();Actors.Reset();
    Clock+=.1f;Auras->UpdateNow(Clock);
    bool bClean=true;for(const auto& Weak:Auras->Components)if(auto* C=Weak.Get())bClean&=IsValid(C->GetOwner())&&!C->GetOwner()->IsActorBeingDestroyed();
    Check(bClean,TEXT("destroyed units unregister their renderers"));
    UE_LOG(LogTemp,Display,TEXT("CIRE_AURA_SMOKE_%s checks=%d signatures=%d known=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Checks,CireAuraData::All().Num(),CireBuffs::KnownIds().Num());
    return Pass;
}
#endif
