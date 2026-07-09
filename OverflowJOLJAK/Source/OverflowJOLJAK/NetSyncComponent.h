// Minimal test component: connects to the OverFlow position-sync server
// (main.cpp) and sends this actor's location once per second, matching the
// server's expected wire format (12 raw bytes = 3 floats, no framing).

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

    // Must match the server's listen address/port (main.cpp defaults to 7777).
    UPROPERTY(EditAnywhere, Category = "NetSync")
    FString ServerIP = TEXT("127.0.0.1");

    UPROPERTY(EditAnywhere, Category = "NetSync")
    int32 ServerPort = 7777;

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
