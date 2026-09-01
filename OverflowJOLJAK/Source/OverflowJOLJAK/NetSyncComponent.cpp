#include "NetSyncComponent.h"

#include "GameFramework/Actor.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "TimerManager.h"
#include "..\..\Shared\Protocol.h"

#include "EnemyCharacter.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Projectile.h"

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

void UNetSyncComponent::SendToServer(float X, float Y, float Z, float Pitch, float Yaw, float Roll)
{
    if (!Socket)
    {
        return;
    }

    cs_packet_player_move mp;
    mp.m_size = sizeof(mp);
    mp.m_type = PKT_C2S_PLAYER_MOVE;
    mp.m_x = X;
    mp.m_y = Y;
    mp.m_z = Z;
    mp.m_pitch = Pitch;
    mp.m_yaw = Yaw;
    mp.m_roll = Roll;

    int32 BytesSent = 0;
    if (!Socket->Send(reinterpret_cast<const uint8*>(&mp), sizeof(mp), BytesSent))
    {
        UE_LOG(LogTemp, Warning, TEXT("NetSyncComponent: send failed"));
    }
}

void UNetSyncComponent::SendAttack(const FVector& Origin, const FVector& Direction)
{
    if (!Socket)
    {
        return;
    }

    const FVector Dir = Direction.GetSafeNormal();

    cs_packet_player_attack ap;
    ap.m_size = sizeof(ap);
    ap.m_type = PKT_C2S_PLAYER_ATTACK;
    ap.m_target_monster_id = -1;   // 서버가 안 쓰지만 구조체에 남아있으니 채워둠
    ap.m_origin_x = Origin.X;
    ap.m_origin_y = Origin.Y;
    ap.m_origin_z = Origin.Z;
    ap.m_dir_x = Dir.X;
    ap.m_dir_y = Dir.Y;
    ap.m_dir_z = Dir.Z;

    int32 BytesSent = 0;
    if (!Socket->Send(reinterpret_cast<const uint8*>(&ap), sizeof(ap), BytesSent))
    {
        UE_LOG(LogTemp, Warning, TEXT("NetSyncComponent: attack send failed"));
    }
}

void UNetSyncComponent::SendFireEvent(const FVector& MuzzleLocation, const FVector& Direction)
{
    if (!Socket)
    {
        return;
    }

    const FVector NormalizedDir = Direction.GetSafeNormal();

    cs_packet_player_fire fp;
    fp.m_size = sizeof(fp);
    fp.m_type = PKT_C2S_FIRE;
    fp.m_muzzle_x = MuzzleLocation.X;
    fp.m_muzzle_y = MuzzleLocation.Y;
    fp.m_muzzle_z = MuzzleLocation.Z;
    fp.m_dir_x = NormalizedDir.X;
    fp.m_dir_y = NormalizedDir.Y;
    fp.m_dir_z = NormalizedDir.Z;

    int32 BytesSent = 0;
    if (!Socket->Send(reinterpret_cast<const uint8*>(&fp), sizeof(fp), BytesSent))
    {
        UE_LOG(LogTemp, Warning, TEXT("NetSyncComponent: fire event send failed"));
    }
}

void UNetSyncComponent::SendPositionTick()
{
    if (AActor* Owner = GetOwner())
    {
        const FVector Loc = Owner->GetActorLocation();
        const FRotator Rot = Owner->GetActorRotation();
        SendToServer(Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll);
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
            AddPlayer(Pkt->m_id, Pkt->m_visual, FVector(Pkt->m_x, Pkt->m_y, Pkt->m_z), FRotator(Pkt->m_pitch, Pkt->m_yaw, Pkt->m_roll));
            break;
        }
        case PKT_S2C_PLAYER_POSITION:
        {
            const sc_packet_player_position* Pkt =
                reinterpret_cast<const sc_packet_player_position*>(RecvBuffer.GetData());
            UpdatePosition(Pkt->m_id, FVector(Pkt->m_x, Pkt->m_y, Pkt->m_z), FRotator(Pkt->m_pitch, Pkt->m_yaw, Pkt->m_roll));
            break;
        }
        case PKT_S2C_REMOVE_PLAYER:
        {
            const sc_packet_remove_player* Pkt =
                reinterpret_cast<const sc_packet_remove_player*>(RecvBuffer.GetData());
            RemovePlayer(Pkt->m_id);
            break;
        }
        case PKT_S2C_MONSTER_SPAWN:
        {
            const sc_packet_monster_spawn* Pkt =
                reinterpret_cast<const sc_packet_monster_spawn*>(RecvBuffer.GetData());
            AddMonster(Pkt->m_id, Pkt->m_monster_type, FVector(Pkt->m_x, Pkt->m_y, Pkt->m_z), Pkt->m_hp);
            break;
        }
        case PKT_S2C_MONSTER_POSITION:
        {
            const sc_packet_monster_position* Pkt =
                reinterpret_cast<const sc_packet_monster_position*>(RecvBuffer.GetData());
            UpdateMonsterPosition(Pkt->m_id, FVector(Pkt->m_x, Pkt->m_y, Pkt->m_z));
            break;
        }
        case PKT_S2C_MONSTER_ATTACK:
        {
            const sc_packet_monster_attack* Pkt =
                reinterpret_cast<const sc_packet_monster_attack*>(RecvBuffer.GetData());
            HandleMonsterAttack(Pkt->m_id, Pkt->m_target_id);
            break;
        }
        case PKT_S2C_MONSTER_HP:
        {
            const sc_packet_monster_hp* Pkt =
                reinterpret_cast<const sc_packet_monster_hp*>(RecvBuffer.GetData());
            UpdateMonsterHp(Pkt->m_id, Pkt->m_hp);
            break;
        }
        case PKT_S2C_MONSTER_REMOVE:
        {
            const sc_packet_monster_remove* Pkt =
                reinterpret_cast<const sc_packet_monster_remove*>(RecvBuffer.GetData());
            RemoveMonster(Pkt->m_id);
            break;
        }
        case PKT_S2C_PLAYER_FIRE:
        {
            const sc_packet_player_fire* Pkt =
                reinterpret_cast<const sc_packet_player_fire*>(RecvBuffer.GetData());
            SpawnRemoteFireCosmetic(
                FVector(Pkt->m_muzzle_x, Pkt->m_muzzle_y, Pkt->m_muzzle_z),
                FVector(Pkt->m_dir_x, Pkt->m_dir_y, Pkt->m_dir_z));
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
    InterpolateMonsters(DeltaTime);
}

void UNetSyncComponent::AddPlayer(int32 Id, int32 Visual, const FVector& Location, const FRotator& Rotation)
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
        VisualClasses[Visual], Location, Rotation, Params);

    if (NewActor)
    {
        RemotePlayers.Add(Id, NewActor);
        TargetLocations.Add(Id, Location);
        TargetRotations.Add(Id, Rotation);
        UE_LOG(LogTemp, Log, TEXT("NetSync: player %d spawned"), Id);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("NetSync: SpawnActor FAILED for id %d"), Id);
    }
}

void UNetSyncComponent::UpdatePosition(int32 Id, const FVector& Location, const FRotator& Rotation)
{
    if (Id == MyId) return;

    // 여기서 바로 SetActorLocation 하면 30Hz 계단이 그대로 보인다.
    // 목표만 갱신하고 실제 이동은 InterpolateRemotePlayers()가 매 프레임 처리.
    if (RemotePlayers.Contains(Id))
    {
        TargetLocations.Add(Id, Location);
        TargetRotations.Add(Id, Rotation);
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

void UNetSyncComponent::AddMonster(int32 Id, uint8 MonsterType, const FVector& Location, int32 Hp)
{
    if (Monsters.Contains(Id)) return;   // 이미 스폰된 몬스터면 중복 스폰 방지 (재접속 시 스폰 패킷 다시 받는 경우 등)

    int32 VisualIndex = MonsterType;
    if (!MonsterVisualClasses.IsValidIndex(VisualIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("NetSync: monster type %d not registered, using 0"), MonsterType);
        VisualIndex = 0;
    }
    if (!MonsterVisualClasses.IsValidIndex(VisualIndex) || !MonsterVisualClasses[VisualIndex])
    {
        UE_LOG(LogTemp, Error, TEXT("NetSync: MonsterVisualClasses is empty. Set it in the editor."));
        return;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* NewActor = GetWorld()->SpawnActor<AActor>(
        MonsterVisualClasses[VisualIndex], Location, FRotator::ZeroRotator, Params);

    if (NewActor)
    {
        Monsters.Add(Id, NewActor);
        MonsterTargetLocations.Add(Id, Location);

        // 스폰된 액터가 AEnemyCharacter라면 서버 id와 초기 HP를 심어준다.
        // (공격할 때 어떤 몬스터인지, HP 갱신 이벤트를 어디로 보낼지 이걸로 찾는다)
        if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(NewActor))
        {
            Enemy->EnemyId = Id;
            Enemy->MaxHp = Hp;
            Enemy->CurrentHp = Hp;
        }

        UE_LOG(LogTemp, Log, TEXT("NetSync: monster %d spawned (type %d, hp %d)"), Id, MonsterType, Hp);
        UE_LOG(LogTemp, Log, TEXT("NetSync: monster %f, %f, %f"), Location.X, Location.Y, Location.Z);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("NetSync: SpawnActor FAILED for monster %d"), Id);
    }
}

void UNetSyncComponent::UpdateMonsterPosition(int32 Id, const FVector& Location)
{
    // ADD보다 POSITION이 먼저 오는 경우와 동일하게, 스폰이 아직 안 된 몬스터면 그냥 버린다.
    if (Monsters.Contains(Id))
    {
        MonsterTargetLocations.Add(Id, Location);
    }
}

void UNetSyncComponent::UpdateMonsterHp(int32 Id, int32 NewHp)
{
    if (AActor** Found = Monsters.Find(Id))
    {
        if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(*Found))
        {
            const int32 PreviousHp = Enemy->CurrentHp;
            Enemy->OnHpChanged(NewHp, PreviousHp);
        }
    }
}

void UNetSyncComponent::RemoveMonster(int32 Id)
{
    if (AActor** Found = Monsters.Find(Id))
    {
        if (IsValid(*Found))
        {
            if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(*Found))
            {
                // 사망 연출(애니메이션/파티클)과 실제 Destroy 타이밍은
                // AEnemyCharacter::Die 쪽(블루프린트에서 오버라이드 가능)에 맡긴다.
                Enemy->Die();
            }
            else
            {
                (*Found)->Destroy();
            }
        }

        Monsters.Remove(Id);
        MonsterTargetLocations.Remove(Id);
        UE_LOG(LogTemp, Log, TEXT("NetSync: monster %d removed (died)"), Id);
    }
}

void UNetSyncComponent::HandleMonsterAttack(int32 MonsterId, int32 TargetPlayerId)
{
    // 실제 애니메이션/이펙트/피격 반응은 이 델리게이트를 구독하는 쪽(Blueprint 등)에서 처리.
    OnMonsterAttack.Broadcast(MonsterId, TargetPlayerId);

    // 해당 몬스터 인스턴스를 찾아서 공격 이벤트 직접 호출 -> 애니메이션 재생
    if (AActor** Found = Monsters.Find(MonsterId))
    {
        if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(*Found))
        {
            if (Enemy->CurrentHp <= 0)
            {
                Enemy->Die();   // 이미 죽은 몬스터가 공격 이벤트를 받으면 그냥 Die() 호출
                return;
            }

            Enemy->OnAttack(TargetPlayerId);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("NetSync: monster %d attacked player %d"), MonsterId, TargetPlayerId);
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

            if (const FRotator* TargetRot = TargetRotations.Find(Pair.Key))
            {
                const FRotator Current2 = RemoteChar->GetActorRotation();

                FRotator YawOnly(0.0f, TargetRot->Yaw, 0.0f);

                FRotator Smooth = FMath::RInterpTo(Current2, *TargetRot, DeltaTime, RotationInterpSpeed);
                Smooth.Pitch = 0.0f;   // 보간 중간값도 확실히 막기
                Smooth.Roll = 0.0f;
                RemoteChar->SetActorRotation(Smooth);
            }
        }
    }
}

void UNetSyncComponent::InterpolateMonsters(float DeltaTime)
{
    for (const TPair<int32, FVector>& Pair : MonsterTargetLocations)
    {
        AActor** Found = Monsters.Find(Pair.Key);
        if (!Found || !IsValid(*Found))
        {
            continue;
        }

        ACharacter* MonsterChar = Cast<ACharacter>(*Found);
        if (!MonsterChar)
        {
            continue;
        }

        const FVector Current = MonsterChar->GetActorLocation();

        // 수평 방향만 계산 (Z는 무브먼트 컴포넌트의 중력이 담당)
        FVector ToTarget = Pair.Value - Current;
        ToTarget.Z = 0.f;

        // 목표에 충분히 가까우면 이동 입력 안 줌 (도착 지점에서 떨림 방지)
        if (ToTarget.SizeSquared() > 100.f)   // 10유닛 이상 떨어졌을 때만
        {
            const FVector Direction = ToTarget.GetSafeNormal();
            MonsterChar->AddMovementInput(Direction, 1.0f);

            FRotator NewRot = Direction.Rotation();
            NewRot.Pitch = 0.f;
            NewRot.Roll = 0.f;
            MonsterChar->SetActorRotation(NewRot);
        }
    }
}

void UNetSyncComponent::SpawnRemoteFireCosmetic(const FVector& MuzzleLocation, const FVector& Direction)
{
    if (!RemoteFireProjectileClass)
    {
        return;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AProjectile* Cosmetic = GetWorld()->SpawnActor<AProjectile>(
        RemoteFireProjectileClass, MuzzleLocation, Direction.Rotation(), Params);

    // ShooterCharacter를 일부러 안 채운다 - 이 연출용 총알은 무엇에 맞아도
    // 서버에 공격 보고를 하지 않는다 (원래 쏜 사람이 이미 보고했으므로 중복 방지).
}
