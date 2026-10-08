#!/usr/bin/env python3
"""
Voxel Plugin 의 FastNoise 를 서버(순수 C++)로 옮긴다.

수학 코드는 한 글자도 건드리지 않는다. include 줄만 정리한다.
SIMD(VectorRegister) 는 제거하지 않는다 - VoxelCompat.h 가 스텁을 공급한다.

사용:
  python port_noise.py <플러그인루트> <출력폴더>
"""

import os
import re
import sys

PUBLIC = "Source/Voxel/Public/FastNoise"
PRIVATE = "Source/Voxel/Private/FastNoise"

FILES = [
    (PUBLIC,  "CrossPlatformSTD.h"),
    (PUBLIC,  "VoxelFastNoiseLUT.h"),
    (PUBLIC,  "VoxelFastNoiseLUT.inl"),
    (PUBLIC,  "VoxelFastNoiseMath.h"),
    (PUBLIC,  "VoxelFastNoiseMath.inl"),
    (PUBLIC,  "VoxelFastNoiseBase.h"),
    (PUBLIC,  "VoxelFastNoiseBase.inl"),
    (PUBLIC,  "VoxelFastNoise_ValueNoise.h"),
    (PUBLIC,  "VoxelFastNoise_ValueNoise.inl"),
    (PUBLIC,  "VoxelFastNoise_GradientPerturb.h"),
    (PUBLIC,  "VoxelFastNoise_GradientPerturb.inl"),
    (PRIVATE, "VoxelFastNoiseLUT.cpp"),
]

# 이 이름이 들어간 #include 줄은 통째로 지운다.
# 해당 타입/매크로는 VoxelCompat.h 가 공급한다.
DROP = [
    "CoreMinimal.h",
    "VoxelMinimal.h",
    "VoxelMacros.h",
    "VoxelContainers/",
    "VoxelUtilities/",
    "VoxelGlobals",
    "VoxelEngine",
    "VoxelDefinitions",
    "Math/TransformCalculus",
    ".generated.h",
]


def patch(text):
    out = []
    for line in text.split("\n"):
        s = line.strip()

        if s.startswith("#include"):
            if any(d in s for d in DROP):
                continue
            # 플러그인 내부 include 경로를 평탄화
            line = re.sub(r'#include\s+"FastNoise/([^"]+)"', r'#include "\1"', line)
            out.append(line)
            continue

        # UENUM / UPROPERTY 마커만 제거 (enum 본문은 그대로 둔다)
        if re.match(r"^\s*(UENUM|UPROPERTY|UCLASS|USTRUCT)\s*\(", line):
            continue

        out.append(line)

    text = "\n".join(out)

    if "#pragma once" in text:
        text = text.replace("#pragma once", '#pragma once\n\n#include "VoxelCompat.h"', 1)
    else:
        text = '#include "VoxelCompat.h"\n' + text

    return text


SLIM = '''// 서버용 슬림 FastNoise.
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
'''


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1

    root, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)

    missing = []
    for folder, name in FILES:
        src = os.path.join(root, folder, name)
        if not os.path.exists(src):
            missing.append(src)
            continue

        with open(src, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()

        print("  " + name)
        with open(os.path.join(dst, name), "w", encoding="utf-8-sig", newline="\n") as f:
            f.write(patch(text))

    with open(os.path.join(dst, "ServerFastNoise.h"), "w", encoding="utf-8-sig", newline="\n") as f:
        f.write(SLIM)
    print("  ServerFastNoise.h  (생성)")

    if missing:
        print("\n찾지 못한 파일:")
        for m in missing:
            print("  " + m)
        return 1

    print("\n완료 -> " + dst)
    print("VoxelCompat.h 를 같은 폴더에 두고 빌드하세요.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
