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

namespace Orange::Engine::Render
{
class Pipeline;
}

namespace Orange::Editor::Mcp
{

// 执行一条 MCP 命令。requestJson = 单行 NDJSON 请求；返回单行 NDJSON 响应。
// 永不抛异常（内部全兜底）。
//
// viewportPipeline = 编辑器当前 viewport 离屏 Pipeline（EditorRenderLayer 的
// mpScenePipeline），供 capture_viewport 回读像素；其它命令不用，可为 nullptr。
std::string ExecuteMcpCommand(const std::string&                  requestJson,
                              EditorHost&                         host,
                              Orange::Engine::Render::Pipeline*   viewportPipeline);

// undo-group 护栏 —— 每帧（命令 drain 前）在主线程调用，与队列是否有命令无关。
// 若 begin_undo_group 开着的组满足任一闭合条件就自动 EndGroup + 清会话态：
//   * 开组已超过 30s（AI 忘调 end_undo_group）；
//   * 开组的那个 MCP 客户端已断开 / 被新连接替换；
//   * 命令栈已被场景切换（New/Open/Stop）Clear（InGroup() 变 false）。
// 防 AI 忘关把命令栈长期卡在组内（ADR-020 §4.E 超时 / 断连护栏）。无开组时 no-op。
void TickMcpUndoGroupGuard(EditorHost& host);

}  // namespace Orange::Editor::Mcp

#endif  // ORANGE_EDITOR_MCP_MCP_COMMAND_HANDLER_H
