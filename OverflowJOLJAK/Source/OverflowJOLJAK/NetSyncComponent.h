#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/ItemTypes.h"
#include "Craft/CraftTypes.h"

#include "NetSyncComponent.generated.h"

class FSocket;

class AProjectile;

// 몬스터가 공격을 실행했을 때 브로드캐스트되는 델리게이트.
// AEnemyCharacter나 플레이어 쪽에서 바인딩해서 공격 애니메이션/이펙트/피격 반응 등을 처리하면 된다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMonsterAttack, int32, MonsterId, int32, TargetPlayerId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnItemChanged, int32, ItemId, int32, Delta, int32, Total);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGaugeChanged, int32, ItemId, float, Ratio);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCraftResult, int32, RecipeId, int32, Result, int32, ObjectId);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OVERFLOWJOLJAK_API UNetSyncComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNetSyncComponent();

    UPROPERTY(EditAnywhere, Category = "NetSync")
    FString ServerIP = TEXT("127.0.0.1");       // server.cpp

    UPROPERTY(EditAnywhere, Category = "NetSync")
    int32 ServerPort = 7777;                    // server.cpp

    /** m_visual 인덱스 -> 스폰할 클래스. 에디터에서 순서대로 채워넣을 것. [0]=젤다 ... */
    UPROPERTY(EditAnywhere, Category = "NetSync")
    TArray<TSubclassOf<AActor>> VisualClasses;

    /** m_monster_type 인덱스 -> 스폰할 몬스터 클래스. 예: [0]=BP_Enemy */
    UPROPERTY(EditAnywhere, Category = "NetSync")
    TArray<TSubclassOf<AActor>> MonsterVisualClasses;

    /** 원격 플레이어 보간 속도. 클수록 즉각적이고 딱딱해진다. */
    UPROPERTY(EditAnywhere, Category = "NetSync")
    float InterpSpeed = 12.0f;

    UPROPERTY(EditAnywhere, Category = "NetSync")
    float RotationInterpSpeed = 10.f;

    /** 몬스터 보간 속도. 서버 틱이 플레이어보다 느리면(10Hz 등) 값을 낮춰서 더 부드럽게 보이게 조절 가능. */
    UPROPERTY(EditAnywhere, Category = "NetSync")
    float MonsterInterpSpeed = 8.0f;

    /** 몬스터가 공격했을 때 브로드캐스트. Blueprint에서 바인딩해서 연출 처리. */
    UPROPERTY(BlueprintAssignable, Category = "NetSync")
    FOnMonsterAttack OnMonsterAttack;

    /** 내 캐릭터가 특정 몬스터를 공격했다고 서버에 알린다. 실제 데미지/사거리 판정은 서버가 함. */
    UFUNCTION(BlueprintCallable, Category = "NetSync")
    void SendAttack(const FVector& Origin, const FVector& Direction);

    UFUNCTION(BlueprintCallable, Category = "NetSync")
    void SendFireEvent(const FVector& MuzzleLocation, const FVector& Direction);

    UPROPERTY(EditAnywhere, Category = "NetSync")
    TSubclassOf<AProjectile> RemoteFireProjectileClass;


    UPROPERTY(EditAnywhere, Category = "Monster")
    bool bMonsterPosIsFeet = true;   // 서버가 발바닥 위치를 보내면 true

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
    TObjectPtr<UDataTable> ItemTable;            // 디테일 패널에서 DT_Items 지정

    UPROPERTY(BlueprintAssignable, Category = "Inventory")
    FOnItemChanged OnItemChanged;

    UFUNCTION(BlueprintPure, Category = "Inventory")
    int32 GetItemCount(int32 ItemId) const;

    // 블루프린트에서 이름(드롭다운)으로 개수 조회
    UFUNCTION(BlueprintPure, Category = "Inventory")
    int32 GetItemCountByRow(const FDataTableRowHandle& Row) const;

    UFUNCTION(BlueprintCallable, Category = "Inventory")
    bool FindItemRow(int32 ItemId, FItemRow& OutRow) const;

    UFUNCTION(BlueprintPure, Category = "Inventory")
    float GetSoilRatio() const;


    UPROPERTY(EditAnywhere, Category = "Deposit")
    TObjectPtr<UStaticMesh> FragmentMesh;          // 파편 메시 (엔진 기본 Cube 등)

    UPROPERTY(EditAnywhere, Category = "Deposit")
    float FragmentScale = 0.1f;                    // 파편 크기 (Cube 100cm 기준 → 10cm)

    UPROPERTY(EditAnywhere, Category = "Deposit")
    float FragmentFlyTime = 0.4f;                  // 빨려 오는 시간

    UPROPERTY(EditAnywhere, Category = "Deposit")
    FName MuzzleSocketName = TEXT("Muzzle");     // 무기 메시의 발사 소켓 이름과 같게

    UPROPERTY(EditAnywhere, Category = "Deposit")
    float FragmentBurstTime = 0.12f;            // 튀어나오는 시간

    UPROPERTY(EditAnywhere, Category = "Deposit")
    float FragmentBurstDistance = 50.f;         // 튀어나오는 거리 (cm)

    UPROPERTY(EditAnywhere, Category = "Deposit")
    FName SuctionTag = TEXT("Suction");         // 흡입구 컴포넌트 태그

    UPROPERTY(BlueprintAssignable, Category = "Inventory")
    FOnGaugeChanged OnGaugeChanged;

    UFUNCTION(BlueprintPure, Category = "Inventory")
    float GetGaugeRatio(int32 ItemId) const;


    // ---------- 시설 / 제작 ----------
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft")
    TObjectPtr<UDataTable> ObjectTable;                 // DT_Objects

    UPROPERTY(BlueprintAssignable, Category = "Craft")
    FOnCraftResult OnCraftResult;                       // 제작 결과 (Result 0 = 성공)

    // 이 시설에서 만들 수 있는 레시피 목록
    UFUNCTION(BlueprintCallable, Category = "Craft")
    TArray<FRecipeInfo> GetRecipesForStation(int32 StationType) const;

    // 재료를 다 가지고 있는지 (버튼 활성화용, 실제 판정은 서버)
    UFUNCTION(BlueprintPure, Category = "Craft")
    bool CanCraft(int32 RecipeId) const;

    UFUNCTION(BlueprintCallable, Category = "Craft")
    void RequestCraft(int32 PrinterId, int32 RecipeId);

    UFUNCTION(BlueprintCallable, Category = "Craft")
    bool FindObjectRow(int32 ObjectType, FObjectRow& OutRow) const;

    // 화면 중앙이 가리키는 시설 (사거리 밖이거나 없으면 None)
    UFUNCTION(BlueprintCallable, Category = "Craft")
    APlacedObject* TraceObject() const;

    UFUNCTION(BlueprintPure, Category = "Craft")
    FText GetCraftResultText(int32 Result) const;


private:
    TArray<int32> Inventory;
    void HandleItemChange(const struct sc_packet_item_change& Pkt);
    void ValidateItemTable() const;

public:
    // Op: 0 = 파기, 1 = 쌓기
    // 로컬로 파지 않고 서버에 요청만 보낸다
    void RequestTerrainEdit(uint8 Op, const FVector& CenterCm, float RadiusCm);

private:
    void HandleTerrainEdit(const struct sc_packet_terrain_edit* Pkt);
    void ApplyEditToChunk(class AVoxelWorld* World, const FIntVector& Chunk,
        uint8 Op, double Cx, double Cy, double Cz, double R);
    class AVoxelWorld* GetVoxelWorld();

    TMap<FIntVector, uint32>    TerrainVersions;   // 청크별 내 버전. 없으면 0 (= 원본 지형)
    TSet<FIntVector>            StaleChunks;       // 중간 편집을 놓친 청크. 3단계에서 서버에 요청
    TWeakObjectPtr<AVoxelWorld> CachedVoxelWorld;  // 매번 찾지 않도록 저장


    double  LastEditSendTime = 0.0;
    FVector LastEditCenter = FVector(FLT_MAX);
    uint8   LastEditOp = 255;
    static constexpr double EditInterval = 0.1;   // 초당 최대 10번


    struct FDepositView
    {
        TWeakObjectPtr<class UInstancedStaticMeshComponent> ISM;
        TArray<FTransform> Pieces;     // 조각별 월드 트랜스폼
        int32 Alive = 0;
    };
    TMap<int32, FDepositView> Deposits;

    struct FFragment
    {
        TWeakObjectPtr<AActor>          Actor;
        TWeakObjectPtr<USceneComponent> Target;
        FName    Socket = NAME_None;
        FVector  Start = FVector::ZeroVector;
        FVector  Burst = FVector::ZeroVector;
        FRotator Spin = FRotator::ZeroRotator;
        float    Elapsed = 0.f;
        float    Delay = 0.f;
    };
    TArray<FFragment> Fragments;

    USceneComponent* FindSuctionPoint(AActor* Actor, FName& OutSocket) const;

    TArray<int32> Gauges;

    void HandleDepositAdd(const struct sc_packet_deposit_add& Pkt);
    void HandlePieceAbsorb(const struct sc_packet_piece_absorb& Pkt);
    void HandleGauge(const struct sc_packet_gauge& Pkt);
    void UpdateFragments(float DeltaTime);
    AActor* FindPlayerActor(int32 Id) const;


protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    FSocket* Socket = nullptr;
    FTimerHandle SendTimerHandle;
    FTimerHandle RecvTimerHandle;
    TArray<uint8> RecvBuffer;

    // GC가 스폰한 액터를 수거해가지 않도록 UPROPERTY 필수
    UPROPERTY()
    TMap<int32, AActor*> RemotePlayers;

    // 30Hz로 띄엄띄엄 오는 좌표. 매 프레임 여기로 보간해 따라간다.
    TMap<int32, FVector> TargetLocations;
    TMap<int32, FRotator> TargetRotations;

    // 몬스터는 플레이어와 완전히 별개의 id 네임스페이스라서 맵도 따로 관리한다.
    UPROPERTY()
    TMap<int32, AActor*> Monsters;

    TMap<int32, FVector> MonsterTargetLocations;

    // 서버가 알려준 내 ID. 로그인 패킷이 생기기 전까지는 -1.
    int32 MyId = -1;

    void ConnectToServer();
    void ReceiveFromServer();
    void SendToServer(float X, float Y, float Z, float Pitch, float Yaw, float Roll);
    void SendPositionTick();

    void AddPlayer(int32 Id, int32 Visual, const FVector& Location, const FRotator& Rotation);
    void UpdatePosition(int32 Id, const FVector& Location, const FRotator& Rotation);
    void RemovePlayer(int32 Id);

    void AddMonster(int32 Id, uint8 MonsterType, const FVector& Location, int32 Hp);
    void UpdateMonsterPosition(int32 Id, const FVector& Location);
    void UpdateMonsterHp(int32 Id, int32 NewHp);
    void RemoveMonster(int32 Id);
    void HandleMonsterAttack(int32 MonsterId, int32 TargetPlayerId);
    void SpawnRemoteFireCosmetic(const FVector& MuzzleLocation, const FVector& Direction);
    void UpdatePlayerHp(int32 Id, int32 NewHp, float Damage);
    void HandlePlayerDeath(int32 Id);
    void HandlePlayerRespawn(int32 Id, const FVector& Location, float Hp);

    void InterpolateRemotePlayers(float DeltaTime);
    void InterpolateMonsters(float DeltaTime);


    UPROPERTY()
    TMap<int32, APlacedObject*> PlacedObjects;

    void HandleObjectAdd(const struct sc_packet_object_add& Pkt);
    void HandleObjectMove(const struct sc_packet_object_move& Pkt);
    void HandleObjectRemove(const struct sc_packet_object_remove& Pkt);
    void HandleObjectHold(const struct sc_packet_object_hold& Pkt);
    void HandleCraftResult(const struct sc_packet_craft_result& Pkt);
};