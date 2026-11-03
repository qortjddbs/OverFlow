// 아이템 패킷 — Protocol.h 맨 아래(PACKET_HEADER 정의 뒤)에서 include 할 것.
#pragma once

// 아이템 번호표. 패킷으로 나가는 값이라 크기(1바이트)를 명시한다.
// 새 아이템은 ITEM_COUNT 바로 위에 추가할 것 (중간에 끼우면 번호가 밀린다)
enum ITEM_TYPE : unsigned char
{
    ITEM_SOIL = 0,          // 흙
    ITEM_SLIME_GEL = 1,     // 슬라임 젤 (슬라임 처치 시)

    ITEM_COUNT             // 아이템 종류 수 (배열 크기, 범위 검사용). 실제 아이템 아님
};

// 흙 게이지: m_total이 "0.01 m^3 단위 정수". 클라는 m_total / SOIL_CAPACITY 로 게이지 비율을 그린다
constexpr int SOIL_CAPACITY = 1000;     // 통 하나 = 10 m^3 (임시, 시연해 보고 조절)
#pragma pack(push, 1)

// 서버 -> 클라: 내 인벤토리의 아이템 하나가 바뀌었다.
// m_delta 로 연출(+1 팝업), m_total 로 숫자 덮어쓰기 -> 버그가 나도 다음 패킷에서 저절로 맞춰진다.
struct sc_packet_item_change : PACKET_HEADER     // 3 + 9바이트
{
    unsigned char m_item;    // ITEM_TYPE
    int m_delta;             // 이번 변화량 (획득 +, 나중에 가공 재료 소모는 -)
    int m_total;             // 바뀐 뒤 총 개수
};

#pragma pack(pop)