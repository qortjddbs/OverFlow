#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "WeaponBase.h"
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

    bool TraceVoxel(FVector& OutPoint);

    void OnDig();   // 추가
    void ExecuteDig(const FVector& Location, float Radius);

	void OnBuild();   // 추가
	void ExecuteBuild(const FVector& Location, float Radius);

    void StartAim();
    void StopAim();

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputMappingContext* IMC_Link;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_LinkMove;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_LinkRotate;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    UInputAction* IA_Fire;   // 추가

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")             // 추가
    UInputAction* IA_ChangeMode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")             // 추가
    UInputAction* IA_Aim;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NetSync")         // 추가
    UNetSyncComponent* NetSyncComponent;                                        // 추가

    // 채굴 관련
    UFUNCTION()
    void OnLeftMousePressed();

    UPROPERTY(EditAnywhere, Category = "Voxel")
    float TraceDistance = 1000.f;

    UPROPERTY(EditAnywhere, Category = "Voxel")
    float DigRadius = 100.f;

    UPROPERTY(VisibleAnywhere, Category = "Voxel")
    class UVoxelSimpleInvokerComponent* VoxelInvoker;

    // 쿨타임
    float LastVoxelTime = 0.f;

    UPROPERTY(EditAnywhere, Category = "Voxel")
    float VoxelCooldown = 0.1f;

    // 총 관련
    UPROPERTY(EditAnywhere, Category = "Aim")
    float DefaultFOV = 90.f;

    UPROPERTY(EditAnywhere, Category = "Aim")
    float ZoomedFOV = 60.f;

    // 조준 시 스프링암 소켓 오프셋 (카메라를 오른쪽 앞으로). 에디터에서 미세조정.
    UPROPERTY(EditAnywhere, Category = "Aim")
    FVector AimSocketOffset = FVector(0.f, 50.f, -50.f);   // Y=오른쪽

    UPROPERTY(EditAnywhere, Category = "Aim")
    FVector DefaultSocketOffset = FVector(0.f, 0.f, 0.f);

    // 지금 조준 중인지. 발사 가능 여부 판정에 씀.
    UPROPERTY(BlueprintReadOnly, Category = "Aim")
    bool bIsAiming = false;


    // 크로스헤어
    UPROPERTY(EditAnywhere, Category = "UI")
    TSubclassOf<UUserWidget> CrosshairClass;
    UPROPERTY()
    UUserWidget* CrosshairWidget;

    void UpdateCrosshair();

public:
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void EquipWeapon(AActor* WeaponToEquip);

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
    AActor* EquippedWeapon;

    // 무기 클래스를 지정할 수 있게 (에디터에서 어떤 무기 BP/클래스 쓸지 지정)
    UPROPERTY(EditDefaultsOnly, Category = "Weapon")
    TSubclassOf<AActor> WeaponClass;

    void OnPrimaryAction();
    void OnSecondaryStart();
    void OnSecondaryHold();
    void OnSecondaryStop();
    void SwitchMode();
    void PrevMode();
    void NextMode();

    void OnWheel(const FInputActionValue& V);
};
