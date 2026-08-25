#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NetSyncComponent.generated.h"

class FSocket;

class AProjectile;

// 몬스터가 공격을 실행했을 때 브로드캐스트되는 델리게이트.
// AEnemyCharacter나 플레이어 쪽에서 바인딩해서 공격 애니메이션/이펙트/피격 반응 등을 처리하면 된다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMonsterAttack, int32, MonsterId, int32, TargetPlayerId);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OVERFLOWJOLJAK_API UNetSyncComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNetSyncComponent();

    UPROPERTY(EditAnywhere, Category = "NetSync")
    FString ServerIP = TEXT("127.0.0.1");       // server.cpp

    UPROPERTY(EditAnywhere, Category = "NetSync")
    int32 ServerPort = 7777;                    // server.cpp

    /** m_visual 인덱스 -> 스폰할 클래스. 에디터에서 순서대로 채워넣을 것. [0]=젤다 ... */
    UPROPERTY(EditAnywhere, Category = "NetSync")
    TArray<TSubclassOf<AActor>> VisualClasses;

    /** m_monster_type 인덱스 -> 스폰할 몬스터 클래스. 예: [0]=BP_Enemy */
    UPROPERTY(EditAnywhere, Category = "NetSync")
    TArray<TSubclassOf<AActor>> MonsterVisualClasses;

    /** 원격 플레이어 보간 속도. 클수록 즉각적이고 딱딱해진다. */
    UPROPERTY(EditAnywhere, Category = "NetSync")
    float InterpSpeed = 12.0f;

    /** 몬스터 보간 속도. 서버 틱이 플레이어보다 느리면(10Hz 등) 값을 낮춰서 더 부드럽게 보이게 조절 가능. */
    UPROPERTY(EditAnywhere, Category = "NetSync")
    float MonsterInterpSpeed = 8.0f;

    /** 몬스터가 공격했을 때 브로드캐스트. Blueprint에서 바인딩해서 연출 처리. */
    UPROPERTY(BlueprintAssignable, Category = "NetSync")
    FOnMonsterAttack OnMonsterAttack;

    /** 내 캐릭터가 특정 몬스터를 공격했다고 서버에 알린다. 실제 데미지/사거리 판정은 서버가 함. */
    UFUNCTION(BlueprintCallable, Category = "NetSync")
    void SendAttack(int32 TargetMonsterId);

    UFUNCTION(BlueprintCallable, Category = "NetSync")
    void SendFireEvent(const FVector& MuzzleLocation, const FVector& Direction);

    UPROPERTY(EditAnywhere, Category = "NetSync")
    TSubclassOf<AProjectile> RemoteFireProjectileClass;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    FSocket* Socket = nullptr;
    FTimerHandle SendTimerHandle;
    FTimerHandle RecvTimerHandle;
    TArray<uint8> RecvBuffer;

    // GC가 스폰한 액터를 수거해가지 않도록 UPROPERTY 필수
    UPROPERTY()
    TMap<int32, AActor*> RemotePlayers;

    // 30Hz로 띄엄띄엄 오는 좌표. 매 프레임 여기로 보간해 따라간다.
    TMap<int32, FVector> TargetLocations;

    // 몬스터는 플레이어와 완전히 별개의 id 네임스페이스라서 맵도 따로 관리한다.
    UPROPERTY()
    TMap<int32, AActor*> Monsters;

    TMap<int32, FVector> MonsterTargetLocations;

    // 서버가 알려준 내 ID. 로그인 패킷이 생기기 전까지는 -1.
    int32 MyId = -1;

    void ConnectToServer();
    void ReceiveFromServer();
    void SendToServer(float X, float Y, float Z);
    void SendPositionTick();

    void AddPlayer(int32 Id, int32 Visual, const FVector& Location);
    void UpdatePosition(int32 Id, const FVector& Location);
    void RemovePlayer(int32 Id);

    void AddMonster(int32 Id, uint8 MonsterType, const FVector& Location, int32 Hp);
    void UpdateMonsterPosition(int32 Id, const FVector& Location);
    void UpdateMonsterHp(int32 Id, int32 NewHp);
    void RemoveMonster(int32 Id);
    void HandleMonsterAttack(int32 MonsterId, int32 TargetPlayerId);

    void InterpolateRemotePlayers(float DeltaTime);
    void InterpolateMonsters(float DeltaTime);

    void SpawnRemoteFireCosmetic(const FVector& MuzzleLocation, const FVector& Direction);
};