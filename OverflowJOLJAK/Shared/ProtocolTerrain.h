// 지형 패킷 — Protocol.h 맨 아래(PACKET_HEADER 정의 뒤)에서 include 할 것.
// Protocol.h 와 같은 폴더에 두고 서버/클라 공유.
//
// 패킷 타입 번호는 기존 목록에 이 넷을 추가할 것 (이름만 맞으면 번호는 자유):
//   PKT_C2S_TERRAIN_EDIT, PKT_S2C_TERRAIN_EDIT, PKT_S2C_TERRAIN_CHUNK, PKT_C2S_TERRAIN_WANT
//
// 기존 cs_packet_dig / build 는 이 패킷으로 대체된다.
#pragma once

namespace TerrainPkt
{
    constexpr int FRAG_PAYLOAD_MAX = 4000;   // 청크 조각 하나의 데이터 상한 (바이트)
    constexpr int MAX_TOUCHED      = 8;      // 편집 하나가 건드리는 청크 최대 수
}

#pragma pack(push, 1)

// 클라 -> 서버: 땅 파기/쌓기 요청. 좌표는 월드 cm.
// 보낸 클라도 여기서 로컬 적용하지 않는다. 서버 방송을 받아서 적용한다.
struct cs_packet_terrain_edit : PACKET_HEADER
{
    unsigned char m_op;          // 0 = 파기, 1 = 쌓기
    float m_x, m_y, m_z;
    float m_radius;
};

struct terrain_touched
{
    int          m_kx, m_ky, m_kz;
    unsigned int m_version;      // 이 편집을 적용한 뒤의 청크 버전
};

// 서버 -> 모든 클라 (보낸 사람 포함). 고정 크기로 보낸다.
// 클라 규칙: 청크마다 내버전 == m_version-1 이면 적용 후 내버전 = m_version,
//            아니면 적용하지 말고 '받아야 할 청크'로 표시만.
struct sc_packet_terrain_edit : PACKET_HEADER
{
    unsigned char m_op;
    float m_x, m_y, m_z;         // 서버가 받은 값 그대로 (재계산 금지)
    float m_radius;
    unsigned int  m_seq;         // 디버그용 전역 순번
    unsigned char m_num_touched;
    terrain_touched m_touched[TerrainPkt::MAX_TOUCHED];
};

// 서버 -> 클라: 파인 청크 하나의 전체 값 (압축). 크면 여러 조각으로 나뉜다.
// 헤더 뒤에 m_payload_len 바이트가 붙는다. m_size = sizeof(이 구조체) + m_payload_len
// 클라 규칙: 조각을 다 모은 뒤 m_version > 내버전 이면 청크 전체를 덮어쓰기.
struct sc_packet_terrain_chunk : PACKET_HEADER
{
    int            m_kx, m_ky, m_kz;
    unsigned int   m_version;
    unsigned int   m_total_bytes;
    unsigned short m_frag_index;
    unsigned short m_frag_count;
    unsigned short m_payload_len;
};

// 클라 -> 서버 (선택): 받아야 할 청크를 바로 요청
struct cs_packet_terrain_want : PACKET_HEADER
{
    int m_kx, m_ky, m_kz;
};

#pragma pack(pop)
