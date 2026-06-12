#ifndef ORANGE_EDITOR_MCP_MCP_BRIDGE_H
#define ORANGE_EDITOR_MCP_MCP_BRIDGE_H

// ---------------------------------------------------------------------------
// McpBridge —— MCP 命令端的"后台 socket 线程 ↔ 主线程帧末执行"集合点。
//
// 这是编辑器引入的**第一个真后台线程**与主线程之间的唯一共享状态。设计要点
// （ADR-020 决策 2 / invariant ①）：
//
//   * socket 线程**永不**触碰 World / ImGui / Vulkan / EnTT —— 它只读写下面
//     两个 mutex 保护的 in/out 队列（字符串进、字符串出）。所有真正的命令执行
//     （解析 JSON、遍历 World、走命令栈）发生在主线程 EditorRenderLayer::
//     ApplyPendingMcpCommands 帧末 drain 时。
//   * 队列元素是**裸 NDJSON 文本行**：请求 `{"id":n,"op":"...","args":{...}}`、
//     响应 `{"id":n,"ok":true,"result":{...}}`。socket 线程不解析它们，故无需在
//     socket 线程上 link 任何 JSON / schema 依赖；id 配对由响应内嵌的 id 完成。
//   * 通信模型（M0）= 单客户端、单 in-flight：socket 线程读一行 → 入队 →
//     阻塞等一条响应出现 → 回写 → 再读下一行。MCP 客户端本就串行调用 tool，
//     故 FIFO 顺序天然保证请求↔响应配对，无需在 socket 线程解析 id。
//
// 与 EditorHost.pendingImports（拖拽导入队列）的唯一区别：那条在 glfwPollEvents
// 主线程内同步 push，无并发故无 mutex；本桥是真后台线程，故 in/out 各加一把锁
// + 一个 condition_variable 让 socket 线程高效等待响应（不忙等）。
//
// 本头只依赖标准库（无 winsock / 无引擎头），可被 EditorHost.h 安全 include —— 把
// winsock 关在 mcp/McpServer.cpp 内（header isolation）。
// ---------------------------------------------------------------------------

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Orange::Editor::Mcp
{

struct McpBridge
{
    // socket 线程 push 裸请求行；主线程 swap-drain 后逐条执行。
    std::mutex               inMutex;
    std::vector<std::string> pendingRequests;

    // 主线程 push 裸响应行；socket 线程取走回写。outCv 让 socket 线程在
    // 响应就绪前阻塞等待（主线程 push 后 notify），而非轮询忙等。
    std::mutex               outMutex;
    std::condition_variable  outCv;
    std::vector<std::string> pendingResponses;

    // 桥是否在运行。McpServer::Start 置 true、Stop 置 false（Stop 同时 notify
    // outCv 唤醒任何在等响应的 socket 线程，使其在关停时及时退出）。
    std::atomic<bool> running{false};
};

}  // namespace Orange::Editor::Mcp

#endif  // ORANGE_EDITOR_MCP_MCP_BRIDGE_H
