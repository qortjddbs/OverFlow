#include "NetSyncComponent.h"

#include "GameFramework/Actor.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "TimerManager.h"

UNetSyncComponent::UNetSyncComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNetSyncComponent::BeginPlay()
{
    Super::BeginPlay();

    ConnectToServer();

    if (AActor* Owner = GetOwner())
    {
        Owner->GetWorldTimerManager().SetTimer(SendTimerHandle, this, &UNetSyncComponent::SendPositionTick, 1.0f, true);
    }
}

void UNetSyncComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (AActor* Owner = GetOwner())
    {
        Owner->GetWorldTimerManager().ClearTimer(SendTimerHandle);
    }

    if (Socket)
    {
        Socket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
        Socket = nullptr;
    }

    Super::EndPlay(EndPlayReason);
}

void UNetSyncComponent::ConnectToServer()
{
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        return;
    }

    Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("NetSyncSocket"), false);
    if (!Socket)
    {
        return;
    }

    TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    bool bIsValidIp = false;
    Addr->SetIp(*ServerIP, bIsValidIp);
    Addr->SetPort(ServerPort);

    // NOTE: Connect() blocks the game thread until the OS-level connect
    // succeeds or times out. Fine for this test component; a production
    // version would connect asynchronously.
    if (!bIsValidIp || !Socket->Connect(*Addr))
    {
        UE_LOG(LogTemp, Warning, TEXT("NetSyncComponent: failed to connect to %s:%d"), *ServerIP, ServerPort);
        SocketSubsystem->DestroySocket(Socket);
        Socket = nullptr;
    }
}

void UNetSyncComponent::SendToServer(float X, float Y, float Z)
{
    if (!Socket)
    {
        return;
    }

    // Raw 12 bytes, no framing - matches main.cpp's HandlePacketBytes(),
    // which memcpy's 3 floats straight off the wire.
    const float Payload[3] = { X, Y, Z };
    int32 BytesSent = 0;
    if (!Socket->Send(reinterpret_cast<const uint8*>(Payload), sizeof(Payload), BytesSent))
    {
        UE_LOG(LogTemp, Warning, TEXT("NetSyncComponent: send failed"));
    }
}

void UNetSyncComponent::SendPositionTick()
{
    if (AActor* Owner = GetOwner())
    {
        const FVector Loc = Owner->GetActorLocation();
        SendToServer(Loc.X, Loc.Y, Loc.Z);
    }
}
