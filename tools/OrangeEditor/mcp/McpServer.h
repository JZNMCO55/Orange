#ifndef ORANGE_EDITOR_MCP_MCP_SERVER_H
#define ORANGE_EDITOR_MCP_MCP_SERVER_H

// ---------------------------------------------------------------------------
// McpServer —— 监听 127.0.0.1:<port> 的后台 TCP 线程（winsock），收 NDJSON 命令、
// 回 NDJSON 响应。命令**不在**本线程执行：本线程只把请求行 push 进 McpBridge 的
// in 队列、阻塞等 out 队列里出现响应行、回写。真正的命令执行在主线程帧末
// （EditorRenderLayer::ApplyPendingMcpCommands）—— 见 McpBridge.h 头注释。
//
// 安全（NF-5）：仅绑 127.0.0.1（localhost），不对外；无鉴权（本机单用户开发
// 工具，对标 Blender MCP）。`--mcp-port` 默认不传 = 本类根本不被构造 = 零监听、
// 零行为变化。
//
// winsock 头被关在 McpServer.cpp —— 本头只暴露标准库类型（header isolation）。
// socket handle 在头里以 std::uintptr_t 存储（Windows SOCKET == UINT_PTR），
// .cpp 内 cast 回 SOCKET。
// ---------------------------------------------------------------------------

#include <atomic>
#include <cstdint>
#include <thread>

namespace Orange::Editor::Mcp
{

    struct McpBridge;

    class McpServer
    {
    public:
        McpServer() = default;
        ~McpServer();

        McpServer(const McpServer&)            = delete;
        McpServer& operator=(const McpServer&) = delete;

        // 启动监听线程。bridge 必须在 McpServer 之后析构（典型：两者都挂在 main 的
        // EditorHost / 栈上，McpServer 先 Stop+join 再让 EditorHost 析构）。
        // 返回 false = 监听 socket 建立失败（WSAStartup / bind / listen 出错），此时
        // 不启动线程、编辑器照常运行（仅 MCP 不可用）。
        bool Start(std::uint16_t port, McpBridge& bridge);

        // 停止监听线程：置 bridge.running=false、关闭 listen / client socket 解除
        // accept/recv 阻塞、notify 唤醒等响应的线程、join。幂等。
        void Stop();

        bool IsRunning() const { return mRunning.load(); }

    private:
        void ThreadMain();

        McpBridge*        mpBridge = nullptr;
        std::thread       mThread;
        std::atomic<bool> mRunning{false};

        // INVALID_SOCKET 在 winsock 是 (SOCKET)(~0)；用 uintptr_t 同值哨兵，避免
        // 在头里拖 winsock。
        static constexpr std::uintptr_t kInvalidSocket = static_cast<std::uintptr_t>(~0ull);
        std::atomic<std::uintptr_t>     mListenSocket{kInvalidSocket};
        std::atomic<std::uintptr_t>     mClientSocket{kInvalidSocket};
        std::uint16_t                   mPort = 0;
    };

} // namespace Orange::Editor::Mcp

#endif // ORANGE_EDITOR_MCP_MCP_SERVER_H
