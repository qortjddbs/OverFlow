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

// 헤더: 패킷 전체 크기(size) + 패킷 종류(type) 모든 패킷은 이 헤더로 시작
#pragma pack(push, 1)
struct PACKET_HEADER
{
    unsigned short m_size; 
    unsigned char  m_type; 
};

struct cs_packet_move : PACKET_HEADER
{
    float m_x, m_y, m_z;
};
#pragma pack(pop)

enum PACKET_TYPE : unsigned char
{
    PKT_CS_MOVE = 1,
};

constexpr unsigned short LISTEN_PORT = 7777;
constexpr int MAX_BUF_SIZE = 4096;                       
constexpr int HEADER_SIZE = sizeof(PACKET_HEADER);       
constexpr int MAX_PACKET_SIZE = sizeof(cs_packet_move);  // 존재하는 패킷 중 제일 큰 거
                                                         
constexpr int PREV_BUF_SIZE = MAX_BUF_SIZE + MAX_PACKET_SIZE;

enum enumOperation
{
    OP_RECV
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
};

HANDLE g_h_iocp = nullptr;
SOCKET g_s_listen = INVALID_SOCKET;
std::atomic<int> g_next_id{ 1 }; // 클라이언트마다 겹치지 않는 id를 하나씩 나눠준다

std::mutex g_lock;                        // g_users 전체를 보호
std::unordered_map<int, SESSION> g_users; // key = client id

std::mutex g_console_lock; // cout/cerr이 여러 스레드에서 섞이지 않게

void error_display(const char* msg, int err_no)
{
    std::lock_guard<std::mutex> lock(g_console_lock);
    std::cerr << "[error] " << msg << " : " << err_no << "\n";
}

// 다음 recv 예약하는 함수
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

// g_lock을 잠그고 g_users에서 클라이언트를 지운다.
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

void process_packet(SESSION* p, int bytes_transferred)
{
    memcpy(p->m_prev_buf + p->m_prev_size, p->m_recv_over.m_netbuf, bytes_transferred);
    int data_size = p->m_prev_size + bytes_transferred;

    char* ptr = p->m_prev_buf;
    while (data_size >= HEADER_SIZE)
    {
        PACKET_HEADER* header = reinterpret_cast<PACKET_HEADER*>(ptr);
        if (data_size < header->m_size)
        {
            break; // 더 올게 남았을 때
        }

        switch (header->m_type)
        {
        case PKT_CS_MOVE:
        {
            cs_packet_move* pkt = reinterpret_cast<cs_packet_move*>(ptr);
            p->m_x = pkt->m_x;
            p->m_y = pkt->m_y;
            p->m_z = pkt->m_z;

            std::lock_guard<std::mutex> lock(g_console_lock);
            std::cout << "[client " << p->m_id << "] pos = (" << p->m_x << ", " << p->m_y << ", " << p->m_z << ")\n";
            break;
        }
        default:
            break; // 모르는 타입은 일단 무시
        }

        ptr += header->m_size;
        data_size -= header->m_size;
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
// 돌려준다. "물리 코어" 수만 세려면 표준 C++에는 방법이 없어서, Win32 API로 직접 세야 한다.
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

// accept는 그냥 블로킹으로
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

        int id = g_next_id.fetch_add(1);        // id 발급

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

    accept_loop();

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
