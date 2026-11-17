// 제작/설치물 패킷과 레시피 표 — Protocol.h 맨 아래, ProtocolItem.h 다음에 include
// 서버와 클라가 같이 쓰므로 언리얼 타입을 쓰지 말 것
#pragma once
#include "Protocol.h"

// ---------------------------------------------------------------- 시설 종류
// 새 종류는 OBJ_COUNT 바로 위에 추가 (중간에 끼우면 번호가 밀린다)
enum OBJECT_TYPE : unsigned char
{
    OBJ_SMALL_PRINTER = 0,     // 소형 프린터 (시작 시 배치, 상호작용 가능)
    OBJ_SMALL_GENERATOR = 1,   // 소형 발전기 (상호작용 없음)
    OBJ_MEDIUM_PRINTER = 2,    // 중형 프린터 (이번 범위에서는 열리지 않음)

    OBJ_COUNT
};

// ---------------------------------------------------------------- 제작 결과
enum CRAFT_RESULT : unsigned char
{
    CRAFT_OK = 0,
    CRAFT_BAD_RECIPE,          // 없는 레시피 번호
    CRAFT_BAD_PRINTER,         // 없는 시설
    CRAFT_WRONG_STATION,       // 이 시설에서 만들 수 없는 레시피
    CRAFT_TOO_FAR,             // 시설에서 너무 멂
    CRAFT_NO_MATERIAL,         // 재료 부족
    CRAFT_NO_SPACE,            // 놓을 땅을 못 찾음
};

// ---------------------------------------------------------------- 규칙 상수
constexpr float INTERACT_RANGE_CM = 500.f;    // 시설을 쓸 수 있는 거리
constexpr float CRAFT_SPAWN_DIST_CM = 300.f;    // 제작물이 시설 왼쪽 몇 cm에 생기는지
constexpr float OBJECT_MOVE_RANGE_CM = 1500.f;   // 잡기/끌기 가능 거리 (플레이어 기준)
constexpr float OBJECT_DRAG_INTERVAL = 0.1f;     // 끄는 중 위치 전송 간격 (초)
constexpr int   NO_HOLDER = -1;                  // 아무도 안 잡고 있음

// ---------------------------------------------------------------- 레시피 표
// 서버: 검증과 차감 / 클라: 레시피 창 표시 (station이 같은 레시피만)
constexpr int MAX_RECIPE_INPUTS = 4;

struct RecipeInput
{
    unsigned char item;        // ITEM_TYPE
    unsigned char count;
};

struct Recipe
{
    unsigned char id;          // = RECIPES 배열의 칸 번호와 같게
    unsigned char station;     // 만드는 시설 (OBJECT_TYPE)
    unsigned char output;      // 만들어지는 시설 (OBJECT_TYPE)
    unsigned char num_inputs;
    RecipeInput   inputs[MAX_RECIPE_INPUTS];
};

constexpr Recipe RECIPES[] =
{
    { 0, OBJ_SMALL_PRINTER, OBJ_SMALL_GENERATOR, 1, { { ITEM_COMPOUND, 1 } } },
    { 1, OBJ_SMALL_PRINTER, OBJ_MEDIUM_PRINTER,  1, { { ITEM_COMPOUND, 2 } } },
};
constexpr int RECIPE_COUNT = static_cast<int>(sizeof(RECIPES) / sizeof(RECIPES[0]));

#pragma pack(push, 1)

// ---------------------------------------------------------------- 공통: 행성 위 위치/방향
// 위치(바닥 중앙) + 로컬 X축(앞) + 로컬 Z축(위). 클라는 MakeFromZX(Z, X)로 회전을 만든다
struct obj_transform_net
{
    float m_x, m_y, m_z;
    float m_xx, m_xy, m_xz;
    float m_zx, m_zy, m_zz;
};

// ---------------------------------------------------------------- 서버 -> 클라
// 시설 생성 (접속 시 목록 전송, 제작 완료 시 방송)
struct sc_packet_object_add : PACKET_HEADER
{
    int               m_id;
    unsigned char     m_type;      // OBJECT_TYPE
    int               m_holder;    // 잡고 있는 플레이어 id, 없으면 NO_HOLDER
    obj_transform_net m_t;
};

// 시설 위치 변경 (끄는 중이면 holder = 끄는 사람, 놓아서 확정되면 NO_HOLDER)
// 클라는 holder가 자기 자신이면 무시한다 (자기 화면은 직접 움직이고 있으므로)
struct sc_packet_object_move : PACKET_HEADER
{
    int               m_id;
    int               m_holder;
    obj_transform_net m_t;
};

// 시설 제거 (나중에 해체 기능용)
struct sc_packet_object_remove : PACKET_HEADER
{
    int m_id;
};

// 잡은 사람 변경
// - 잡기 성공: 모두에게 holder = 잡은 사람
// - 잡기 실패: 요청자에게만 holder = 현재 잡고 있는 사람
// - 놓기 / 잡은 사람 접속 끊김: 모두에게 holder = NO_HOLDER
struct sc_packet_object_hold : PACKET_HEADER
{
    int m_id;
    int m_holder;
};

// 제작 요청의 결과 (요청한 사람에게만)
// 재료 차감은 sc_packet_item_change(음수 delta)로 따로 온다
struct sc_packet_craft_result : PACKET_HEADER
{
    unsigned char m_recipe;
    unsigned char m_result;        // CRAFT_RESULT
    int           m_object_id;     // 성공 시 생긴 시설 id, 실패 시 -1
};

// ---------------------------------------------------------------- 클라 -> 서버
// 제작 요청
struct cs_packet_craft : PACKET_HEADER
{
    int           m_printer_id;    // 상호작용한 시설 id
    unsigned char m_recipe;
};

// 잡기 요청
struct cs_packet_object_grab : PACKET_HEADER
{
    int m_id;
};

// 끄는 중 위치(OBJECT_DRAG_INTERVAL마다) / 놓기(m_release = 1)
// 위치는 원하는 지점, 방향은 앞쪽 벡터
// 서버: 끄는 중이면 그대로 전달, 놓기면 땅에 맞춰 확정 후 방송 + holder 해제
struct cs_packet_object_drag : PACKET_HEADER
{
    int           m_id;
    float         m_x, m_y, m_z;
    float         m_fx, m_fy, m_fz;
    unsigned char m_release;       // 0 = 끄는 중, 1 = 놓기
};

#pragma pack(pop)