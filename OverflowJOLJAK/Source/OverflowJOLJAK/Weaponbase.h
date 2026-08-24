#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeaponBase.generated.h"

class UStaticMeshComponent;
class AProjectile;

// 무기의 동작 모드. 같은 무기를 키 입력으로 전환해 쓰는 개념이라
// 무기 종류별 상속(BP_Rifle, BP_Shotgun...)과는 별개의 축이다.
// 새 모드가 생기면 여기 값을 추가하고 OnPrimaryAction()의 switch에 케이스를 늘리면 된다.
UENUM(BlueprintType)
enum class EWeaponMode : uint8
{
    Attack  UMETA(DisplayName = "Attack"),
    // 예정: Build, Scan 등
};

// 무기 공용 베이스 클래스. 발사 로직(조준 계산, 쿨다운, 총알 스폰)만 여기 있고,
// 실제 총 모델/이펙트/사운드는 이 클래스를 상속한 블루프린트에서 채운다.
//
// 타격 판정은 이 클래스가 아니라 날아가는 총알(AProjectile)이 담당한다.
// 여기서 하는 라인 트레이스는 "총알을 어느 방향으로 쏠지" 목표점을 잡기 위한 용도일 뿐이다.
UCLASS()
class OVERFLOWJOLJAK_API AWeaponBase : public AActor
{
    GENERATED_BODY()

public:
    AWeaponBase();

    // ===== 모드 =====

    // 현재 모드. 플레이어 입력(주 액션)이 들어오면 이 값에 따라 다른 동작을 한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Mode")
    EWeaponMode CurrentMode = EWeaponMode::Attack;

    // 주 액션(왼쪽 클릭) 진입점. 캐릭터는 이 함수만 호출하고,
    // 실제로 무슨 일이 일어날지는 현재 모드가 결정한다.
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void OnPrimaryAction();

    UFUNCTION(BlueprintCallable, Category = "Weapon|Mode")
    void SetMode(EWeaponMode NewMode);

    // 모드 전환 키를 하나만 두고 순환시키고 싶을 때 사용.
    UFUNCTION(BlueprintCallable, Category = "Weapon|Mode")
    void CycleMode();

    // 모드가 바뀐 직후 호출됨. 블루프린트에서 오버라이드해서
    // 무기 외형 변경, UI 갱신, 전환 사운드 등을 처리하면 된다.
    UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Mode")
    void OnModeChanged(EWeaponMode NewMode, EWeaponMode PreviousMode);

    // ===== 공격 모드 =====

    // 발사. 카메라 중앙에서 트레이스해 목표점을 구하고, 총구 소켓에서 그 방향으로 총알을 스폰한다.
    // (카메라에서 목표점을 잡는 이유: 화면 중앙 조준점과 실제 탄착점을 일치시키기 위해.
    //  총구에서 쏘는 이유: 총알이 총에서 나가는 게 시각적으로 자연스럽기 때문.)
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void Fire();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
    UStaticMeshComponent* WeaponMesh;

    // 발사할 총알 클래스. 에디터에서 BP_Projectile 등을 지정한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
    TSubclassOf<AProjectile> ProjectileClass;

    // 총구 위치를 나타내는 무기 메쉬의 소켓 이름. 총알이 여기서 스폰된다.
    UPROPERTY(EditAnywhere, Category = "Weapon")
    FName MuzzleSocketName = TEXT("Muzzle");

    // 조준 트레이스 최대 거리. 이 안에 아무것도 없으면 트레이스 끝점을 목표로 삼는다.
    UPROPERTY(EditAnywhere, Category = "Weapon")
    float AimTraceRange = 10000.f;

    // 총구와 목표점이 이 거리보다 가까우면 방향 계산이 불안정해지므로,
    // 그땐 목표점 대신 카메라 정면 방향으로 발사한다.
    UPROPERTY(EditAnywhere, Category = "Weapon")
    float MinAimDistance = 100.f;

    // 클라이언트 쪽 1차 연사 제한. 서버도 PLAYER_ATTACK_COOLDOWN_MS로 한 번 더 검증한다.
    UPROPERTY(EditAnywhere, Category = "Weapon")
    float FireCooldown = 0.2f;

    // 발사가 실제로 일어났을 때(쿨다운 통과) 호출됨.
    // 블루프린트에서 오버라이드해서 총구 화염, 발사음, 반동 등을 재생하면 된다.
    UFUNCTION(BlueprintImplementableEvent, Category = "Weapon")
    void OnFireEffects(const FVector& MuzzleLocation, const FVector& TargetLocation);

protected:
    virtual void BeginPlay() override;

private:
    float LastFireTime = -1000.f;
};