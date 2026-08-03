#include "NetSyncComponent.h"

#include "GameFramework/Actor.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "TimerManager.h"
#include "..\..\Shared\Protocol.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UNetSyncComponent::UNetSyncComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UNetSyncComponent::BeginPlay()
{
    Super::BeginPlay();

    // 원격 플레이어 표시용으로 스폰된 복제본은 컨트롤러가 없다 -> 접속하지 않는다
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
    {
        return;
    }

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
        UE_LOG(LogTemp, Warning, TEXT("RECV %d bytes"), BytesRead);
    }

    // 2. RecvBuffer에 쌓인 걸 패킷 단위로 잘라서 처리
    while (RecvBuffer.Num() >= sizeof(PACKET_HEADER))
    {
        PACKET_HEADER* Header = reinterpret_cast<PACKET_HEADER*>(RecvBuffer.GetData());

        if (RecvBuffer.Num() < Header->m_size)
        {
            break;
        }

        UE_LOG(LogTemp, Warning, TEXT("PKT type=%d size=%d"), Header->m_type, Header->m_size);

        switch (Header->m_type)
        {
        case PKT_S2C_ADD_PLAYER:
        {
            const sc_packet_add_player* Pkt =
                reinterpret_cast<const sc_packet_add_player*>(RecvBuffer.GetData());
            AddPlayer(Pkt->m_id, Pkt->m_visual, FVector(Pkt->m_x, Pkt->m_y, Pkt->m_z));
            break;
        }
        case PKT_S2C_POSITION:
        {
            const sc_packet_position* Pkt =
                reinterpret_cast<const sc_packet_position*>(RecvBuffer.GetData());
            UpdatePosition(Pkt->m_id, FVector(Pkt->m_x, Pkt->m_y, Pkt->m_z));
            break;
        }
        case PKT_S2C_REMOVE_PLAYER:
        {
            const sc_packet_remove_player* Pkt =
                reinterpret_cast<const sc_packet_remove_player*>(RecvBuffer.GetData());
            RemovePlayer(Pkt->m_id);
            break;
        }

        default:
            break;
        }

        RecvBuffer.RemoveAt(0, Header->m_size);
    }
}

void UNetSyncComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    UE_LOG(LogTemp, Warning, TEXT("TICK ALIVE"));
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    ReceiveFromServer();
    InterpolateRemotePlayers(DeltaTime);
}

void UNetSyncComponent::AddPlayer(int32 Id, int32 Visual, const FVector& Location)
{
    UE_LOG(LogTemp, Warning, TEXT("ADDPLAYER CALLED id=%d visual=%d"), Id, Visual);

    if (Id == MyId) return;                     // 내 자신은 스폰하지 않는다
    if (RemotePlayers.Contains(Id)) return;     // 중복 방어

    if (!VisualClasses.IsValidIndex(Visual))
    {
        UE_LOG(LogTemp, Warning, TEXT("NetSync: visual %d not registered, using 0"), Visual);
        Visual = 0;
    }
    if (!VisualClasses.IsValidIndex(Visual) || !VisualClasses[Visual])
    {
        UE_LOG(LogTemp, Error, TEXT("NetSync: VisualClasses is empty. Set it in the editor."));
        return;
    }

    FActorSpawnParameters Params;
    // 시작 위치가 겹쳐도 스폰이 조용히 실패하지 않도록.
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* NewActor = GetWorld()->SpawnActor<AActor>(
        VisualClasses[Visual], Location, FRotator::ZeroRotator, Params);

    if (NewActor)
    {
        RemotePlayers.Add(Id, NewActor);
        TargetLocations.Add(Id, Location);
        UE_LOG(LogTemp, Log, TEXT("NetSync: player %d spawned"), Id);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("NetSync: SpawnActor FAILED for id %d"), Id);
    }
}

void UNetSyncComponent::UpdatePosition(int32 Id, const FVector& Location)
{
    if (Id == MyId) return;

    // 여기서 바로 SetActorLocation 하면 30Hz 계단이 그대로 보인다.
    // 목표만 갱신하고 실제 이동은 InterpolateRemotePlayers()가 매 프레임 처리.
    if (RemotePlayers.Contains(Id))
    {
        TargetLocations.Add(Id, Location);
    }
    // ADD_PLAYER보다 POSITION이 먼저 도착하는 경우가 실제로 있다. 그냥 버린다.
}

void UNetSyncComponent::RemovePlayer(int32 Id)
{
    if (AActor** Found = RemotePlayers.Find(Id))
    {
        if (IsValid(*Found))
        {
            (*Found)->Destroy();
        }
        RemotePlayers.Remove(Id);
        TargetLocations.Remove(Id);
        UE_LOG(LogTemp, Log, TEXT("NetSync: player %d removed"), Id);
    }
}

void UNetSyncComponent::InterpolateRemotePlayers(float DeltaTime)
{
    for (const TPair<int32, FVector>& Pair : TargetLocations)
    {
        AActor** Found = RemotePlayers.Find(Pair.Key);
        if (!Found || !IsValid(*Found))
        {
            continue;
        }

        const FVector Current = (*Found)->GetActorLocation();
        const FVector Next = FMath::VInterpTo(Current, Pair.Value, DeltaTime, InterpSpeed);
        (*Found)->SetActorLocation(Next);

        const FVector Velocity = (Next - Current) / DeltaTime;

        if (ACharacter* RemoteChar = Cast<ACharacter>(*Found))
        {
            if (UCharacterMovementComponent* Move = RemoteChar->GetCharacterMovement())
            {
                Move->Velocity = Velocity;
            }

            if (!Velocity.IsNearlyZero(1.0f))
            {
                FRotator NewRot = Velocity.Rotation();
                NewRot.Pitch = 0.0f;
                NewRot.Roll = 0.0f;
                RemoteChar->SetActorRotation(NewRot);
            }
        }
    }
}
