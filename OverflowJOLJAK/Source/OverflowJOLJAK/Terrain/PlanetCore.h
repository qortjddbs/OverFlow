// 공유 지형 공식 — 언리얼(클라)과 IOCP 서버가 "이 파일 그대로" 컴파일한다.
// 공식(노이즈 조합)을 바꾸려면 여기를 고치고 양쪽을 다시 빌드한다.
// 수치(반지름, 노이즈 세기, 시드, 커브)는 코드가 아니라 Planet.cfg 에 있다.
//
// 서버: include 전에 PLANET_SERVER 를 1 로 정의 (이식된 FastNoise 사용)
// 클라: 아무것도 정의하지 않음 (플러그인 FastNoise 사용)
//
// 부호: 음수 = 땅, 양수 = 허공    좌표: 복셀 단위 (월드 cm / VoxelSize)
#pragma once

#if defined(PLANET_SERVER) && PLANET_SERVER
    #include "FastNoise/ServerFastNoise.h"
#else
    #include "FastNoise/VoxelFastNoise.h"
    #include "FastNoise/VoxelFastNoise.inl"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

// ===========================================================================
// 행성 수치. 에디터가 Planet.cfg 로 저장하고, 서버와 클라가 읽는다.
// ===========================================================================
struct FPlanetConfig
{
    double Radius        = 10000.0;   // 복셀. 월드 반지름 = Radius * VoxelSize
    double Frequency     = 2.0;
    double NoiseStrength = 0.02;
    int    NoiseSeed     = 1443;
    int    Octaves       = 15;

    // 커브를 균일 간격으로 샘플링한 값. 비어 있으면 커브 출력 0 (완전한 구체).
    double CurveMinT = -1.6;
    double CurveStep = 0.0125;
    std::vector<double> CurveSamples;

    std::string ToText() const
    {
        std::string S = "# Planet.cfg - 에디터 PlanetVoxelGenerator 가 자동 생성. 손으로 고치지 말 것.\n";
        char B[64];
        auto Num = [&](const char* K, double V) { std::snprintf(B, sizeof(B), "%.17g", V); S += K; S += '='; S += B; S += '\n'; };
        auto Int = [&](const char* K, int V)    { std::snprintf(B, sizeof(B), "%d", V);    S += K; S += '='; S += B; S += '\n'; };

        Num("Radius", Radius);
        Num("Frequency", Frequency);
        Num("NoiseStrength", NoiseStrength);
        Int("NoiseSeed", NoiseSeed);
        Int("Octaves", Octaves);
        Num("CurveMinT", CurveMinT);
        Num("CurveStep", CurveStep);

        S += "CurveSamples=";
        for (size_t i = 0; i < CurveSamples.size(); ++i)
        {
            std::snprintf(B, sizeof(B), "%.17g", CurveSamples[i]);
            if (i) S += ',';
            S += B;
        }
        S += '\n';
        return S;
    }

    bool FromText(const std::string& Text, std::string* OutError = nullptr)
    {
        std::istringstream In(Text);
        std::string Line;
        int Found = 0;

        while (std::getline(In, Line))
        {
            if (!Line.empty() && Line.back() == '\r') Line.pop_back();
            if (Line.empty() || Line[0] == '#') continue;

            const size_t Eq = Line.find('=');
            if (Eq == std::string::npos) continue;

            const std::string K = Line.substr(0, Eq);
            const std::string V = Line.substr(Eq + 1);

            if      (K == "Radius")        { Radius        = std::strtod(V.c_str(), nullptr); ++Found; }
            else if (K == "Frequency")     { Frequency     = std::strtod(V.c_str(), nullptr); ++Found; }
            else if (K == "NoiseStrength") { NoiseStrength = std::strtod(V.c_str(), nullptr); ++Found; }
            else if (K == "NoiseSeed")     { NoiseSeed     = std::atoi(V.c_str());            ++Found; }
            else if (K == "Octaves")       { Octaves       = std::atoi(V.c_str());            ++Found; }
            else if (K == "CurveMinT")     { CurveMinT     = std::strtod(V.c_str(), nullptr); ++Found; }
            else if (K == "CurveStep")     { CurveStep     = std::strtod(V.c_str(), nullptr); ++Found; }
            else if (K == "CurveSamples")
            {
                CurveSamples.clear();
                const char* P = V.c_str();
                while (*P)
                {
                    char* End = nullptr;
                    const double D = std::strtod(P, &End);
                    if (End == P) break;
                    CurveSamples.push_back(D);
                    P = End;
                    if (*P == ',') ++P;
                }
                ++Found;
            }
        }

        if (Found < 8)
        {
            if (OutError) *OutError = "Planet.cfg 에 항목이 빠져 있습니다";
            return false;
        }
        if (Radius <= 0 || CurveStep <= 0 || Octaves <= 0)
        {
            if (OutError) *OutError = "Planet.cfg 값이 잘못됐습니다";
            return false;
        }
        return true;
    }

    // 접속 시 서버/클라 불일치 검사용 (나중 단계에서 사용)
    uint64_t Hash() const
    {
        const std::string S = ToText();
        uint64_t H = 1469598103934665603ull;
        for (unsigned char C : S) { H ^= C; H *= 1099511628211ull; }
        return H;
    }
};

struct FPlanetCoreTrace
{
    double Dist = 0;
    double PerturbedX = 0, PerturbedY = 0, PerturbedZ = 0;
    double RawIQ = 0;
    double ClampedIQ = 0;
    double CurveInput = 0;
    double CurveOutput = 0;
    double SurfaceRadius = 0;
    double Result = 0;
};

// ===========================================================================
// 지형 공식. 생성 후에는 읽기 전용이라 여러 스레드에서 동시에 불러도 된다.
// ===========================================================================
class FPlanetCore
{
public:
    explicit FPlanetCore(const FPlanetConfig& InCfg) : Cfg(InCfg)
    {
        // --- 3D Gradient Perturb (원본 VoxelExample_Planet 그대로) ---
        PerturbNoise.SetSeed(1337);
        PerturbNoise.SetInterpolation(EVoxelNoiseInterpolation::Quintic);

        // --- 3D IQ Noise ---
        IQNoise.SetSeed(Cfg.NoiseSeed);
        IQNoise.SetInterpolation(EVoxelNoiseInterpolation::Quintic);
        IQNoise.SetFractalOctavesAndGain(Cfg.Octaves, 0.6);
        IQNoise.SetFractalLacunarity(2.0);
        IQNoise.SetFractalType(EVoxelNoiseFractalType::FBM);
        IQNoise.SetMatrixFromRotation_3D(FRotator(40.000000, 45.000000, 50.000000));

        CurveMin = CurveMax = 0.0;
        for (size_t i = 0; i < Cfg.CurveSamples.size(); ++i)
        {
            const double s = Cfg.CurveSamples[i];
            if (i == 0 || s < CurveMin) CurveMin = s;
            if (i == 0 || s > CurveMax) CurveMax = s;
        }
    }

    const FPlanetConfig& GetConfig() const { return Cfg; }

    FPlanetCoreTrace Trace(double vx, double vy, double vz, bool bSphereOnly = false) const
    {
        FPlanetCoreTrace Out;
        Out.Dist = std::sqrt(vx * vx + vy * vy + vz * vz);

        if (bSphereOnly || Out.Dist < 1e-9)
        {
            Out.SurfaceRadius = Cfg.Radius;
            Out.Result = Out.Dist - Cfg.Radius;
            return Out;
        }

        v_flt nx = vx / Out.Dist;
        v_flt ny = vy / Out.Dist;
        v_flt nz = vz / Out.Dist;

        // 원본의 float 리터럴을 그대로 둔다 (0.02f != 0.02)
        PerturbNoise.GradientPerturb_3D(nx, ny, nz, v_flt(0.02f), v_flt(0.01f));
        Out.PerturbedX = nx;
        Out.PerturbedY = ny;
        Out.PerturbedZ = nz;

        v_flt dx, dy, dz;
        Out.RawIQ = IQNoise.IQNoise_3D_Deriv(nx, ny, nz, Cfg.Frequency, Cfg.Octaves, dx, dy, dz);

        const double lo = -0.653693, hi = 0.750231;   // 원본 하드코딩 클램프
        Out.ClampedIQ = Out.RawIQ < lo ? lo : (Out.RawIQ > hi ? hi : Out.RawIQ);

        Out.CurveInput  = (Out.ClampedIQ - v_flt(0.1f)) / v_flt(0.5f);
        Out.CurveOutput = EvalCurve(Out.CurveInput);

        Out.SurfaceRadius = Cfg.Radius + Cfg.Radius * Out.CurveOutput * Cfg.NoiseStrength;
        Out.Result = Out.Dist - Out.SurfaceRadius;
        return Out;
    }

    double Value(double vx, double vy, double vz) const
    {
        return Trace(vx, vy, vz).Result;
    }

    // 박스 안에서 밀도가 가질 수 있는 범위 (보수적으로 넓게).
    // 플러그인이 이걸로 속이 꽉 찬/빈 청크를 계산 없이 건너뛴다. 절대 좁히지 말 것.
    void ValueRange(double minX, double minY, double minZ,
                    double maxX, double maxY, double maxZ,
                    double& OutMin, double& OutMax) const
    {
        const double ax = Near(minX, maxX), ay = Near(minY, maxY), az = Near(minZ, maxZ);
        const double bx = Far (minX, maxX), by = Far (minY, maxY), bz = Far (minZ, maxZ);

        const double dNear = std::sqrt(ax * ax + ay * ay + az * az);
        const double dFar  = std::sqrt(bx * bx + by * by + bz * bz);

        const double S = Cfg.Radius * Cfg.NoiseStrength;
        const double a = Cfg.Radius + S * CurveMin;
        const double b = Cfg.Radius + S * CurveMax;
        const double surfMin = a < b ? a : b;
        const double surfMax = a < b ? b : a;

        OutMin = dNear - surfMax - 1.0;
        OutMax = dFar  - surfMin + 1.0;
    }

private:
    // 균일 격자 Catmull-Rom. 범위 밖은 양 끝 값으로 고정.
    double EvalCurve(double t) const
    {
        const std::vector<double>& S = Cfg.CurveSamples;
        const int N = static_cast<int>(S.size());
        if (N == 0) return 0.0;
        if (N == 1) return S[0];

        const double MaxT = Cfg.CurveMinT + (N - 1) * Cfg.CurveStep;
        if (t <= Cfg.CurveMinT) return S[0];
        if (t >= MaxT)          return S[N - 1];

        const double f = (t - Cfg.CurveMinT) / Cfg.CurveStep;
        int i = static_cast<int>(f);
        if (i < 0) i = 0;
        if (i > N - 2) i = N - 2;
        const double a = f - i;

        const double p0 = S[i > 0 ? i - 1 : 0];
        const double p1 = S[i];
        const double p2 = S[i + 1];
        const double p3 = S[i + 2 < N ? i + 2 : N - 1];

        const double a2 = a * a, a3 = a2 * a;
        return 0.5 * ((2.0 * p1)
                    + (-p0 + p2) * a
                    + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * a2
                    + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * a3);
    }

    static double Near(double lo, double hi) { return lo > 0 ? lo : (hi < 0 ? -hi : 0.0); }
    static double Far (double lo, double hi) { const double A = std::fabs(lo), B = std::fabs(hi); return A > B ? A : B; }

    FPlanetConfig   Cfg;
    FVoxelFastNoise PerturbNoise;
    FVoxelFastNoise IQNoise;
    double CurveMin = 0, CurveMax = 0;
};
