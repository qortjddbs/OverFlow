// 지형 편집 공유 정의 — 서버와 클라가 "이 파일 그대로" 컴파일한다.
// 표준 C++ 만 사용 (언리얼 타입, VoxelCompat 없음). server.cpp 에서 include 해도 안전하다.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <vector>

namespace VoxelEdit
{
    // ------------------------------------------------------------------ 상수
    constexpr int     CHUNK      = 32;                        // 청크 한 변 (복셀)
    constexpr int     CHUNK_VOX  = CHUNK * CHUNK * CHUNK;     // 32768
    constexpr double  VOXEL_SIZE = 100.0;                     // AVoxelWorld 의 VoxelSize 와 일치시킬 것
    constexpr int16_t QMAX       = 32767;                     // FVoxelValue16::MAX_VOXELVALUE

    // "아직 계산 안 함" 표시. 정상 값은 -32767 ~ 32767 이라 겹치지 않는다
    // (플러그인 FVoxelValue::Special() 과 같은 값).
    // 서버: 이 칸은 생성기로 계산하면 된다.
    // 클라: 스냅샷에서 이 칸은 건드리지 않는다 (아무도 안 판 칸이라 양쪽 값이 같다).
    constexpr int16_t UNSET      = -32768;

    // 반경 상한 (복셀). 16 이하면 한 편집이 건드리는 청크가 최대 2x2x2 = 8개.
    constexpr double  MAX_RADIUS_VOX = 16.0;
    constexpr int     MAX_TOUCHED    = 8;

    enum class EOp : uint8_t { Dig = 0, Build = 1 };

    // ------------------------------------------------------------------ 청크 좌표
    struct ChunkKey
    {
        int32_t x = 0, y = 0, z = 0;
        bool operator==(const ChunkKey& o) const { return x == o.x && y == o.y && z == o.z; }
    };

    struct ChunkKeyHash
    {
        size_t operator()(const ChunkKey& k) const
        {
            uint64_t h = static_cast<uint32_t>(k.x) * 73856093ull;
            h ^= static_cast<uint32_t>(k.y) * 19349663ull;
            h ^= static_cast<uint32_t>(k.z) * 83492791ull;
            return static_cast<size_t>(h);
        }
    };

    inline int32_t FloorDiv(int32_t a, int32_t b)
    {
        return (a >= 0) ? (a / b) : -((-a + b - 1) / b);
    }

    inline ChunkKey KeyOf(int32_t vx, int32_t vy, int32_t vz)
    {
        return { FloorDiv(vx, CHUNK), FloorDiv(vy, CHUNK), FloorDiv(vz, CHUNK) };
    }

    // 청크 안 인덱스 (x 가 가장 빠름)
    inline int LocalIndex(int lx, int ly, int lz)
    {
        return lx + CHUNK * (ly + CHUNK * lz);
    }

    // ------------------------------------------------------------------ 양자화
    // 플러그인 TVoxelValueImpl<int16>(float) 생성자를 비트 단위로 복제한 것.
    //   F = ClampToStorage(RoundToInt(Clamp(float(v), -10.f, 10.f) * MAX))
    //   RoundToInt(F) = ConstExprUtils::Floor(F + 0.5f)
    // 이 함수를 바꾸면 서버 저장값과 클라 저장값이 달라진다.
    inline int16_t Quantize(double InValue)
    {
        float f = static_cast<float>(InValue);
        f = (f < -10.f) ? -10.f : ((f > 10.f) ? 10.f : f);

        const float m = f * static_cast<float>(QMAX);
        const float g = m + 0.5f;

        int32_t r;
        if (static_cast<int32_t>(g) == g) r = static_cast<int32_t>(g);
        else if (g < 0)                   r = static_cast<int32_t>(g) - 1;
        else                              r = static_cast<int32_t>(g);

        if (r < -QMAX) r = -QMAX;
        if (r >  QMAX) r =  QMAX;
        return static_cast<int16_t>(r);
    }

    // 플러그인 ToFloat() 와 동일: float(storage) / float(MAX)
    inline float Dequantize(int16_t q)
    {
        return static_cast<float>(q) / static_cast<float>(QMAX);
    }

    // ------------------------------------------------------------------ 편집 연산
    // 월드(cm) -> 복셀. 서버와 클라가 패킷의 float 를 이 함수로 똑같이 변환한다.
    inline double ToVoxel(float WorldCm) { return static_cast<double>(WorldCm) / VOXEL_SIZE; }

    // 한 복셀에 구체 편집을 적용. 입력/출력 모두 저장 형식(int16).
    //   파기: max(old, -sd)   채우기: min(old, sd)   (음수 = 속)
    inline int16_t ApplySphere(int16_t OldQ,
                               int32_t vx, int32_t vy, int32_t vz,
                               double cx, double cy, double cz, double r, EOp Op)
    {
        const double dx = vx - cx;
        const double dy = vy - cy;
        const double dz = vz - cz;
        const double d2 = dx * dx;
        const double e2 = dy * dy;
        const double f2 = dz * dz;
        const float  sd = static_cast<float>(std::sqrt(d2 + e2 + f2) - r);

        const float oldV = Dequantize(OldQ);
        const float newV = (Op == EOp::Dig)
            ? ((oldV > -sd) ? oldV : -sd)
            : ((oldV <  sd) ? oldV :  sd);

        return Quantize(newV);
    }

    // 편집이 영향을 줄 수 있는 복셀 박스 (양끝 포함).
    // 표면에서 1복셀 넘게 떨어지면 값이 ±1 로 포화되어 변하지 않으므로 r+2 면 충분하다.
    inline void SphereBounds(double cx, double cy, double cz, double r,
                             int32_t& minX, int32_t& minY, int32_t& minZ,
                             int32_t& maxX, int32_t& maxY, int32_t& maxZ)
    {
        minX = static_cast<int32_t>(std::floor(cx - r - 2.0));
        minY = static_cast<int32_t>(std::floor(cy - r - 2.0));
        minZ = static_cast<int32_t>(std::floor(cz - r - 2.0));
        maxX = static_cast<int32_t>(std::ceil (cx + r + 2.0));
        maxY = static_cast<int32_t>(std::ceil (cy + r + 2.0));
        maxZ = static_cast<int32_t>(std::ceil (cz + r + 2.0));
    }

    // ------------------------------------------------------------------ RLE
    // 편집된 청크는 대부분 -QMAX / +QMAX 연속 구간이라 수십~수백 바이트로 줄어든다.
    // 형식: [uint16 run][int16 value] 반복, 리틀엔디언.
    inline void EncodeRLE(const int16_t* In, std::vector<uint8_t>& Out)
    {
        Out.clear();
        int i = 0;
        while (i < CHUNK_VOX)
        {
            const int16_t v = In[i];
            int run = 1;
            while (i + run < CHUNK_VOX && In[i + run] == v && run < 0xFFFF) ++run;

            const uint16_t r16 = static_cast<uint16_t>(run);
            const uint16_t v16 = static_cast<uint16_t>(v);
            Out.push_back(static_cast<uint8_t>(r16 & 0xFF));
            Out.push_back(static_cast<uint8_t>(r16 >> 8));
            Out.push_back(static_cast<uint8_t>(v16 & 0xFF));
            Out.push_back(static_cast<uint8_t>(v16 >> 8));
            i += run;
        }
    }

    // 실패(손상된 데이터)면 false. 성공 시 Out 은 정확히 CHUNK_VOX 개.
    inline bool DecodeRLE(const uint8_t* In, size_t Len, int16_t* Out)
    {
        if (Len % 4 != 0) return false;
        int w = 0;
        for (size_t p = 0; p < Len; p += 4)
        {
            const uint16_t run = static_cast<uint16_t>(In[p] | (In[p + 1] << 8));
            const int16_t  val = static_cast<int16_t>(In[p + 2] | (In[p + 3] << 8));
            if (run == 0 || w + run > CHUNK_VOX) return false;
            for (int k = 0; k < run; ++k) Out[w++] = val;
        }
        return w == CHUNK_VOX;
    }
}
