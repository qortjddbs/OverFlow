#include "PlanetVoxelGenerator.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FPlanetConfig UPlanetVoxelGenerator::MakeConfig() const
{
    FPlanetConfig C;
    // float UPROPERTY 를 그대로 double 로 옮긴다. 서버도 파일에서 같은 double 을 읽는다.
    C.Radius        = static_cast<double>(Radius);
    C.Frequency     = static_cast<double>(Frequency);
    C.NoiseStrength = static_cast<double>(NoiseStrength);
    C.NoiseSeed     = NoiseSeed;
    C.Octaves       = 15;
    C.CurveMinT     = -1.6;    // iq 클램프 범위 -> t 는 -1.507 ~ 1.300. 여유를 둔 값
    C.CurveStep     = 0.0125;

    if (UCurveFloat* Curve = PlanetCurve.LoadSynchronous())
    {
        C.CurveSamples.reserve(257);
        for (int32 i = 0; i < 257; ++i)
        {
            const float t = static_cast<float>(C.CurveMinT + i * C.CurveStep);
            C.CurveSamples.push_back(static_cast<double>(Curve->GetFloatValue(t)));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("PlanetVoxelGenerator: 커브를 못 찾음. 완전한 구체로 생성"));
    }
    return C;
}

FString UPlanetVoxelGenerator::GetConfigPath()
{
    return FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("Planet.cfg"));
}

void UPlanetVoxelGenerator::ExportPlanetConfig()
{
    const FPlanetConfig C = MakeConfig();
    const FString Path = GetConfigPath();
    const FString Text = UTF8_TO_TCHAR(C.ToText().c_str());

    if (FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogTemp, Log, TEXT("Planet.cfg 저장: %s  (hash %llu)"), *Path, C.Hash());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Planet.cfg 저장 실패: %s"), *Path);
    }
}

#if WITH_EDITOR
void UPlanetVoxelGenerator::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    ExportPlanetConfig();
}
#endif

TVoxelSharedRef<FVoxelGeneratorInstance> UPlanetVoxelGenerator::GetInstance()
{
    return MakeVoxelShared<FPlanetVoxelGeneratorInstance>(*this);
}
