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

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    FSocket* Socket = nullptr;
    FTimerHandle SendTimerHandle;

    void ConnectToServer();
    void SendToServer(float X, float Y, float Z);
    void SendPositionTick();
};
