// 아이템 패킷 — Protocol.h 맨 아래(PACKET_HEADER 정의 뒤)에서 include 할 것.
#pragma once

// 아이템 번호표. 패킷으로 나가는 값이라 크기(1바이트)를 명시한다.
// 새 아이템은 ITEM_COUNT 바로 위에 추가할 것 (중간에 끼우면 번호가 밀린다)
enum ITEM_TYPE : unsigned char
{
    ITEM_SOIL = 0,          // 흙 (초기상태)
    ITEM_SLIME_GEL = 1,     // 슬라임 젤 (슬라임 처치 시)
    ITEM_RESIN = 2,         // 레진
    ITEM_COMPOUND = 3,      // 화합물
    ITEM_ORGANIC = 4,       // 유기물

    ITEM_COUNT             // 아이템 종류 수 (배열 크기, 범위 검사용). 실제 아이템 아님
};

// 흙 게이지: m_total이 "단위 정수". 클라는 m_total / SOIL_CAPACITY 로 게이지 비율을 그린다
constexpr int SOIL_CAPACITY = 1000;     // 통 하나 (꾹 누르고 약 1분)

constexpr int   MAX_DEPOSIT_PIECES = 16;          // 송이 하나의 최대 조각 수
constexpr float DEPOSIT_PIECE_BASE_SCALE = 0.4f;  // 조각 기본 배율 (메시가 약 1m라서)
constexpr int   GAUGE_MAX = 100;                  // 게이지가 이만큼 차면 자원 1개
#pragma pack(push, 1)

// 서버 -> 클라: 내 인벤토리의 아이템 하나가 바뀌었다.
// m_delta 로 연출(+1 팝업), m_total 로 숫자 덮어쓰기 -> 버그가 나도 다음 패킷에서 저절로 맞춰진다.
struct sc_packet_item_change : PACKET_HEADER     // 3 + 9바이트
{
    unsigned char m_item;    // ITEM_TYPE
    int m_delta;             // 이번 변화량 (획득 +, 나중에 가공 재료 소모는 -)
    int m_total;             // 바뀐 뒤 총 개수
};

// 조각 하나 (48바이트)
struct deposit_piece_net
{
    float m_x, m_y, m_z;       // 피벗(바닥 중앙) 위치
    float m_xx, m_xy, m_xz;    // 조각의 로컬 X축 방향
    float m_zx, m_zy, m_zz;    // 조각의 로컬 Z축(위) 방향
    float m_sx, m_sy, m_sz;    // 축별 배율 (기본 배율 제외)
};

// 서버 -> 클라: 송이 생성 (접속 시 목록 전송에도 사용)
struct sc_packet_deposit_add : PACKET_HEADER        // 3 + 4 + 1 + 1 + 2 + 48 x 16 = 779 바이트 (현재)
{
    int                m_id;
    unsigned char      m_item;         // ITEM_TYPE
    unsigned char      m_count;        // 조각 수
    unsigned short     m_alive_mask;   // 남아 있는 조각 (i번째 비트 = i번 조각)
    deposit_piece_net  m_pieces[MAX_DEPOSIT_PIECES];
};

// 서버 -> 클라: 조각 하나가 빨려 들어감
struct sc_packet_piece_absorb : PACKET_HEADER
{
    int           m_deposit_id;
    unsigned char m_piece;       // 조각 번호
    int           m_player_id;   // 빨아들인 플레이어 (파편이 날아갈 방향)
};

// 서버 -> 클라(본인): 게이지 값
struct sc_packet_gauge : PACKET_HEADER
{
    unsigned char m_item;
    int           m_value;       // 0 ~ GAUGE_MAX
};
#pragma pack(pop)