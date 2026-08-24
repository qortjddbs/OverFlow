#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "MyCharacter.generated.h"

class UInputMappingContext;
class UInputAction;
class UNetSyncComponent;        // 추가
class AWeaponBase;

UCLASS()
class OVERFLOWJOLJAK_API AMyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AMyCharacter();

protected:
    virtual void BeginPlay() override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void Fire(const FInputActionValue& Value);   // 추가

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputMappingContext* IMC_Link;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_LinkMove;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_LinkRotate;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Fire;   // 추가

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NetSync")         // 추가
    UNetSyncComponent* NetSyncComponent;                                        // 추가

public:
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void EquipWeapon(AActor* WeaponToEquip);

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
    AActor* EquippedWeapon;

    // 무기 클래스를 지정할 수 있게 (에디터에서 어떤 무기 BP/클래스 쓸지 지정)
    UPROPERTY(EditDefaultsOnly, Category = "Weapon")
    TSubclassOf<AActor> WeaponClass;
};
