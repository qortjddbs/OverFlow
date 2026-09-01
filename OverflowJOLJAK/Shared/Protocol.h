// 26. 08. 24 최신화
#pragma once

// 헤더: 패킷 전체 크기(size) + 패킷 종류(type) 모든 패킷은 이 헤더로 시작
#pragma pack(push, 1)   // 패딩 (컴파일러가 자동으로 끼워넣는 빈 공간 - 성능을 위해) 없애기
struct PACKET_HEADER
{
    unsigned short m_size;  // 2바이트
    unsigned char  m_type;  // 1바이트
};

struct cs_packet_player_move : PACKET_HEADER   // 3 + 12바이트
{
    float m_x, m_y, m_z;
    float m_pitch, m_yaw, m_roll;
};

struct sc_packet_add_player : PACKET_HEADER     // 3 + 20바이트
{
    int m_id;
    int m_visual;
    float m_x, m_y, m_z;
    float m_pitch, m_yaw, m_roll;
};

struct sc_packet_player_position : PACKET_HEADER       // 3 + 16바이트
{
    int m_id;
    float m_x, m_y, m_z;
    float m_pitch, m_yaw, m_roll;
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

struct sc_packet_monster_position : PACKET_HEADER   // 3 + 16바이트
{
    int m_id;
    float m_x, m_y, m_z;
};

struct sc_packet_monster_attack : PACKET_HEADER     // 3 + 8바이트
{
    int m_id;
    int m_target_id;
};

struct cs_packet_player_attack : PACKET_HEADER      // 3 + 24바이트
{
    int m_target_monster_id; // 추가
    float m_origin_x, m_origin_y, m_origin_z;   // 발사 원점
    float m_dir_x, m_dir_y, m_dir_z;            // 발사 방향
};

struct sc_packet_monster_hp : PACKET_HEADER         // 3 + 8바이트
{
    int m_id;
    int m_hp;
};

struct sc_packet_monster_remove : PACKET_HEADER     // 3 + 4바이트
{
    int m_id;
};

struct cs_packet_player_fire : PACKET_HEADER        // 3 + 24바이트
{
    float m_muzzle_x, m_muzzle_y, m_muzzle_z;
    float m_dir_x, m_dir_y, m_dir_z;
};

struct sc_packet_player_fire : PACKET_HEADER        // 3 + 28바이트
{
    int m_shooter_id;
    float m_muzzle_x, m_muzzle_y, m_muzzle_z;
    float m_dir_x, m_dir_y, m_dir_z;
};

#pragma pack(pop)   // 여기까지만 적용

enum PACKET_TYPE : unsigned char    // 네트워크를 통해 밖으로 나가기 때문에 타입(크기) 명시
{
    PKT_C2S_PLAYER_MOVE = 1,
    PKT_S2C_ADD_PLAYER = 2,
    PKT_S2C_PLAYER_POSITION = 3,
    PKT_S2C_REMOVE_PLAYER = 4,
    PKT_S2C_MONSTER_SPAWN = 5,
    PKT_S2C_MONSTER_POSITION = 6,
    PKT_S2C_MONSTER_ATTACK = 7,
    PKT_C2S_PLAYER_ATTACK = 8,
    PKT_S2C_MONSTER_HP = 9,
    PKT_S2C_MONSTER_REMOVE = 10,
    PKT_C2S_FIRE = 11,
    PKT_S2C_PLAYER_FIRE = 12
};