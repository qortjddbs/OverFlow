// 서버의 현재 지형 = 생성기(원본) + 편집된 청크(절대값 int16).
//
// 저장/전송 원칙
//   - 편집된 청크만 메모리에 둔다. 나머지는 PlanetGen 으로 계산한다.
//   - 청크마다 version. 편집할 때마다 +1. 클라는 청크별 버전을 추적한다.
//   - 손으로 조각한 초기 맵도 같은 형식(Load)으로 들어온다. 부팅 시 version 1.
//   - 접속 시 전체 전송 금지. StreamNearby 가 플레이어 주변의 뒤처진 청크만 보낸다.
//
// 동시성
//   - 읽기(Value, 스트리밍)는 shared_lock, 편집은 unique_lock.
//   - 편집 브로드캐스트는 onCommitted 콜백으로 "락 안에서" 큐잉한다.
//     그래야 IOCP 워커가 여럿이어도 모든 클라가 같은 순서로 편집을 받는다.
//   - FClientTerrainState 는 편집 콜백(unique)과 StreamNearby(shared) 안에서만 만진다.
//     둘은 서로 배타이므로 별도 락이 필요 없다. 한 클라의 StreamNearby 는 한 스레드에서만 부를 것.
#pragma once

#include "PlanetGen.h"
#include "VoxelEditShared.h"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

struct FClientTerrainState
{
    // 클라가 가지고 있다고 서버가 아는 청크 버전. 없으면 0 (= 생성기 원본).
    std::unordered_map<VoxelEdit::ChunkKey, uint32_t, VoxelEdit::ChunkKeyHash> Versions;
};

struct FEditResult
{
    uint8_t  Op = 0;
    float    Cx = 0, Cy = 0, Cz = 0, Radius = 0;   // 받은 월드 cm 그대로
    uint32_t Seq = 0;
    int      NumTouched = 0;
    VoxelEdit::ChunkKey Keys[VoxelEdit::MAX_TOUCHED];
    uint32_t Versions[VoxelEdit::MAX_TOUCHED] = {};
};

class FVoxelField
{
public:
    explicit FVoxelField(const FPlanetGen& InGen) : Gen(InGen) {}

    // ---------------------------------------------------------------- 읽기
    // 저장 형식과 같은 양자화를 거친 값 [-1, 1]. 판정용.
    float Value(int32_t vx, int32_t vy, int32_t vz) const;

    // ---------------------------------------------------------------- 편집
    // 검증(사거리 등)은 호출 전에 할 것. 반경은 MAX_RADIUS_VOX 로 잘린다.
    // 실제로 값이 바뀐 청크가 없으면 false 이고 콜백도 불리지 않는다.
    bool ApplyEdit(uint8_t Op, float Cx, float Cy, float Cz, float Radius,
                   const std::function<void(const FEditResult&)>& OnCommitted);

    // 편집을 어떤 클라에 큐잉할 때 onCommitted 안에서 같이 호출.
    // 클라 쪽 적용 규칙과 똑같이 서버가 그 클라의 상태를 따라간다.
    static void TrackEditSent(const FEditResult& R, FClientTerrainState& Client);

    // ---------------------------------------------------------------- 스트리밍
    using FSendFn = std::function<void(const void* Data, size_t Len)>;

    // 플레이어(복셀 좌표) 주변 RadiusChunks 안에서 클라가 뒤처진 편집 청크를
    // 가까운 순으로 최대 MaxChunks 개 보낸다. 서버 틱(예: 10Hz)마다 클라별로 호출.
    void StreamNearby(double Pvx, double Pvy, double Pvz, int RadiusChunks, int MaxChunks,
                      FClientTerrainState& Client, const FSendFn& Send) const;

    // 특정 청크를 즉시 보냄 (cs_packet_terrain_chunk_want 처리용)
    void SendChunk(const VoxelEdit::ChunkKey& Key, FClientTerrainState& Client, const FSendFn& Send) const;

    // ---------------------------------------------------------------- 저장
    // 형식: [uint32 count] { [int32 kx,ky,kz][uint32 version][uint32 len][RLE] } ...
    // 손조각 굽기 도구도 이 형식으로 파일을 만든다.
    bool Save(const char* Path) const;
    bool Load(const char* Path);

    size_t NumEditedChunks() const;

private:
    struct FChunk
    {
        uint32_t Version = 0;
        int16_t  V[VoxelEdit::CHUNK_VOX];   // UNSET = 아직 계산 안 함 (생성기 값)

        FChunk() { for (int i = 0; i < VoxelEdit::CHUNK_VOX; ++i) V[i] = VoxelEdit::UNSET; }
    };

    void SendChunkLocked(const VoxelEdit::ChunkKey& Key, const FChunk& C,
                         FClientTerrainState& Client, const FSendFn& Send) const;

    const FPlanetGen& Gen;

    mutable std::shared_mutex Mutex;
    std::unordered_map<VoxelEdit::ChunkKey, std::unique_ptr<FChunk>, VoxelEdit::ChunkKeyHash> Chunks;
    uint32_t Seq = 0;
};
