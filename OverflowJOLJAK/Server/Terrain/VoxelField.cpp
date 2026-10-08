#define _CRT_SECURE_NO_WARNINGS
#include "VoxelField.h"
#include "..\..\Shared\Protocol.h"   // PACKET_HEADER + ProtocolTerrain.h

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

using namespace VoxelEdit;

// ============================================================================ 읽기

float FVoxelField::Value(int32_t vx, int32_t vy, int32_t vz) const
{
    const ChunkKey K = KeyOf(vx, vy, vz);
    {
        std::shared_lock<std::shared_mutex> Lock(Mutex);
        auto It = Chunks.find(K);
        if (It != Chunks.end())
        {
            const int i = LocalIndex(vx - K.x * CHUNK, vy - K.y * CHUNK, vz - K.z * CHUNK);
            const int16_t q = It->second->V[i];
            if (q != UNSET) return Dequantize(q);
        }
    }
    // 편집 안 된 곳: 생성기 값을 저장 형식으로 양자화 (클라 플러그인과 같은 경로)
    return Dequantize(Quantize(Gen.Value(vx, vy, vz)));
}

// ============================================================================ 편집

bool FVoxelField::ApplyEdit(uint8_t Op, float Cx, float Cy, float Cz, float Radius,
                            const std::function<void(const FEditResult&)>& OnCommitted)
{
    if (Op > static_cast<uint8_t>(EOp::Build)) return false;

    const double cx = ToVoxel(Cx), cy = ToVoxel(Cy), cz = ToVoxel(Cz);
    double r = ToVoxel(Radius);
    if (!(r > 0.0)) return false;
    if (r > MAX_RADIUS_VOX) r = MAX_RADIUS_VOX;

    int32_t minX, minY, minZ, maxX, maxY, maxZ;
    SphereBounds(cx, cy, cz, r, minX, minY, minZ, maxX, maxY, maxZ);

    const int32_t sx = maxX - minX + 1, sy = maxY - minY + 1, sz = maxZ - minZ + 1;

    const ChunkKey K0 = KeyOf(minX, minY, minZ);
    const ChunkKey K1 = KeyOf(maxX, maxY, maxZ);

    auto BoxIndex = [&](int32_t x, int32_t y, int32_t z)
    {
        return (static_cast<size_t>(z - minZ) * sy + (y - minY)) * sx + (x - minX);
    };

    // 1) 이미 값이 있는 칸은 복사해 오고 (shared_lock, 짧음)
    std::vector<int16_t> GenBox(static_cast<size_t>(sx) * sy * sz, UNSET);
    {
        std::shared_lock<std::shared_mutex> Lock(Mutex);
        for (int32_t kz = K0.z; kz <= K1.z; ++kz)
        for (int32_t ky = K0.y; ky <= K1.y; ++ky)
        for (int32_t kx = K0.x; kx <= K1.x; ++kx)
        {
            auto It = Chunks.find(ChunkKey{ kx, ky, kz });
            if (It == Chunks.end()) continue;
            const FChunk& C = *It->second;

            const int32_t bx = kx * CHUNK, by = ky * CHUNK, bz = kz * CHUNK;
            const int32_t x0 = std::max(minX, bx), x1 = std::min(maxX, bx + CHUNK - 1);
            const int32_t y0 = std::max(minY, by), y1 = std::min(maxY, by + CHUNK - 1);
            const int32_t z0 = std::max(minZ, bz), z1 = std::min(maxZ, bz + CHUNK - 1);

            for (int32_t z = z0; z <= z1; ++z)
            for (int32_t y = y0; y <= y1; ++y)
            for (int32_t x = x0; x <= x1; ++x)
                GenBox[BoxIndex(x, y, z)] = C.V[LocalIndex(x - bx, y - by, z - bz)];
        }
    }

    // 2) 값이 없는 칸만 락 밖에서 생성기로 계산 (같은 자리를 또 파면 거의 0)
    for (int32_t z = minZ; z <= maxZ; ++z)
    for (int32_t y = minY; y <= maxY; ++y)
    for (int32_t x = minX; x <= maxX; ++x)
    {
        int16_t& g = GenBox[BoxIndex(x, y, z)];
        if (g == UNSET) g = Quantize(Gen.Value(x, y, z));
    }

    // 3) 적용 + 순번 + 브로드캐스트를 한 번의 unique_lock 안에서
    std::unique_lock<std::shared_mutex> Lock(Mutex);

    FEditResult R;
    R.Op = Op; R.Cx = Cx; R.Cy = Cy; R.Cz = Cz; R.Radius = Radius;

    for (int32_t kz = K0.z; kz <= K1.z; ++kz)
    for (int32_t ky = K0.y; ky <= K1.y; ++ky)
    for (int32_t kx = K0.x; kx <= K1.x; ++kx)
    {
        const ChunkKey K{ kx, ky, kz };

        auto It = Chunks.find(K);
        const bool bNew = (It == Chunks.end());
        if (bNew) It = Chunks.emplace(K, std::unique_ptr<FChunk>(new FChunk())).first;
        FChunk& C = *It->second;

        const int32_t bx = kx * CHUNK, by = ky * CHUNK, bz = kz * CHUNK;
        const int32_t x0 = std::max(minX, bx), x1 = std::min(maxX, bx + CHUNK - 1);
        const int32_t y0 = std::max(minY, by), y1 = std::min(maxY, by + CHUNK - 1);
        const int32_t z0 = std::max(minZ, bz), z1 = std::min(maxZ, bz + CHUNK - 1);

        bool bChanged = false;
        for (int32_t z = z0; z <= z1; ++z)
        for (int32_t y = y0; y <= y1; ++y)
        for (int32_t x = x0; x <= x1; ++x)
        {
            int16_t& v = C.V[LocalIndex(x - bx, y - by, z - bz)];
            if (v == UNSET) v = GenBox[BoxIndex(x, y, z)];   // 계산한 생성기 값을 저장해 두고 다음에 재사용
            const int16_t n = ApplySphere(v, x, y, z, cx, cy, cz, r, static_cast<EOp>(Op));
            if (n != v) { v = n; bChanged = true; }
        }

        if (bChanged)
        {
            ++C.Version;
            if (R.NumTouched < MAX_TOUCHED)
            {
                R.Keys[R.NumTouched] = K;
                R.Versions[R.NumTouched] = C.Version;
                ++R.NumTouched;
            }
        }
        else if (bNew)
        {
            Chunks.erase(It);   // 방금 만들었는데 안 바뀌었으면 메모리 낭비
        }
    }

    if (R.NumTouched == 0) return false;

    R.Seq = ++Seq;
    OnCommitted(R);            // 여기서 모든 클라 송신 큐에 넣는다 (블로킹 금지)
    return true;
}

void FVoxelField::TrackEditSent(const FEditResult& R, FClientTerrainState& Client)
{
    for (int i = 0; i < R.NumTouched; ++i)
    {
        uint32_t& Local = Client.Versions[R.Keys[i]];
        if (Local + 1 == R.Versions[i]) Local = R.Versions[i];
        // 아니면 클라는 stale 로 둔다. StreamNearby 가 나중에 스냅샷을 보낸다.
    }
}

// ============================================================================ 스트리밍

void FVoxelField::SendChunkLocked(const ChunkKey& K, const FChunk& C,
                                  FClientTerrainState& Client, const FSendFn& Send) const
{
    std::vector<uint8_t> Rle;
    EncodeRLE(C.V, Rle);

    const int Total = static_cast<int>(Rle.size());
    const int Count = (Total + TerrainPkt::FRAG_PAYLOAD_MAX - 1) / TerrainPkt::FRAG_PAYLOAD_MAX;

    std::vector<uint8_t> Buf;
    for (int f = 0; f < Count; ++f)
    {
        const int Off = f * TerrainPkt::FRAG_PAYLOAD_MAX;
        const int Len = std::min(TerrainPkt::FRAG_PAYLOAD_MAX, Total - Off);

        sc_packet_terrain_chunk H;
        H.m_size        = static_cast<unsigned short>(sizeof(H) + Len);
        H.m_type        = PKT_S2C_TERRAIN_CHUNK;
        H.m_kx = K.x; H.m_ky = K.y; H.m_kz = K.z;
        H.m_version     = C.Version;
        H.m_total_bytes = static_cast<unsigned int>(Total);
        H.m_frag_index  = static_cast<unsigned short>(f);
        H.m_frag_count  = static_cast<unsigned short>(Count);
        H.m_payload_len = static_cast<unsigned short>(Len);

        Buf.resize(sizeof(H) + Len);
        std::memcpy(Buf.data(), &H, sizeof(H));
        std::memcpy(Buf.data() + sizeof(H), Rle.data() + Off, Len);
        Send(Buf.data(), Buf.size());
    }

    Client.Versions[K] = C.Version;
}

void FVoxelField::StreamNearby(double Pvx, double Pvy, double Pvz, int RadiusChunks, int MaxChunks,
                               FClientTerrainState& Client, const FSendFn& Send) const
{
    const ChunkKey P = KeyOf(static_cast<int32_t>(std::floor(Pvx)),
                             static_cast<int32_t>(std::floor(Pvy)),
                             static_cast<int32_t>(std::floor(Pvz)));
    const int64_t R2 = static_cast<int64_t>(RadiusChunks) * RadiusChunks;

    std::shared_lock<std::shared_mutex> Lock(Mutex);

    // 편집된 청크 수가 수천 단위를 넘으면 공간 인덱스로 바꿀 것
    std::vector<std::pair<int64_t, const std::pair<const ChunkKey, std::unique_ptr<FChunk>>*>> Cand;
    for (const auto& E : Chunks)
    {
        const int64_t dx = E.first.x - P.x, dy = E.first.y - P.y, dz = E.first.z - P.z;
        const int64_t d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > R2) continue;

        auto It = Client.Versions.find(E.first);
        const uint32_t Have = (It == Client.Versions.end()) ? 0u : It->second;
        if (Have >= E.second->Version) continue;

        Cand.emplace_back(d2, &E);
    }

    std::sort(Cand.begin(), Cand.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    const int N = std::min<int>(MaxChunks, static_cast<int>(Cand.size()));
    for (int i = 0; i < N; ++i)
        SendChunkLocked(Cand[i].second->first, *Cand[i].second->second, Client, Send);
}

void FVoxelField::SendChunk(const ChunkKey& K, FClientTerrainState& Client, const FSendFn& Send) const
{
    std::shared_lock<std::shared_mutex> Lock(Mutex);
    auto It = Chunks.find(K);
    if (It == Chunks.end()) return;   // 편집 안 된 청크: 클라가 생성기로 가지고 있다
    SendChunkLocked(K, *It->second, Client, Send);
}

// ============================================================================ 저장

bool FVoxelField::Save(const char* Path) const
{
    std::shared_lock<std::shared_mutex> Lock(Mutex);

    FILE* F = std::fopen(Path, "wb");
    if (!F) return false;

    const uint32_t Count = static_cast<uint32_t>(Chunks.size());
    std::fwrite(&Count, 4, 1, F);

    std::vector<uint8_t> Rle;
    for (const auto& E : Chunks)
    {
        EncodeRLE(E.second->V, Rle);
        const uint32_t Len = static_cast<uint32_t>(Rle.size());
        std::fwrite(&E.first.x, 4, 1, F);
        std::fwrite(&E.first.y, 4, 1, F);
        std::fwrite(&E.first.z, 4, 1, F);
        std::fwrite(&E.second->Version, 4, 1, F);
        std::fwrite(&Len, 4, 1, F);
        std::fwrite(Rle.data(), 1, Len, F);
    }
    std::fclose(F);
    return true;
}

bool FVoxelField::Load(const char* Path)
{
    FILE* F = std::fopen(Path, "rb");
    if (!F) return false;

    std::unique_lock<std::shared_mutex> Lock(Mutex);

    uint32_t Count = 0;
    if (std::fread(&Count, 4, 1, F) != 1) { std::fclose(F); return false; }

    std::vector<uint8_t> Rle;
    for (uint32_t i = 0; i < Count; ++i)
    {
        ChunkKey K; uint32_t Ver = 0, Len = 0;
        if (std::fread(&K.x, 4, 1, F) != 1 || std::fread(&K.y, 4, 1, F) != 1 ||
            std::fread(&K.z, 4, 1, F) != 1 || std::fread(&Ver, 4, 1, F) != 1 ||
            std::fread(&Len, 4, 1, F) != 1)
        { std::fclose(F); return false; }

        Rle.resize(Len);
        if (Len && std::fread(Rle.data(), 1, Len, F) != Len) { std::fclose(F); return false; }

        std::unique_ptr<FChunk> C(new FChunk());
        if (!DecodeRLE(Rle.data(), Len, C->V)) { std::fclose(F); return false; }
        C->Version = Ver ? Ver : 1;
        Chunks[K] = std::move(C);
    }
    std::fclose(F);
    return true;
}

size_t FVoxelField::NumEditedChunks() const
{
    std::shared_lock<std::shared_mutex> Lock(Mutex);
    return Chunks.size();
}
