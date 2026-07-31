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

#include "..\Shared\Protocol.h"

#pragma comment(lib, "Ws2_32.lib")      // winsock2.h의 진짜 코드 가져오기

// constexpr -> 컴파일할 때 이미 확정되는 상수 (배열 크기에 넣어야 하기 때문 + 실수 방지)
// 실수 = 런타임에만 정해지는 값을 넣으면 컴파일 에러를 띄워줌. (그냥 const는 이게 안됨)
constexpr unsigned short LISTEN_PORT = 7777;
constexpr int MAX_BUF_SIZE = 4096;
constexpr int HEADER_SIZE = sizeof(PACKET_HEADER);
constexpr int MAX_PACKET_SIZE = sizeof(sc_packet_add_player);  // 존재하는 패킷 중 제일 큰 거

constexpr int PREV_BUF_SIZE = MAX_BUF_SIZE + MAX_PACKET_SIZE;

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

    char m_prev_buf[PREV_BUF_SIZE];
    int  m_prev_size = 0;

    float m_x = 0.f;
    float m_y = 0.f;
    float m_z = 0.f;

    int m_visual = 0;       // 일단 임시로 생성. 나중가면 enum으로 따로 만들어야될듯. (디폴트 0 -> 젤다)
};

HANDLE g_h_iocp = nullptr;
SOCKET g_s_listen = INVALID_SOCKET;
std::atomic<int> g_next_id{ 1 }; // 클라이언트마다 겹치지 않는 id를 하나씩 나눠준다

std::mutex g_lock;                        // g_users 전체를 보호
std::unordered_map<int, SESSION> g_users; // key = client id

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

// g_lock을 잠그고 g_users에서 클라이언트를 지운다
void disconnect(int id) 
{
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_users.find(id);
    if (it != g_users.end())
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

        for (auto& [myId, session] : g_users)
        {
            if (id == myId) continue;
            send_packet(&session, &rp, sizeof(rp));
        }

        g_users.erase(it);
    }
}

void update_position(SESSION* me)
{
    sc_packet_position up;
    up.m_size = sizeof(up);
    up.m_type = PKT_S2C_POSITION;
    up.m_id = me->m_id;
    up.m_x = me->m_x;
    up.m_y = me->m_y;
    up.m_z = me->m_z;

    {
        std::lock_guard<std::mutex> lock(g_lock);
        for (auto& [id, session] : g_users)
        {
            if (id == me->m_id) continue;
            send_packet(&session, &up, sizeof(up));
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
        case PKT_C2S_MOVE:
        {
            cs_packet_move* pkt = reinterpret_cast<cs_packet_move*>(ptr);
            p->m_x = pkt->m_x;
            p->m_y = pkt->m_y;
            p->m_z = pkt->m_z;

            update_position(p);

            // std::lock_guard<std::mutex> lock(g_console_lock);
            // std::cout << "[client " << p->m_id << "] pos = (" << p->m_x << ", " << p->m_y << ", " << p->m_z << ")\n";
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

    {
        std::lock_guard<std::mutex> lock(g_lock);

        // 새로 들어온 애만 빼고 나머지한테 새로 들어온 애 정보 보내기
        for (auto& [id, session] : g_users)
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
        for (auto& [id, session] : g_users)
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

        g_lock.lock();
        SESSION& s = g_users[id];
        s.m_id = id;
        s.m_s = c_socket;
        s.m_addr = std::string(addr_str) + ":" + std::to_string(ntohs(client_addr.sin_port));
        SESSION* p = &s;
        g_lock.unlock();

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

    accept_loop();

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
        t.join();
    }

    closesocket(g_s_listen);
    CloseHandle(g_h_iocp);
    WSACleanup();
    return 0;
}