// 26. 08. 22 최신화
#pragma once

// 헤더: 패킷 전체 크기(size) + 패킷 종류(type) 모든 패킷은 이 헤더로 시작
#pragma pack(push, 1)   // 패딩 (컴파일러가 자동으로 끼워넣는 빈 공간 - 성능을 위해) 없애기
struct PACKET_HEADER
{
    unsigned short m_size;  // 2바이트
    unsigned char  m_type;  // 1바이트
};

struct cs_packet_move : PACKET_HEADER   // 3 + 12바이트
{
    float m_x, m_y, m_z;
};

struct sc_packet_add_player : PACKET_HEADER     // 3 + 20바이트
{
    int m_id;
    int m_visual;
    float m_x, m_y, m_z;
};

struct sc_packet_position : PACKET_HEADER       // 3 + 16바이트
{
    int m_id;
    float m_x, m_y, m_z;
};

struct sc_packet_remove_player : PACKET_HEADER      // 3 + 4바이트
{
    int m_id;
};

struct sc_packet_monster_spawn : PACKET_HEADER      // 3 + 21바이트
{
    int m_id;
    unsigned char m_monster_type;
    float m_x, m_y, m_z;
    int m_hp;
};
#pragma pack(pop)   // 여기까지만 적용

enum PACKET_TYPE : unsigned char    // 네트워크를 통해 밖으로 나가기 때문에 타입(크기) 명시
{
    PKT_C2S_MOVE = 1,
    PKT_S2C_ADD_PLAYER = 2,
    PKT_S2C_POSITION = 3,
    PKT_S2C_REMOVE_PLAYER = 4,
    PKT_S2C_MONSTER_SPAWN = 5
};