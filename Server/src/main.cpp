// OverFlow position-sync server.
//
// Accepts multiple Unreal clients over TCP. Each client is expected to call
// send_to_server(x, y, z) once per second, which puts three raw floats
// (12 bytes, no framing) on the wire. The server assigns each connection its
// own client id on accept and prints every position update it receives.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    constexpr unsigned short kListenPort = 7777;
    constexpr int kRecvBufferSize = 4096;
    constexpr size_t kPositionPacketSize = sizeof(float) * 3; // x, y, z

    struct PerIoContext
    {
        OVERLAPPED overlapped{};
        WSABUF wsabuf{};
        char buffer[kRecvBufferSize]{};
    };

    struct PerClientContext
    {
        SOCKET socket = INVALID_SOCKET;
        int clientId = 0;
        std::string address;
        std::vector<char> pending; // partial-packet reassembly buffer
    };

    HANDLE g_iocp = nullptr;
    std::atomic<int> g_nextClientId{1};
    std::mutex g_consoleMutex;
    std::mutex g_clientsMutex;
    std::unordered_map<PerClientContext*, std::unique_ptr<PerClientContext>> g_clients;

    void LogLine(const std::string& line)
    {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        printf("%s\n", line.c_str());
        fflush(stdout);
    }

    bool PostRecv(PerClientContext* client)
    {
        auto* io = new PerIoContext();
        io->wsabuf.buf = io->buffer;
        io->wsabuf.len = kRecvBufferSize;

        DWORD flags = 0;
        DWORD bytesRecvd = 0;
        int result = WSARecv(client->socket, &io->wsabuf, 1, &bytesRecvd, &flags, &io->overlapped, nullptr);
        if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
        {
            delete io;
            return false;
        }
        return true;
    }

    void RemoveClient(PerClientContext* client)
    {
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        auto it = g_clients.find(client);
        if (it != g_clients.end())
        {
            closesocket(client->socket);
            LogLine("[Server] Client " + std::to_string(client->clientId) + " (" + client->address + ") disconnected.");
            g_clients.erase(it);
        }
    }

    void HandlePacketBytes(PerClientContext* client)
    {
        while (client->pending.size() >= kPositionPacketSize)
        {
            float xyz[3];
            memcpy(xyz, client->pending.data(), kPositionPacketSize);
            client->pending.erase(client->pending.begin(), client->pending.begin() + kPositionPacketSize);

            char msg[128];
            snprintf(msg, sizeof(msg), "[Client %d] pos = (%.2f, %.2f, %.2f)", client->clientId, xyz[0], xyz[1], xyz[2]);
            LogLine(msg);
        }
    }

    void WorkerThread()
    {
        while (true)
        {
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0;
            LPOVERLAPPED overlapped = nullptr;

            BOOL ok = GetQueuedCompletionStatus(g_iocp, &bytesTransferred, &completionKey, &overlapped, INFINITE);
            if (overlapped == nullptr)
            {
                break; // shutdown sentinel posted from main()
            }

            auto* client = reinterpret_cast<PerClientContext*>(completionKey);
            auto* io = CONTAINING_RECORD(overlapped, PerIoContext, overlapped);

            if (!ok || bytesTransferred == 0)
            {
                delete io;
                RemoveClient(client);
                continue;
            }

            client->pending.insert(client->pending.end(), io->buffer, io->buffer + bytesTransferred);
            HandlePacketBytes(client);
            delete io;

            if (!PostRecv(client))
            {
                RemoveClient(client);
            }
        }
    }

    void AcceptLoop(SOCKET listenSocket)
    {
        while (true)
        {
            sockaddr_in clientAddr{};
            int addrLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
            if (clientSocket == INVALID_SOCKET)
            {
                if (WSAGetLastError() == WSAEINTR)
                {
                    break;
                }
                continue;
            }

            char addrStr[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));

            auto client = std::make_unique<PerClientContext>();
            client->socket = clientSocket;
            client->clientId = g_nextClientId.fetch_add(1);
            client->address = std::string(addrStr) + ":" + std::to_string(ntohs(clientAddr.sin_port));

            PerClientContext* rawClient = client.get();
            {
                std::lock_guard<std::mutex> lock(g_clientsMutex);
                g_clients.emplace(rawClient, std::move(client));
            }

            CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, reinterpret_cast<ULONG_PTR>(rawClient), 0);

            LogLine("[Server] Client " + std::to_string(rawClient->clientId) + " connected from " + rawClient->address);

            if (!PostRecv(rawClient))
            {
                RemoveClient(rawClient);
            }
        }
    }
}

int main(int argc, char** argv)
{
    unsigned short port = kListenPort;
    if (argc > 1)
    {
        port = static_cast<unsigned short>(std::atoi(argv[1]));
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (g_iocp == nullptr)
    {
        fprintf(stderr, "CreateIoCompletionPort failed: %lu\n", GetLastError());
        WSACleanup();
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
    {
        fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
        return 1;
    }

    unsigned int workerCount = std::thread::hardware_concurrency();
    if (workerCount == 0)
    {
        workerCount = 4;
    }

    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (unsigned int i = 0; i < workerCount; ++i)
    {
        workers.emplace_back(WorkerThread);
    }

    LogLine("[Server] Listening on port " + std::to_string(port) + " with " + std::to_string(workerCount) + " worker threads.");

    AcceptLoop(listenSocket);

    for (unsigned int i = 0; i < workerCount; ++i)
    {
        PostQueuedCompletionStatus(g_iocp, 0, 0, nullptr);
    }
    for (auto& t : workers)
    {
        t.join();
    }

    closesocket(listenSocket);
    CloseHandle(g_iocp);
    WSACleanup();
    return 0;
}
