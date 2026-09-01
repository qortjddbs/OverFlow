#include <winsock2.h>       // 함수 선언만 가져오기
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>

#include "..\Shared\Protocol.h"

#pragma comment(lib, "Ws2_32.lib")      // winsock2.h의 진짜 코드 가져오기

// constexpr -> 컴파일할 때 이미 확정되는 상수 (배열 크기에 넣어야 하기 때문 + 실수 방지)
// 실수 = 런타임에만 정해지는 값을 넣으면 컴파일 에러를 띄워줌. (그냥 const는 이게 안됨)

// 네트워크 관련 상수들
constexpr unsigned short LISTEN_PORT = 7777;
constexpr int MAX_BUF_SIZE = 4096;
constexpr int HEADER_SIZE = sizeof(PACKET_HEADER);
constexpr int MAX_PACKET_SIZE = sizeof(cs_packet_player_attack);  // 존재하는 패킷 중 제일 큰 거
constexpr int PREV_BUF_SIZE = MAX_BUF_SIZE + MAX_PACKET_SIZE;

// 플레이어 관련 상수들
constexpr int PLAYER_ATTACK_DAMAGE = 40;

// 몬스터 관련 상수들
constexpr float MONSTER_SPAWN_POSITION_X = 0.f;
constexpr float MONSTER_SPAWN_POSITION_Y = 0.f;
constexpr float MONSTER_SPAWN_POSITION_Z = 1000.f;
constexpr float MONSTER_SPAWN_MIN_RADIUS = 300.f;
constexpr float MONSTER_SPAWN_MAX_RADIUS = 1500.f;
constexpr float MONSTER_CHASE_RANGE = 1000.f;
constexpr float MONSTER_ATTACK_RANGE = 100.f;
constexpr float MONSTER_ATTACK_COOL = 1.f;
constexpr int MONSTER_HEARTBEAT = 100;
constexpr float MONSTER_MOVE_SPEED = 10.f;
//constexpr float MONSTER_HIT_RADIUS = 34.f;      // 몬스터 중심으로부터 히트박스(구) 반지름 길이

constexpr float MONSTER_HIT_RADIUS = 60.f;   // 몸통 반지름 (X,Y 조준 허용 오차). 슬라임 크기에 맞게.
constexpr float MONSTER_HIT_HEIGHT = 2000.f;  // 판정 기둥 높이. 서버-클라 Z 오차 흡수용으로 넉넉히.
constexpr float MONSTER_HIT_Z_MARGIN = 1000.f;  // 기둥을 몬스터 z에서 아래로 얼마나 더 내릴지 (여유).

enum enumOperation      // 얘는 내부에서만 쓰이는 값이라 따로 명시하지 않음
{
    OP_RECV,
    OP_SEND
};

struct EXP_OVER
{
    WSAOVERLAPPED  m_wsaOver;
    WSABUF         m_wsaBuf;
    char           m_netbuf[MAX_BUF_SIZE];
    enumOperation  m_Operation;
};

struct SESSION
{
    int         m_id = 0;
    SOCKET      m_s = INVALID_SOCKET;
    std::string m_addr; // 로그 출력용 "IP:포트" 문자열 (실제 통신에는 안 쓰임)
    EXP_OVER    m_recv_over{};

    char m_prev_buf[PREV_BUF_SIZE]{};
    int  m_prev_size = 0;

    float m_x = 0.f;
    float m_y = 0.f;
    float m_z = 0.f;

    float m_pitch = 0.f;
    float m_yaw = 0.f;
    float m_roll = 0.f;

    int m_visual = 0;       // 일단 임시로 생성. 나중가면 enum으로 따로 만들어야될듯. (디폴트 0 -> 기본 캐릭터)
};

enum MonsterState
{
    IDLE,
    CHASE,
    ATTACK
};

struct MONSTER
{
    int m_id = 0;
    unsigned int m_monster_type = 0;     // pragma pack이 없어 unsigned char로 해도 4바이트로 들어감. 그래서 그냥 unsigned int로
    float m_x = 0.f;
    float m_y = 0.f;
    float m_z = 0.f;
    int m_hp = 0;

    MonsterState m_state = IDLE;
    int m_target_id = 0;
    std::chrono::steady_clock::time_point m_last_attack;        // 항상 앞으로만 흐르는 시계(steady_clock)로 잰, 특정 시각 하나(time_point)
};

HANDLE g_h_iocp = nullptr;
SOCKET g_s_listen = INVALID_SOCKET;
std::atomic<int> g_next_id{ 1 }; // 클라이언트마다 겹치지 않는 id를 하나씩 나눠준다

std::mutex g_player_lock;                        // g_players 전체를 보호
std::unordered_map<int, SESSION> g_players;   // key = client id

std::mutex g_monster_lock;
std::vector <MONSTER> g_monsters;

std::vector<std::pair<int, std::chrono::steady_clock::time_point>> g_pending_respawn;

std::mutex g_console_lock;                  // cout/cerr이 여러 스레드에서 섞이지 않게

void error_display(const char* msg, int err_no)
{
    std::lock_guard<std::mutex> lock(g_console_lock);
    std::cerr << "[error] " << msg << " : " << err_no << "\n";  // std::cout과 같이 콘솔에 출력하는데, 버퍼링 없이 즉시 출력.
}

// std::thread::hardware_concurrency()는 하이퍼스레딩까지 포함한 "논리 프로세서" 수를
// 돌려준다. "물리 코어" 수만 세려면 표준 C++에는 방법이 없어서, Win32 API로 직접 세야 한다. (나중에 보기)
unsigned int get_physical_core_count()
{
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len); // 필요한 버퍼 크기만 먼저 물어본다

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0)
    {
        return std::thread::hardware_concurrency(); // 실패하면 논리 코어 수로 대체
    }

    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    if (!GetLogicalProcessorInformation(info.data(), &len))
    {
        return std::thread::hardware_concurrency();
    }

    unsigned int core_count = 0;
    for (const auto& e : info)
    {
        if (e.Relationship == RelationProcessorCore)
        {
            ++core_count; // 이 엔트리 하나 = 물리 코어 하나 (하이퍼스레딩 여부와 무관)
        }
    }
    return core_count;
}

// 다음 recv 예약하는 함수 (OS 커널에 비동기로 예약)
bool post_recv(SESSION* p)
{
    ZeroMemory(&p->m_recv_over.m_wsaOver, sizeof(WSAOVERLAPPED));
    p->m_recv_over.m_wsaBuf.buf = p->m_recv_over.m_netbuf;
    p->m_recv_over.m_wsaBuf.len = MAX_BUF_SIZE;
    p->m_recv_over.m_Operation = OP_RECV;

    DWORD recv_bytes = 0;
    DWORD flags = 0;
    int ret = WSARecv(p->m_s, &p->m_recv_over.m_wsaBuf, 1, &recv_bytes, &flags,
        &p->m_recv_over.m_wsaOver, nullptr);    // 이게 본체 (이 함수의 존재 이유)

    if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        error_display("WSARecv", WSAGetLastError());
        return false;
    }
    return true;
}

bool send_packet(SESSION* target, const void* pkt, int size)
{
    const char* packet = reinterpret_cast<const char*>(pkt);

    int total_sent = 0;
    while (total_sent < size)
    {
        int ret = send(target->m_s, packet + total_sent, size - total_sent, 0);
        if (ret == SOCKET_ERROR)
        {
            error_display("send", WSAGetLastError());
            return false;
        }
        total_sent += ret;
    }
    return true;
}

// g_player_lock을 잠그고 g_players에서 클라이언트를 지운다
void disconnect(int id) 
{
    std::lock_guard<std::mutex> lock(g_player_lock);
    auto it = g_players.find(id);
    if (it != g_players.end())
    {
        closesocket(it->second.m_s);
        {
            std::lock_guard<std::mutex> lock(g_console_lock);
            std::cout << "[server] client " << it->second.m_id << " (" << it->second.m_addr << ") disconnected\n";
        }

        sc_packet_remove_player rp;
        rp.m_size = sizeof(rp);
        rp.m_type = PKT_S2C_REMOVE_PLAYER;
        rp.m_id = id;

        for (auto& [myId, session] : g_players)
        {
            if (id == myId) continue;
            send_packet(&session, &rp, sizeof(rp));
        }

        g_players.erase(it);
    }
}

void update_position(SESSION* me)
{
    sc_packet_player_position up;
    up.m_size = sizeof(up);
    up.m_type = PKT_S2C_PLAYER_POSITION;
    up.m_id = me->m_id;
    up.m_x = me->m_x;
    up.m_y = me->m_y;
    up.m_z = me->m_z;
	up.m_pitch = me->m_pitch;
	up.m_yaw = me->m_yaw;
	up.m_roll = me->m_roll;

    {
        std::lock_guard<std::mutex> lock(g_player_lock);
        for (auto& [id, session] : g_players)
        {
            if (id == me->m_id) continue;
            send_packet(&session, &up, sizeof(up));
        }
    }
}

void handle_player_attack(SESSION* attacker, cs_packet_player_attack* pkt)  // 공격 판정
{

    float dx = pkt->m_dir_x;
    float dy = pkt->m_dir_y;
    float dz = pkt->m_dir_z;

    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 0.0001f)
    {
        return;     // 방향이 없는 패킷은 그냥 무시
    }
     
    float nx = dx / len;
    float ny = dy / len;
    float nz = dz / len;

    sc_packet_monster_hp hp_pkt{};
    bool hit_broadcast = false;

    sc_packet_monster_remove remove_pkt{};
    bool remove_broadcast = false;

    {
        std::lock_guard<std::mutex> monster_lock(g_monster_lock);
        float closest_t = 50000.f;
        MONSTER* hit_mon = nullptr;

        for (auto& mon : g_monsters)
        {
            // 1. 발사 원점 -> 몬스터까지의 거리 (OM)
            float omx = mon.m_x - pkt->m_origin_x;
            float omy = mon.m_y - pkt->m_origin_y;
            float omz = mon.m_z - pkt->m_origin_z;

            // 2. 방향벡터에 투영 (내적, dot product)
            float t = omx * nx + omy * ny + omz * nz;
            if (t < 0.f)
            {
                continue;   // 발사자 뒤쪽 몬스터는 무시
            }

            // 3. 광선 위에서 몬스터에 제일 가까운 점 (P)
            float px = pkt->m_origin_x + nx * t;
            float py = pkt->m_origin_y + ny * t;
            float pz = pkt->m_origin_z + nz * t;

            // 4. 그 점에서 몬스터 중심까지의 거리
            float ddx = mon.m_x - px;
            float ddy = mon.m_y - py;
            float ddz = mon.m_z - pz;
            //float dist = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
            //float dist = sqrtf((mon.m_x - pkt->m_origin_x) * (mon.m_x - pkt->m_origin_x) + (mon.m_y - pkt->m_origin_y) * (mon.m_y - pkt->m_origin_y));
            float dist = fabsf((mon.m_y - pkt->m_origin_y) * nx - (mon.m_x - pkt->m_origin_x) * ny);

            std::cout << "  monster " << mon.m_id << " dist=" << dist << " (radius=" << MONSTER_HIT_RADIUS << ")\n";   // 임시
            std::cout << "  monster " << mon.m_id << " pos=(" << mon.m_x << "," << mon.m_y << "," << mon.m_z << ") dist=" << dist << "\n";


            std::cout << "monster " << mon.m_id << " t=" << t << "\n";
            if (dist <= MONSTER_HIT_RADIUS && t < closest_t)
            {
                closest_t = t;
                hit_mon = &mon;
            }
        }

        if (hit_mon != nullptr)
        {
            hit_mon->m_hp -= PLAYER_ATTACK_DAMAGE;
            std::cout << "monster " << hit_mon->m_id << " hp=" << hit_mon->m_hp << "\n";

            if (hit_mon->m_hp <= 0)
            {
                remove_pkt.m_size = sizeof(remove_pkt);
                remove_pkt.m_type = PKT_S2C_MONSTER_REMOVE;
                remove_pkt.m_id = hit_mon->m_id;
                remove_broadcast = true;

                int dead_id = hit_mon->m_id;

                // remove_if는 조건에 맞는 애들 앞으로 모아주기 (뒤에는 쓰레기 값) / erase(start, end)는 start부터 end까지 지우기
                // [] -> 캡처 : 외부 변수 사용 (여기서는 call by value - 참조가 아니라 복사)
                g_monsters.erase(std::remove_if(g_monsters.begin(), g_monsters.end(), [dead_id](const MONSTER& m) {
                    return m.m_id == dead_id;
                    }), g_monsters.end());
                g_pending_respawn.push_back({ dead_id, std::chrono::steady_clock::now() + std::chrono::seconds(10) });
            }
            else 
            {
                hp_pkt.m_size = sizeof(hp_pkt);
                hp_pkt.m_type = PKT_S2C_MONSTER_HP;
                hp_pkt.m_id = hit_mon->m_id;
                hp_pkt.m_hp = hit_mon->m_hp;
                hit_broadcast = true;
            }
        }
    }       // 몬스터 락 해제

    if (hit_broadcast)
    {
        std::lock_guard<std::mutex> player_lock(g_player_lock);
        for (auto& [id, session] : g_players)
        {
            send_packet(&session, &hp_pkt, sizeof(hp_pkt));
        }
    } else if (remove_broadcast)
    {
        std::lock_guard<std::mutex> player_lock(g_player_lock);
        for (auto& [id, session] : g_players)
        {
            send_packet(&session, &remove_pkt, sizeof(remove_pkt));
        }
    }
}

// TCP 패킷 재조립
void process_packet(SESSION* p, int bytes_transferred)
{
    // memcpy (복사받을 곳 - 포인터, 복사할 곳 - 포인터, 복사할 데이터의 길이)
    memcpy(p->m_prev_buf + p->m_prev_size, p->m_recv_over.m_netbuf, bytes_transferred);
    int data_size = p->m_prev_size + bytes_transferred;     // 지금까지 들어온 데이터 크기

    char* ptr = p->m_prev_buf;
    while (data_size >= HEADER_SIZE)
    {
        PACKET_HEADER* header = reinterpret_cast<PACKET_HEADER*>(ptr);
        if (data_size < header->m_size)
        {
            break; // 더 올게 남았을 때
        }

        switch (header->m_type)     // 실제 처리작업
        {
        case PKT_C2S_PLAYER_MOVE:
        {
            cs_packet_player_move* pkt = reinterpret_cast<cs_packet_player_move*>(ptr);
            p->m_x = pkt->m_x;
            p->m_y = pkt->m_y;
            p->m_z = pkt->m_z;
            p->m_pitch = pkt->m_pitch;
            p->m_yaw = pkt->m_yaw;
            p->m_roll = pkt->m_roll;

            update_position(p);

            std::lock_guard<std::mutex> lock(g_console_lock);
            //std::cout << "[client " << p->m_id << "] pos = (" << p->m_x << ", " << p->m_y << ", " << p->m_z << ")\n";
            break;
        }
        case PKT_C2S_PLAYER_ATTACK:
        {
            cs_packet_player_attack* pkt = reinterpret_cast<cs_packet_player_attack*>(ptr);
             std::cout << "[attack pkt] origin=(" << pkt->m_origin_x << "," << pkt->m_origin_y << "," << pkt->m_origin_z
               << ") dir=(" << pkt->m_dir_x << "," << pkt->m_dir_y << "," << pkt->m_dir_z << ")\n";   // 임시
            handle_player_attack(p, pkt);
            break;
        }
        default:
            break; // 모르는 타입은 일단 무시
        }

        ptr += header->m_size;          // 처리한 만큼 포인터 뒤로 옮기기
        data_size -= header->m_size;    // 처리한 만큼 남아있는 데이터 크기에서 빼기
    }

    // 패킷 처리하고 남은 자투리는 버퍼 맨 앞으로 옮겨서 다음 recv를 기다림
    if (data_size > 0)
    {
        memcpy(p->m_prev_buf, ptr, data_size);
    }
    p->m_prev_size = data_size;
}

void worker_thread()
{
    while (true)
    {
        DWORD bytes_transferred = 0;
        ULONG_PTR key = 0;
        WSAOVERLAPPED* over = nullptr;

        // 처리할 게 있으면 처리 (worker_thread의 본체)
        // main의 PQCS와 연계 (nullptr을 읽으면 종료되게끔)
        BOOL ret = GetQueuedCompletionStatus(g_h_iocp, &bytes_transferred, &key, &over, INFINITE);
        if (over == nullptr)
        {
            break; // main()에서 넣어준 종료 신호
        }

        SESSION* p = reinterpret_cast<SESSION*>(key);

        if (!ret || bytes_transferred == 0)
        {
            disconnect(p->m_id);
            continue;
        }

        process_packet(p, bytes_transferred);

        if (!post_recv(p))
        {
            disconnect(p->m_id);
        }
    }
}

void add_player_notification(SESSION* p)
{
    sc_packet_add_player ap;
    ap.m_size = sizeof(ap);
    ap.m_type = PKT_S2C_ADD_PLAYER;
    ap.m_id = p->m_id;
    ap.m_visual = p->m_visual;
    ap.m_x = p->m_x;
    ap.m_y = p->m_y;
    ap.m_z = p->m_z;
    ap.m_pitch = p->m_pitch;
	ap.m_yaw = p->m_yaw;
	ap.m_roll = p->m_roll;

    {
        std::lock_guard<std::mutex> lock(g_player_lock);

        // 새로 들어온 애만 빼고 나머지한테 새로 들어온 애 정보 보내기
        for (auto& [id, session] : g_players)
        {
            if (id == p->m_id)
            {
                continue;
            }
            send_packet(&session, &ap, sizeof(sc_packet_add_player));

            std::lock_guard<std::mutex> console_lock(g_console_lock);
            std::cout << "[server] notified client " << id << " about new player " << p->m_id << "\n";
        }

        // 새로 들어온 애한테 나머지 애들 정보 보내주기
        for (auto& [id, session] : g_players)
        {
            if (id == p->m_id)
            {
                continue;
            }

            sc_packet_add_player add;
            add.m_size = sizeof(add);
            add.m_type = PKT_S2C_ADD_PLAYER;
            add.m_id = session.m_id;
            add.m_visual = session.m_visual;
            add.m_x = session.m_x;
            add.m_y = session.m_y;
            add.m_z = session.m_z;

            send_packet(p, &add, sizeof(add));

            std::lock_guard<std::mutex> console_lock(g_console_lock);
            std::cout << "[server] sent existing player " << id << " info to new client " << p->m_id << "\n";
        }
    }
}

void send_monster_list(SESSION* p)
{
    std::lock_guard<std::mutex> lock(g_monster_lock);

    for (auto& m : g_monsters)
    {
        sc_packet_monster_spawn ms;
        ms.m_size = sizeof(ms);
        ms.m_type = PKT_S2C_MONSTER_SPAWN;
        ms.m_id = m.m_id;
        ms.m_hp = m.m_hp;
        ms.m_x = m.m_x;
        ms.m_y = m.m_y;
        ms.m_z = m.m_z;
        ms.m_monster_type = 0;

        send_packet(p, &ms, sizeof(ms));
    }
}

// accept는 일단 블로킹으로
void accept_loop()
{
    while (true)
    {
        sockaddr_in client_addr{};
        int addr_len = sizeof(client_addr);
        SOCKET c_socket = accept(g_s_listen, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);     // 여기서 블로킹
        if (c_socket == INVALID_SOCKET)
        {
            continue;
        }

        char addr_str[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));      // inet_ntop -> 소켓에 저장된 IP주소를 문자열로 바꿔주는 함수

        int id = g_next_id.fetch_add(1);        // id 발급 (fetch_add는 더하기 전 값을 반환함)

        g_player_lock.lock();
        SESSION& s = g_players[id];
        s.m_id = id;
        s.m_s = c_socket;
        s.m_addr = std::string(addr_str) + ":" + std::to_string(ntohs(client_addr.sin_port));
        SESSION* p = &s;
        g_player_lock.unlock();

        CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), g_h_iocp, reinterpret_cast<ULONG_PTR>(p), 0);

        {
            std::lock_guard<std::mutex> lock(g_console_lock);
            std::cout << "[server] client " << p->m_id << " connected from " << p->m_addr << "\n";
        }

        if (!post_recv(p))
        {
            disconnect(id);
            continue;
        }

        add_player_notification(p);
        send_monster_list(p);
    }
}

void spawn_initial_monsters()
{
    for (int i = 0; i < 10; ++i)
    {
        float angle = static_cast<float>(rand()) / RAND_MAX * 2.f * 3.14159265f;            // 랜덤한 각도 구하기
        float radius = MONSTER_SPAWN_MIN_RADIUS + static_cast<float>(rand()) / RAND_MAX * (MONSTER_SPAWN_MAX_RADIUS - MONSTER_SPAWN_MIN_RADIUS);        // 랜덤한 반지름 구하기

        MONSTER m;
        m.m_id = i + 1;
        m.m_x = MONSTER_SPAWN_POSITION_X + radius * cosf(angle);
        m.m_y = MONSTER_SPAWN_POSITION_Y + radius + sinf(angle);
        m.m_z = MONSTER_SPAWN_POSITION_Z;
        m.m_monster_type = 0;
        m.m_hp = 100;

        g_monsters.push_back(m);
    }
}

void monster_ai_tick()      // 별도 쓰레드가 실행
{
    while (true) 
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(MONSTER_HEARTBEAT));
        {
            std::lock_guard<std::mutex> player_lock(g_player_lock);        // 항상 플레이어 락 먼저
            std::lock_guard<std::mutex> monster_lock(g_monster_lock);       // 그 다음 몬스터 락 (데드락 방지)

            auto now2 = std::chrono::steady_clock::now();
            for (auto it = g_pending_respawn.begin(); it != g_pending_respawn.end(); )
            {
                if (now2 >= it->second)
                {
                    float angle = static_cast<float>(rand()) / RAND_MAX * 2.f * 3.14159265f;
                    float radius = MONSTER_SPAWN_MIN_RADIUS + static_cast<float>(rand()) / RAND_MAX * (MONSTER_SPAWN_MAX_RADIUS - MONSTER_SPAWN_MIN_RADIUS);

                    MONSTER m;
                    m.m_id = it->first;
                    m.m_x = MONSTER_SPAWN_POSITION_X + radius * cosf(angle);
                    m.m_y = MONSTER_SPAWN_POSITION_Y + radius * sinf(angle);
                    m.m_z = MONSTER_SPAWN_POSITION_Z;
                    m.m_hp = 100;
                    g_monsters.push_back(m);

                    sc_packet_monster_spawn sp{};
                    sp.m_size = sizeof(sp); sp.m_type = PKT_S2C_MONSTER_SPAWN;
                    sp.m_id = m.m_id; sp.m_x = m.m_x; sp.m_y = m.m_y; sp.m_z = m.m_z; sp.m_hp = m.m_hp;
                    for (auto& [id, session] : g_players) send_packet(&session, &sp, sizeof(sp));

                    it = g_pending_respawn.erase(it);
                }
                else ++it;
            }

            for (auto& mon : g_monsters) 
            {
                float min_distance = MONSTER_CHASE_RANGE;
                int player_id = 0;

                float l_dx, l_dy;

                for (auto& [id, session] : g_players) 
                {
                    float dx = session.m_x - mon.m_x;
                    float dy = session.m_y - mon.m_y;
                    float distance = sqrtf(dx * dx + dy * dy);

                    if (distance <= min_distance) 
                    {
                        min_distance = distance;
                        player_id = id;
                    
                        l_dx = dx;
                        l_dy = dy;
                    }
                }

                if (player_id == 0)
                {
                    mon.m_state = IDLE;
                    mon.m_target_id = 0;
                }
                else if (min_distance <= MONSTER_CHASE_RANGE && min_distance > MONSTER_ATTACK_RANGE)
                {
                    mon.m_state = CHASE;
                    mon.m_target_id = player_id;

                    // 정규화 (방향만 뽑기)
                    float nx = l_dx / min_distance;
                    float ny = l_dy / min_distance;

                    mon.m_x += nx * MONSTER_MOVE_SPEED;
                    mon.m_y += ny * MONSTER_MOVE_SPEED;

                    sc_packet_monster_position mp;
                    mp.m_size = sizeof(mp);
                    mp.m_type = PKT_S2C_MONSTER_POSITION;
                    mp.m_id = mon.m_id;
                    mp.m_x = mon.m_x;
                    mp.m_y = mon.m_y;
                    mp.m_z = mon.m_z;

                    for (auto& [id, session] : g_players)
                    {
                        send_packet(&session, &mp, sizeof(mp));
                    }
                }
                else if (min_distance <= MONSTER_ATTACK_RANGE)
                {
                    mon.m_state = ATTACK;
                    mon.m_target_id = player_id;

                    auto now = std::chrono::steady_clock::now();
                    if (now - mon.m_last_attack >= std::chrono::duration<float>(MONSTER_ATTACK_COOL))
                    {
                        sc_packet_monster_attack ma;
                        ma.m_size = sizeof(ma);
                        ma.m_type = PKT_S2C_MONSTER_ATTACK;
                        ma.m_id = mon.m_id;
                        ma.m_target_id = mon.m_target_id;

                        for (auto& [id, session] : g_players)
                        {
                            send_packet(&session, &ma, sizeof(ma));
                        }

                        mon.m_last_attack = now;
                    }
                }
            }
        }
    }
}

int main()
{
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    g_h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (g_h_iocp == nullptr)
    {
        error_display("CreateIoCompletionPort", GetLastError());
        WSACleanup();
        return 1;
    }

    g_s_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_s_listen == INVALID_SOCKET)
    {
        error_display("socket", WSAGetLastError());
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(LISTEN_PORT);

    if (bind(g_s_listen, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR)
    {
        error_display("bind", WSAGetLastError());
        return 1;
    }

    if (listen(g_s_listen, SOMAXCONN) == SOCKET_ERROR)
    {
        error_display("listen", WSAGetLastError());
        return 1;
    }

    srand(static_cast<unsigned>(time(nullptr)));
    spawn_initial_monsters();

    unsigned int worker_count = get_physical_core_count();
    if (worker_count == 0)
    {
        worker_count = 4;       // cpu 코어 갯수 못읽으면 기본으로 쓰레드 4개 설정
    }

    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (unsigned int i = 0; i < worker_count; ++i)
    {
        workers.emplace_back(worker_thread);
    }

    std::cout << "[server] listening on port " << LISTEN_PORT << " with " << worker_count << " worker threads\n";

    std::thread(monster_ai_tick).detach();      // detach : 정리해야 할 공유 자원이 없을 경우. (알아서 돌아라, 신경 안 쓴다)

    // 추가로 위 코드는 아래 두 줄과 완전히 동일한 코드 (대신 어딘가에 저장을 하지 않아 변수명을 부여할 필요가 없음)
    // std::thread ai_thread(monster_ai_tick);
    // ai_thread.detach();

    accept_loop();      // 지금 코드는 사실상 여기서 다음으로 안넘어감. (정상 종료가 없기 때문)

    for (unsigned int i = 0; i < worker_count; ++i)
    {
        PostQueuedCompletionStatus(g_h_iocp, 0, 0, nullptr);    
        // PQCS는 원래 쓰레드 풀에 작업을 넣어주는 함수. 
        // 근데 여기서는 overlapped 포인터가 들어가야 하는 
        // 자리(마지막 인자)에 nullptr을 넣음으로써 
        // GQCS에서 이 작업을 꺼냈을 때 종료하게끔 설계.
    }
    for (auto& t : workers)
    {
        t.join();       // join : 정리해야 할 공유 자원이 있는 경우 (소켓, IOCP 핸들 등)
    }

    closesocket(g_s_listen);
    CloseHandle(g_h_iocp);
    WSACleanup();
    return 0;
}