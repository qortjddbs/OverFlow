#include "MyCharacter.h"
#include "WeaponBase.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NetSyncComponent.h"                               // 추가

#include "VoxelWorld.h"
#include "VoxelTools/Gen/VoxelSphereTools.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Blueprint/UserWidget.h"

#include "Components/CapsuleComponent.h"


// 2026-10-13 
#include "Engine/LocalPlayer.h"
#include "Math/RotationMatrix.h"

// Sets default values
AMyCharacter::AMyCharacter()
{
	//bUseControllerRotationYaw = false;

	//GetCharacterMovement()->bOrientRotationToMovement = true;
	//GetCharacterMovement()->RotationRate = FRotator(0.f, 720.f, 0.f);
	//GetCharacterMovement()->MaxWalkSpeed = 500.f;

    // 2026-10-13
    // Tick 함수가 실행될 수 있도록 설정
    PrimaryActorTick.bCanEverTick = true;

    // 몸의 회전은 행성 기준으로 직접 처리
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;

    UCharacterMovementComponent* Movement = GetCharacterMovement();

    Movement->bOrientRotationToMovement = false;
    Movement->bUseControllerDesiredRotation = false;

    Movement->GravityScale = 1.f;
    Movement->MaxWalkSpeed = 500.f;
    Movement->JumpZVelocity = 500.f;
    Movement->AirControl = 0.35f;
    // ===============================================

	NetSyncComponent = CreateDefaultSubobject<UNetSyncComponent>(TEXT("NetSyncComponent"));     // 추가

    VoxelInvoker = CreateDefaultSubobject<UVoxelSimpleInvokerComponent>(TEXT("VoxelInvoker"));  // 추가
    VoxelInvoker->SetupAttachment(RootComponent);
}

// Called when the game starts or when spawned
void AMyCharacter::BeginPlay()
{
    Super::BeginPlay();

	// 2026-10-13 
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;

    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->bUseControllerDesiredRotation = false;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 720.f, 0.f);

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
            {
                Subsystem->AddMappingContext(IMC_Link, 0);
            }
        }
    }

    if (WeaponClass)
    {
        FActorSpawnParameters Params;
        Params.Owner = this;   // WeaponBase::Fire()가 Cast<ACharacter>(GetOwner())로 소유자를 찾으므로 필수

        if (AActor* SpawnedWeapon = GetWorld()->SpawnActor<AActor>(WeaponClass, Params))
        {
            EquipWeapon(SpawnedWeapon);
        }
    }

    // 2026-10-13 BP_Astronaut에 이미 있는 SpringArm과 Camera를 찾아 설정
    // BP_Astronaut에 이미 있는 SpringArm을 찾습니다.
    PlanetCameraArm = FindComponentByClass<USpringArmComponent>();

    if (PlanetCameraArm)
    {
        PlanetCameraArm->bUsePawnControlRotation = false;

        // 캐릭터의 회전을 따라가도록 합니다.
        PlanetCameraArm->SetUsingAbsoluteRotation(true);
        PlanetCameraArm->bInheritPitch = true;
        PlanetCameraArm->bInheritYaw = true;
        PlanetCameraArm->bInheritRoll = true;

        // 확인할 때는 카메라 지연을 끔
        PlanetCameraArm->bEnableCameraLag = false;
        PlanetCameraArm->bEnableCameraRotationLag = false;
       
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Planet movement: SpringArm component was not found.")
        );
    }

    if (UCameraComponent* Camera =
        FindComponentByClass<UCameraComponent>())
    {
        Camera->bUsePawnControlRotation = false;
    }

    if (IsLocallyControlled())
    {
        UpdatePlanetOrientation();
    }
}

// 2026-10-13 Tick, UpdatePlanetOrientation() 추가
void AMyCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 현재 직접 조작하는 캐릭터만 갱신
    if (!IsLocallyControlled())
    {
        return;
    }

    UpdatePlanetOrientation();
}
void AMyCharacter::UpdatePlanetOrientation()
{
    const FVector FromCenter = GetActorLocation() - PlanetCenter;

    if (FromCenter.IsNearlyZero())
    {
        return;
    }

    const FVector NewUp = FromCenter.GetSafeNormal();

    if (!bPlanetViewInitialized)
    {
        // 처음에는 현재 시점 방향을 사용
        const FVector InitialForward = Controller ? Controller->GetControlRotation().Vector() : GetActorForwardVector();

        PlanetViewForward =
            FVector::VectorPlaneProject(
                InitialForward,
                NewUp
            ).GetSafeNormal();

        if (PlanetViewForward.IsNearlyZero())
        {
            FVector UnusedAxis;
            NewUp.FindBestAxisVectors(
                PlanetViewForward,
                UnusedAxis
            );
        }

        bPlanetViewInitialized = true;
    }
    else
    {
        // 행성 표면을 따라 이동한 만큼 카메라 기준을 보정
        const FQuat SurfaceChange = FQuat::FindBetweenNormals(PlanetUp, NewUp);

        PlanetViewForward = SurfaceChange.RotateVector(PlanetViewForward);

        PlanetViewForward = FVector::VectorPlaneProject(PlanetViewForward, NewUp).GetSafeNormal();

        if (PlanetViewForward.IsNearlyZero())
        {
            FVector UnusedAxis;
            NewUp.FindBestAxisVectors(PlanetViewForward,UnusedAxis);
        }
    }

    PlanetUp = NewUp;

    GetCharacterMovement()->SetGravityDirection(-PlanetUp);

    UpdatePlanetCamera();
}

void AMyCharacter::UpdatePlanetCamera()
{
    if (!bPlanetViewInitialized)
    {
        return;
    }

    const FQuat SurfaceView = FRotationMatrix::MakeFromXZ(PlanetViewForward, PlanetUp).ToQuat();

    if (PlanetCameraArm)
    {
        const FQuat PitchRotation = FRotator(PlanetCameraPitch, 0.f, 0.f).Quaternion();

        PlanetCameraArm->SetWorldRotation(SurfaceView * PitchRotation);
    }

    // 조준 중에만 몸이 시점 방향을 따라간다
    // 카메라의 위아래 각도는 몸에 적용하지 않음
    if (bIsAiming)
    {
        SetActorRotation(SurfaceView);
    }
}
// ===============================================
void AMyCharacter::OnLeftMousePressed()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;

    FVector ViewLoc;
    FRotator ViewRot;
    PC->GetPlayerViewPoint(ViewLoc, ViewRot);

    const FVector End = ViewLoc + ViewRot.Vector() * TraceDistance;

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, End, ECC_Visibility, Params))
        return;

    if (!Cast<AVoxelWorld>(Hit.GetActor())) return;

    // 바뀐 부분: 직접 파지 않고 서버에 요청
    if (UNetSyncComponent* Net = FindComponentByClass<UNetSyncComponent>())
    {
        Net->RequestTerrainEdit(0, Hit.ImpactPoint, DigRadius);
    }

    // 파는 소리·이펙트는 여기서 바로 재생해도 된다
    // 지형은 서버 응답을 받은 뒤에 바뀐다
}

void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        EIC->BindAction(IA_LinkMove, ETriggerEvent::Triggered, this, &AMyCharacter::Move);
        EIC->BindAction(IA_LinkRotate, ETriggerEvent::Triggered, this, &AMyCharacter::Look);
        EIC->BindAction(IA_Fire, ETriggerEvent::Triggered, this, &AMyCharacter::Fire);   // 추가
        EIC->BindAction(IA_ChangeMode, ETriggerEvent::Triggered, this, &AMyCharacter::OnWheel); 
        EIC->BindAction(IA_Aim, ETriggerEvent::Started, this, &AMyCharacter::OnSecondaryStart);
        EIC->BindAction(IA_Aim, ETriggerEvent::Triggered, this, &AMyCharacter::OnSecondaryHold);
        EIC->BindAction(IA_Aim, ETriggerEvent::Completed, this, &AMyCharacter::OnSecondaryStop);
    }
}

// 2026-10-13 2d 이동에서 행성 중심 움직임으로 수정
void AMyCharacter::Move(const FInputActionValue& Value)
{
    // 2026-10-13 위 2d 이동 주석 처리 후 행성 중심 움직임을 나중에 서버 권위적으로 수정 해야함
    if (!Controller || !IsLocallyControlled())
    {
        return;
    }

    // 입력을 처리하기 전에 현재 표면 방향을 맞추기
    UpdatePlanetOrientation();

    const FVector2D MoveValue = Value.Get<FVector2D>();

    const FVector ForwardDirection = PlanetViewForward;

    const FVector RightDirection = FVector::CrossProduct(PlanetUp,ForwardDirection).GetSafeNormal();

    AddMovementInput(ForwardDirection, MoveValue.X);
    AddMovementInput(RightDirection, MoveValue.Y);
}

// 2026-10-13 2d 회전에서 행성 중심 회전으로 수정
void AMyCharacter::Look(const FInputActionValue& Value)
{
	// 2d 회전 주석 처리 후 행성 중심 회전으로 수정
    if (!Controller || !IsLocallyControlled())
    {
        return;
    }

    UpdatePlanetOrientation();

    const FVector2D LookValue = Value.Get<FVector2D>();

    PlanetViewForward = PlanetViewForward.RotateAngleAxis( LookValue.X * PlanetLookSensitivity,PlanetUp).GetSafeNormal();

    const float PitchSign = bInvertPlanetLookY ? -1.f : 1.f;

    PlanetCameraPitch = FMath::Clamp(PlanetCameraPitch - LookValue.Y * PlanetLookSensitivity * PitchSign, -80.f,80.f);

    UpdatePlanetCamera();
}

void AMyCharacter::UpdateCrosshair()
{
    AWeaponBase* W = Cast<AWeaponBase>(EquippedWeapon);
    bool bShow = (bIsAiming) || (W && W->CurrentMode == EWeaponMode::Mining);

    if (bShow && !CrosshairWidget && CrosshairClass)
    {
        CrosshairWidget = CreateWidget<UUserWidget>(GetWorld(), CrosshairClass);
        CrosshairWidget->AddToViewport();
    }
    else if (!bShow && CrosshairWidget)
    {
        CrosshairWidget->RemoveFromParent();
        CrosshairWidget = nullptr;
    }
}

void AMyCharacter::EquipWeapon(AActor* WeaponToEquip)
{
    if (!WeaponToEquip) return;

    EquippedWeapon = WeaponToEquip;

    EquippedWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("WeaponSocket_R"));
}

void AMyCharacter::PlayerHpChanged_Implementation(float NewHp, float PreviousHp)
{
    CurrentHp = NewHp;
}

void AMyCharacter::Fire(const FInputActionValue& Value)
{
    //if (!bIsAiming)
    //{
    //    return;   // 조준 중이 아니면 발사 안 함
    //}

    //if (AWeaponBase* Weapon = Cast<AWeaponBase>(EquippedWeapon))
    //{
    //    Weapon->OnPrimaryAction();
    //}
    OnPrimaryAction();
}

bool AMyCharacter::TraceVoxel(FVector& OutPoint)
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return false;
    FVector L; FRotator R;
    PC->GetPlayerViewPoint(L, R);
    FHitResult Hit;
    FCollisionQueryParams P; P.AddIgnoredActor(this);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, L, L + R.Vector() * TraceDistance, ECC_Visibility, P))
        return false;
    OutPoint = Hit.ImpactPoint;
    return true;
}

void AMyCharacter::OnDig()
{
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now - LastVoxelTime < VoxelCooldown) return;

    LastVoxelTime = Now;
    FVector P; 
    if (TraceVoxel(P)) {
        ExecuteDig(P, DigRadius);
    }
}

void AMyCharacter::ExecuteDig(const FVector& Location, float Radius)
{
    const FVector Up = Location.GetSafeNormal();
    const float Lift = FMath::Max(Radius - DigDepth, 0.f);
    const FVector Center = Location + Up * Lift;

    if (UNetSyncComponent* Net = FindComponentByClass<UNetSyncComponent>())
    {
        Net->RequestTerrainEdit(0, Center, Radius);
    }
}

void AMyCharacter::OnBuild()
{
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now - LastVoxelTime < VoxelCooldown) return;

    LastVoxelTime = Now;
    FVector P;
    if (TraceVoxel(P)) {
        ExecuteBuild(P, DigRadius);
    }
}

void AMyCharacter::ExecuteBuild(const FVector& Location, float Radius)
{
    const float CapR = GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float CapH = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Up = GetActorLocation().GetSafeNormal();
    const FVector A = GetActorLocation() - Up * (CapH - CapR);
    const FVector B = GetActorLocation() + Up * (CapH - CapR);
    const FVector Closest = FMath::ClosestPointOnSegment(Location, A, B);
    if (FVector::Dist(Location, Closest) < Radius + CapR) return;   // 내 몸과 겹침

    if (UNetSyncComponent* Net = FindComponentByClass<UNetSyncComponent>())
    {
        Net->RequestTerrainEdit(1, Location, Radius);
    }
}

void AMyCharacter::StartAim()
{
    bIsAiming = true;

	GetCharacterMovement()->MaxWalkSpeed = 250.f;   // 이동 속도 절반으로

    // 2026-10-13 bUseControllerRotationYaw true -> false 로 변경 
    bUseControllerRotationYaw = false;                            // 카메라 도는 대로 몸 회전
    GetCharacterMovement()->bOrientRotationToMovement = false;

    if (USpringArmComponent* Arm = FindComponentByClass<USpringArmComponent>())
    {
        Arm->SocketOffset = AimSocketOffset;   // 카메라를 오른쪽 앞으로
    }
    if (UCameraComponent* Cam = FindComponentByClass<UCameraComponent>())
    {
        Cam->SetFieldOfView(ZoomedFOV);
    }

    UpdateCrosshair();
}

void AMyCharacter::StopAim()
{
    bIsAiming = false;

    GetCharacterMovement()->MaxWalkSpeed = 500.f;   // 이동 속도 절반으로
    
    bUseControllerRotationYaw = false;
	// 2026-10-13 GetCharacterMovement()->bOrientRotationToMovement false -> true 로 변경
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->bUseControllerDesiredRotation = false;

    if (USpringArmComponent* Arm = FindComponentByClass<USpringArmComponent>())
    {
        Arm->SocketOffset = DefaultSocketOffset;
    }
    if (UCameraComponent* Cam = FindComponentByClass<UCameraComponent>())
    {
        Cam->SetFieldOfView(DefaultFOV);
    }

    UpdateCrosshair();
}

void AMyCharacter::OnPrimaryAction()
{
    AWeaponBase* W = Cast<AWeaponBase>(EquippedWeapon);
    if (!W) return;
    switch (W->CurrentMode)
    {
    case EWeaponMode::Attack:
        if (!bIsAiming) return;
        W->Fire();
        break;
    case EWeaponMode::Mining:
        OnDig();
        break;
    }
}

void AMyCharacter::SwitchMode()
{
    if (AWeaponBase* W = Cast<AWeaponBase>(EquippedWeapon))
        W->CycleMode();

    UpdateCrosshair();
}

void AMyCharacter::NextMode()
{
    if (AWeaponBase* W = Cast<AWeaponBase>(EquippedWeapon))
        W->CycleMode();
}

void AMyCharacter::PrevMode()
{
    if (AWeaponBase* W = Cast<AWeaponBase>(EquippedWeapon))
        W->CyclePrevMode();
}

void AMyCharacter::OnWheel(const FInputActionValue& V)
{
    float Axis = V.Get<float>();
    if (Axis > 0) PrevMode();
    else if (Axis < 0) NextMode();

    UpdateCrosshair();
}

void AMyCharacter::OnSecondaryStart()
{
    AWeaponBase* W = Cast<AWeaponBase>(EquippedWeapon);
    if (!W) return;
    switch (W->CurrentMode)
    {
    case EWeaponMode::Attack: StartAim(); break;   // 발사 모드: 줌
    }
}

void AMyCharacter::OnSecondaryHold()
{
    AWeaponBase* W = Cast<AWeaponBase>(EquippedWeapon);
    if (!W) return;
    if (W->CurrentMode == EWeaponMode::Mining)
        OnBuild();   // 채굴 모드: 꾹 누르는 동안 계속 생성
}

void AMyCharacter::OnSecondaryStop()
{
    AWeaponBase* W = Cast<AWeaponBase>(EquippedWeapon);
    if (W && W->CurrentMode == EWeaponMode::Attack) StopAim();
}