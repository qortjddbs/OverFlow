#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EnemyCharacter.generated.h"

// 이 클래스는 네트워크 동기화 로직을 직접 갖지 않는다.
// 원격 스폰/위치 보간은 각 클라이언트의 NetSyncComponent(로컬 플레이어 소유)가
// MonsterVisualClasses 배열을 통해 범용으로 처리한다. (AddMonster / InterpolateMonsters)
//
// HP/사망도 마찬가지 원칙: 데미지 계산은 서버에서만 하고, 이 클래스는 서버가 보내준
// 결과값(OnHpChanged, Die)을 받아서 "어떻게 보여줄지"만 담당한다.
UCLASS()
class OVERFLOWJOLJAK_API AEnemyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AEnemyCharacter();

    // 서버가 부여한 이 몬스터의 고유 id. NetSyncComponent가 스폰 직후 채워준다.
    // 플레이어가 이 몬스터를 공격할 때 서버에 "몇 번 몬스터를 때렸는지" 알려주려면 이 값이 필요하다.
    UPROPERTY(BlueprintReadOnly, Category = "Enemy")
    int32 EnemyId = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Enemy")
    int32 MaxHp = 100;

    UPROPERTY(BlueprintReadOnly, Category = "Enemy")
    int32 CurrentHp = 100;

    // 서버로부터 새 HP를 받았을 때 호출됨 (NetSyncComponent가 호출).
    // 기본 구현은 CurrentHp만 갱신. 블루프린트에서 이 이벤트를 오버라이드해서
    // 피격 이펙트/히트 리액션 애니메이션을 재생하면 된다.
    UFUNCTION(BlueprintNativeEvent, Category = "Enemy")
    void OnHpChanged(int32 NewHp, int32 PreviousHp);
    virtual void OnHpChanged_Implementation(int32 NewHp, int32 PreviousHp);

    // 서버가 이 몬스터의 HP가 0 이하가 되어 제거했다고 알려왔을 때 호출됨.
    // 기본 구현은 즉시 Destroy(). 블루프린트에서 오버라이드해서 사망 애니메이션/파티클을
    // 재생한 뒤 원하는 타이밍에 직접 Destroy Actor를 호출하도록 바꿀 수 있다.
    UFUNCTION(BlueprintNativeEvent, Category = "Enemy")
    void Die();
    virtual void Die_Implementation();

    // 이 몬스터가 공격을 실행했을 때 호출됨. 블루프린트에서 오버라이드해서
    // 공격 애니메이션 몽타주를 재생하면 된다.
    UFUNCTION(BlueprintNativeEvent, Category = "Enemy")
    void OnAttack(int32 TargetPlayerId);
    virtual void OnAttack_Implementation(int32 TargetPlayerId);

protected:
    virtual void BeginPlay() override;
};