#include "CireBuffs.h"
#include "CireScalingKits.h" // scaling-kits
#include "CireGame.h"
#include "CireSkillRuntime.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UCireBuffState::UCireBuffState()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.2f;
    SetIsReplicatedByDefault(true);
}
void UCireBuffState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UCireBuffState,Buffs);
}
const FCireBuffEntry* UCireBuffState::Find(FName Id) const
{
    return Buffs.FindByPredicate([Id](const FCireBuffEntry& E){return E.Id==Id;});
}
int32 UCireBuffState::Prune(float Now,int32 Phase,bool bOwnerAlive)
{
    const int32 Before=Buffs.Num();
    Buffs.RemoveAll([&](const FCireBuffEntry& E)
    {
        if(!bOwnerAlive)return true;
        if(E.Phase!=INDEX_NONE&&Phase!=INDEX_NONE&&E.Phase!=Phase)return true;
        return E.EndTime>E.StartTime&&E.EndTime<=Now;
    });
    return Before-Buffs.Num();
}
void UCireBuffState::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta,Type,Tick);
    AActor* Owner=GetOwner();
    if(!Owner||!Owner->HasAuthority()||Buffs.IsEmpty())return;
    const bool bAlive=CireSkillRuntime::Alive(Owner)||(Cast<ACireHero>(Owner)&&!Cast<ACireHero>(Owner)->bDrafted);
    if(Prune(Owner->GetWorld()->GetTimeSeconds(),CireSkillRuntime::Phase(Owner->GetWorld()),bAlive)>0)Owner->ForceNetUpdate();
}

float CireBuffs::ServerNow(const UWorld* World)
{
    if(!World)return 0;
    if(World->GetAuthGameMode())return World->GetTimeSeconds();
    if(const auto* State=World->GetGameState())return State->GetServerWorldTimeSeconds();
    return World->GetTimeSeconds();
}
UCireBuffState* CireBuffs::Get(const AActor* Unit)
{
    return Unit?Unit->FindComponentByClass<UCireBuffState>():nullptr;
}
bool CireBuffs::Apply(AActor* Target,FName Id,float DurationSeconds,AActor* Source,int32 Stacks)
{
    if(!IsValid(Target)||!Target->HasAuthority()||Id.IsNone()||!FMath::IsFinite(DurationSeconds)||DurationSeconds<0)return false;
    auto* State=Get(Target);if(!State)return false;
    const float Now=Target->GetWorld()->GetTimeSeconds();
    State->Prune(Now,CireSkillRuntime::Phase(Target->GetWorld()),true);
    FCireBuffEntry* Entry=State->Buffs.FindByPredicate([Id](const FCireBuffEntry& E){return E.Id==Id;});
    const float End=DurationSeconds>0?Now+FMath::Min(DurationSeconds,3600.f):0.f;
    if(Entry)
    {
        // A refresh keeps the original start so the start burst is not replayed every re-cast.
        Entry->EndTime=End<=0||Entry->EndTime<=Entry->StartTime?End:FMath::Max(Entry->EndTime,End);
        Entry->Stacks=static_cast<uint8>(FMath::Clamp(Stacks,1,255));
        if(Source)Entry->Source=Source;
    }
    else
    {
        if(State->Buffs.Num()>=MaxEntriesPerUnit)
        {
            // Evict the record closest to expiry; permanent records are kept.
            int32 Victim=INDEX_NONE;float Soonest=MAX_flt;
            for(int32 I=0;I<State->Buffs.Num();++I)
                if(State->Buffs[I].EndTime>State->Buffs[I].StartTime&&State->Buffs[I].EndTime<Soonest){Soonest=State->Buffs[I].EndTime;Victim=I;}
            if(Victim==INDEX_NONE)return false;
            State->Buffs.RemoveAt(Victim);
        }
        FCireBuffEntry New;New.Id=Id;New.StartTime=Now;New.EndTime=End;New.Stacks=static_cast<uint8>(FMath::Clamp(Stacks,1,255));
        New.Source=Source;New.SourceTeam=static_cast<int8>(FMath::Clamp(CireSkillRuntime::Team(Source),-1,127));
        New.Phase=CireSkillRuntime::Phase(Target->GetWorld());
        State->Buffs.Add(New);
    }
    Target->ForceNetUpdate();
    return true;
}
bool CireBuffs::Remove(AActor* Target,FName Id)
{
    auto* State=Get(Target);if(!State||!Target->HasAuthority())return false;
    const int32 Removed=State->Buffs.RemoveAll([Id](const FCireBuffEntry& E){return E.Id==Id;});
    if(Removed)Target->ForceNetUpdate();
    return Removed>0;
}
void CireBuffs::ClearAll(AActor* Target)
{
    auto* State=Get(Target);if(!State||!Target->HasAuthority()||State->Buffs.IsEmpty())return;
    State->Buffs.Reset();Target->ForceNetUpdate();
}
bool CireBuffs::IsActive(const AActor* Unit,FName Id)
{
    const auto* State=Get(Unit);if(!State)return false;
    const FCireBuffEntry* E=State->Find(Id);if(!E)return false;
    return E->EndTime<=E->StartTime||E->EndTime>ServerNow(Unit->GetWorld());
}
const TArray<FName>& CireBuffs::KnownIds()
{
    static const TArray<FName> BaseIds={
        // Champion skills recorded with Apply.
        TEXT("iron_guard"),TEXT("war_cry"),TEXT("challenge_of_iron"),TEXT("sanctuary"),TEXT("bastion_of_dawn"),
        TEXT("mass_aegis"),TEXT("wellspring"),TEXT("frost_bind"),TEXT("shield_slam"),
        // Monster-side records.
        TEXT("taunted"),TEXT("npc_tank_provoke_debuff"),
        // Derived from existing replicated state (no record needed).
        TEXT("guarded"),TEXT("taunting"),TEXT("slowed"),TEXT("poisoned"),
        TEXT("boss_leader_frenzy"),TEXT("boss_siege_fury"),TEXT("enraged"),TEXT("rallied"),
        TEXT("npc_tank_wall"),TEXT("npc_tank_guard"),TEXT("npc_tank_provoke"),
        // Passives with a visible state.
        TEXT("battle_rhythm"),TEXT("soul_conduit"),
        // Data-ready ids for item actives and future skills.
        TEXT("blood_rage"),TEXT("frost_weapon"),TEXT("blessing"),TEXT("regeneration"),TEXT("stunned"),
        // Item actives and consumables (progression-shop): records and inventory timed buffs.
        TEXT("oathshield"),TEXT("toll_of_the_grave"),TEXT("borrowed_time"),TEXT("scatter"),TEXT("mana_restore"),
        // monster-races: race-skill riders and themed marks (CireRaces::OnAbilityReleased).
        TEXT("npc_rooted"),TEXT("npc_silenced"),TEXT("npc_tide"),TEXT("npc_ink"),TEXT("npc_mind"),TEXT("npc_thorns"),TEXT("npc_spores"),
        TEXT("npc_bloodlust"),TEXT("npc_sundered"),TEXT("npc_dragonfire"),TEXT("npc_scaleward"),TEXT("npc_runic"),TEXT("npc_feral"),
        TEXT("npc_profane"),TEXT("npc_void"),
        // champion-draft: crowd control, heal cuts, armor break and Executioner (CireCrowdControl).
        TEXT("silenced"),TEXT("interrupted"),TEXT("healing_cut"),TEXT("heal_cut_done"),TEXT("armor_broken"),TEXT("executioner_ready"),
        // new-champions: bounty/witch marks, tracking, banishment, self buffs and Aetheri construct fields.
        TEXT("bounty_mark"),TEXT("witch_mark"),TEXT("tracked"),TEXT("banished"),TEXT("hunters_stride"),TEXT("warding_talisman"),TEXT("moonlit_sprint"),TEXT("overcharge"),
        TEXT("aether_aegis"),TEXT("aether_haste"),TEXT("aether_weakened"),TEXT("aether_nexus"),TEXT("npc_aether_empowered"),
        // champion-draft: dodge-roll skill states (CireRollSkills::BuffIds).
        TEXT("tumblers_edge"),TEXT("killer_instinct"),TEXT("windrunner"),TEXT("quickened_mind"),TEXT("momentum"),TEXT("blur_step"),
        TEXT("mine_layer"),TEXT("taunting_tumble"),TEXT("shield_tumble"),TEXT("venom_tumble"),TEXT("shadow_dance"),TEXT("evasive_stance")};
    static const TArray<FName> Ids=[]{TArray<FName> Out=BaseIds;for(const FName Id:CireKits::BuffIds())Out.AddUnique(Id);return Out;}(); // scaling-kits
    return Ids;
}
