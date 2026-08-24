#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Projectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

// 실제로 날아가는 총알. 총구에서 스폰되어 목표 방향으로 직선 비행하다가
// AEnemyCharacter에 맞으면 발사자의 NetSyncComponent를 통해 서버에 "때렸다"고 보고한다.
//
// 데미지 계산과 최종 유효성 판정은 서버(handle_player_attack)가 하므로,
// 이 클래스는 "무엇에 맞았는지"만 판단해서 보고할 뿐이다.
UCLASS()
class OVERFLOWJOLJAK_API AProjectile : public AActor
{
    GENERATED_BODY()

public:
    AProjectile();

    // 발사한 캐릭터. 자기 자신에게 맞는 걸 막고, SendAttack을 보낼 NetSyncComponent를 찾는 데 쓴다.
    // WeaponBase::Fire()가 스폰 직후 설정해준다.
    UPROPERTY()
    ACharacter* ShooterCharacter = nullptr;

    // 충돌 판정용 구체. 총알 크기가 작아 빠르게 날아가면 관통(터널링)할 수 있어
    // ProjectileMovement의 bSweepCollision(기본 켜짐)에 의존한다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    USphereComponent* CollisionComp;

    // 눈에 보이는 총알 구체. 블루프린트에서 메쉬/머티리얼을 지정하면 된다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    UStaticMeshComponent* MeshComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    UProjectileMovementComponent* ProjectileMovement;

    // 총알이 무언가에 맞았을 때 호출됨. 블루프린트에서 오버라이드해서
    // 피격 파티클, 사운드, 데칼 등을 재생하면 된다. (기본 구현은 아무것도 안 함)
    UFUNCTION(BlueprintImplementableEvent, Category = "Projectile")
    void OnProjectileHit(const FVector& HitLocation, AActor* HitActor);

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
        FVector NormalImpulse, const FHitResult& Hit);
};