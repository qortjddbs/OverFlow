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

    // 서버(server.cpp)의 size(2)+type(1) 헤더 포맷에 맞춘 패킷.
    // Type = 1 은 서버 쪽 PKT_CS_MOVE 와 맞춰야 한다.
    #pragma pack(push, 1)
    struct
    {
        uint16 Size;
        uint8 Type;
        float X, Y, Z;
    } Packet;
    #pragma pack(pop)

    Packet.Size = sizeof(Packet);
    Packet.Type = 1; // PKT_CS_MOVE
    Packet.X = X;
    Packet.Y = Y;
    Packet.Z = Z;

    int32 BytesSent = 0;
    if (!Socket->Send(reinterpret_cast<const uint8*>(&Packet), sizeof(Packet), BytesSent))
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
