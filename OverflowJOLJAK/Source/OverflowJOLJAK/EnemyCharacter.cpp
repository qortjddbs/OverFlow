#include "EnemyCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

AEnemyCharacter::AEnemyCharacter()
{
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = false;
}

void AEnemyCharacter::BeginPlay()
{
    Super::BeginPlay();
    // 원격 스폰 액터는 컨트롤러가 없으므로 별도 초기화 불필요.
    CurrentHp = MaxHp;
}

void AEnemyCharacter::OnHpChanged_Implementation(int32 NewHp, int32 PreviousHp)
{
    CurrentHp = NewHp;
    // 기본 구현은 값만 갱신. 피격 이펙트/애니메이션이 필요하면
    // 블루프린트에서 이 이벤트(OnHpChanged)를 오버라이드해서 추가하면 된다.
}

void AEnemyCharacter::Die_Implementation()
{
    // 기본 구현은 즉시 제거. 사망 연출이 필요하면 블루프린트에서
    // 이 이벤트(Die)를 오버라이드해서 애니메이션/파티클 재생 후 Destroy Actor를 호출하도록 바꾸면 된다.
    Destroy();
}