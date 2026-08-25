#include "WeaponBase.h"

#include "Projectile.h"
#include "NetSyncComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"

AWeaponBase::AWeaponBase()
{
    WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
    RootComponent = WeaponMesh;

    // 무기는 순수 비주얼 부착물이라 캐릭터/월드와 물리적으로 부딪히면 안 된다.
    // 이걸 안 끄면 캐릭터 소켓에 붙는 순간 무기 메쉬가 캐릭터 캡슐과 계속 충돌 판정을 일으켜서
    // 캐릭터가 이상하게 밀리거나 아예 못 움직이게 된다.
    WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
}

void AWeaponBase::BeginPlay()
{
    Super::BeginPlay();
}

void AWeaponBase::OnPrimaryAction()
{
    // 캐릭터는 이 함수만 부르고, 무슨 일이 일어날지는 여기서 모드가 결정한다.
    // 새 모드를 추가하면 여기 케이스를 늘리면 되고, Fire()는 건드릴 필요가 없다.
    switch (CurrentMode)
    {
    case EWeaponMode::Attack:
        Fire();
        break;

    default:
        UE_LOG(LogTemp, Warning, TEXT("WeaponBase: unhandled weapon mode"));
        break;
    }
}

void AWeaponBase::SetMode(EWeaponMode NewMode)
{
    if (CurrentMode == NewMode)
    {
        return;
    }

    const EWeaponMode PreviousMode = CurrentMode;
    CurrentMode = NewMode;

    OnModeChanged(NewMode, PreviousMode);
}

void AWeaponBase::CycleMode()
{
    // 모드가 하나뿐인 지금은 아무 일도 일어나지 않는다.
    // 모드를 추가하면 enum 순서대로 순환한다. (마지막 다음은 처음으로)
    constexpr uint8 ModeCount = static_cast<uint8>(EWeaponMode::Attack) + 1;

    const uint8 Next = (static_cast<uint8>(CurrentMode) + 1) % ModeCount;
    SetMode(static_cast<EWeaponMode>(Next));
}

void AWeaponBase::Fire()
{
    // 1. 연사 제한
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now - LastFireTime < FireCooldown)
    {
        return;
    }
    LastFireTime = Now;

    ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
    if (!OwnerChar)
    {
        UE_LOG(LogTemp, Warning, TEXT("WeaponBase: no owner character"));
        return;
    }

    if (!ProjectileClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("WeaponBase: ProjectileClass is not set in the editor"));
        return;
    }

    UCameraComponent* Camera = OwnerChar->FindComponentByClass<UCameraComponent>();
    if (!Camera)
    {
        UE_LOG(LogTemp, Warning, TEXT("WeaponBase: owner has no CameraComponent, cannot aim"));
        return;
    }

    // 2. 조준 목표점 계산 - 화면 중앙(카메라)에서 트레이스.
    //    맞은 게 있으면 그 지점, 없으면 최대 사거리 끝점이 목표가 된다.
    const FVector CameraStart = Camera->GetComponentLocation();
    const FVector CameraEnd = CameraStart + Camera->GetForwardVector() * AimTraceRange;

    FCollisionQueryParams TraceParams;
    TraceParams.AddIgnoredActor(OwnerChar);
    TraceParams.AddIgnoredActor(this);

    FHitResult AimHit;
    const bool bAimHit = GetWorld()->LineTraceSingleByChannel(
        AimHit, CameraStart, CameraEnd, ECC_Visibility, TraceParams);

    const FVector TargetPoint = bAimHit ? AimHit.ImpactPoint : CameraEnd;

    // 3. 총구 위치에서 목표점을 향하는 방향으로 총알 발사.
    //    소켓이 없으면 무기 액터 위치로 대체(경고 로그 출력).
    FVector MuzzleLocation = GetActorLocation();
    if (WeaponMesh->DoesSocketExist(MuzzleSocketName))
    {
        MuzzleLocation = WeaponMesh->GetSocketLocation(MuzzleSocketName);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("WeaponBase: muzzle socket '%s' not found on weapon mesh"),
            *MuzzleSocketName.ToString());
    }

    // 목표점이 총구에 너무 가까우면 (TargetPoint - MuzzleLocation)이 거의 0 벡터가 되어
    // GetSafeNormal()이 영벡터를 돌려주고, 총알이 방향을 못 잡아 제자리에 서버린다.
    // (예: 발밑 바닥을 정면으로 조준했을 때)
    // 이 경우엔 그냥 카메라가 보는 방향으로 쏜다 - 어차피 코앞이라 궤적 차이가 보이지 않는다.
    FVector FireDirection = TargetPoint - MuzzleLocation;
    if (FireDirection.SizeSquared() < MinAimDistance * MinAimDistance)
    {
        FireDirection = Camera->GetForwardVector();
    }
    else
    {
        FireDirection = FireDirection.GetSafeNormal();
    }

    const FRotator FireRotation = FireDirection.Rotation();

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = OwnerChar;
    SpawnParams.Instigator = OwnerChar;
    // 총구가 벽에 파묻힌 상태에서도 총알이 조용히 안 나오는 일이 없도록
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AProjectile* Bullet = GetWorld()->SpawnActor<AProjectile>(
        ProjectileClass, MuzzleLocation, FireRotation, SpawnParams);

    if (Bullet)
    {
        // 총알이 자기 자신에게 맞는 걸 막고, 맞았을 때 서버에 보고할 경로를 알려준다.
        Bullet->ShooterCharacter = OwnerChar;
    }

    // 다른 플레이어들도 이 총알을 볼 수 있게 발사 사실을 서버에 알린다 (순수 연출용 - 데미지 판정과 무관).
    if (UNetSyncComponent* NetSync = OwnerChar->FindComponentByClass<UNetSyncComponent>())
    {
        NetSync->SendFireEvent(MuzzleLocation, FireDirection);
    }

    OnFireEffects(MuzzleLocation, TargetPoint);
}