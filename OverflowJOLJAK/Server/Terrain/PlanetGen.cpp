// 공유 공식을 서버 FastNoise 로 컴파일한다.
#define PLANET_SERVER 1

#include "PlanetGen.h"
#include "PlanetCore.h"

#include <fstream>
#include <sstream>

static FPlanetConfig LoadConfig(const char* Path, bool& bOk, std::string& Err)
{
    FPlanetConfig C;
    std::ifstream F(Path, std::ios::binary);
    if (!F)
    {
        bOk = false;
        Err = std::string("Planet.cfg 를 열 수 없음: ") + Path;
        return C;
    }
    std::stringstream SS;
    SS << F.rdbuf();
    bOk = C.FromText(SS.str(), &Err);
    return C;
}

struct FPlanetGen::FImpl
{
    FPlanetCore Core;
    explicit FImpl(const FPlanetConfig& C) : Core(C) {}
};

FPlanetGen::FPlanetGen(const char* ConfigPath)
{
    const FPlanetConfig C = LoadConfig(ConfigPath, bOk, Err);
    Impl.reset(new FImpl(C));
}

FPlanetGen::~FPlanetGen() = default;

double   FPlanetGen::Radius() const     { return Impl->Core.GetConfig().Radius; }
uint64_t FPlanetGen::ConfigHash() const { return Impl->Core.GetConfig().Hash(); }

double FPlanetGen::Value(double vx, double vy, double vz) const
{
    return Impl->Core.Value(vx, vy, vz);
}

FPlanetTrace FPlanetGen::Trace(double vx, double vy, double vz, bool bSphereOnly) const
{
    const FPlanetCoreTrace C = Impl->Core.Trace(vx, vy, vz, bSphereOnly);
    FPlanetTrace T;
    T.Dist = C.Dist;
    T.PerturbedX = C.PerturbedX; T.PerturbedY = C.PerturbedY; T.PerturbedZ = C.PerturbedZ;
    T.RawIQ = C.RawIQ; T.ClampedIQ = C.ClampedIQ;
    T.CurveInput = C.CurveInput; T.CurveOutput = C.CurveOutput;
    T.SurfaceRadius = C.SurfaceRadius; T.Result = C.Result;
    return T;
}
