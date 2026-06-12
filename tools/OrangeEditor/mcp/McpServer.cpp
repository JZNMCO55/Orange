#include "McpServer.h"

#include "McpBridge.h"

// winsock 头集中在本 TU（header isolation）。WIN32_LEAN_AND_MEAN 砍掉
// windows.h 的 winsock1（与 winsock2 冲突）+ 大量无关声明。
#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    pragma comment(lib, "ws2_32.lib")
#endif

#include <orange/engine/core/Log.h>

#include <chrono>
#include <string>
#include <utility>

namespace Orange::Editor::Mcp
{

#if defined(_WIN32)

namespace
{
// uintptr_t 哨兵 ↔ winsock SOCKET 互转（头里不暴露 SOCKET 类型）。
constexpr std::uintptr_t kInvalid = static_cast<std::uintptr_t>(INVALID_SOCKET);

SOCKET ToSock(std::uintptr_t v) { return static_cast<SOCKET>(v); }
std::uintptr_t FromSock(SOCKET s) { return static_cast<std::uintptr_t>(s); }
}  // namespace

McpServer::~McpServer()
{
    Stop();
}

bool McpServer::Start(std::uint16_t port, McpBridge& bridge)
{
    if (mRunning.load()) { return true; }  // 幂等
    mpBridge = &bridge;
    mPort    = port;

    WSADATA wsa{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        ORANGE_LOG_ERROR("[mcp] WSAStartup 失败；MCP 桥不可用");
        return false;
    }

    SOCKET listenSock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET)
    {
        ORANGE_LOG_ERROR("[mcp] socket() 失败 (err={})；MCP 桥不可用", ::WSAGetLastError());
        ::WSACleanup();
        return false;
    }

    // SO_REUSEADDR：编辑器重启时旧端口可能仍处 TIME_WAIT，允许立即复用，避免
    // "重启编辑器后 --mcp-port 占用" 假错。
    BOOL reuse = TRUE;
    ::setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = ::htons(port);
    // 仅绑 loopback（127.0.0.1）—— 不对外暴露（NF-5）。
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        ORANGE_LOG_ERROR("[mcp] bind 127.0.0.1:{} 失败 (err={})；MCP 桥不可用",
                         port, ::WSAGetLastError());
        ::closesocket(listenSock);
        ::WSACleanup();
        return false;
    }
    if (::listen(listenSock, 1) == SOCKET_ERROR)
    {
        ORANGE_LOG_ERROR("[mcp] listen 失败 (err={})；MCP 桥不可用", ::WSAGetLastError());
        ::closesocket(listenSock);
        ::WSACleanup();
        return false;
    }

    mListenSocket.store(FromSock(listenSock));
    bridge.running.store(true);
    mRunning.store(true);
    mThread = std::thread(&McpServer::ThreadMain, this);

    ORANGE_LOG_INFO("[mcp] MCP 命令端监听 127.0.0.1:{}（等 orange-mcp 连接）", port);
    return true;
}

void McpServer::Stop()
{
    if (!mRunning.exchange(false)) { return; }  // 幂等：未运行直接返回

    if (mpBridge != nullptr)
    {
        mpBridge->running.store(false);
        // 唤醒任何阻塞在 outCv 等响应的 socket 线程，让它看到 running=false 退出。
        mpBridge->outCv.notify_all();
    }

    // 关闭 listen / client socket：解除线程在 accept / recv 上的阻塞。
    const std::uintptr_t ls = mListenSocket.exchange(kInvalid);
    if (ls != kInvalid) { ::closesocket(ToSock(ls)); }
    const std::uintptr_t cs = mClientSocket.exchange(kInvalid);
    if (cs != kInvalid) { ::closesocket(ToSock(cs)); }

    if (mThread.joinable()) { mThread.join(); }

    ::WSACleanup();
    ORANGE_LOG_INFO("[mcp] MCP 命令端已停止");
}

void McpServer::ThreadMain()
{
    McpBridge& bridge = *mpBridge;

    while (mRunning.load())
    {
        const std::uintptr_t ls = mListenSocket.load();
        if (ls == kInvalid) { break; }

        // 阻塞等一个客户端（M0 单客户端）。Stop 关闭 listen socket 时 accept
        // 返回错误 → 退出循环。
        SOCKET client = ::accept(ToSock(ls), nullptr, nullptr);
        if (client == INVALID_SOCKET)
        {
            if (!mRunning.load()) { break; }
            // 偶发 accept 错误：短暂退避后重试（不刷屏）。
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        mClientSocket.store(FromSock(client));
        ORANGE_LOG_INFO("[mcp] 客户端已连接");

        // ---- 单连接的 NDJSON 收发循环 ----------------------------------
        std::string recvBuffer;
        char        chunk[4096];
        bool        clientAlive = true;
        while (clientAlive && mRunning.load())
        {
            const int n = ::recv(client, chunk, static_cast<int>(sizeof(chunk)), 0);
            if (n <= 0)
            {
                // 0 = 对端正常关闭；<0 = 错误 / socket 被 Stop 关闭。
                clientAlive = false;
                break;
            }
            recvBuffer.append(chunk, static_cast<std::size_t>(n));

            // 按 '\n' 切出完整请求行，逐条入队 + 等响应回写。
            std::size_t nl;
            while ((nl = recvBuffer.find('\n')) != std::string::npos)
            {
                std::string line = recvBuffer.substr(0, nl);
                recvBuffer.erase(0, nl + 1);
                // 容忍 CRLF：剥掉行尾 '\r'。
                if (!line.empty() && line.back() == '\r') { line.pop_back(); }
                if (line.empty()) { continue; }

                // 入队请求（主线程帧末 drain 执行）。
                {
                    std::lock_guard<std::mutex> lk(bridge.inMutex);
                    bridge.pendingRequests.push_back(std::move(line));
                }

                // 阻塞等一条响应出现（单 in-flight，FIFO 保证配对）。
                std::string responseLine;
                {
                    std::unique_lock<std::mutex> lk(bridge.outMutex);
                    bridge.outCv.wait(lk, [&] {
                        return !bridge.pendingResponses.empty() || !bridge.running.load();
                    });
                    if (!bridge.pendingResponses.empty())
                    {
                        responseLine = std::move(bridge.pendingResponses.front());
                        bridge.pendingResponses.erase(bridge.pendingResponses.begin());
                    }
                }
                if (responseLine.empty())
                {
                    // running 被置 false 且无响应 → 关停中，停止处理本连接。
                    clientAlive = false;
                    break;
                }

                responseLine.push_back('\n');
                // 整行写完（send 可能短写，循环补齐）。
                std::size_t sent = 0;
                while (sent < responseLine.size() && mRunning.load())
                {
                    const int w = ::send(client,
                                         responseLine.data() + sent,
                                         static_cast<int>(responseLine.size() - sent), 0);
                    if (w <= 0) { clientAlive = false; break; }
                    sent += static_cast<std::size_t>(w);
                }
            }
        }

        const std::uintptr_t cs = mClientSocket.exchange(kInvalid);
        if (cs != kInvalid) { ::closesocket(ToSock(cs)); }
        ORANGE_LOG_INFO("[mcp] 客户端已断开");
    }
}

#else  // 非 Windows：编辑器当前 Windows-only，提供空实现保编译。

McpServer::~McpServer() { Stop(); }
bool McpServer::Start(std::uint16_t, McpBridge&) { return false; }
void McpServer::Stop() {}
void McpServer::ThreadMain() {}

#endif  // _WIN32

}  // namespace Orange::Editor::Mcp
