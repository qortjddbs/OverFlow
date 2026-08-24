#include "MyCharacter.h"
#include "WeaponBase.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NetSyncComponent.h"                               // 추가

// Sets default values
AMyCharacter::AMyCharacter()
{
	bUseControllerRotationYaw = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 720.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 500.f;

	NetSyncComponent = CreateDefaultSubobject<UNetSyncComponent>(TEXT("NetSyncComponent"));     // 추가
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

void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        EIC->BindAction(IA_LinkMove, ETriggerEvent::Triggered, this, &AMyCharacter::Move);
        EIC->BindAction(IA_LinkRotate, ETriggerEvent::Triggered, this, &AMyCharacter::Look);
        EIC->BindAction(IA_Fire, ETriggerEvent::Started, this, &AMyCharacter::Fire);   // 추가
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

void AMyCharacter::EquipWeapon(AActor* WeaponToEquip)
{
    if (!WeaponToEquip) return;

    EquippedWeapon = WeaponToEquip;

    EquippedWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("WeaponSocket_R"));
}

void AMyCharacter::Fire(const FInputActionValue& Value)
{
    if (AWeaponBase* Weapon = Cast<AWeaponBase>(EquippedWeapon))
    {
        Weapon->Fire();
    }
}