#include "CireStatusVisual.h"
#include "CireGame.h"
#include "CireRealm.h"
#include "ProceduralMeshComponent.h"
#include "Components/CapsuleComponent.h"

#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

UCireStatusVisualComponent::UCireStatusVisualComponent(){PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.033f;}
void CireStatusVisual::Attach(ACharacter* Unit){
    if(!IsValid(Unit)||Unit->GetNetMode()==NM_DedicatedServer)return;
    auto* Component=NewObject<UCireStatusVisualComponent>(Unit);Unit->AddInstanceComponent(Component);Component->RegisterComponent();
}
void UCireStatusVisualComponent::CreateShapes(ACharacter* Unit){
    const FLinearColor Colors[]={FLinearColor(.95f,.65f,.12f,.16f),FLinearColor(.12f,.65f,1.f,.3f),FLinearColor(.25f,.9f,.04f,.3f),FLinearColor(1.f,.25f,.04f,.3f)};
    for(int32 I=0;I<4;++I){
        auto* Shape=NewObject<UProceduralMeshComponent>(Unit);Unit->AddInstanceComponent(Shape);
        Shape->SetupAttachment(Unit->GetRootComponent());
        Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);Shape->SetGenerateOverlapEvents(false);Shape->SetCastShadow(false);Shape->SetCanEverAffectNavigation(false);
        Shape->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpell/M_SpellGlow.M_SpellGlow")));
        TArray<FVector> V,Normals;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> C;TArray<FProcMeshTangent> Tangents;
        constexpr int32 Segments=32,Rings=10;
        for(int32 R=0;R<=Rings;++R)for(int32 S=0;S<=Segments;++S){
            const float A=2*PI*S/Segments,B=I==0?PI*R/Rings:2*PI*R/Rings;
            FVector P=I==0?FVector(50*FMath::Sin(B)*FMath::Cos(A),50*FMath::Sin(B)*FMath::Sin(A),50*FMath::Cos(B)):
                FVector((48+3*FMath::Cos(B))*FMath::Cos(A),(48+3*FMath::Cos(B))*FMath::Sin(A),3*FMath::Sin(B));
            V.Add(P);Normals.Add(P.GetSafeNormal());UV.Add(FVector2D(float(S)/Segments,float(R)/Rings));C.Add(Colors[I]);
            if(R<Rings&&S<Segments){int32 K=R*(Segments+1)+S;Indices.Append({K,K+Segments+1,K+1,K+1,K+Segments+1,K+Segments+2});}
        }
        Shape->CreateMeshSection_LinearColor(0,V,Indices,Normals,UV,C,Tangents,false);
        Shape->SetHiddenInGame(true);Shape->RegisterComponent();Shapes.Add(Shape);
    }
}
void UCireStatusVisualComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick){
    Super::TickComponent(Delta,Type,Tick);
    auto* Unit=Cast<ACharacter>(GetOwner());if(!Unit)return;
    auto* State=Unit->GetWorld()->GetGameState<ACireGameState>();
    const float Now=State?State->GetServerWorldTimeSeconds():Unit->GetWorld()->GetTimeSeconds();
    bool Active[4]={false,false,false,false};
    if(const auto* Hero=Cast<ACireHero>(Unit);Hero&&!Hero->bDead&&Hero->Health>0){Active[0]=Hero->ShieldUntil>Now;Active[1]=Hero->SlowUntil>Now;Active[2]=Hero->PoisonAreaCount>0;Active[3]=Hero->TauntUntil>Now;}
    else if(const auto* Monster=Cast<ACireMonster>(Unit);Monster&&Monster->Health>0){Active[1]=Monster->SlowUntil>Now;Active[2]=Monster->PoisonAreaCount>0;}
    const bool Visible=CireRealm::CanObserve(Unit->GetWorld()->GetFirstPlayerController(),Unit);
    if(Shapes.IsEmpty()&&Visible&&(Active[0]||Active[1]||Active[2]||Active[3]))CreateShapes(Unit);
    const float Feet=-Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    for(int32 I=0;I<Shapes.Num();++I){
        Shapes[I]->SetHiddenInGame(!Visible||!Active[I]);
        const float Pulse=1.f+.05f*FMath::Sin(Now*(2.5f+I));
        Shapes[I]->SetRelativeScale3D(I==0?FVector(1.05f*Pulse,1.05f*Pulse,1.9f):FVector((.9f+.18f*I)*Pulse,(.9f+.18f*I)*Pulse,1.f));
        Shapes[I]->SetRelativeLocation(I==0?FVector::ZeroVector:FVector(0,0,Feet+8+I*7));
        Shapes[I]->SetRelativeRotation(FRotator(0,Now*(I%2?40:-40),0));
    }
}
