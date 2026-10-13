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

// 2026-10-13 추가
class USpringArmComponent;

UCLASS()
class OVERFLOWJOLJAK_API AMyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AMyCharacter();

    // 2026-10-13 추가
    virtual void Tick(float DeltaSeconds) override;

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

    UPROPERTY(BlueprintReadOnly, Category = "Health")
    float MaxHp = 100.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Health")
    float CurrentHp = 100.0f;

    // 채굴 관련
    UFUNCTION()
    void OnLeftMousePressed();

    UPROPERTY(EditAnywhere, Category = "Voxel")
    float TraceDistance = 1000.f;

    UPROPERTY(EditAnywhere, Category = "Voxel")
    float DigRadius = 200.f;

    UPROPERTY(VisibleAnywhere, Category = "Voxel")
    class UVoxelSimpleInvokerComponent* VoxelInvoker;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float DigDepth = 100.f;   // 한 번에 파이는 최대 깊이 (cm)

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


    // 2026-10-13 행성 중심 기반 움직임
    // 
    // 행성 중심의 월드 좌표입니다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet Movement")
    FVector PlanetCenter = FVector::ZeroVector;

    // 현재 위치에서 행성 바깥을 향하는 방향입니다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet Movement")
    FVector PlanetUp = FVector::UpVector;

    // 마우스 입력에 곱할 감도입니다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet Movement")
    float PlanetLookSensitivity = 1.f;

    // 카메라의 위아래 각도입니다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet Movement")
    float PlanetCameraPitch = -15.f;

    // BP_Astronaut에 이미 있는 SpringArm을 저장합니다.
    UPROPERTY(Transient)
    USpringArmComponent* PlanetCameraArm = nullptr;

    FVector PlanetViewForward = FVector::ForwardVector;
    bool bPlanetViewInitialized = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet Movement")
    bool bInvertPlanetLookY = true;

    // 중력 방향과 캐릭터 자세를 갱신합니다.
    void UpdatePlanetOrientation();
    
    void UpdatePlanetCamera();
    // ============================================= 여기까지 추가

public:
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void EquipWeapon(AActor* WeaponToEquip);

    UFUNCTION(BlueprintNativeEvent, Category = "Hp")
    void PlayerHpChanged(float NewHp, float PreviousHp);
    virtual void PlayerHpChanged_Implementation(float NewHp, float PreviousHp); 

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
