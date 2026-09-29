// 공유 지형 공식(PlanetCore.h)을 Voxel Plugin 생성기로 감싼 것.
//
// 사용법
//   1) 콘텐츠 브라우저에서 이 클래스를 부모로 블루프린트 클래스를 만든다 (BP_PlanetGenerator)
//   2) AVoxelWorld 의 Generator 를 BP_PlanetGenerator 로 지정
//   3) BP 의 클래스 디폴트에서 수치를 바꾸면 Config/Planet.cfg 가 자동 저장된다
//      커브 에셋 자체를 고쳤을 때는 "Export Planet Config" 버튼을 누를 것
//   4) 서버는 재시작하면 새 Planet.cfg 를 읽는다
#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "PlanetCore.h"
#include "PlanetVoxelGenerator.generated.h"

UCLASS(Blueprintable)
class UPlanetVoxelGenerator : public UVoxelGenerator
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    float Radius = 10000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    float Frequency = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    float NoiseStrength = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    int32 NoiseSeed = 1443;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    TSoftObjectPtr<UCurveFloat> PlanetCurve = TSoftObjectPtr<UCurveFloat>(FSoftObjectPath(
        TEXT("/Voxel/Examples/VoxelGraphs/Planet/VoxelExample_Planet_Curve.VoxelExample_Planet_Curve")));

    // 색은 클라 전용이라 Planet.cfg 에 들어가지 않는다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Visual")
    FLinearColor Color = FLinearColor(0.35f, 0.28f, 0.22f, 1.0f);

    // UPROPERTY -> FPlanetConfig (커브를 샘플링해서 넣는다)
    FPlanetConfig MakeConfig() const;

    // <프로젝트>/Config/Planet.cfg
    static FString GetConfigPath();

    UFUNCTION(CallInEditor, Category = "Planet")
    void ExportPlanetConfig();

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

    //~ Begin UVoxelGenerator Interface
    virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
    //~ End UVoxelGenerator Interface
};

class FPlanetVoxelGeneratorInstance
    : public TVoxelGeneratorInstanceHelper<FPlanetVoxelGeneratorInstance, UPlanetVoxelGenerator>
{
public:
    using Super = TVoxelGeneratorInstanceHelper<FPlanetVoxelGeneratorInstance, UPlanetVoxelGenerator>;

    const FVoxelMaterial Material;
    const FPlanetCore Core;

    explicit FPlanetVoxelGeneratorInstance(UPlanetVoxelGenerator& Object)
        : Super(&Object)
        , Material(FVoxelMaterial::CreateFromColor(Object.Color))
        , Core(Object.MakeConfig())
    {
    }

    inline v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
    {
        return Core.Value(X, Y, Z);   // LOD 무시: 서버와 같은 값을 내기 위해
    }

    inline FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
    {
        return Material;
    }

    TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
    {
        double Lo, Hi;
        Core.ValueRange(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z,
                        Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z, Lo, Hi);
        return TVoxelRange<v_flt>(Lo, Hi);
    }

    // 구체 행성: "위" 는 중심에서 바깥 방향
    FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final
    {
        return FVector(X, Y, Z).GetSafeNormal();
    }
};
