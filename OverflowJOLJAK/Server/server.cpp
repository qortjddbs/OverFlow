// OverFlow 위치 동기화 서버 (졸업작품 1차 미션용)
//
// 미팅일지(2026-06-29) 스펙: 언리얼 클라이언트가 1초마다 send_to_server(x, y, z)를
// 호출해서 float 3개(12바이트, 헤더 없음)를 TCP로 그대로 보낸다.
// 서버는 여러 클라이언트를 IOCP로 받아서 좌표가 도착할 때마다 화면에 출력한다.
//
// '게임 서버 프로그래밍' 03~07강에서 배운 이름/구조를 그대로 따라갔다.
//   - SESSION, EXP_OVER 구조체              : 05강 OVER_EX -> 07강 EXP_OVER로 배운 것
//   - g_h_iocp / g_s_listen / g_users        : 05~07강 전역 변수 네이밍
//   - m_prev_size로 조각난 패킷 이어붙이기   : 05강에서 배운 방식 (구현은 직접 - 아래 참고)
//   - g_lock 하나로 g_users 전체를 보호      : 06강 PLAN_A. concurrent_unordered_map
//     + shared_ptr(PLAN_C)도 써봤는데 지금 단계엔 너무 복잡해서 다시 이걸로 돌아왔다.
//     9강 시야처리 갈 때 다시 PLAN_C로 넘어가면 된다.
//
// 참고: m_x/m_y/m_z는 강의 예제(2D, short)와 달리 float로 뒀다. 젤다류 오픈월드는
// 좌표가 격자 단위가 아니라 연속적인 3D라서 이 부분만 미션 스펙에 맞춰 확장했다.

#include <winsock2.h>
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

#pragma comment(lib, "Ws2_32.lib")

// ── 상수 ─────────────────────────────────────────────────────
constexpr unsigned short LISTEN_PORT = 7777;
constexpr int MAX_BUF_SIZE = 4096;                            // 05강 OVER_EX::m_netbuf 크기
constexpr int POS_PACKET_SIZE = sizeof(float) * 3;            // x, y, z (헤더 없음)
constexpr int PREV_BUF_SIZE = MAX_BUF_SIZE + POS_PACKET_SIZE; // 이전 조각 + 새 데이터 합쳐도 안 넘치게

// 이 미션은 recv만 쓴다. 나중에 서버 -> 클라 전송이 필요해지면 그때 OP_SEND를 추가한다.
enum enumOperation
{
    OP_RECV
};

// 05강 OVER_EX / 07강 EXP_OVER : OVERLAPPED에 recv용 버퍼를 같이 묶어둔 구조체.
struct EXP_OVER
{
    WSAOVERLAPPED  m_wsaOver;
    WSABUF         m_wsaBuf;
    char           m_netbuf[MAX_BUF_SIZE]; // 슬라이드는 unsigned char지만 WSABUF.buf가 char*라 char로 선언
    enumOperation  m_Operation;
};

// 05~07강 SESSION : 클라이언트 한 명당 정보를 담는 구조체.
struct SESSION
{
    int         m_id = 0;
    SOCKET      m_s = INVALID_SOCKET;
    std::string m_addr; // 로그 출력용 "IP:포트" 문자열 (실제 통신에는 안 쓰임)
    EXP_OVER    m_recv_over{};

    // 05강에서 배운 '조각난 패킷 이어붙이기'. recv가 12바이트 딱 맞게 오지 않을 수 있어서
    // 못다 읽은 바이트를 다음 recv 데이터 앞에 붙여서 다시 해석한다.
    char m_prev_buf[PREV_BUF_SIZE];
    int  m_prev_size = 0;

    // 마지막으로 받은 위치. 지금은 화면 출력에만 쓰지만, 나중에 다른 클라이언트에게
    // 브로드캐스트할 때도 이 값을 그대로 쓰면 된다.
    float m_x = 0.f;
    float m_y = 0.f;
    float m_z = 0.f;
};

// ── 전역 ─────────────────────────────────────────────────────
HANDLE g_h_iocp = nullptr;
SOCKET g_s_listen = INVALID_SOCKET;
std::atomic<int> g_next_id{ 1 }; // 클라이언트마다 겹치지 않는 id를 하나씩 나눠준다

std::mutex g_lock;                        // g_users 전체를 보호 (PLAN_A)
std::unordered_map<int, SESSION> g_users; // key = client id

std::mutex g_console_lock; // cout/cerr이 여러 스레드에서 섞이지 않게

// 03강 error_display : WinSock 에러를 그대로 출력해주는 함수.
void error_display(const char* msg, int err_no)
{
    std::lock_guard<std::mutex> lock(g_console_lock);
    std::cerr << "[error] " << msg << " : " << err_no << "\n";
}

// EXP_OVER를 초기화하고 WSARecv를 건다.
bool post_recv(SESSION* p)
{
    ZeroMemory(&p->m_recv_over.m_wsaOver, sizeof(WSAOVERLAPPED));
    p->m_recv_over.m_wsaBuf.buf = p->m_recv_over.m_netbuf;
    p->m_recv_over.m_wsaBuf.len = MAX_BUF_SIZE;
    p->m_recv_over.m_Operation = OP_RECV;

    DWORD recv_bytes = 0;
    DWORD flags = 0;
    int ret = WSARecv(p->m_s, &p->m_recv_over.m_wsaBuf, 1, &recv_bytes, &flags,
        &p->m_recv_over.m_wsaOver, nullptr);

    if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        error_display("WSARecv", WSAGetLastError());
        return false;
    }
    return true;
}

// g_lock을 잠그고 g_users에서 클라이언트를 지운다. (06강에서 배운 lock()/unlock() 방식)
void disconnect(int id)
{
    g_lock.lock();
    auto it = g_users.find(id);
    if (it != g_users.end())
    {
        closesocket(it->second.m_s);
        {
            std::lock_guard<std::mutex> lock(g_console_lock);
            std::cout << "[server] client " << it->second.m_id << " (" << it->second.m_addr << ") disconnected\n";
        }
        g_users.erase(it);
    }
    g_lock.unlock();
}

// 패킷 조립. 05강에서 배운 방식: 이전에 못다 읽은 바이트 뒤에 이번에 받은 바이트를
// 붙이고, 12바이트(x, y, z)씩 잘라서 처리한다. 마지막에 12바이트가 안 되는 나머지는
// 다음 recv를 위해 m_prev_buf에 저장해둔다.
void process_packet(SESSION* p, int bytes_transferred)
{
    // 1. 이전 조각 뒤에 이번에 받은 바이트를 이어붙인다.
    memcpy(p->m_prev_buf + p->m_prev_size, p->m_recv_over.m_netbuf, bytes_transferred);
    int data_size = p->m_prev_size + bytes_transferred;

    // 2. 12바이트씩 끊어서 x, y, z로 해석한다. TCP는 스트림이라 recv 한 번에
    //    정확히 12바이트씩 온다는 보장이 없어서, 여러 개가 쌓여 있을 수도 있다.
    char* ptr = p->m_prev_buf;
    while (data_size >= POS_PACKET_SIZE)
    {
        float xyz[3];
        memcpy(xyz, ptr, POS_PACKET_SIZE);
        p->m_x = xyz[0];
        p->m_y = xyz[1];
        p->m_z = xyz[2];

        // 3. 확인용으로 화면에 출력한다.
        {
            std::lock_guard<std::mutex> lock(g_console_lock);
            std::cout << "[client " << p->m_id << "] pos = (" << p->m_x << ", " << p->m_y << ", " << p->m_z << ")\n";
        }

        ptr += POS_PACKET_SIZE;
        data_size -= POS_PACKET_SIZE;
    }

    // 남은 조각(12바이트 미만)은 버퍼 맨 앞으로 옮겨서 다음 recv를 기다린다.
    if (data_size > 0)
    {
        memcpy(p->m_prev_buf, ptr, data_size);
    }
    p->m_prev_size = data_size;
}

// 07강 IOCP_2 워커 스레드: GetQueuedCompletionStatus로 완료된 recv를 계속 받아온다.
void worker_thread()
{
    while (true)
    {
        DWORD bytes_transferred = 0;
        ULONG_PTR key = 0;
        WSAOVERLAPPED* over = nullptr;

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

// std::thread::hardware_concurrency()는 하이퍼스레딩까지 포함한 "논리 프로세서" 수를
// 돌려준다 (예: 8코어/16스레드 CPU면 16). 교수님 말씀대로 "물리 코어" 수만 세려면
// 표준 C++에는 방법이 없어서, Win32 API로 직접 세야 한다.
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

// accept는 그냥 블로킹으로 받는다 (AcceptEx는 이 강의 범위 밖).
void accept_loop()
{
    while (true)
    {
        sockaddr_in client_addr{};
        int addr_len = sizeof(client_addr);
        SOCKET c_socket = accept(g_s_listen, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (c_socket == INVALID_SOCKET)
        {
            continue;
        }

        char addr_str[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));      // inet_ntop -> 소켓에 저장된 IP주소를 문자열로 바꿔주는 함수

        int id = g_next_id.fetch_add(1);

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

    unsigned int worker_count = get_physical_core_count();
    if (worker_count == 0)
    {
        worker_count = 4;
    }

    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (unsigned int i = 0; i < worker_count; ++i)
    {
        workers.emplace_back(worker_thread);
    }

    std::cout << "[server] listening on port " << LISTEN_PORT << " with " << worker_count << " worker threads\n";

    accept_loop();  // 이 함수는 끝나지 않는다 (Ctrl+C로 종료). 그래서 아래 join 코드는
                    // 지금은 실행되지 않는데, 나중에 accept_loop에 종료 신호를 받는
                    // 로직을 추가하면 정상적으로 정리된다.

    for (unsigned int i = 0; i < worker_count; ++i)
    {
        PostQueuedCompletionStatus(g_h_iocp, 0, 0, nullptr);
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
