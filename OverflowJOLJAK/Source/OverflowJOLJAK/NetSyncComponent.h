#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NetSyncComponent.generated.h"

class FSocket;

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

    /** 원격 플레이어 보간 속도. 클수록 즉각적이고 딱딱해진다. */
    UPROPERTY(EditAnywhere, Category = "NetSync")
    float InterpSpeed = 12.0f;


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

    // 서버가 알려준 내 ID. 로그인 패킷이 생기기 전까지는 -1.
    int32 MyId = -1;

    void ConnectToServer();
    void ReceiveFromServer();
    void SendToServer(float X, float Y, float Z);
    void SendPositionTick();

    void AddPlayer(int32 Id, int32 Visual, const FVector& Location);
    void UpdatePosition(int32 Id, const FVector& Location);
    void RemovePlayer(int32 Id);

    void InterpolateRemotePlayers(float DeltaTime);
};