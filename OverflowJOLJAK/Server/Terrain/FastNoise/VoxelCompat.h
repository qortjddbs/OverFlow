// 서버용 호환 계층 — 언리얼 없이 Voxel Plugin 노이즈 코드를 컴파일하기 위한 최소 정의.
//
// 설계 원칙: 플러그인 원본 파일을 "전혀 수정하지 않는다".
// include 줄만 정리하고, 필요한 타입/함수는 전부 여기서 공급한다.
// 값이 다르면 지형이 달라지므로 언리얼 의미론과 정확히 같아야 한다.
#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <type_traits>

// ---------------------------------------------------------------- 기본 타입
using int8   = std::int8_t;    using uint8  = std::uint8_t;
using int16  = std::int16_t;   using uint16 = std::uint16_t;
using int32  = std::int32_t;   using uint32 = std::uint32_t;
using int64  = std::int64_t;   using uint64 = std::uint64_t;

// VOXEL_DOUBLE_PRECISION = 1 (플러그인 기본값). 반드시 플러그인과 일치시킬 것.
using v_flt = double;

// ---------------------------------------------------------------- 매크로
// FN_FORCEINLINE 계열은 VoxelFastNoiseMath.h 가 정의한다. 여기서 만들지 말 것.
#ifndef FORCEINLINE
#define FORCEINLINE inline
#endif

#ifndef VOXEL_API
#define VOXEL_API
#endif

#ifndef VOXEL_DEBUG
#define VOXEL_DEBUG 0
#endif

#ifndef PLATFORM_MAC
#define PLATFORM_MAC 0
#endif

#define VOXEL_ASYNC_FUNCTION_COUNTER()
#define checkVoxelSlow(...)
#define ensureVoxelSlow(x)                (!!(x))
#define ensureVoxelSlowNoSideEffects(x)   (!!(x))

#ifndef PI
#define PI 3.1415926535897932
#endif

template <typename T>
constexpr T&& Forward(std::remove_reference_t<T>& v) noexcept { return static_cast<T&&>(v); }
template <typename T>
constexpr T&& Forward(std::remove_reference_t<T>&& v) noexcept { return static_cast<T&&>(v); }

// ---------------------------------------------------------------- 고정 배열
template <typename T, int32 N>
struct TVoxelStaticArray
{
    T Data[N];

    void Memzero() { std::memset(Data, 0, sizeof(Data)); }
    constexpr int32 Num() const { return N; }

    T&       operator[](int32 i)       { return Data[i]; }
    const T& operator[](int32 i) const { return Data[i]; }
};

// ---------------------------------------------------------------- 벡터
struct FVector
{
    double X = 0, Y = 0, Z = 0;
    FVector() = default;
    FVector(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}
};

struct FVector2D
{
    double X = 0, Y = 0;
    FVector2D() = default;
    FVector2D(double InX, double InY) : X(InX), Y(InY) {}
};

struct FVector4
{
    double X = 0, Y = 0, Z = 0, W = 0;
    FVector4() = default;
    FVector4(double InX, double InY, double InZ, double InW) : X(InX), Y(InY), Z(InZ), W(InW) {}
};

// ---------------------------------------------------------------- FMath
struct FMath
{
    template <typename T> static T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
    template <typename T> static T Max(T a, T b)          { return a > b ? a : b; }
    template <typename T> static T Min(T a, T b)          { return a < b ? a : b; }
    template <typename T> static T Abs(T v)               { return v < T(0) ? -v : v; }
    template <typename T> static T Square(T v)            { return v * v; }
    template <typename T> static T Lerp(T a, T b, T t)    { return a + (b - a) * t; }

    static double Sqrt(double v)             { return std::sqrt(v); }
    static double Pow(double a, double b)    { return std::pow(a, b); }
    static double Fmod(double a, double b)   { return std::fmod(a, b); }
    static double Floor(double v)            { return std::floor(v); }
    static int32  FloorToInt(double v)       { return static_cast<int32>(std::floor(v)); }
    static int32  CeilToInt(double v)        { return static_cast<int32>(std::ceil(v)); }
    static int32  RoundToInt(double v)       { return static_cast<int32>(std::floor(v + 0.5)); }
    static int32  TruncToInt(double v)       { return static_cast<int32>(v); }
    static double DegreesToRadians(double d) { return d * (PI / 180.0); }

    static void SinCos(double* OutSin, double* OutCos, double A)
    {
        *OutSin = std::sin(A);
        *OutCos = std::cos(A);
    }
};

// ---------------------------------------------------------------- 회전 / 행렬
//
// 검증 필요 지점.
// 언리얼 FRotator 는 (Pitch, Yaw, Roll) 순서이고, 아래 행렬은
// FRotationTranslationMatrix 의 구성을 옮긴 것이다.
// SetMatrixFromRotation_3D 가 호출하는 ToMatrix(FRotator) 가 정말 이 행렬인지
// 숫자 대조로 반드시 확인할 것 (README 의 3-2 단계).
struct FRotator
{
    double Pitch = 0, Yaw = 0, Roll = 0;
    FRotator() = default;
    FRotator(double InPitch, double InYaw, double InRoll)
        : Pitch(InPitch), Yaw(InYaw), Roll(InRoll) {}
};

struct FMatrix
{
    // M[row][col], 언리얼과 동일한 행 벡터 규약
    double M[4][4] = {
        {1,0,0,0},
        {0,1,0,0},
        {0,0,1,0},
        {0,0,0,1}
    };

    // 언리얼: TransformPosition(V) == TransformFVector4(FVector4(V, 1))
    FVector4 TransformPosition(const FVector& V) const
    {
        return FVector4(
            V.X * M[0][0] + V.Y * M[1][0] + V.Z * M[2][0] + M[3][0],
            V.X * M[0][1] + V.Y * M[1][1] + V.Z * M[2][1] + M[3][1],
            V.X * M[0][2] + V.Y * M[1][2] + V.Z * M[2][2] + M[3][2],
            V.X * M[0][3] + V.Y * M[1][3] + V.Z * M[2][3] + M[3][3]);
    }
};

inline FMatrix ToMatrix(const FRotator& R)
{
    double SP, CP, SY, CY, SR, CR;
    FMath::SinCos(&SP, &CP, FMath::DegreesToRadians(R.Pitch));
    FMath::SinCos(&SY, &CY, FMath::DegreesToRadians(R.Yaw));
    FMath::SinCos(&SR, &CR, FMath::DegreesToRadians(R.Roll));

    FMatrix Out;

    Out.M[0][0] = CP * CY;
    Out.M[0][1] = CP * SY;
    Out.M[0][2] = SP;
    Out.M[0][3] = 0.0;

    Out.M[1][0] = SR * SP * CY - CR * SY;
    Out.M[1][1] = SR * SP * SY + CR * CY;
    Out.M[1][2] = -SR * CP;
    Out.M[1][3] = 0.0;

    Out.M[2][0] = -(CR * SP * CY + SR * SY);
    Out.M[2][1] = CY * SR - CR * SP * SY;
    Out.M[2][2] = CR * CP;
    Out.M[2][3] = 0.0;

    Out.M[3][0] = 0.0;
    Out.M[3][1] = 0.0;
    Out.M[3][2] = 0.0;
    Out.M[3][3] = 1.0;

    return Out;
}

// 2D 경로는 Planet 생성기가 쓰지 않는다. 컴파일만 통과시키기 위한 껍데기.
struct FQuat2D
{
    double Angle = 0;
    explicit FQuat2D(double InAngle = 0) : Angle(InAngle) {}
};

struct FMatrix2x2
{
    double M[2][2] = {{1,0},{0,1}};

    FMatrix2x2() = default;
    FMatrix2x2(const FQuat2D& Q)
    {
        const double C = std::cos(Q.Angle);
        const double S = std::sin(Q.Angle);
        M[0][0] =  C; M[0][1] = S;
        M[1][0] = -S; M[1][1] = C;
    }
};

inline FMatrix2x2 ToMatrix(const FQuat2D& Q) { return FMatrix2x2(Q); }

// ---------------------------------------------------------------- SIMD 스텁
//
// 플러그인의 2D SIMD 경로를 "컴파일만" 되게 하는 4-wide 스칼라 구현.
// Planet 생성기는 3D 스칼라 경로만 쓰므로 실제로 호출되지 않는다.
// 그래도 동작은 맞춰 두었다 — 나중에 쓰게 되더라도 결과가 같도록.
struct VectorRegister     { float V[4] = {0,0,0,0}; };
struct VectorRegister4Int { int32 V[4] = {0,0,0,0}; };
using  VectorRegisterInt  = VectorRegister4Int;

inline VectorRegister MakeVectorRegister(float a, float b, float c, float d)
{ VectorRegister R; R.V[0]=a; R.V[1]=b; R.V[2]=c; R.V[3]=d; return R; }

inline VectorRegister4Int MakeVectorRegisterInt(int32 a, int32 b, int32 c, int32 d)
{ VectorRegister4Int R; R.V[0]=a; R.V[1]=b; R.V[2]=c; R.V[3]=d; return R; }

inline VectorRegister VectorAdd(const VectorRegister& a, const VectorRegister& b)
{ VectorRegister R; for (int i=0;i<4;++i) R.V[i]=a.V[i]+b.V[i]; return R; }

inline VectorRegister VectorSubtract(const VectorRegister& a, const VectorRegister& b)
{ VectorRegister R; for (int i=0;i<4;++i) R.V[i]=a.V[i]-b.V[i]; return R; }

inline VectorRegister VectorMultiply(const VectorRegister& a, const VectorRegister& b)
{ VectorRegister R; for (int i=0;i<4;++i) R.V[i]=a.V[i]*b.V[i]; return R; }

inline VectorRegister VectorFloor(const VectorRegister& a)
{ VectorRegister R; for (int i=0;i<4;++i) R.V[i]=std::floor(a.V[i]); return R; }

inline VectorRegister4Int VectorFloatToInt(const VectorRegister& a)
{ VectorRegister4Int R; for (int i=0;i<4;++i) R.V[i]=static_cast<int32>(a.V[i]); return R; }

inline VectorRegister VectorIntToFloat(const VectorRegister4Int& a)
{ VectorRegister R; for (int i=0;i<4;++i) R.V[i]=static_cast<float>(a.V[i]); return R; }

inline VectorRegister4Int VectorIntAdd(const VectorRegister4Int& a, const VectorRegister4Int& b)
{ VectorRegister4Int R; for (int i=0;i<4;++i) R.V[i]=a.V[i]+b.V[i]; return R; }

inline VectorRegister4Int VectorIntAnd(const VectorRegister4Int& a, const VectorRegister4Int& b)
{ VectorRegister4Int R; for (int i=0;i<4;++i) R.V[i]=a.V[i]&b.V[i]; return R; }

inline VectorRegister4Int VectorIntXor(const VectorRegister4Int& a, const VectorRegister4Int& b)
{ VectorRegister4Int R; for (int i=0;i<4;++i) R.V[i]=a.V[i]^b.V[i]; return R; }

inline VectorRegister4Int VectorIntMultiply(const VectorRegister4Int& a, const VectorRegister4Int& b)
{ VectorRegister4Int R; for (int i=0;i<4;++i) R.V[i]=a.V[i]*b.V[i]; return R; }

inline void VectorIntStore(const VectorRegister4Int& a, void* Ptr)
{ std::memcpy(Ptr, a.V, sizeof(a.V)); }

inline VectorRegister VectorLoad(const float* Ptr)
{ VectorRegister R; std::memcpy(R.V, Ptr, sizeof(R.V)); return R; }

namespace GlobalVectorConstants
{
    inline const VectorRegister4Int IntOne = MakeVectorRegisterInt(1, 1, 1, 1);
}
