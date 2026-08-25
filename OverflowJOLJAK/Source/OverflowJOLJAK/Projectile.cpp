#include "Projectile.h"

#include "EnemyCharacter.h"
#include "NetSyncComponent.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/ProjectileMovementComponent.h"

AProjectile::AProjectile()
{
    PrimaryActorTick.bCanEverTick = false;

    CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
    CollisionComp->InitSphereRadius(8.f);
    CollisionComp->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    CollisionComp->SetNotifyRigidBodyCollision(true);   // OnHit 델리게이트를 받으려면 필요 (= Simulation Generates Hit Events)
    RootComponent = CollisionComp;

    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    MeshComp->SetupAttachment(CollisionComp);
    // 실제 충돌 판정은 CollisionComp가 전담. 메쉬는 순수 비주얼이라 충돌을 꺼둔다.
    // (안 끄면 메쉬와 구체가 각각 충돌해서 OnHit이 두 번 불리거나 이상하게 튕긴다)
    MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
    ProjectileMovement->UpdatedComponent = CollisionComp;
    ProjectileMovement->InitialSpeed = 4000.f;
    ProjectileMovement->MaxSpeed = 4000.f;
    ProjectileMovement->bRotationFollowsVelocity = true;
    ProjectileMovement->ProjectileGravityScale = 0.f;   // 직선 비행 (중력 낙하 원하면 0.1~0.3 정도로)

    InitialLifeSpan = 3.f;   // 아무것도 안 맞으면 3초 뒤 자동 소멸 (총알이 무한히 쌓이는 것 방지)
}

void AProjectile::BeginPlay()
{
    Super::BeginPlay();

    CollisionComp->OnComponentHit.AddDynamic(this, &AProjectile::OnHit);

    if (ShooterCharacter)
    {
        // 총구가 캐릭터 몸에 가까워서 발사 즉시 자기 자신에게 맞는 걸 방지
        CollisionComp->IgnoreActorWhenMoving(ShooterCharacter, true);
    }
}

void AProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
    FVector NormalImpulse, const FHitResult& Hit)
{
    if (!OtherActor || OtherActor == this || OtherActor == ShooterCharacter)
    {
        return;
    }

    // 피격 연출은 무엇에 맞았든 재생 (벽에 맞아도 스파크는 나와야 하니까)
    OnProjectileHit(Hit.ImpactPoint, OtherActor);

    //===============================================================================
    // 총알 계산을 클라에서 할경우
    //// 지금은 몬스터만 데미지 대상. 벽/다른 플레이어를 맞혔으면 총알만 사라진다.
    //if (AEnemyCharacter* HitEnemy = Cast<AEnemyCharacter>(OtherActor))
    //{
    //    if (ShooterCharacter)
    //    {
    //        if (UNetSyncComponent* NetSync = ShooterCharacter->FindComponentByClass<UNetSyncComponent>())
    //        {
    //            // 총알이 태어난 위치(발사 원점)와 진행 방향을 같이 보낸다
    //            const FVector Origin = GetActorLocation();          // 맞은 지점 근처지만, 원점 개념으로 충분
    //            const FVector Direction = GetVelocity().GetSafeNormal();
    //            NetSync->SendAttack(HitEnemy->EnemyId, Origin, Direction);
    //            UE_LOG(LogTemp, Log, TEXT("Projectile hit enemy %d, reported to server"), HitEnemy->EnemyId);
    //        }
    //    }
    //}
    //===============================================================================

    Destroy();
}