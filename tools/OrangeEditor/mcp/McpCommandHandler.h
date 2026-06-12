#ifndef ORANGE_EDITOR_MCP_MCP_COMMAND_HANDLER_H
#define ORANGE_EDITOR_MCP_MCP_COMMAND_HANDLER_H

// ---------------------------------------------------------------------------
// McpCommandHandler —— MCP 命令在**主线程帧末**的执行入口。
//
// EditorRenderLayer::ApplyPendingMcpCommands 从 McpBridge 的 in 队列 swap-drain
// 出裸请求行后，对每行调 ExecuteMcpCommand(requestJson, host)，拿到一行裸响应
// JSON，再 push 回 out 队列让 socket 线程回写。
//
// 本函数运行在主线程、ImGui 帧末（与 ApplyPendingImports 同位）——可以安全地读
// World / schema registry。组件读写一律走 ComponentSchemaRegistry（schema-first
// 纪律，ADR-020 invariant ③）。整条命令 try/catch 兜底：单条命令任何异常都转成
// `{"ok":false,"error":...}`，绝不让编辑器崩或 World 进入半改状态（NF-6）。
//
// M0 仅实现 ping / get_scene_info；M1/M2 在同一 dispatch 表内追加 op，不改本签名。
// ---------------------------------------------------------------------------

#include <string>

struct EditorHost;

namespace Orange::Editor::Mcp
{

// 执行一条 MCP 命令。requestJson = 单行 NDJSON 请求；返回单行 NDJSON 响应。
// 永不抛异常（内部全兜底）。
std::string ExecuteMcpCommand(const std::string& requestJson, EditorHost& host);

}  // namespace Orange::Editor::Mcp

#endif  // ORANGE_EDITOR_MCP_MCP_COMMAND_HANDLER_H
