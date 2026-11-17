// 제작/설치물 패킷과 레시피 표 — Protocol.h 맨 아래, ProtocolItem.h 다음에 include
// 서버와 클라가 같이 쓰므로 언리얼 타입을 쓰지 말 것
#pragma once
#include "Protocol.h"

// ---------------------------------------------------------------- 설치물 종류
// 새 종류는 OBJ_COUNT 바로 위에 추가 (중간에 끼우면 번호가 밀린다)
enum OBJECT_TYPE : unsigned char
{
    OBJ_SMALL_PRINTER = 0,     // 소형 프린터 (시작 시 배치)
    OBJ_SMELTER = 1,           // 제련 기계 (임시: 레시피로 제작)
    OBJ_STORAGE = 2,           // 보관함 (임시)

    OBJ_COUNT
};

// ---------------------------------------------------------------- 제작 결과
enum CRAFT_RESULT : unsigned char
{
    CRAFT_OK = 0,
    CRAFT_BAD_RECIPE,          // 없는 레시피 번호
    CRAFT_BAD_PRINTER,         // 없는 프린터이거나 프린터가 아님
    CRAFT_TOO_FAR,             // 프린터에서 너무 멂
    CRAFT_NO_MATERIAL,         // 재료 부족
    CRAFT_NO_SPACE,            // 놓을 땅을 못 찾음
};

// ---------------------------------------------------------------- 규칙 상수
constexpr float INTERACT_RANGE_CM = 500.f;    // 프린터를 쓸 수 있는 거리
constexpr float CRAFT_SPAWN_DIST_CM = 300.f;    // 제작물이 프린터 옆 몇 cm에 생기는지
constexpr float OBJECT_MOVE_RANGE_CM = 1500.f;   // 플레이어에서 이 거리 안으로만 옮길 수 있음

// ---------------------------------------------------------------- 레시피 표
// 서버: 검증과 차감에 사용 / 클라: 레시피 UI 표시에 사용
// 재료는 ProtocolItem.h의 ITEM_TYPE 번호
constexpr int MAX_RECIPE_INPUTS = 4;

struct RecipeInput
{
    unsigned char item;        // ITEM_TYPE
    unsigned char count;
};

struct Recipe
{
    unsigned char id;          // = RECIPES 배열의 칸 번호와 같게
    unsigned char output;      // OBJECT_TYPE
    unsigned char num_inputs;
    RecipeInput   inputs[MAX_RECIPE_INPUTS];
};

// 임시 레시피. 수치는 기획에 맞게 바꿀 것 (id는 0부터 순서대로)
constexpr Recipe RECIPES[] =
{
    { 0, OBJ_SMELTER, 2, { { ITEM_RESIN, 2 }, { ITEM_COMPOUND, 1 } } },
    { 1, OBJ_STORAGE, 1, { { ITEM_RESIN, 1 } } },
};
constexpr int RECIPE_COUNT = static_cast<int>(sizeof(RECIPES) / sizeof(RECIPES[0]));

#pragma pack(push, 1)

// ---------------------------------------------------------------- 공통: 행성 위 위치/방향
// 위치 + 로컬 X축(앞) + 로컬 Z축(위). 클라는 MakeFromZX(Z, X)로 회전을 만든다
struct obj_transform_net
{
    float m_x, m_y, m_z;
    float m_xx, m_xy, m_xz;
    float m_zx, m_zy, m_zz;
};

// ---------------------------------------------------------------- 서버 -> 클라
// 설치물 생성 (접속 시 목록 전송, 제작 완료 시 방송)
struct sc_packet_object_add : PACKET_HEADER
{
    int               m_id;
    unsigned char     m_type;      // OBJECT_TYPE
    obj_transform_net m_t;
};

// 설치물 위치 변경 (옮기기 확정 시 방송)
struct sc_packet_object_move : PACKET_HEADER
{
    int               m_id;
    obj_transform_net m_t;
};

// 설치물 제거 (나중에 해체 기능용)
struct sc_packet_object_remove : PACKET_HEADER
{
    int m_id;
};

// 제작 요청의 결과 (요청한 사람에게만)
// 재료 차감은 sc_packet_item_change(음수 delta)로 따로 온다
struct sc_packet_craft_result : PACKET_HEADER
{
    unsigned char m_recipe;
    unsigned char m_result;        // CRAFT_RESULT
    int           m_object_id;     // 성공 시 생긴 설치물 id, 실패 시 -1
};

// ---------------------------------------------------------------- 클라 -> 서버
// 제작 요청
struct cs_packet_craft : PACKET_HEADER
{
    int           m_printer_id;
    unsigned char m_recipe;
};

// 옮기기 요청 (드래그를 놓았을 때 1번)
// 위치는 원하는 지점, 방향은 앞쪽 벡터. 서버가 땅에 맞춰 최종 위치/축을 정해 방송한다
struct cs_packet_object_move : PACKET_HEADER
{
    int   m_id;
    float m_x, m_y, m_z;
    float m_fx, m_fy, m_fz;
};

#pragma pack(pop)