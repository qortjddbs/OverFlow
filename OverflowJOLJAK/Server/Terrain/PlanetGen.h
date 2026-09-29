// 서버용 래퍼. 공유 공식(PlanetCore.h)을 감싸되 언리얼/노이즈 타입은 노출하지 않는다.
// server.cpp 에서는 이 헤더만 include 할 것.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct FPlanetTrace
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

class FPlanetGen
{
public:
    // ConfigPath: 에디터가 저장한 Planet.cfg. 서버 작업 폴더(Server/) 기준이면 "../Config/Planet.cfg"
    explicit FPlanetGen(const char* ConfigPath);
    ~FPlanetGen();

    FPlanetGen(const FPlanetGen&) = delete;
    FPlanetGen& operator=(const FPlanetGen&) = delete;

    // 파일을 못 읽었으면 false. 이때 지형은 기본값(반지름 10000, 커브 없음)이다.
    bool Ok() const { return bOk; }
    const std::string& Error() const { return Err; }

    double   Radius() const;
    uint64_t ConfigHash() const;   // 나중에 접속 시 클라와 비교

    // 복셀 좌표 -> 원시 밀도. 여러 스레드에서 동시에 불러도 된다.
    double Value(double vx, double vy, double vz) const;

    FPlanetTrace Trace(double vx, double vy, double vz, bool bSphereOnly = false) const;

private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
    bool bOk = false;
    std::string Err;
};
