#include "NetSyncComponent.h"

#include "GameFramework/Actor.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "TimerManager.h"
#include "..\..\Shared\Protocol.h"

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
        Owner->GetWorldTimerManager().SetTimer(SendTimerHandle, this, &UNetSyncComponent::SendPositionTick, 1.0f / 30.0f, true);
        Owner->GetWorldTimerManager().SetTimer(RecvTimerHandle, this, &UNetSyncComponent::ReceiveFromServer, 0.03f, true);
    }
}

void UNetSyncComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (AActor* Owner = GetOwner())
    {
        Owner->GetWorldTimerManager().ClearTimer(SendTimerHandle);
        Owner->GetWorldTimerManager().ClearTimer(RecvTimerHandle);
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

    cs_packet_move mp;
    mp.m_size = sizeof(mp);
    mp.m_type = PKT_C2S_MOVE;
    mp.m_x = X;
    mp.m_y = Y;
    mp.m_z = Z;

    int32 BytesSent = 0;
    if (!Socket->Send(reinterpret_cast<const uint8*>(&mp), sizeof(mp), BytesSent))
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

void UNetSyncComponent::ReceiveFromServer()
{
    if (!Socket)
    {
        return;
    }

    // 1. 소켓에 쌓인 데이터를 전부 RecvBuffer로 옮겨오기
    uint32 PendingDataSize = 0;
    while (Socket->HasPendingData(PendingDataSize))
    {
        uint8 Temp[4096];
        int32 BytesRead = 0;

        if (!Socket->Recv(Temp, sizeof(Temp), BytesRead))
        {
            break;
        }

        RecvBuffer.Append(Temp, BytesRead);
    }

    // 2. RecvBuffer에 쌓인 걸 패킷 단위로 잘라서 처리
    while (RecvBuffer.Num() >= sizeof(PACKET_HEADER))
    {
        PACKET_HEADER* Header = reinterpret_cast<PACKET_HEADER*>(RecvBuffer.GetData());

        if (RecvBuffer.Num() < Header->m_size)
        {
            break;
        }

        switch (Header->m_type)
        {
        case PKT_S2C_ADD_PLAYER:
            UE_LOG(LogTemp, Log, TEXT("ADD_PLAYER packet received"));
            break;

        case PKT_S2C_POSITION:
            UE_LOG(LogTemp, Log, TEXT("POSITION packet received"));
            break;

        case PKT_S2C_REMOVE_PLAYER:
            UE_LOG(LogTemp, Log, TEXT("REMOVE_PLAYER packet received"));
            break;

        default:
            break;
        }

        RecvBuffer.RemoveAt(0, Header->m_size);
    }
}