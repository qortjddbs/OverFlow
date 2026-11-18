#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Craft/PlacedObject.h"
#include "CraftTypes.generated.h"

class UTexture2D;

// DT_Objects 한 행 = 시설 종류 하나
USTRUCT(BlueprintType)
struct FObjectRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 ObjectType = 0;                    // OBJECT_TYPE 번호와 같게
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UTexture2D> Icon = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<APlacedObject> ActorClass;  // 스폰할 블루프린트
};

// 블루프린트용 레시피 (공용 헤더의 RECIPES를 옮겨 담음)
USTRUCT(BlueprintType)
struct FRecipeInputInfo
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 ItemId = 0;
    UPROPERTY(BlueprintReadOnly) int32 Count = 0;
};

USTRUCT(BlueprintType)
struct FRecipeInfo
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 RecipeId = 0;
    UPROPERTY(BlueprintReadOnly) int32 StationType = 0;     // 만드는 시설
    UPROPERTY(BlueprintReadOnly) int32 OutputType = 0;      // 만들어지는 시설
    UPROPERTY(BlueprintReadOnly) TArray<FRecipeInputInfo> Inputs;
};