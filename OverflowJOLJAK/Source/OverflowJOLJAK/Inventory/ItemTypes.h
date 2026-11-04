#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ItemTypes.generated.h"

// 데이터 테이블 한 행 = 아이템 하나
USTRUCT(BlueprintType)
struct FItemRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 ItemId = 0;     // 서버 ITEM_TYPE 번호와 같게
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UTexture2D> Icon = nullptr;
};