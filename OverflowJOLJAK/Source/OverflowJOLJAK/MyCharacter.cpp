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

// Sets default values
AMyCharacter::AMyCharacter()
{
	bUseControllerRotationYaw = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 720.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 500.f;

	NetSyncComponent = CreateDefaultSubobject<UNetSyncComponent>(TEXT("NetSyncComponent"));     // 추가

    VoxelInvoker = CreateDefaultSubobject<UVoxelSimpleInvokerComponent>(TEXT("VoxelInvoker"));  // 추가
    VoxelInvoker->SetupAttachment(RootComponent);
}

// Called when the game starts or when spawned
void AMyCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(IMC_Link, 0);
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

}

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

    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(Hit.GetActor());
    if (!VoxelWorld) return;

    UVoxelSphereTools::RemoveSphere(VoxelWorld, Hit.ImpactPoint, DigRadius);
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

void AMyCharacter::Move(const FInputActionValue& Value)
{
    FVector2D MoveValue = Value.Get<FVector2D>();

    if (!Controller) return;

    FRotator ControlRotation = Controller->GetControlRotation();
    FRotator YawRotation(0.f, ControlRotation.Yaw, 0.f);

    FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
    FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

    AddMovementInput(ForwardDirection, MoveValue.X);
    AddMovementInput(RightDirection, MoveValue.Y);
}

void AMyCharacter::Look(const FInputActionValue& Value)
{
    FVector2D LookValue = Value.Get<FVector2D>();

    AddControllerYawInput(LookValue.X);
    AddControllerPitchInput(-LookValue.Y);
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
    // 나중에 이 안을 서버 RPC로 교체
    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(
        UGameplayStatics::GetActorOfClass(GetWorld(), AVoxelWorld::StaticClass()));
    if (!VoxelWorld) return;

    UVoxelSphereTools::RemoveSphere(VoxelWorld, Location, Radius);
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
    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(
        UGameplayStatics::GetActorOfClass(GetWorld(), AVoxelWorld::StaticClass()));
    if (!VoxelWorld) return;

    UVoxelSphereTools::AddSphere(VoxelWorld, Location, Radius);
}

void AMyCharacter::StartAim()
{
    bIsAiming = true;

	GetCharacterMovement()->MaxWalkSpeed = 250.f;   // 이동 속도 절반으로

    bUseControllerRotationYaw = true;                            // 카메라 도는 대로 몸 회전
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
    GetCharacterMovement()->bOrientRotationToMovement = true;

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