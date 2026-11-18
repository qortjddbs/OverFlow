#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlacedObject.generated.h"

// 서버가 관리하는 시설(프린터, 발전기 등)의 공통 부모
// 루트는 빈 Scene Component. 메시(스태틱/스켈레탈)와 클릭용 충돌은 블루프린트에서 붙인다
UCLASS(Blueprintable)
class OVERFLOWJOLJAK_API APlacedObject : public AActor
{
    GENERATED_BODY()

public:
    APlacedObject();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Object")
    int32 ObjectId = -1;          // 서버가 정한 id
    
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Object")
    int32 ObjectType = -1;        // OBJECT_TYPE

    //UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Object") int32 ObjectId = -1;
    //UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Object") int32 ObjectType = -1;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Object")
    int32 HolderId = -1;          // 잡고 있는 플레이어 (-1 = 없음)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Object")
    TObjectPtr<USceneComponent> Root;
};