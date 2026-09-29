// 서버용 슬림 FastNoise.
// 원본 VoxelFastNoise.h 는 Cubic/White/Perlin/Simplex/Cellular 까지 엮지만,
// VoxelExample_Planet 이 쓰는 것은 ValueNoise(IQNoise) 와 GradientPerturb 뿐이다.
#pragma once

#include "VoxelCompat.h"
#include "VoxelFastNoise_ValueNoise.h"
#include "VoxelFastNoise_GradientPerturb.h"

class FVoxelFastNoise : public
    TVoxelFastNoise_ValueNoise<
    TVoxelFastNoise_GradientPerturb<
    FVoxelFastNoiseBase>>
{
public:
    FVoxelFastNoise() = default;
};

#include "VoxelFastNoiseLUT.inl"
#include "VoxelFastNoiseMath.inl"
#include "VoxelFastNoiseBase.inl"
#include "VoxelFastNoise_ValueNoise.inl"
#include "VoxelFastNoise_GradientPerturb.inl"
